// BC2 test-range settings override, injected into BFBC2Game.exe (build 795745).
//
// The retail client ignores Client.*/Server.* on its command line: its
// override loader (0x5017c0) and add-override call (0x501ab0) have no callers.
// The live settings registry still has a locked set-by-name entry point,
// SettingsManager::Set (0x4f4bd0, thiscall on [0x155cfdc], key and value as
// eastl::string). This DLL calls it with pairs from BC2TestRange.cfg next to
// the DLL, then reads each value back through the registry getter (0x4f4c40).
// A single-player load ("newSP", 0x7600f0) uses whatever Client.Level holds
// at that moment.
//
// Every native address is checked against the exact build first: image size,
// PE timestamp and the code bytes of both entry points. Any mismatch writes
// nothing.
//
// Give.* keys give weapons through the engine's own give/replace call, see
// "Weapon give" below. They never go to the settings registry.
#include <windows.h>
#include <MinHook.h>
#include <intrin.h>
#include <tlhelp32.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace {
constexpr std::uintptr_t ImageBase = 0x400000;
constexpr std::uint32_t ImageBytes = 0x1913000, PeTimestamp = 1314210909;
constexpr std::uintptr_t SettingsGlobal = 0x155cfdc, SetByName = 0x4f4bd0, GetByName = 0x4f4c40;
// 0x4f4bd0: test ecx,ecx; je; mov eax,[esp+8]; mov edx,[esp+4]; push 0; push eax; add ecx,-4; push edx; call 0x4f3fa0; ret 8
constexpr unsigned char SetCode[] = {0x85,0xC9,0x74,0x17,0x8B,0x44,0x24,0x08,0x8B,0x54,0x24,0x04,0x6A,0x00,0x50,0x83,0xC1,0xFC,0x52,0xE8};
// 0x4f4c40: test ecx,ecx; je +8; add ecx,-4; jmp 0x4f4600
constexpr unsigned char GetCode[] = {0x85,0xC9,0x74,0x08,0x83,0xC1,0xFC,0xE9};

// 0x4014c0: the engine's eastl::string construct-from-C-string (ClientSettings'
// constructor builds Level = "Levels/SP_01" with it at 0x1387c52).
constexpr std::uintptr_t StringFromCString = 0x4014c0;
constexpr unsigned char StringCode[] = {0x53,0x55,0x56,0x57,0x8B,0x7C,0x24,0x14,0x8B,0xC7,0x8B,0xE9,0x8D,0x50,0x01};

struct EaString { const char* begin; const char* end; const char* capacity; void* allocator; };
using StringFn = void(__thiscall*)(EaString* self, const char* text);

// Starting the level. The menu's NEW CAMPAIGN runs newSP (0x7600f0,
// thiscall, int difficulty, ret 4), which queues a type-3 request on the
// UIManager (no lock), reading Client.Level. UIManager::update (0x959900,
// thiscall with two stack arguments, ret 8) drains that queue, so the call is made from a
// detour of update installed by swapping its vtable slot (0x1440860).
constexpr std::uintptr_t UiManagerGlobal = 0x19a4ca8, MainMenuOffset = 0x82b24;
constexpr std::uintptr_t UiUpdate = 0x959900, UiUpdateSlot = 0x1440860, NewSinglePlayer = 0x7600f0;
constexpr unsigned char UiUpdateCode[] = {0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x24,0x53,0x56,0x57,0x8b,0xf1,0xe8,0x1d};
constexpr unsigned char NewSpCode[] = {0x81,0xec,0x34,0x01,0x00,0x00,0x8b,0x0d,0xa8,0x4c,0x9a,0x01,0x53,0x56,0x57,0x68};
void Log(const char* format, ...);
using UpdateFn = void(__thiscall*)(void* self, std::uintptr_t a, std::uintptr_t b);
using NewSpFn = void(__thiscall*)(void* self, int difficulty);
volatile LONG startArmed = 0, started = 0;
int startDifficulty = 1, startWaitFrames = 60;
long long launchTicks = 0;
unsigned menuFrames = 0;

volatile LONG detourCalls = 0;

// Skipping the opening cutscene. skipCutscene (UI command 0x9a) runs
// 0x73fcd0: no arguments, plain ret; it queues message 0x12/0x1b on the
// MessageManager. A cutscene entity listens for it only while playing and
// honours it when its data is IsSkippable (SP_02's CS_11 is), so calls made
// before it plays are ignored. The detour keeps calling it every
// SkipEveryFrames UI updates for skipSeconds after newSP, which also covers a
// chain of cutscenes, then restores the slot.
constexpr std::uintptr_t SkipCutscene = 0x73fcd0;
// E8 ?? ?? ?? ?? D9 EE 8B 48 18 8B 01 8B 50 20 51 D9 1C 24 6A 00 6A 1B 6A 12
constexpr unsigned char SkipCutsceneCode[] = {0xe8,0,0,0,0,0xd9,0xee,0x8b,0x48,0x18,0x8b,0x01,0x8b,0x50,0x20,0x51,0xd9,0x1c,0x24,0x6a,0x00,0x6a,0x1b,0x6a,0x12};
constexpr LONG SkipEveryFrames = 30;
volatile LONG skipArmed = 0;
int skipSeconds = 60;
// Set once the give has run on a settled soldier: the player has control,
// so the skip stops and the UI slot is restored early.
volatile LONG skipGameplaySeen = 0;
long long newSpTicks = 0;
LONG skipCalls = 0, skipNextCall = 0;
bool SkipCodeMatches() {
    const auto* code = reinterpret_cast<const unsigned char*>(SkipCutscene);
    for (unsigned i = 0; i < sizeof SkipCutsceneCode; ++i)
        if ((i < 1 || i > 4) && code[i] != SkipCutsceneCode[i]) return false;
    return true;
}
// If the menu never shows within this many UI updates (about ten minutes at
// 60 Hz), give up the start and restore the slot.
constexpr LONG StartGiveUpCalls = 60 * 60 * 10;
void __fastcall UpdateDetour(void* self, void*, std::uintptr_t a, std::uintptr_t b);

// Puts the original update back into the vtable slot, only while the slot
// still holds our detour. Safe from inside the detour: it calls the
// original directly, never through the slot. quiet: no logging (DllMain).
bool RestoreUiSlot(const char* why, bool quiet = false) {
    auto* slot = reinterpret_cast<volatile LONG*>(UiUpdateSlot);
    const LONG detour = LONG(reinterpret_cast<std::uintptr_t>(&UpdateDetour));
    if (*slot != detour) return false;
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(UiUpdateSlot), 4, PAGE_READWRITE, &old)) {
        if (!quiet) Log("UI update slot 0x%x NOT restored (%s): VirtualProtect failed %lu", unsigned(UiUpdateSlot), why, GetLastError());
        return false;
    }
    const bool restored = InterlockedCompareExchange(slot, LONG(UiUpdate), detour) == detour;
    VirtualProtect(reinterpret_cast<void*>(UiUpdateSlot), 4, old, &old);
    if (!quiet) {
        if (restored) Log("UI update slot 0x%x restored to 0x%x (%s)", unsigned(UiUpdateSlot), unsigned(UiUpdate), why);
        else Log("UI update slot 0x%x changed concurrently; left as is (%s)", unsigned(UiUpdateSlot), why);
    }
    return restored;
}

void __fastcall UpdateDetour(void* self, void*, std::uintptr_t a, std::uintptr_t b) {
    const LONG call = InterlockedIncrement(&detourCalls);
    if (call == 1 || call == 30) Log("update detour call %ld this=%p uimgr=%p thread=%lu", call, self,
        *reinterpret_cast<void* volatile*>(UiManagerGlobal), GetCurrentThreadId());
    bool finished = false;
    const char* why = nullptr;
    if (startArmed && !started) {
        void* menu = *reinterpret_cast<void* volatile*>(reinterpret_cast<char*>(self) + MainMenuOffset);
        if (menu && ++menuFrames >= unsigned(startWaitFrames) && !InterlockedExchange(&started, 1)) {
            Log("calling newSP(%d) menu=%p", startDifficulty, menu);
            reinterpret_cast<NewSpFn>(NewSinglePlayer)(self, startDifficulty);
            Log("newSP(%d) queued on the UI thread after %u menu frames, %.1f s after DLL load", startDifficulty, menuFrames,
                double(GetTickCount64() - launchTicks) / 1000.0);
            newSpTicks = GetTickCount64();
            if (skipArmed) { skipNextCall = call + SkipEveryFrames; Log("skip: calling skipCutscene every %ld UI updates for %d s", SkipEveryFrames, skipSeconds); }
            else { finished = true; why = "newSP called"; }
        } else if (!menu && call >= StartGiveUpCalls && !InterlockedExchange(&started, 1)) {
            Log("REFUSED: main menu never appeared in %ld UI updates; newSP not called", call);
            finished = true; why = "start abandoned";
        }
    }
    if (skipArmed && started && newSpTicks && call >= skipNextCall) {
        const double since = double(GetTickCount64() - newSpTicks) / 1000.0;
        if (skipGameplaySeen) {
            Log("skip: stopped after %ld skipCutscene calls, %.1f s after newSP: give done, player in control", skipCalls, since);
            InterlockedExchange(&skipArmed, 0); finished = true; why = "gameplay reached";
        } else if (since >= skipSeconds) {
            Log("skip: window over after %ld skipCutscene calls, %.1f s after newSP", skipCalls, since);
            InterlockedExchange(&skipArmed, 0); finished = true; why = "skip window over";
        } else {
            reinterpret_cast<void(*)()>(SkipCutscene)();
            ++skipCalls; skipNextCall = call + SkipEveryFrames;
            if (skipCalls <= 3 || skipCalls % 20 == 0) Log("skip: skipCutscene call %ld, %.1f s after newSP", skipCalls, since);
        }
    }
    reinterpret_cast<UpdateFn>(UiUpdate)(self, a, b);
    if (finished) RestoreUiSlot(why);
}

bool ArmStart() {
    if (std::memcmp(reinterpret_cast<const void*>(UiUpdate), UiUpdateCode, sizeof UiUpdateCode)
        || std::memcmp(reinterpret_cast<const void*>(NewSinglePlayer), NewSpCode, sizeof NewSpCode)
        || *reinterpret_cast<std::uintptr_t*>(UiUpdateSlot) != UiUpdate) return false;
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(UiUpdateSlot), 4, PAGE_READWRITE, &old)) return false;
    *reinterpret_cast<std::uintptr_t*>(UiUpdateSlot) = reinterpret_cast<std::uintptr_t>(&UpdateDetour);
    VirtualProtect(reinterpret_cast<void*>(UiUpdateSlot), 4, old, &old);
    return true;
}
using SetFn = void(__thiscall*)(void* self, const EaString* key, const EaString* value);
using GetFn = void*(__thiscall*)(void* self, const char* name, const void* fallback);

HMODULE self;
FILE* logFile;
SRWLOCK logLock = SRWLOCK_INIT; // the worker and the game thread both log

void Log(const char* format, ...) {
    if (!logFile) return;
    AcquireSRWLockExclusive(&logLock);
    SYSTEMTIME t; GetLocalTime(&t);
    std::fprintf(logFile, "%02u:%02u:%02u.%03u [%lu] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId());
    va_list args; va_start(args, format); std::vfprintf(logFile, format, args); va_end(args);
    std::fputc('\n', logFile); std::fflush(logFile);
    ReleaseSRWLockExclusive(&logLock);
}

std::wstring Beside(const wchar_t* name) {
    wchar_t path[MAX_PATH]{}; GetModuleFileNameW(self, path, MAX_PATH);
    std::wstring s(path); s.resize(s.find_last_of(L"\\/") + 1); return s + name;
}

bool BuildMatches() {
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(GetModuleHandleW(nullptr));
    if (reinterpret_cast<std::uintptr_t>(dos) != ImageBase || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(reinterpret_cast<const char*>(dos) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.SizeOfImage != ImageBytes || nt->FileHeader.TimeDateStamp != PeTimestamp) return false;
    return !std::memcmp(reinterpret_cast<const void*>(SetByName), SetCode, sizeof SetCode)
        && !std::memcmp(reinterpret_cast<const void*>(GetByName), GetCode, sizeof GetCode)
        && !std::memcmp(reinterpret_cast<const void*>(StringFromCString), StringCode, sizeof StringCode);
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0; while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// Same line format as the engine's dead loader: "Key value", # comments.
std::vector<std::pair<std::string, std::string>> ReadConfig() {
    std::vector<std::pair<std::string, std::string>> out;
    FILE* f = _wfopen(Beside(L"BC2TestRange.cfg").c_str(), L"r");
    if (!f) return out;
    char line[1024];
    while (std::fgets(line, sizeof line, f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == '#' || s[0] == ';') continue;
        auto space = s.find_first_of(" \t");
        if (space == std::string::npos) continue;
        std::string key = s.substr(0, space), value = Trim(s.substr(space));
        if (key.find('.') == std::string::npos || key.size() > 128 || value.size() > 256) continue;
        out.emplace_back(key, value);
    }
    std::fclose(f);
    return out;
}

EaString View(const std::string& s) {
    return {s.c_str(), s.c_str() + s.size(), s.c_str() + s.size() + 1, nullptr};
}

// SEH helpers stay free of C++ objects (MSVC C2712).
bool ReadEaString(const void* field, char* out, std::size_t size) {
    __try {
        auto* s = static_cast<const EaString*>(field);
        if (!s->begin || s->end < s->begin || std::size_t(s->end - s->begin) >= size - 2) return false;
        std::snprintf(out, size, "\"%.*s\"", int(s->end - s->begin), s->begin);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadBytes(const void* field, char* out, std::size_t size) {
    __try {
        auto* b = static_cast<const unsigned char*>(field);
        std::snprintf(out, size, "bytes %02x %02x %02x %02x", b[0], b[1], b[2], b[3]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

std::string Describe(void* field, const std::string& value) {
    if (!field) return "<absent>";
    char text[300];
    // String fields are eastl::string; bool/int fields are plain scalars.
    const bool stringLike = !value.empty() && (value[0] == '/' || std::isalpha(static_cast<unsigned char>(value[0])))
        && value != "true" && value != "false";
    if (stringLike) return ReadEaString(field, text, sizeof text) ? text : "<unreadable string>";
    return ReadBytes(field, text, sizeof text) ? text : "<unreadable>";
}

// ---- Extra level bundles ---------------------------------------------------
// A level only holds the SoldierWeaponData its own bundles carry: SP_02 mounts
// sp_02 and sp_common, so multiplayer-only weapons (M9 US_hg_M9-3, M1911,
// MP412, ...) are not loadable there; they live in mp_common/mp2_common.
// Bundle redirects come from Dist/win32/package.mft, parsed at boot into a
// backend object in main's frame (ctor 0xbd2bd0, vtable 0x1479698), published
// at [0x155d000]; its +0x324 byte goes to 1 once the file is parsed (0xbd3575).
// Each "BundleSource" node (32 hash buckets at +0x32c, chained at node+0x48)
// holds up to 16 interned targets: +4 count, +8 targets. GetTarget (0xbd2d30)
// hands them out in reverse, so a line appended last is mounted first and has
// the lowest priority, which is how the file lists the *_common bundles.
//
// TestRange.Mount "<source> <target>" lines are fed through the engine's own
// line parser ParseLine (0xbd3040, thiscall on a parser state, (line, end,
// backend, mode 2), ret 0x10), as a BundleSource line then a BundleTarget
// line, so game:/package: expand exactly as the file's do and the add runs
// through AddTarget (0xbd2fb0). Nothing on disk changes. It runs after the
// parse and long before newSP; level loads are the only readers of a level
// node, and the backend's string and node pools are only used by the parser.
constexpr std::uintptr_t BundleBackendGlobal = 0x155d000, BundleBackendVtable = 0x1479698;
constexpr std::uintptr_t ParseMftLine = 0xbd3040, AddBundleTarget = 0xbd2fb0;
constexpr std::size_t BackendParsed = 0x324, BackendBuckets = 0x32c, ParserSource = 0x68;
constexpr std::size_t NodeCount = 4, NodeTargets = 8, NodeNext = 0x48, NodeMaxTargets = 16;
// 0xbd3040: sub esp,0x210; push ebx; mov ebx,[esp+0x218]; cmp byte [ebx],0; push esi; push edi; mov esi,ecx
constexpr unsigned char ParseMftLineCode[] = {0x81,0xec,0x10,0x02,0x00,0x00,0x53,0x8b,0x9c,0x24,0x18,0x02,0x00,0x00,0x80,0x3b,0x00,0x56,0x57,0x8b,0xf1};
// 0xbd2fb0: push ebx; push ebp; mov ebp,[esp+0xc]; push esi; push edi; push ebp; mov edi,ecx; call hash
constexpr unsigned char AddBundleTargetCode[] = {0x53,0x55,0x8b,0x6c,0x24,0x0c,0x56,0x57,0x55,0x8b,0xf9,0xe8};
using ParseLineFn = bool(__thiscall*)(void* parser, const char* line, const char* end, void* backend, int mode);
std::vector<std::pair<std::string, std::string>> mounts;
bool traceUi, netIdRegister, reequipGuard, shaderOrder = true, gapFill = true;
std::string levelPath;  // Client.Level, e.g. "Levels/SP_02"

// The engine's own source hash (0xbd27b0): djb2 xor over lower-cased bytes.
std::uint32_t BundleHash(const char* s) {
    std::uint32_t h = 0x1505;
    for (; *s; ++s) h = (h * 33) ^ std::uint32_t(std::tolower(static_cast<unsigned char>(*s)) & 0xff);
    return h;
}

// Logs the node's targets; returns its count, or -1 when there is no node.
int LogBundleNode(const char* backend, const char* source) {
    __try {
        auto* node = *reinterpret_cast<char* const*>(backend + BackendBuckets + (BundleHash(source) & 31) * 4);
        for (; node; node = *reinterpret_cast<char* const*>(node + NodeNext)) {
            if (_stricmp(*reinterpret_cast<const char* const*>(node), source)) continue;
            const int count = *reinterpret_cast<const int*>(node + NodeCount);
            for (int i = 0; i < count && i < int(NodeMaxTargets); ++i)
                Log("mount:   %s target %d %s", source, i, *reinterpret_cast<const char* const*>(node + NodeTargets + i * 4));
            return count;
        }
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

bool ParseLineGuarded(void* parser, const char* line, const char* end, void* backend) {
    __try { return reinterpret_cast<ParseLineFn>(ParseMftLine)(parser, line, end, backend, 2); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Foreign level bundles carry data whose references point into bundles this
// level never mounts (or at same-named partitions the level's own bundles
// already loaded in a different version), so those references stay null.
// The level load's blueprint walk assumes every reference resolved. Each guard
// below replaces one native load with a jump to a stub that does the same
// load, null-checks it, and on null leaves through the native code's own skip
// or epilogue. Both crashes are from live crash reports with mp_common on SP_02.
//
// WeaponUnlocks: walk 0x5ac4c0 null-checks its SoldierEntityData Weapons loop
// (0x5ac62c) but not the WeaponUnlocks loop (+0x1a0, 12-byte
// SoldierWeaponUnlockData, Weapon at +4): it died at 0x5ac6d8 with ecx 0.
// Loop head 0x5ac6d6: mov ecx,[ebx]; mov esi,[ecx+0x50]; test esi,esi; je.
// A null Weapon or +0x50 goes to the loop's own increment 0x5ac759.
constexpr unsigned char UnlockLoopCode[] = {0x8b,0x0b,0x8b,0x71,0x50,0x85,0xf6,0x74,0x19};
// Weapon registration 0x5a5a00 (thiscall, one argument, ret 4) reads a
// WeaponComponentData's +0xd8 or a SoldierWeaponData's +0x98 into [esp+0x10]
// and dereferences it at 0x5a5a61 (died with edx 0). At 0x5a5a5d: mov edx,
// [esp+0x10]; mov edi,[edx+0x40]. A null either way leaves through the
// function's epilogue 0x5a5d9d (pop edi/esi/ebp/ebx; add esp,0x50; ret 4).
constexpr unsigned char WeaponRegisterCode[] = {0x8b,0x54,0x24,0x10,0x8b,0x7a,0x40,0x8b,0x87,0x84,0x01,0x00,0x00};
// Client re-equip 0x8bbd40 fetches the selected client weapon (0x882640,
// slot vector +0x260) and reads its +0x30 at once (0x7efcb0). Drawing a weapon
// from a foreign bundle the moment it is given finds that entry still null
// (live fault: read 0x30 at 0x7efcb0, 6 ms after the give). At 0x8bbdea:
// call 0x882640; mov ecx,eax. A null skips the equip block to 0x8bbe17.
constexpr unsigned char ReequipCode[] = {0xe8,0x51,0x68,0xfc,0xff,0x8b,0xc8,0xe8,0xba,0x3e,0xf3,0xff};

struct Emitter {
    unsigned char* p;
    void Bytes(std::initializer_list<unsigned char> b) { for (auto c : b) *p++ = c; }
    void Rel(std::uintptr_t to) { const auto d = std::int32_t(to - (reinterpret_cast<std::uintptr_t>(p) + 4)); std::memcpy(p, &d, 4); p += 4; }
    void Jz(std::uintptr_t to) { Bytes({0x0f, 0x84}); Rel(to); }
    void Jmp(std::uintptr_t to) { Bytes({0xe9}); Rel(to); }
};

// Verifies `expected` at `site`, then points the site's first five bytes at
// the stub `build` writes.
template <std::size_t N, class Build>
bool InstallGuard(const char* name, std::uintptr_t site, const unsigned char (&expected)[N], Build build) {
    auto* head = reinterpret_cast<unsigned char*>(site);
    if (std::memcmp(head, expected, N)) { Log("mount: REFUSED: %s guard site 0x%x did not verify", name, unsigned(site)); return false; }
    auto* stub = static_cast<unsigned char*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    DWORD old = 0;
    if (!stub || !VirtualProtect(head, 5, PAGE_EXECUTE_READWRITE, &old)) { Log("mount: REFUSED: %s guard could not be written", name); return false; }
    Emitter e{stub}; build(e, site);
    Emitter site5{head}; site5.Jmp(reinterpret_cast<std::uintptr_t>(stub));
    VirtualProtect(head, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), head, 5);
    Log("mount: %s guard at 0x%x (stub %p)", name, unsigned(site), stub);
    return true;
}

// With foreign bundles the game can quit on its own; log who asked. The
// game links the CRT dynamically, so its import slots for msvcr80 exit
// (0x1405504) and _exit (0x14056d0) and user32 PostQuitMessage (0x14058b4)
// are swapped for loggers that record the caller's return addresses and then
// do what was asked. Each slot must hold the export GetProcAddress returns.
using QuitFn = void(__cdecl*)(int);
using PostQuitFn = void(WINAPI*)(int);
QuitFn exitOriginal, underscoreExitOriginal;
PostQuitFn postQuitOriginal;
void LogQuit(const char* what, int code) {
    void* frames[32]{};
    const USHORT n = RtlCaptureStackBackTrace(2, 32, frames, nullptr);
    char text[32 * 11 + 1]{}; int at = 0;
    for (USHORT i = 0; i < n; ++i) at += std::snprintf(text + at, sizeof text - at, " %p", frames[i]);
    Log("exit: %s(%d) on thread %lu from%s", what, code, GetCurrentThreadId(), text);
}
void __cdecl ExitLogged(int code) { LogQuit("exit", code); exitOriginal(code); }
void __cdecl UnderscoreExitLogged(int code) { LogQuit("_exit", code); underscoreExitOriginal(code); }
void WINAPI PostQuitLogged(int code) { LogQuit("PostQuitMessage", code); postQuitOriginal(code); }

template <class Fn>
void SwapImport(std::uintptr_t slot, const wchar_t* module, const char* name, Fn& original, Fn logger) {
    auto* entry = reinterpret_cast<Fn*>(slot);
    const auto expected = reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(module), name));
    DWORD old = 0;
    if (!expected || *entry != expected || !VirtualProtect(entry, 4, PAGE_READWRITE, &old)) { Log("exit: %s import slot 0x%x not proven; not logged", name, unsigned(slot)); return; }
    original = expected; *entry = logger;
    VirtualProtect(entry, 4, old, &old);
}

// The main loop (0x570330) ends when the quit flag [0x155cfec] is set: by
// the loop itself when the app update returns false (0x57038e), or by
// 0x5418c1, 0x77c121 (mov [flag],1) and 0x9638bf (mov [flag],ecx). Each store
// becomes a jump to a stub that logs the site and the code addresses on the
// stack above it, then does the store and returns.
void __cdecl LogQuitFlag(std::uintptr_t site, const std::uint32_t* stack) {
    char text[24 * 11 + 1]{}; int at = 0, found = 0;
    const auto [lo, hi] = std::pair<std::uint32_t, std::uint32_t>{0x401000, 0x13b6000};
    for (int i = 0; i < 256 && found < 24; ++i) {
        std::uint32_t v = 0;
        __try { v = stack[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { break; }
        if (v >= lo && v < hi) { at += std::snprintf(text + at, sizeof text - at, " %08x", v); ++found; }
    }
    Log("exit: at 0x%x on thread %lu; stack code addresses%s", unsigned(site), GetCurrentThreadId(), text);
}
constexpr std::uintptr_t QuitFlag = 0x155cfec;
constexpr unsigned char QuitStoreOne[] = {0xc7,0x05,0xec,0xcf,0x55,0x01,0x01,0x00,0x00,0x00};
constexpr unsigned char QuitStoreEcx[] = {0x89,0x0d,0xec,0xcf,0x55,0x01};

// The engine's top-level handler turns a crash into a clean return from
// WinMain and the shutdown that follows overwrites crashreport.xml, so the
// first access violations are logged here as they are raised.
volatile LONG faultsLogged = 0;
LONG CALLBACK LogFault(EXCEPTION_POINTERS* info) {
    const CONTEXT& c = *info->ContextRecord;
    // The give's memory scan probes under __try; its faults are expected.
    HMODULE at = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(std::uintptr_t(c.Eip)), &at);
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || at == self || InterlockedIncrement(&faultsLogged) > 8) return EXCEPTION_CONTINUE_SEARCH;
    Log("fault: access violation at %08lx (%s %08lx) thread %lu eax %08lx ebx %08lx ecx %08lx edx %08lx esi %08lx edi %08lx ebp %08lx esp %08lx",
        c.Eip, info->ExceptionRecord->ExceptionInformation[0] ? "write" : "read", ULONG(info->ExceptionRecord->ExceptionInformation[1]), GetCurrentThreadId(),
        c.Eax, c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
    LogQuitFlag(c.Eip, reinterpret_cast<const std::uint32_t*>(c.Esp));
    return EXCEPTION_CONTINUE_SEARCH;
}

void LogGameExits() {
    AddVectoredExceptionHandler(1, &LogFault);
    for (std::uintptr_t site : {0x57038eu, 0x5418c1u, 0x77c121u, 0x9638bfu}) {
        const bool ecx = site == 0x9638bf;
        const auto build = [](Emitter& e, std::uintptr_t at) {
            const bool fromEcx = at == 0x9638bf;
            e.Bytes({0x60, 0x8d, 0x44, 0x24, 0x20, 0x50});            // pushad; lea eax,[esp+0x20]; push eax
            e.Bytes({0x68}); const auto s = std::uint32_t(at); std::memcpy(e.p, &s, 4); e.p += 4; // push site
            e.Bytes({0xe8}); e.Rel(reinterpret_cast<std::uintptr_t>(&LogQuitFlag));             // call logger
            e.Bytes({0x83, 0xc4, 0x08, 0x61});                        // add esp,8; popad
            if (fromEcx) { e.Bytes({0x89, 0x0d}); const auto f = std::uint32_t(QuitFlag); std::memcpy(e.p, &f, 4); e.p += 4; e.Jmp(at + 6); }
            else { e.Bytes({0xc7, 0x05}); const auto f = std::uint32_t(QuitFlag); std::memcpy(e.p, &f, 4); e.p += 4; e.Bytes({1, 0, 0, 0}); e.Jmp(at + 10); }
        };
        if (ecx) InstallGuard("quit-flag", site, QuitStoreEcx, build);
        else InstallGuard("quit-flag", site, QuitStoreOne, build);
    }
    SwapImport<QuitFn>(0x1405504, L"msvcr80.dll", "exit", exitOriginal, &ExitLogged);
    SwapImport<QuitFn>(0x14056d0, L"msvcr80.dll", "_exit", underscoreExitOriginal, &UnderscoreExitLogged);
    SwapImport<PostQuitFn>(0x14058b4, L"user32.dll", "PostQuitMessage", postQuitOriginal, &PostQuitLogged);
    Log("exit: exit/_exit/PostQuitMessage imports logged");
}

// UI trace for foreign-bundle runs: every UIManager game event (0x929a70,
// thiscall, const char* name) and every queued UI request (0x956e10,
// thiscall, request*: +0 type, 0x110 bytes; newSP queues type 3), with the
// code addresses on the caller's stack, so a drop back to the menu shows
// which request or event started it.
using GameEventFn = void(__thiscall*)(void* ui, const char* name);
using PushRequestFn = void(__thiscall*)(void* ui, const std::uint32_t* request);
GameEventFn gameEventOriginal;
PushRequestFn pushRequestOriginal;
void __fastcall GameEventLogged(void* ui, void*, const char* name) {
    char text[64]{};
    __try { std::snprintf(text, sizeof text, "%s", name); } __except (EXCEPTION_EXECUTE_HANDLER) { std::strcpy(text, "?"); }
    Log("ui: event \"%s\"", text);
    LogQuitFlag(0x929a70, reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    gameEventOriginal(ui, name);
}
void __fastcall PushRequestLogged(void* ui, void*, const std::uint32_t* request) {
    __try { Log("ui: request type %u [%08x %08x %08x %08x %08x %08x]", request[0], request[1], request[2], request[3], request[4], request[5], request[6]); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("ui: request (unreadable)"); }
    LogQuitFlag(0x956e10, reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    pushRequestOriginal(ui, request);
}
// UI state pushes: 0x9254b0 is push_back on the state deque at
// UIManager+0xc3ec0 (thiscall, int* state, ret 4); the state machine 0x956650
// pops them, and state 0 loads and shows the main menu.
using PushStateFn = void(__thiscall*)(void* deque, const int* state);
PushStateFn pushStateOriginal;
void __fastcall PushStateLogged(void* deque, void*, const int* state) {
    Log("ui: push state %d (deque %p)", state ? *state : -1, deque);
    LogQuitFlag(0x9254b0, reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    pushStateOriginal(deque, state);
}

// UIManager message handler 0x9640d0 (vtable 0x1440808, on UIManager+0x82af8):
// thiscall (category, type, data, extra), ret 0x10; two-level switch that
// pushes UI states (0x964fdc pushes 0, 0x9650fa pushes 4).
using UiMessageFn = void(__thiscall*)(void* self, int category, int type, void* data, int extra);
UiMessageFn uiMessageOriginal;
// Queued messages: the message manager is [ctx+0x18] with ctx from the
// lazily built singleton getter 0x7ff060; vtable +0x24 posts (category,
// message*, extra), message type at +0x20. Hooked from the first UI message
// (a game thread, singleton already built) to log who posts 0x2e and 0x25.
using PostFn = void(__thiscall*)(void* manager, int category, const std::uint32_t* message, int extra);
PostFn postOriginal;
volatile LONG postHooked = 0;
void __fastcall PostLogged(void* manager, void*, int category, const std::uint32_t* message, int extra) {
    if (category == 0x2e || category == 0x25) {
        std::uint32_t type = 0;
        __try { type = message ? message[8] : 0; } __except (EXCEPTION_EXECUTE_HANDLER) {}
        Log("ui: post category 0x%x type 0x%x message %p", category, type, message);
        LogQuitFlag(0x24, reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    }
    postOriginal(manager, category, message, extra);
}
void HookPost() {
    if (InterlockedExchange(&postHooked, 1)) return;
    void* ctx = reinterpret_cast<void* (*)()>(0x7ff060)();
    void* manager = ctx ? *reinterpret_cast<void**>(static_cast<char*>(ctx) + 0x18) : nullptr;
    void* post = manager ? (*reinterpret_cast<void***>(manager))[0x24 / 4] : nullptr;
    const bool ok = post && MH_CreateHook(post, reinterpret_cast<void*>(&PostLogged), reinterpret_cast<void**>(&postOriginal)) == MH_OK && MH_EnableHook(post) == MH_OK;
    Log("ui: message manager %p post %p hooked %d", manager, post, ok);
}
// Bundle loads: resource manager [0x154c444], vtable +0x70
// loadBundle(out handle*, const char* name, int flags) (weapon 1P bundles
// "async/weapon/<name>" via 0x80f120). Logged with the handle it returns.
using LoadBundleFn = void**(__thiscall*)(void* manager, void** out, const char* name, int flags);
LoadBundleFn loadBundleOriginal;
volatile LONG bundleHooked = 0;
void** __fastcall LoadBundleLogged(void* manager, void*, void** out, const char* name, int flags) {
    void** result = loadBundleOriginal(manager, out, name, flags);
    char text[128]{};
    __try { std::snprintf(text, sizeof text, "%s", name); } __except (EXCEPTION_EXECUTE_HANDLER) { std::strcpy(text, "?"); }
    void* handle = nullptr;
    __try { handle = result ? *result : nullptr; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("bundle: load \"%s\" flags %d -> %p", text, flags, handle);
    return result;
}
void HookBundleLoads() {
    if (InterlockedExchange(&bundleHooked, 1)) return;
    void* manager = *reinterpret_cast<void* volatile*>(0x154c444);
    void* load = manager ? (*reinterpret_cast<void***>(manager))[0x70 / 4] : nullptr;
    const bool ok = load && MH_CreateHook(load, reinterpret_cast<void*>(&LoadBundleLogged), reinterpret_cast<void**>(&loadBundleOriginal)) == MH_OK && MH_EnableHook(load) == MH_OK;
    Log("bundle: resource manager %p loadBundle %p hooked %d", manager, load, ok);
}

void __fastcall UiMessageLogged(void* self, void*, int category, int type, void* data, int extra) {
    HookPost();
    HookBundleLoads();
    // 0x2b/0x19, 0x2b/0x1b and 0x2b/0x1c arrive many times a second (HUD).
    if (category != 0x2b || (type != 0x19 && type != 0x1b && type != 0x1c))
        Log("ui: message category %d type 0x%x data %p extra %d", category, type, data, extra);
    // The level-exit chain seen in mount runs: 0x1b/0x27 and 0x25/0x28 right
    // after spawn, then 0x2e/0x14 (push state 4) and 0x2e/0x10 (push state 0).
    if ((category == 0x1b && type == 0x27) || (category == 0x25 && type == 0x28) || category == 0x2e)
        LogQuitFlag(0x9640d0, reinterpret_cast<const std::uint32_t*>(_AddressOfReturnAddress()));
    uiMessageOriginal(self, category, type, data, extra);
}

// Client connection watch. 0x655cb0 is the client connection update
// (thiscall, float dt): state at +0x54 (2 = disconnect requested, 3 = tear
// down), reason at +0x58, server peer at +0x18. On its first call a hardware
// write watchpoint (DR0, 4 bytes) goes on this+0x54 in every thread; the
// vectored handler logs each write with its code address and stack.
using ConnUpdateFn = void(__thiscall*)(void* self, float dt);
ConnUpdateFn connUpdateOriginal;
volatile LONG connWatched = 0;
std::uintptr_t connStateAddress;

LONG CALLBACK ConnWatchFault(EXCEPTION_POINTERS* info) {
    if (info->ExceptionRecord->ExceptionCode != EXCEPTION_SINGLE_STEP || !(info->ContextRecord->Dr6 & 1)) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT& c = *info->ContextRecord;
    const auto* state = reinterpret_cast<const std::uint32_t*>(connStateAddress);
    Log("watch: connection state now %u reason %u, written by code before %08lx on thread %lu", state[0], state[1], c.Eip, GetCurrentThreadId());
    LogQuitFlag(c.Eip, reinterpret_cast<const std::uint32_t*>(c.Esp));
    c.Dr6 = 0;
    return EXCEPTION_CONTINUE_EXECUTION;
}

DWORD WINAPI ArmConnWatch(void*) {
    const DWORD self = GetCurrentThreadId(), pid = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te{sizeof te};
    int armed = 0;
    for (BOOL more = Thread32First(snap, &te); more; more = Thread32Next(snap, &te)) {
        if (te.th32OwnerProcessID != pid || te.th32ThreadID == self) continue;
        HANDLE t = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
        if (!t) continue;
        if (SuspendThread(t) != DWORD(-1)) {
            CONTEXT c{}; c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
            if (GetThreadContext(t, &c)) {
                c.Dr0 = connStateAddress;
                c.Dr7 = (c.Dr7 & ~0x000f0003ul) | 1ul | (1ul << 16) | (3ul << 18); // L0, write, 4 bytes
                if (SetThreadContext(t, &c)) ++armed;
            }
            ResumeThread(t);
        }
        CloseHandle(t);
    }
    CloseHandle(snap);
    Log("watch: DR0 on connection state %08x armed in %d threads", unsigned(connStateAddress), armed);
    return 0;
}

void __fastcall ConnUpdateLogged(void* self, void*, float dt) {
    if (!InterlockedExchange(&connWatched, 1)) {
        connStateAddress = reinterpret_cast<std::uintptr_t>(self) + 0x54;
        Log("watch: client connection %p state %u reason %u", self, reinterpret_cast<std::uint32_t*>(connStateAddress)[0], reinterpret_cast<std::uint32_t*>(connStateAddress)[1]);
        AddVectoredExceptionHandler(1, &ConnWatchFault);
        if (HANDLE t = CreateThread(nullptr, 0, ArmConnWatch, nullptr, 0, nullptr)) CloseHandle(t);
    }
    connUpdateOriginal(self, dt);
}

void TraceUi() {
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) return;
    if (MH_CreateHook(reinterpret_cast<void*>(0x655cb0), reinterpret_cast<void*>(&ConnUpdateLogged), reinterpret_cast<void**>(&connUpdateOriginal)) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(0x655cb0)) != MH_OK) Log("watch: connection update hook failed");
    if (MH_CreateHook(reinterpret_cast<void*>(0x9640d0), reinterpret_cast<void*>(&UiMessageLogged), reinterpret_cast<void**>(&uiMessageOriginal)) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(0x9640d0)) != MH_OK) Log("ui: message hook failed");
    if (MH_CreateHook(reinterpret_cast<void*>(0x9254b0), reinterpret_cast<void*>(&PushStateLogged), reinterpret_cast<void**>(&pushStateOriginal)) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(0x9254b0)) != MH_OK) Log("ui: state push hook failed");
    const bool events = MH_CreateHook(reinterpret_cast<void*>(0x929a70), reinterpret_cast<void*>(&GameEventLogged), reinterpret_cast<void**>(&gameEventOriginal)) == MH_OK
        && MH_EnableHook(reinterpret_cast<void*>(0x929a70)) == MH_OK;
    const bool requests = MH_CreateHook(reinterpret_cast<void*>(0x956e10), reinterpret_cast<void*>(&PushRequestLogged), reinterpret_cast<void**>(&pushRequestOriginal)) == MH_OK
        && MH_EnableHook(reinterpret_cast<void*>(0x956e10)) == MH_OK;
    Log("ui: trace events %d requests %d", events, requests);
}

bool InstallNullGuards() {
    LogGameExits();
    TraceUi();
    const bool unlocks = InstallGuard("weapon-unlock", 0x5ac6d6, UnlockLoopCode, [](Emitter& e, std::uintptr_t) {
        e.Bytes({0x8b, 0x0b, 0x85, 0xc9}); e.Jz(0x5ac759);           // mov ecx,[ebx]; test ecx,ecx
        e.Bytes({0x8b, 0x71, 0x50, 0x85, 0xf6}); e.Jz(0x5ac759);     // mov esi,[ecx+0x50]; test esi,esi
        e.Jmp(0x5ac6df);
    });
    const bool weapons = InstallGuard("weapon-register", 0x5a5a5d, WeaponRegisterCode, [](Emitter& e, std::uintptr_t) {
        e.Bytes({0x8b, 0x54, 0x24, 0x10, 0x85, 0xd2}); e.Jz(0x5a5d9d); // mov edx,[esp+0x10]; test edx,edx
        e.Bytes({0x8b, 0x7a, 0x40, 0x85, 0xff}); e.Jz(0x5a5d9d);       // mov edi,[edx+0x40]; test edi,edi
        e.Jmp(0x5a5a64);
    });
    const bool reequip = !reequipGuard || InstallGuard("client-reequip", 0x8bbdea, ReequipCode, [](Emitter& e, std::uintptr_t) {
        e.Bytes({0xe8}); e.Rel(0x882640);                            // call selected client weapon (ecx = esi)
        e.Bytes({0x85, 0xc0}); e.Jz(0x8bbe17);                      // test eax,eax
        e.Bytes({0x8b, 0xc8}); e.Jmp(0x8bbdf1);                     // mov ecx,eax
    });
    return unlocks && weapons && reequip;
}

// Mesh shaders come from per-level shader databases
// (Levels/<L>/Shaders/Dx11_Single.res); mp_common and mp2_common carry none, and
// SP_02's has no entries for MP weapon materials (US_rif_XM8c/Shaders,
// US_hg_M9Beretta/Shaders), so those meshes never draw. Mounting an MP level
// patch bundle (e.g. package:levels/mp_009sdm) loads its database as a normal
// resource, and the Dx11ShaderSystem [0x1572b18] registers it through vtable
// 0x14481f8 +0x68 = RegisterDatabase 0x9cb590 (thiscall (db, ctx), ret 8): it
// appends db to the vector at ss+0x220 and re-merges every database in vector
// order: 0x9c8120 clears the merged maps and calls MergeDatabase 0x9c3f60
// (thiscall on ss, (db, &vector), ret 8) for each. In the first two maps the
// last database merged wins a shared name; the third map's per-name permutation
// list is rebuilt from every database in vector order, the first one winning.
// The database name is the char* at db+0x8c.
//
// SP's shaders must win everywhere and the foreign database only fill gaps, so
// the vector keeps its native order (the level's database before the later
// foreign one, first in the third map) and, after the last database merges,
// the level's database is merged once more, so it also wins the first two
// maps. (Moving the level database last instead made the foreign permutations
// win the third map: SP fence posts rendered black and red.)
constexpr std::uintptr_t ShaderSystemGlobal = 0x1572b18, MergeDatabase = 0x9c3f60;
constexpr std::size_t SsShaderState = 0x23c, ShaderStateFlag = 0xce, DbStateFlag = 0xec, DbName = 0x8c;
// 0x9c3f60: push ebp; mov ebp,esp; and esp,-8; sub esp,0x674; push ebx; push esi; mov esi,ecx
constexpr unsigned char MergeDatabaseCode[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x81,0xec,0x74,0x06,0x00,0x00,0x53,0x56,0x8b,0xf1};
using MergeDatabaseFn = void(__thiscall*)(void* ss, void* db, void* databases);
MergeDatabaseFn mergeOriginal;

const char* DatabaseName(void* db) {
    __try { return *reinterpret_cast<const char* const*>(static_cast<char*>(db) + DbName); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool StartsWithI(const char* s, const std::string& prefix) {
    return s && _strnicmp(s, prefix.c_str(), prefix.size()) == 0;
}

void __fastcall MergeDatabaseDetour(void* ss, void*, void* db, void* databases) {
    mergeOriginal(ss, db, databases);
    auto** begin = static_cast<void***>(databases)[0];
    auto** end = static_cast<void***>(databases)[1];
    if (begin == end || end[-1] != db) return;  // only after the last one
    const std::string own = levelPath + "/";
    void* level = nullptr; unsigned foreign = 0;
    for (auto** at = begin; at != end; ++at) {
        const char* name = DatabaseName(*at);
        if (StartsWithI(name, own)) level = *at;
        else if (StartsWithI(name, "Levels/")) ++foreign;
    }
    if (!level || !foreign || level == db) return;
    // 0x9c8120 sets this flag from each database before merging it.
    if (auto* state = *reinterpret_cast<char**>(static_cast<char*>(ss) + SsShaderState))
        state[ShaderStateFlag] = static_cast<char*>(level)[DbStateFlag];
    mergeOriginal(ss, level, databases);
    static unsigned logged;
    if (logged != foreign) {
        logged = foreign;
        Log("shaders: %s merged again after %u foreign database(s); they only fill gaps", DatabaseName(level), foreign);
    }
}

void InstallShaderOrder() {
    if (levelPath.empty() || std::memcmp(reinterpret_cast<const void*>(MergeDatabase), MergeDatabaseCode, sizeof MergeDatabaseCode)) {
        Log("shaders: REFUSED: MergeDatabase 0x%x did not verify or Client.Level unset", unsigned(MergeDatabase));
        return;
    }
    if (const MH_STATUS init = MH_Initialize(); init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { Log("shaders: REFUSED: MinHook init failed"); return; }
    if (MH_CreateHook(reinterpret_cast<void*>(MergeDatabase), reinterpret_cast<void*>(&MergeDatabaseDetour), reinterpret_cast<void**>(&mergeOriginal)) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(MergeDatabase)) != MH_OK) {
        Log("shaders: REFUSED: MergeDatabase hook failed"); return;
    }
    Log("shaders: MergeDatabase 0x%x hooked; %s shaders win, foreign databases fill gaps", unsigned(MergeDatabase), levelPath.c_str());
}

// Mounted bundles only fill gaps. The level bundle loader 0x4b56a0 gets the
// bundle's targets from 0x4a5c50, reads them first-listed first and keeps a
// "seen" name set, so SP's own targets already win every name they carry. But
// SP's targets also carry deleted entries (flag bit 0 of the entry iterator's
// output, kind "*deleted*"); the loader puts their names into the seen set,
// so a deletion read first shadows the live copy a mounted bundle brings:
// SP_02 deletes 25 *_Scoped_AnimTree1p.res, and the MG3/F2000/M95/QBY88/QJY88/
// AKS74u/SCAR Kobra and Scoped variants built a 1P anim tree from a null
// resource (client fault at 0xf691cf). A name SP deleted is a gap: the filter
// keeps it hidden from SP's own later targets, exactly as native, lets the
// first live copy from a mounted target fill it, and ignores every deletion a
// mounted target carries. Site 0x4b59ea, per entry, before the deleted-flag
// branch: test byte [esp+0x104],1 (deleted flag); name [esp+0xec], kind
// [esp+0xf0], current archive path [esp+0xa0]; 0x4b6370 is the native
// next-entry path (it balances the 0x509340 scope opened at 0x4b59d4). edx
// (0) and ebp are live; the displaced test sets the flags its je reads.
constexpr std::uintptr_t EntryFilterSite = 0x4b59ea, EntryFilterResume = 0x4b59f2, EntryFilterSkip = 0x4b6370;
// test byte [esp+0x104],1; je 0x4b5a74
constexpr unsigned char EntryFilterCode[] = {0xf6,0x84,0x24,0x04,0x01,0x00,0x00,0x01,0x0f,0x84};
std::vector<std::string> mountedSuffixes;  // "levels/mp_common/level-00.fbrb"
// Bundle iteration owns its path storage. Retain a value, never its borrowed
// pointer: another callback may run after the prior archive storage is freed.
std::string filterLastArchive;
bool filterLastMounted;
unsigned filterFills, filterLoads;
std::unordered_set<std::uint32_t> levelDeleted;
volatile LONG entrySkip;

bool ReadU32(std::uintptr_t at, std::uint32_t& out);
bool ReadU8(std::uintptr_t at, std::uint8_t& out);
bool ReadCString(std::uintptr_t at, char* out, std::size_t size);

bool IsMountedPath(const char* path) {
    if (!path) return false;
    const std::size_t n = std::strlen(path);
    for (const auto& suffix : mountedSuffixes)
        if (n >= suffix.size() && !_stricmp(path + n - suffix.size(), suffix.c_str())) return true;
    return false;
}

const char* FileName(const char* path) {
    const char* slash = std::strrchr(path, '/');
    return slash ? slash + 1 : path;
}

int __cdecl EntryFilter(const std::uint8_t* sp) {
    std::uint32_t nameAt = 0, archiveAt = 0;
    std::uint8_t flags = 0;
    char name[1024]{}, archive[1024]{};
    const auto stack = reinterpret_cast<std::uintptr_t>(sp);
    // Snapshot complete strings under the existing bounded SEH reader before
    // hashing, logging or comparing. Invalid input leaves the native decision.
    if (!ReadU32(stack + 0xec, nameAt) || !ReadU32(stack + 0xa0, archiveAt)
        || !ReadU8(stack + 0x104, flags)
        || !ReadCString(nameAt, name, sizeof name)
        || !ReadCString(archiveAt, archive, sizeof archive)) return 0;
    const bool deleted = flags & 1;
    const bool mounted = IsMountedPath(archive);
    if (filterLastArchive.empty() || _stricmp(archive, filterLastArchive.c_str())) {
        // A new bundle load starts with a different bundle file or with SP's
        // own target after a mounted one.
        if (filterLastArchive.empty() || _stricmp(FileName(archive), FileName(filterLastArchive.c_str())) || (!mounted && filterLastMounted)) {
            if (filterFills) Log("gapfill: load %u filled %u names SP had deleted", filterLoads, filterFills);
            levelDeleted.clear(); filterFills = 0; ++filterLoads;
        }
        filterLastArchive = archive; filterLastMounted = mounted;
    }
    const std::uint32_t hash = BundleHash(name);
    if (!mounted) {
        if (deleted) { levelDeleted.insert(hash); return 1; }
        return levelDeleted.count(hash) ? 1 : 0;  // hidden by a deletion higher up, as native
    }
    if (deleted) return 1;  // a mounted bundle never deletes anything
    if (levelDeleted.erase(hash) && ++filterFills <= 40) Log("gapfill: %s from %s", name, archive);
    return 0;
}

void InstallGapFill() {
    for (const auto& [source, target] : mounts) {
        const auto colon = target.find(':');
        mountedSuffixes.push_back(colon == std::string::npos ? target : target.substr(colon + 1));
    }
    InstallGuard("gap-fill entry filter", EntryFilterSite, EntryFilterCode, [](Emitter& e, std::uintptr_t) {
        const auto skip = reinterpret_cast<std::uintptr_t>(&entrySkip);
        e.Bytes({0x60});                                      // pushad
        e.Bytes({0x8d, 0x44, 0x24, 0x20, 0x50});              // lea eax,[esp+0x20] (site esp); push eax
        e.Bytes({0xe8}); e.Rel(reinterpret_cast<std::uintptr_t>(&EntryFilter));
        e.Bytes({0x83, 0xc4, 0x04});                          // add esp,4
        e.Bytes({0xa3}); std::memcpy(e.p, &skip, 4); e.p += 4; // mov [entrySkip],eax
        e.Bytes({0x61});                                      // popad
        e.Bytes({0x83, 0x3d}); std::memcpy(e.p, &skip, 4); e.p += 4; e.Bytes({0x00}); // cmp dword [entrySkip],0
        e.Bytes({0x0f, 0x85}); e.Rel(EntryFilterSkip);        // jne next entry
        e.Bytes({0xf6, 0x84, 0x24, 0x04, 0x01, 0x00, 0x00, 0x01}); // displaced: test byte [esp+0x104],1
        e.Jmp(EntryFilterResume);
    });
}

bool ApplyMounts() {
    if (std::memcmp(reinterpret_cast<const void*>(ParseMftLine), ParseMftLineCode, sizeof ParseMftLineCode)
        || std::memcmp(reinterpret_cast<const void*>(AddBundleTarget), AddBundleTargetCode, sizeof AddBundleTargetCode)) {
        Log("mount: REFUSED: ParseLine 0x%x or AddTarget 0x%x did not verify", unsigned(ParseMftLine), unsigned(AddBundleTarget));
        return false;
    }
    char* backend = nullptr;
    for (int i = 0; i < 60000; ++i, Sleep(1)) {
        backend = *reinterpret_cast<char* volatile*>(BundleBackendGlobal);
        if (backend && *reinterpret_cast<volatile unsigned char*>(backend + BackendParsed)) break;
    }
    if (!backend || !*reinterpret_cast<volatile unsigned char*>(backend + BackendParsed)
        || *reinterpret_cast<std::uintptr_t*>(backend) != BundleBackendVtable) {
        Log("mount: REFUSED: bundle backend at [0x%x] = %p never finished package.mft", unsigned(BundleBackendGlobal), backend);
        return false;
    }
    // The parse loop's state (0xbd34f0 keeps it on its stack, 0x268 bytes);
    // ParseLine only writes the source path at +0x68..+0x267 for these lines.
    alignas(4) static char parser[0x300];
    bool ok = true;
    for (auto& [source, target] : mounts) {
        std::memset(parser, 0, sizeof parser);
        const std::string sourceLine = "BundleSource " + source, targetLine = "BundleTarget " + target;
        if (!ParseLineGuarded(parser, sourceLine.data(), sourceLine.data() + sourceLine.size(), backend) || !parser[ParserSource]) {
            Log("mount: REFUSED: source \"%s\" did not parse", source.c_str()); ok = false; continue;
        }
        const char* expanded = parser + ParserSource;
        const int before = LogBundleNode(backend, expanded);
        if (before >= int(NodeMaxTargets)) { Log("mount: REFUSED: %s already has %d targets", expanded, before); ok = false; continue; }
        if (!ParseLineGuarded(parser, targetLine.data(), targetLine.data() + targetLine.size(), backend)) {
            Log("mount: REFUSED: target \"%s\" did not parse", target.c_str()); ok = false; continue;
        }
        const int after = LogBundleNode(backend, expanded);
        Log("mount: %s += %s (%d -> %d targets)", source.c_str(), target.c_str(), before < 0 ? 0 : before, after);
        if (after != (before < 0 ? 1 : before + 1)) ok = false;
    }
    if (shaderOrder) InstallShaderOrder();
    if (gapFill) InstallGapFill();
    return InstallNullGuards() && ok;
}

// ---- Weapon give -----------------------------------------------------------
// Give/replace 0x6bcf80 is the engine's own path (spawn loop 0x6bd350, unlocks
// 0x6bd480, ground pickup 0x6c5cd0 with its call at 0x6c5f3e): thiscall on the
// ServerSoldierEntity, (SoldierWeaponData*, slot, selectNow, forceSlot),
// ret 0x10, returns the new 0x24-byte server item (+4 data, +8 soldier,
// +0xc effects, +0x10 firing). The engine fills ammo itself and the client
// gets the weapon by ghost replication, so nothing is written client-side.
//
// It runs on the game thread from a detour on the client player input update
// (the same function Bc2Gameplay hooks), and only when that thread is the one
// the engine itself used for its own give calls at spawn. The data pointer is
// resolved by name on the worker thread, read-only, and re-checked on the
// game thread just before the call.
constexpr std::uintptr_t GiveWeapon = 0x6bcf80, PickupGiveCall = 0x6c5f3e, Pickup = 0x6c5cd0;
// 0x6bcf80: sub esp,38; cmp dword [esp+3c],0; push ebx; mov ebx,[esp+44]; push ebp/esi/edi; mov esi,ecx; je +be
constexpr unsigned char GiveCode[] = {0x83,0xEC,0x38,0x83,0x7C,0x24,0x3C,0x00,0x53,0x8B,0x5C,0x24,0x44,0x55,0x56,0x57,0x8B,0xF1,0x0F,0x84,0xBE,0x00,0x00,0x00};
// 0x6c5cd0 ServerPickupEntity::Pickup(ServerSoldierEntity*) prologue.
constexpr unsigned char PickupCode[] = {0x81,0xEC,0xC4,0x00,0x00,0x00,0x53,0x55,0x8B,0xAC,0x24,0xD0,0x00,0x00,0x00,0x56,0x57,0x8B,0xBD,0x20,0x02,0x00,0x00};
constexpr std::uintptr_t ServerSoldierVtable = 0x1424850;
constexpr std::uintptr_t ClientContext = 0x15713d8, ServerContext = 0x1566b18;
// Ammo override setters on the firing object, called by the pickup after its
// give (0x6c5f68): reserve first, then loaded, both thiscall(int), ret 4,
// skipped when the entry's loaded is -1. Reserve is ignored natively while
// firing+0xa8 bit 8 (non-finite ammo) is set.
constexpr std::uintptr_t SetLoaded = 0x6cd520, SetReserve = 0x6cd850, PickupAmmoApply = 0x6c5f68;
// 0x6cd520: mov eax,[esp+4]; mov [ecx+7c],eax; ret 4
constexpr unsigned char SetLoadedCode[] = {0x8B,0x44,0x24,0x04,0x89,0x41,0x7C,0xC2,0x04,0x00};
// 0x6cd850: push ecx; push esi; mov esi,ecx; test byte [esi+a8],8; jne; mov eax,[esi+c]; mov eax,[eax+10]; cmp eax,-1
constexpr unsigned char SetReserveCode[] = {0x51,0x56,0x8B,0xF1,0xF6,0x86,0xA8,0x00,0x00,0x00,0x08,0x75,0x2A,0x8B,0x46,0x0C,0x8B,0x40,0x10,0x83,0xF8,0xFF};
// 0x6c5f68: cmp dword [edi],-1; mov ebx,[ebx+10]; je; mov eax,[edi+4]; push eax; mov ecx,ebx; call 0x6cd850
constexpr unsigned char AmmoApplyCode[] = {0x83,0x3F,0xFF,0x8B,0x5B,0x10,0x74,0x59,0x8B,0x47,0x04,0x50,0x8B,0xCB,0xE8};
// 0x6d7c80 Inventory::SetSelected: thiscall on soldier+0x2b4, (int slot, bool validate), ret 8.
// The give calls it itself when selectNow is set; this DLL calls it only to
// select a slot whose give was skipped because the weapon is already held.
constexpr std::uintptr_t SelectSlot = 0x6d7c80;
constexpr unsigned char SelectCode[] = {0x80,0x7C,0x24,0x08,0x00,0x56,0x57,0x8B,0x7C,0x24,0x0C,0x8B,0xF1,0x74,0x18,0x8B,0x8E,0x48,0x01,0x00,0x00,0x8B,0x01};
// ServerPickupEntity (ctor 0x6ad610): entry vector +0x34/+0x38, 0x20-byte entries, +0 SoldierWeaponData*.
constexpr std::uint32_t ServerPickupVtable = 0x1424358;
// Patterns from the repo's discovery code (Bc2Profile.cpp DiscoverGameplay,
// tools/capture_reload_server.py); each must match exactly once.
constexpr const char* ClientContextPattern = "B8 01 00 00 00 84 05 ?? ?? ?? ?? 75 66 09 05 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 33 C0 68 ?? ?? ?? ?? C7 05";
constexpr const char* ServerContextPattern = "B8 01 00 00 00 84 05 ?? ?? ?? ?? 0F 85 86 00 00 00 09 05 ?? ?? ?? ?? 53 33 DB 53 B9 ?? ?? ?? ?? E8";
constexpr const char* InputUpdatePattern = "83 EC 08 53 55 56 57 8B F1 E8 ?? ?? ?? ?? D9 44 24 1C 51 8B 8E 74 0C 00 00 D9 1C 24 8B E8 8B 7D 0C";
constexpr const char* GivePattern = "83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84 BE 00 00 00";
constexpr unsigned SlotCount = 9, MaxItems = 16;
const char* const SlotNames[SlotCount] = {"Primary1","Primary2","GL1","GL2","StaticGadget","DynGadget1","DynGadget2","Melee","HandGrenade"};

using GiveFn = void*(__thiscall*)(void* soldier, void* data, std::uint32_t slot, std::uint32_t selectNow, std::uint32_t forceSlot);
using SetAmmoFn = void(__thiscall*)(void* firing, int count);
using SelectFn = void(__thiscall*)(void* inventory, int slot, std::uint32_t validate);
GiveFn giveOriginal;
bool ammoSettersProven, selectProven;

struct GiveRequest {
    bool wanted = false, remove = false;
    std::string name;
    std::uintptr_t data = 0; // resolved by the worker
};
struct GivePlan {
    GiveRequest slots[SlotCount];
    int select = -1, loaded = 0, reserve = 0;
    bool any = false, setLoaded = false, setReserve = false;
};
GivePlan plan;
std::atomic<bool> planResolved{false}, planDone{false};

// Arsenal: an ordered list of weapons cycled through Primary1/Primary2 for
// testing every gun without restarting. Arsenal.Gun <name> adds one (in
// order); Arsenal.All 1 adds every resident SoldierWeaponData, sorted by
// name, minus Arsenal.Exclude prefixes (default AI_). Next/previous come from
// hotkeys while the game has focus (Arsenal.NextKey F7, Arsenal.PrevKey F6)
// or from the named events Local\BC2TestRange.ArsenalNext/ArsenalPrev, so the
// launcher or the VR mod can step it too. Swaps are at least
// Arsenal.MinIntervalMs apart (default 2500). A soldier holds one item per slot
// and only slots 0..8 are switchable (docs/WEAPON-VR-COVERAGE-20261010.md 4).
struct ArsenalEntry { std::string name; std::uintptr_t data; };
// Binoculars: no 1P anim data (client fault 0xf68fc3). NAM_Handgrenade: client
// handler lookup 0x8ac380 reads a null [obj+0x2c] at 0x8ac396 (unresolved).
std::vector<std::string> arsenalNames, arsenalExclude{"AI_", "Binoculars", "NAM_Handgrenade"};
bool arsenalAll;
int arsenalNextKey = VK_F7, arsenalPrevKey = VK_F6;
std::vector<ArsenalEntry> arsenal;
std::atomic<bool> arsenalReady{false};
std::atomic<int> arsenalRequest{0};
int arsenalIndex = -1, arsenalMinIntervalMs = 2500;
std::string arsenalStart;  // Arsenal.Start: the first step gives this gun
// Guns with physical reloads lead the rotation, in this order. Matched on the
// data's Name (+0xc), the key the VR mod's physical-reload profiles use
// (src/games/bc2: XM8_sp_s, SPAS12_sp, F2000_sp, AEK971_sp). Arsenal.First
// lines replace the list.
std::vector<std::string> arsenalFirst{"XM8_sp_s", "SPAS12_sp", "F2000_sp", "AEK971_sp"};
bool arsenalFirstSet;
ULONGLONG arsenalLastSwap = 0;
bool ArsenalWanted() { return arsenalAll || !arsenalNames.empty(); }
std::atomic<std::uintptr_t> soldierVtableOfData{0};
std::atomic<DWORD> nativeGiveThread{0};
std::atomic<unsigned> nativeGiveCalls{0};

// Everything below reads game memory only under SEH and never touches C++
// objects inside __try (MSVC C2712).
bool ReadU32(std::uintptr_t at, std::uint32_t& out) {
    if (at < 0x10000 || at > 0xFFFFFFFB) return false;
    __try { out = *reinterpret_cast<const volatile std::uint32_t*>(at); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool ReadU8(std::uintptr_t at, std::uint8_t& out) {
    if (at < 0x10000) return false;
    __try { out = *reinterpret_cast<const volatile std::uint8_t*>(at); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Copies a NUL-terminated char* field target into out; false if unreadable or too long.
bool ReadCString(std::uintptr_t at, char* out, std::size_t size) {
    if (at < 0x10000 || !size) return false;
    __try {
        const char* s = reinterpret_cast<const char*>(at);
        for (std::size_t i = 0; i + 1 < size; ++i) {
            const char c = s[i]; out[i] = c;
            if (!c) return i > 0;
            if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7e) return false;
        }
        return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool PatternBytes(const char* text, unsigned char* bytes, bool* mask, std::size_t& n) {
    n = 0;
    for (const char* p = text; *p;) {
        if (*p == ' ') { ++p; continue; }
        if (n >= 64) return false;
        if (p[0] == '?' && p[1] == '?') { mask[n] = false; bytes[n++] = 0; p += 2; continue; }
        unsigned v = 0; if (std::sscanf(p, "%2x", &v) != 1) return false;
        mask[n] = true; bytes[n++] = static_cast<unsigned char>(v); p += 2;
    }
    return n > 0;
}
// Unique match of a pattern in the executable sections of the image, or 0.
std::uintptr_t FindUnique(const char* pattern) {
    unsigned char bytes[64]; bool mask[64]; std::size_t n = 0;
    if (!PatternBytes(pattern, bytes, mask, n)) return 0;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ImageBase);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ImageBase + dos->e_lfanew);
    auto* section = IMAGE_FIRST_SECTION(nt);
    std::uintptr_t found = 0; unsigned count = 0;
    for (unsigned s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const auto* begin = reinterpret_cast<const unsigned char*>(ImageBase + section->VirtualAddress);
        const std::size_t size = section->Misc.VirtualSize;
        for (std::size_t i = 0; i + n <= size; ++i) {
            if (begin[i] != bytes[0]) continue;
            std::size_t k = 1; while (k < n && (!mask[k] || begin[i + k] == bytes[k])) ++k;
            if (k == n) { found = reinterpret_cast<std::uintptr_t>(begin + i); ++count; }
        }
    }
    return count == 1 ? found : 0;
}
std::uintptr_t CallTarget(std::uintptr_t call) {
    if (*reinterpret_cast<const unsigned char*>(call) != 0xE8) return 0;
    std::int32_t rel; std::memcpy(&rel, reinterpret_cast<const void*>(call + 1), 4);
    return call + 5 + rel;
}
bool CallsWithin(std::uintptr_t begin, std::size_t size, std::uintptr_t target) {
    for (std::size_t i = 0; i + 5 <= size; ++i) if (CallTarget(begin + i) == target) return true;
    return false;
}
std::uintptr_t inputUpdateAddress;
// Static proof for every native the give path reads or calls. Writes nothing.
bool GiveBuildMatches() {
    if (std::memcmp(reinterpret_cast<const void*>(GiveWeapon), GiveCode, sizeof GiveCode)) { Log("give: code at 0x%x differs", unsigned(GiveWeapon)); return false; }
    if (FindUnique(GivePattern) != GiveWeapon) { Log("give: signature is not unique at 0x%x", unsigned(GiveWeapon)); return false; }
    if (std::memcmp(reinterpret_cast<const void*>(Pickup), PickupCode, sizeof PickupCode) || CallTarget(PickupGiveCall) != GiveWeapon) { Log("give: pickup call 0x%x does not reach 0x%x", unsigned(PickupGiveCall), unsigned(GiveWeapon)); return false; }
    const std::uintptr_t client = FindUnique(ClientContextPattern), server = FindUnique(ServerContextPattern);
    if (!client || *reinterpret_cast<const std::uint32_t*>(client + 21) != ClientContext || *reinterpret_cast<const unsigned char*>(client + 0x73) != 0xB8
        || *reinterpret_cast<const std::uint32_t*>(client + 0x74) != ClientContext || *reinterpret_cast<const unsigned char*>(client + 0x78) != 0xC3) {
        Log("give: client context getter not proven (match 0x%x)", unsigned(client)); return false;
    }
    if (!server || *reinterpret_cast<const std::uint32_t*>(server + 0x1c) != ServerContext || *reinterpret_cast<const unsigned char*>(server + 0x97) != 0xB8
        || *reinterpret_cast<const std::uint32_t*>(server + 0x98) != ServerContext || *reinterpret_cast<const unsigned char*>(server + 0x9c) != 0xC3) {
        Log("give: server context getter not proven (match 0x%x)", unsigned(server)); return false;
    }
    // Same checks DiscoverGameplay makes: calls the client context getter at
    // +9, ret 4 at +0x1e8, and the router dispatch at +0xe5.
    const std::uintptr_t update = FindUnique(InputUpdatePattern);
    static const unsigned char ret4[] = {0xC2,4,0}, dispatch[] = {0x8b,0x13,0x8b,0x86,0x74,0x0c,0,0,0x8b,0x52,0x0c,0x50,0x8b,0xcb,0xff,0xd2};
    if (!update || CallTarget(update + 9) != client || std::memcmp(reinterpret_cast<const void*>(update + 0x1e8), ret4, 3)
        || std::memcmp(reinterpret_cast<const void*>(update + 0xe5), dispatch, sizeof dispatch)) {
        Log("give: player input update not proven (match 0x%x)", unsigned(update)); return false;
    }
    inputUpdateAddress = update;
    ammoSettersProven = !std::memcmp(reinterpret_cast<const void*>(SetLoaded), SetLoadedCode, sizeof SetLoadedCode)
        && !std::memcmp(reinterpret_cast<const void*>(SetReserve), SetReserveCode, sizeof SetReserveCode)
        && !std::memcmp(reinterpret_cast<const void*>(PickupAmmoApply), AmmoApplyCode, sizeof AmmoApplyCode)
        && CallTarget(PickupAmmoApply + sizeof AmmoApplyCode - 1) == SetReserve && CallsWithin(PickupAmmoApply, 0x40, SetLoaded);
    selectProven = !std::memcmp(reinterpret_cast<const void*>(SelectSlot), SelectCode, sizeof SelectCode);
    Log("give: proven give 0x%x, pickup call 0x%x, client ctx getter 0x%x, server ctx getter 0x%x, input update 0x%x, ammo setters %s, selector %s",
        unsigned(GiveWeapon), unsigned(PickupGiveCall), unsigned(client), unsigned(server), unsigned(update),
        ammoSettersProven ? "proven" : "NOT proven", selectProven ? "proven" : "NOT proven");
    return true;
}

// Local player -> server soldier, same chain and checks as
// tools/capture_reload_server.py chain() and Bc2PlayerPair.cpp.
struct Chain {
    std::uint32_t clientPlayer, playerId, serverPlayer, soldier, inventory, begin, end, count, selected;
    std::uint32_t items[MaxItems];
};
bool ReadChain(Chain& c) {
    std::memset(&c, 0, sizeof c);
    std::uint32_t manager, weak, clientSoldier, serverManager, capacity, players, id2, check, vt, a, b; std::uint8_t flags;
    if (!ReadU32(ClientContext + 8, manager) || !ReadU32(manager + 0xb4, c.clientPlayer) || !ReadU8(c.clientPlayer + 0xccd, flags) || !(flags & 8)) return false;
    if (!ReadU32(c.clientPlayer + 0x154, c.playerId) || !ReadU32(c.clientPlayer + 0xc54, weak) || !ReadU32(weak, clientSoldier) || clientSoldier < 0x10004) return false;
    clientSoldier -= 4;
    if (!ReadU32(ServerContext + 8, serverManager) || !ReadU32(serverManager + 4, capacity) || !capacity || capacity > 256 || c.playerId >= capacity) return false;
    if (!ReadU32(serverManager + 0x6c, players) || !ReadU32(players + c.playerId * 4, c.serverPlayer) || !ReadU32(c.serverPlayer + 0x154, id2) || id2 != c.playerId) return false;
    if (!ReadU32(c.serverPlayer + 0xc3c, c.soldier) || !ReadU32(c.serverPlayer + 0xc6c, check) || check != c.soldier) return false;
    if (!ReadU32(c.soldier, vt) || vt != ServerSoldierVtable || !ReadU32(c.soldier + 0x220, check) || check != c.serverPlayer) return false;
    if (!ReadU32(c.soldier + 0xc, a) || !ReadU32(clientSoldier + 0xc, b) || a != b) return false;
    if (!ReadU32(c.soldier + 0x2b4, c.inventory) || !ReadU32(c.inventory + 0x14c, c.selected)) return false;
    if (!ReadU32(c.soldier + 0x2b8, c.begin) || !ReadU32(c.soldier + 0x2bc, c.end) || c.begin < 0x10000 || c.end < c.begin || (c.end - c.begin) % 4 || c.end - c.begin > MaxItems * 4) return false;
    c.count = (c.end - c.begin) / 4;
    for (unsigned i = 0; i < c.count; ++i) if (!ReadU32(c.begin + i * 4, c.items[i])) return false;
    return true;
}

// SoldierWeaponData: vtable, Name char* at +0xc, WeaponAssetName char* at +0x40.
bool DataName(std::uintptr_t data, char* name, std::size_t size) {
    std::uint32_t text; return ReadU32(data + 0xc, text) && ReadCString(text, name, size);
}
bool NameMatches(std::uintptr_t data, const char* wanted) {
    char name[128], asset[160]; std::uint32_t text;
    if (DataName(data, name, sizeof name) && !_stricmp(name, wanted)) return true;
    if (!ReadU32(data + 0x40, text) || !ReadCString(text, asset, sizeof asset)) return false;
    const char* tail = std::strrchr(asset, '/');
    return !_stricmp(asset, wanted) || (tail && !_stricmp(tail + 1, wanted));
}

// EntryInputActionEnum names (read-only): the 24-byte {name, ..., value at +16}
// table that starts with "EiaThrottle" (as Bc2Profile verifies), logged once.
void LogEntryActionNames() {
    static bool done = false; if (done) return; done = true;
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(ImageBase);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(ImageBase + dos->e_lfanew);
    std::uintptr_t text = 0;
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned k = 0; k < nt->FileHeader.NumberOfSections && !text; ++k, ++section) {
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        const auto* b = reinterpret_cast<const char*>(ImageBase + section->VirtualAddress);
        for (std::size_t i = 0; i + 12 <= section->Misc.VirtualSize; ++i) if (!std::memcmp(b + i, "EiaThrottle", 12)) { text = reinterpret_cast<std::uintptr_t>(b + i); break; }
    }
    if (!text) { Log("actions: EiaThrottle not found"); return; }
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned k = 0; k < nt->FileHeader.NumberOfSections; ++k, ++section) {
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) continue;
        const auto base = ImageBase + section->VirtualAddress;
        for (std::size_t i = 0; i + 50 * 24 <= section->Misc.VirtualSize; i += 4) {
            std::uint32_t name = 0, value = 1; ReadU32(base + i, name); ReadU32(base + i + 16, value);
            if (name != text || value != 0) continue;
            char line[1600] = ""; std::size_t used = 0;
            for (unsigned n = 0; n < 50 && used + 40 < sizeof line; ++n) {
                std::uint32_t p = 0, v = 0; ReadU32(base + i + n * 24, p); ReadU32(base + i + n * 24 + 16, v);
                char nm[40] = "?"; if (p) { for (unsigned c = 0; c + 4 < sizeof nm; c += 4) { std::uint32_t w = 0; if (!ReadU32(p + c, w)) { nm[c] = 0; break; } std::memcpy(nm + c, &w, 4); nm[c + 4] = 0; if (std::memchr(&w, 0, 4)) break; } nm[sizeof nm - 1] = 0; }
                used += std::snprintf(line + used, sizeof line - used, " %u=%s", v, nm);
            }
            Log("actions: table 0x%08x:%s", unsigned(base + i), line);
            return;
        }
    }
    Log("actions: table not found (name 0x%08x)", unsigned(text));
}
void LogInventory(const char* when) {
    Chain c; if (!ReadChain(c)) { Log("%s: inventory unreadable", when); return; }
    Log("%s: soldier 0x%08x inventory 0x%08x items %u selected %u", when, c.soldier, c.inventory, c.count, c.selected);
    for (unsigned i = 0; i < c.count; ++i) {
        std::uint32_t data = 0, firing = 0, owner = 0; int loaded = 0, reserve = 0; char name[128] = "-";
        if (c.items[i]) {
            ReadU32(c.items[i] + 4, data); ReadU32(c.items[i] + 8, owner); ReadU32(c.items[i] + 0x10, firing);
            if (data && !DataName(data, name, sizeof name)) std::strcpy(name, "<unreadable>");
            if (firing) { std::uint32_t v; if (ReadU32(firing + 0x7c, v)) loaded = int(v); if (ReadU32(firing + 0x80, v)) reserve = int(v); }
        }
        Log("%s:   slot %u %-12s item 0x%08x data 0x%08x %s firing 0x%08x loaded %d reserve %d%s", when, i, i < SlotCount ? SlotNames[i] : "?",
            c.items[i], data, name, firing, loaded, reserve, c.items[i] && owner != c.soldier ? " (owner differs)" : "");
    }
    // WeaponSwitchingData (inventory +4): SwitchMap entries at +0x18/+0x1c, 24 bytes each:
    // targets [+8,+0xc) of slot indices, input action +0x10, from-state +0x14 (docs/LAUNCHER-BINDINGS-20261001.md).
    std::uint32_t sw = 0, mb = 0, me = 0, cur = 0;
    ReadU32(c.inventory + 0x14c, cur);
    if (ReadU32(c.inventory + 4, sw) && sw && ReadU32(sw + 0x18, mb) && ReadU32(sw + 0x1c, me) && me > mb && me - mb <= 128 * 24 && (me - mb) % 24 == 0) {
        Log("%s: switch map 0x%08x, %u entries, current state %u", when, sw, (me - mb) / 24, cur);
        for (std::uint32_t at = mb; at < me; at += 24) {
            std::uint32_t from = 0, action = 0, tb = 0, te = 0; char targets[64] = ""; std::size_t used = 0;
            ReadU32(at + 20, from); ReadU32(at + 16, action); ReadU32(at + 8, tb); ReadU32(at + 12, te);
            for (std::uint32_t t = tb; t < te && t - tb <= 40 && used + 4 < sizeof targets; t += 4) { std::uint32_t v = 0; ReadU32(t, v); used += std::snprintf(targets + used, sizeof targets - used, "%u ", v); }
            Log("%s:   from %u action %u -> [ %s]", when, from, action, targets);
        }
    } else Log("%s: switch map unreadable (data 0x%08x)", when, sw);
}

// Read-only scan of committed private/mapped memory for objects whose first
// word is the SoldierWeaponData vtable. Collects candidate addresses only.
// Also collects ServerPickupEntity objects (second vtable) into pickups.
unsigned ScanRegion(std::uintptr_t begin, std::size_t size, std::uint32_t vtable, std::uintptr_t* out, unsigned room,
                    std::uintptr_t* pickups, unsigned& pickupCount, unsigned pickupRoom) {
    unsigned n = 0;
    __try {
        auto* words = reinterpret_cast<const std::uint32_t*>(begin);
        for (std::size_t i = 0; i < size / 4 && n < room; ++i) {
            if (words[i] == vtable) out[n++] = begin + i * 4;
            else if (words[i] == ServerPickupVtable && pickupCount < pickupRoom) pickups[pickupCount++] = begin + i * 4;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return n;
}
std::vector<std::uintptr_t> FindByVtable(std::uint32_t vtable, std::vector<std::uintptr_t>& pickups) {
    std::vector<std::uintptr_t> found(16384);
    pickups.assign(4096, 0);
    unsigned n = 0, pickupCount = 0;
    MEMORY_BASIC_INFORMATION m{};
    for (std::uintptr_t at = 0x10000; at < 0xFFFF0000 && VirtualQuery(reinterpret_cast<void*>(at), &m, sizeof m); ) {
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(m.BaseAddress), next = base + m.RegionSize;
        const DWORD p = m.Protect & 0xff;
        const bool readable = p == PAGE_READWRITE || p == PAGE_READONLY || p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_READ;
        if (m.State == MEM_COMMIT && m.Type != MEM_IMAGE && readable && !(m.Protect & PAGE_GUARD) && n < found.size())
            n += ScanRegion(base, m.RegionSize, vtable, found.data() + n, unsigned(found.size() - n), pickups.data(), pickupCount, unsigned(pickups.size()));
        if (next <= at) break;
        at = next;
    }
    found.resize(n); pickups.resize(pickupCount);
    return found;
}

// The give name of a data object: the tail of its WeaponAssetName (+0x40),
// e.g. "US_hg_M9-3", else its Name (+0xc).
bool GiveName(std::uintptr_t data, char* out, std::size_t size) {
    char asset[160]; std::uint32_t text;
    if (ReadU32(data + 0x40, text) && ReadCString(text, asset, sizeof asset)) {
        const char* tail = std::strrchr(asset, '/');
        const char* name = tail ? tail + 1 : asset;
        if (*name && std::strlen(name) < size) { std::strcpy(out, name); return true; }
    }
    return DataName(data, out, size);
}

// A first-person weapon: WeaponFiring set and WeaponStates[0].Meshes1p holding
// at least one mesh. Giving data without one (Binoculars) faults inside the
// give (live: read 0 at 0xe93ed7). WeaponStates (+0x88) is {vt, info, begin,
// end} with 0xc8-byte states; Meshes1p (state +0x80) is {table, ?, capacity,
// count, items} (Bc2SelectedMeshes1p.cpp).
bool HasFirstPersonMesh(std::uintptr_t data) {
    std::uint32_t firing, begin, end, count, items, mesh, vt;
    return ReadU32(data + 0x98, firing) && firing >= 0x10000
        && ReadU32(data + 0x90, begin) && ReadU32(data + 0x94, end) && begin >= 0x10000 && end > begin && (end - begin) % 0xc8 == 0
        && ReadU32(begin + 0x8c, count) && count >= 1 && count <= 8 && ReadU32(begin + 0x90, items) && ReadU32(items, mesh) && mesh >= 0x10000
        && ReadU32(mesh, vt) && vt >= ImageBase && vt < ImageBase + ImageBytes;
}

// The client builds a given weapon's 1P anim tree from its async bundle
// async/weapon/<name> (Package/async/weapon or Dist/win32/async/weapon,
// <name>-00.fbrb). Data whose bundle does not exist (MEC_shg_TOZ194*, whose
// clips live in sp_shg_toz194; Binoculars) faulted in the client anim build
// (0xf68fc3), so the arsenal skips it.
bool HasAsyncBundle(const char* name) {
    wchar_t exe[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) return true;
    std::wstring dir(exe); dir.resize(dir.find_last_of(L"\\/") + 1);
    std::wstring file;
    for (const char* c = name; *c; ++c) file += wchar_t(std::tolower(static_cast<unsigned char>(*c)));
    file += L"-00.fbrb";
    for (const wchar_t* sub : {L"Package\\async\\weapon\\", L"Dist\\win32\\async\\weapon\\"})
        if (GetFileAttributesW((dir + sub + file).c_str()) != INVALID_FILE_ATTRIBUTES) return true;
    return false;
}

void ResolveArsenal(const std::vector<std::uintptr_t>& candidates) {
    std::vector<ArsenalEntry> found;
    const auto usable = [](std::uintptr_t at) { return HasFirstPersonMesh(at); };
    for (const auto& wanted : arsenalNames) {
        auto it = std::find_if(candidates.begin(), candidates.end(), [&](std::uintptr_t at) { return NameMatches(at, wanted.c_str()) && usable(at); });
        if (it == candidates.end()) Log("arsenal: %s not loaded in this level or has no first-person mesh; skipped", wanted.c_str());
        else found.push_back({wanted, *it});
    }
    if (arsenalAll) {
        std::vector<ArsenalEntry> all;
        for (auto at : candidates) {
            char name[160];
            if (!usable(at) || !GiveName(at, name, sizeof name)) continue;
            if (!HasAsyncBundle(name)) { static unsigned noted; if (noted++ < 20) Log("arsenal: %s has no async/weapon bundle; skipped", name); continue; }
            bool excluded = false;
            for (const auto& prefix : arsenalExclude) excluded |= !_strnicmp(name, prefix.c_str(), prefix.size());
            const auto same = [&](const ArsenalEntry& e) { return !_stricmp(e.name.c_str(), name); };
            if (excluded || std::any_of(found.begin(), found.end(), same) || std::any_of(all.begin(), all.end(), same)) continue;
            all.push_back({name, at});
        }
        std::sort(all.begin(), all.end(), [](const ArsenalEntry& a, const ArsenalEntry& b) { return _stricmp(a.name.c_str(), b.name.c_str()) < 0; });
        found.insert(found.end(), all.begin(), all.end());
    }
    // Exact Give names can prioritize one representative per gun; native data
    // names still support the original whole-family priorities.
    const auto rank = [](const ArsenalEntry& e) {
        char name[128] = "";
        DataName(e.data, name, sizeof name);
        for (std::size_t i = 0; i < arsenalFirst.size(); ++i)
            if (!_stricmp(e.name.c_str(), arsenalFirst[i].c_str()) || !_stricmp(name, arsenalFirst[i].c_str())) return int(i);
        return int(arsenalFirst.size());
    };
    std::stable_sort(found.begin(), found.end(), [&](const ArsenalEntry& a, const ArsenalEntry& b) { return rank(a) < rank(b); });
    arsenal = std::move(found);
    for (std::size_t i = 0; i < arsenal.size(); ++i) Log("arsenal: %3u %s 0x%08x", unsigned(i + 1), arsenal[i].name.c_str(), unsigned(arsenal[i].data));
    Log("arsenal: %u weapons; next/previous by keys 0x%02x/0x%02x or events Local\\BC2TestRange.ArsenalNext/ArsenalPrev",
        unsigned(arsenal.size()), arsenalNextKey, arsenalPrevKey);
    arsenalReady.store(!arsenal.empty(), std::memory_order_release);
}

// Worker thread: wait for the soldier, take the vtable from a held weapon,
// resolve every requested name. Reads only.
bool ResolvePlan() {
    Chain c{};
    for (int i = 0; i < 4 * 60 * 30 && !(ReadChain(c) && c.count); ++i) Sleep(250);
    if (!c.count) { Log("give: REFUSED: local server soldier never appeared"); return false; }
    Log("give: soldier found: client player 0x%08x id %u server player 0x%08x soldier 0x%08x inventory 0x%08x", c.clientPlayer, c.playerId, c.serverPlayer, c.soldier, c.inventory);
    LogInventory("give: before");
    std::uint32_t vtable = 0, data = 0;
    for (unsigned i = 0; i < c.count && !vtable; ++i) {
        char name[128];
        if (c.items[i] && ReadU32(c.items[i] + 4, data) && ReadU32(data, vtable) && DataName(data, name, sizeof name)
            && vtable >= ImageBase && vtable < ImageBase + ImageBytes) Log("give: SoldierWeaponData vtable 0x%08x from held %s (0x%08x)", vtable, name, data);
        else vtable = 0;
    }
    if (!vtable) { Log("give: REFUSED: no held weapon gives a SoldierWeaponData vtable"); return false; }
    soldierVtableOfData = vtable;
    const DWORD t0 = GetTickCount();
    // Same class as the held weapon's data, which the engine itself admitted
    // with IsA(SoldierWeaponData, TypeInfo 0x1bfca60). The dump lists no
    // subclass of SoldierWeaponData, so an exact vtable match is that IsA.
    std::vector<std::uintptr_t> pickups;
    const auto scanned = FindByVtable(vtable, pickups);
    // Live pickup entries first: data the engine already gives from a pickup.
    std::vector<std::uintptr_t> candidates;
    for (auto pickup : pickups) {
        std::uint32_t begin, end, entry, entryVt;
        if (!ReadU32(pickup + 0x34, begin) || !ReadU32(pickup + 0x38, end) || begin < 0x10000 || end < begin || end - begin > 64 * 0x20 || (end - begin) % 0x20) continue;
        for (std::uint32_t at = begin; at < end; at += 0x20)
            if (ReadU32(at, entry) && ReadU32(entry, entryVt) && entryVt == vtable) candidates.push_back(entry);
    }
    const std::size_t fromPickups = candidates.size();
    candidates.insert(candidates.end(), scanned.begin(), scanned.end());
    Log("give: scan found %u objects with the vtable and %u pickups (%u pickup entries) in %lu ms", unsigned(scanned.size()),
        unsigned(pickups.size()), unsigned(fromPickups), GetTickCount() - t0);
    for (unsigned s = 0; s < SlotCount; ++s) {
        auto& r = plan.slots[s];
        if (!r.wanted || r.remove) continue;
        unsigned matches = 0;
        for (const auto& at : candidates) {
            std::uint32_t firingData;
            if (!NameMatches(at, r.name.c_str()) || !ReadU32(at + 0x98, firingData) || firingData < 0x10000) continue;
            char name[128] = "?"; DataName(at, name, sizeof name);
            if (!matches) r.data = at;
            if (matches < 4) Log("give: slot %u %s candidate 0x%08x name %s%s%s", s, r.name.c_str(), unsigned(at), name,
                std::size_t(&at - candidates.data()) < fromPickups ? " (pickup entry)" : "", matches ? "" : " (used)");
            ++matches;
        }
        if (!matches) Log("give: slot %u %s: not loaded (no SoldierWeaponData with that name in this level)", s, r.name.c_str());
        else Log("give: slot %u %s: data resolved 0x%08x (%u candidates)", s, r.name.c_str(), unsigned(r.data), matches);
    }
    if (ArsenalWanted()) ResolveArsenal(candidates);
    return true;
}

// One give on the game thread. Re-checks the data right before the call.
// Weapons reach the client as u16 net ids in the soldier ghost, resolved by
// 0x5deb30 on the process-wide table [0x156d500] (shared by the in-process
// server and client). 0x5debd0 (thiscall, data*) returns a data object's id
// or 0; 0x5e5d70 (thiscall, data*) assigns the next id. Data from a mounted
// foreign bundle has no id, so the client cannot decode the ghost and drops
// the connection (reason 6). Give.* data without an id gets one first.
constexpr std::uintptr_t NetIdTable = 0x156d500, NetIdOfData = 0x5debd0, NetIdRegister = 0x5e5d70;
using NetIdOfFn = std::uint32_t(__thiscall*)(void* table, std::uint32_t data);
using NetIdRegisterFn = void(__thiscall*)(void* table, std::uint32_t data);
void EnsureNetId(std::uint32_t data, const char* name) {
    void* table = *reinterpret_cast<void* volatile*>(NetIdTable);
    if (!table) { Log("netid: table [0x%x] absent; %s not checked", unsigned(NetIdTable), name); return; }
    const std::uint32_t before = reinterpret_cast<NetIdOfFn>(NetIdOfData)(table, data);
    if (before) { Log("netid: %s 0x%08x has net id %u", name, data, before); return; }
    if (!netIdRegister) { Log("netid: %s 0x%08x has NO net id (TestRange.RegisterNetIds off)", name, data); return; }
    reinterpret_cast<NetIdRegisterFn>(NetIdRegister)(table, data);
    Log("netid: %s 0x%08x had no net id; registered as %u", name, data, reinterpret_cast<NetIdOfFn>(NetIdOfData)(table, data));
}

// AI weapon ballistics cache [0x1567180]: SoldierWeaponData* -> 64 bytes, built
// by 0x5a5a00 (thiscall on the cache, (data), ret 4; it reads WeaponFiring
// +0x98 and its PrimaryFire +0x40) and read by 0x58ee90 (thiscall on the cache,
// (data), ret 4; node+0x14 or 0). The level loader (0x5ac927) registers only
// weapons that loaded entity data reaches and that have AIData (+0xf0); the
// weapon-selected handlers (0x5afb72, 0x58f5b0) look the given weapon up and
// copy from the result unchecked, so a mounted weapon with AIData that no
// entity reaches faulted at 0xe93ed7 (G3/Mk14EBR Kobra and Scoped,
// Binoculars). Before a give we do what the loader does: look up, register on
// a miss, and refuse the give if it is still missing.
constexpr std::uintptr_t AiCacheGlobal = 0x1567180, AiCacheFind = 0x58ee90, AiCacheBuild = 0x5a5a00;
// 0x58ee90: push ecx; push esi; mov esi,ecx; lea eax,[esp+0xc]
constexpr unsigned char AiCacheFindCode[] = {0x51,0x56,0x8b,0xf1,0x8d,0x44,0x24,0x0c};
// 0x5a5a00: sub esp,0x50; push ebx; push ebp; push esi; mov esi,[esp+0x60]
constexpr unsigned char AiCacheBuildCode[] = {0x83,0xec,0x50,0x53,0x55,0x56,0x8b,0x74,0x24,0x60};
using AiCacheFn = void*(__thiscall*)(void* cache, std::uint32_t data);
bool EnsureAiCache(std::uint32_t data, const char* name) {
    std::uint32_t ai = 0, firing = 0, primary = 0;
    if (!ReadU32(data + 0xf0, ai) || !ai) return true;  // no AIData: the handlers never look it up
    void* cache = *reinterpret_cast<void* volatile*>(AiCacheGlobal);
    if (!cache || std::memcmp(reinterpret_cast<const void*>(AiCacheFind), AiCacheFindCode, sizeof AiCacheFindCode)
        || std::memcmp(reinterpret_cast<const void*>(AiCacheBuild), AiCacheBuildCode, sizeof AiCacheBuildCode)) {
        Log("aicache: %s: cache [0x%x] %p or its functions not proven; give refused", name, unsigned(AiCacheGlobal), cache); return false;
    }
    if (reinterpret_cast<AiCacheFn>(AiCacheFind)(cache, data)) return true;
    if (!ReadU32(data + 0x98, firing) || firing < 0x10000 || !ReadU32(firing + 0x40, primary) || primary < 0x10000) {
        Log("aicache: %s 0x%08x not cached and has no firing/primary fire to build from; give refused", name, data); return false;
    }
    reinterpret_cast<AiCacheFn>(AiCacheBuild)(cache, data);
    const bool ok = reinterpret_cast<AiCacheFn>(AiCacheFind)(cache, data) != nullptr;
    Log("aicache: %s 0x%08x was not cached; registered with 0x%x: %s", name, data, unsigned(AiCacheBuild), ok ? "ok" : "STILL MISSING, give refused");
    return ok;
}

void GiveOne(const Chain& c, unsigned slot, const GiveRequest& r, bool selectNow) {
    std::uint32_t vt = 0;
    if (!r.remove && (!ReadU32(r.data, vt) || vt != soldierVtableOfData.load() || !NameMatches(r.data, r.name.c_str()))) {
        Log("give: slot %u %s: data 0x%08x changed since the scan; skipped", slot, r.name.c_str(), unsigned(r.data)); return;
    }
    std::uint32_t held = 0;
    if (slot < c.count && c.items[slot] && ReadU32(c.items[slot] + 4, held) && held == r.data && !r.remove) {
        Log("give: slot %u %s already held (item 0x%08x data 0x%08x); give skipped", slot, r.name.c_str(), c.items[slot], held);
        if (!selectNow || c.selected == slot) return;
        if (!selectProven) { Log("give: select slot %u skipped: selector 0x%x not proven", slot, unsigned(SelectSlot)); return; }
        Log("give: calling 0x%x(inventory 0x%08x, slot %u, validate 1)", unsigned(SelectSlot), c.inventory, slot);
        reinterpret_cast<SelectFn>(SelectSlot)(reinterpret_cast<void*>(c.inventory), int(slot), 1);
        return;
    }
    if (!r.remove && slot >= c.count) Log("give: slot %u is past the item vector (%u); the give resizes it", slot, c.count);
    if (!r.remove && !EnsureAiCache(std::uint32_t(r.data), r.name.c_str())) return;
    if (!r.remove) EnsureNetId(r.data, r.name.c_str());
    Log("give: calling 0x%x(soldier 0x%08x, data 0x%08x %s, slot %u %s, selectNow %d, forceSlot 1)", unsigned(GiveWeapon), c.soldier,
        unsigned(r.data), r.remove ? "(remove)" : r.name.c_str(), slot, SlotNames[slot], selectNow ? 1 : 0);
    void* item = giveOriginal(reinterpret_cast<void*>(c.soldier), reinterpret_cast<void*>(r.data), slot, selectNow ? 1 : 0, 1);
    const auto at = reinterpret_cast<std::uintptr_t>(item);
    std::uint32_t data = 0, owner = 0, firing = 0;
    if (at) { ReadU32(at + 4, data); ReadU32(at + 8, owner); ReadU32(at + 0x10, firing); }
    Log("give: slot %u returned 0x%08x (item data 0x%08x %s, owner 0x%08x %s, firing 0x%08x)", slot, unsigned(at), data,
        data == r.data ? "ok" : "MISMATCH", owner, owner == c.soldier ? "ok" : "MISMATCH", firing);
    if (r.remove || !firing || (!plan.setLoaded && !plan.setReserve)) return;
    // Same rule as the pickup: nothing unless loaded is given and not -1.
    if (!plan.setLoaded || plan.loaded == -1) { Log("give: slot %u ammo override skipped: Give.Loaded unset or -1 (native full ammo kept)", slot); return; }
    if (!ammoSettersProven) { Log("give: ammo override skipped: setters 0x%x/0x%x not proven", unsigned(SetLoaded), unsigned(SetReserve)); return; }
    std::uint32_t current = 0; std::uint8_t flags = 0; ReadU32(firing + 0x80, current); ReadU8(firing + 0xa8, flags);
    const int reserveWanted = plan.setReserve ? plan.reserve : int(current);
    if (plan.setReserve && (flags & 8)) Log("give: slot %u reserve %d will be ignored natively: firing+0xa8 has bit 8 (non-finite ammo)", slot, plan.reserve);
    reinterpret_cast<SetAmmoFn>(SetReserve)(reinterpret_cast<void*>(firing), reserveWanted);
    reinterpret_cast<SetAmmoFn>(SetLoaded)(reinterpret_cast<void*>(firing), plan.loaded);
    std::uint32_t loaded = 0, reserve = 0; ReadU32(firing + 0x7c, loaded); ReadU32(firing + 0x80, reserve);
    Log("give: slot %u ammo set reserve %d then loaded %d, read back loaded %d reserve %d", slot, reserveWanted, plan.loaded, int(loaded), int(reserve));
}

// Pass-through detour: records which thread the engine itself gives on.
void* __fastcall GiveHook(void* soldier, void*, void* data, std::uint32_t slot, std::uint32_t selectNow, std::uint32_t forceSlot) {
    if (*reinterpret_cast<std::uint32_t*>(soldier) == ServerSoldierVtable) {
        DWORD none = 0; nativeGiveThread.compare_exchange_strong(none, GetCurrentThreadId());
        if (nativeGiveCalls.fetch_add(1) < 12) {
            char name[128] = "-"; if (data) DataName(reinterpret_cast<std::uintptr_t>(data), name, sizeof name);
            Log("give: native give soldier 0x%08x data 0x%08x %s slot %u select %u force %u", unsigned(reinterpret_cast<std::uintptr_t>(soldier)),
                unsigned(reinterpret_cast<std::uintptr_t>(data)), name, slot, selectNow & 0xff, forceSlot & 0xff);
        }
    }
    return giveOriginal(soldier, data, slot, selectNow, forceSlot);
}

// Where the give runs. ServerSoldierEntity vtable slot 68 (entry 0x1424960)
// is 0x695ed0, thiscall(soldier, uint inputIndex), ret 4: it applies one
// queued client input to that soldier. Its only caller, the server's
// client-input drain (0x659917 in 0x659710), calls it once per input, and
// after it returns no item pointer is live and nothing walks +0x2b8, so a
// post-call of it is a safe place for 0x6bcf80. (A firing Step post-call is
// not: its callers keep the item and the vector iterator live.) The entry is
// swapped, which covers every soldier, AI included, so the local server
// soldier is matched before anything runs; the entry is put back once the
// give is done and on detach.
constexpr std::uintptr_t SoldierInputSlot = 0x1424960, SoldierInput = 0x695ed0;
constexpr unsigned char SoldierInputCode[] = {0x53,0x8b,0x5c,0x24,0x08,0x56,0x8b,0xf1,0x8b,0x86,0x94,0x00,0x00,0x00,0x2b,0x86,0x90,0x00,0x00,0x00,0xc1,0xf8};
using SoldierInputFn = void(__thiscall*)(void* soldier, std::uint32_t index);
void __fastcall SoldierInputHook(void* soldier, void*, std::uint32_t index);
bool RestoreSoldierUpdate();
constexpr std::uintptr_t SoldierUpdateSlot = 0x1423010, SoldierUpdate = 0x6e9000; // firing Update, see below
// Swaps a vtable entry from `from` to `to`, only while it still holds `from`.
bool SwapEntry(std::uintptr_t entry, std::uintptr_t from, std::uintptr_t to) {
    DWORD old = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(entry), 4, PAGE_READWRITE, &old)) return false;
    const bool swapped = InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(entry), LONG(to), LONG(from)) == LONG(from);
    VirtualProtect(reinterpret_cast<void*>(entry), 4, old, &old);
    return swapped;
}
bool RestoreSoldierInput() {
    return SwapEntry(SoldierInputSlot, reinterpret_cast<std::uintptr_t>(&SoldierInputHook), SoldierInput);
}

unsigned stableFrames;
std::uint32_t stableSoldier;
// Selection watch after the give (game thread only).
ULONGLONG watchStart = 0, lastSelectionChange = 0; bool watchFinished = false; unsigned reselects = 0;
void WatchSelection(std::uintptr_t at);
bool RestoreSoldierUpdate();
bool threadWarned, firstServerCall = true;
void TryGive(std::uintptr_t soldier, bool serverSimThread = false) {
    if (planDone.load(std::memory_order_acquire) || !planResolved.load(std::memory_order_acquire)) return;
    std::uint32_t vt = 0;
    if (!ReadU32(soldier, vt) || vt != ServerSoldierVtable) return;
    Chain c;
    if (!ReadChain(c) || c.soldier != soldier || !c.count) { stableFrames = 0; return; }
    if (firstServerCall) {
        firstServerCall = false;
        Log("give: server input slot runs on thread %lu for the local soldier 0x%08x (native give thread %lu)",
            GetCurrentThreadId(), c.soldier, nativeGiveThread.load());
    }
    // Let the spawn settle for about a second of frames on the same soldier.
    if (c.soldier != stableSoldier) { stableSoldier = c.soldier; stableFrames = 0; }
    if (++stableFrames < 60) return;
    const DWORD native = nativeGiveThread.load();
    // The spawn gives run on a load-time thread, not the per-frame server
    // thread (live: spawn 48644, server copy Update 45768); a server-copy
    // Update post-call is on the server simulation thread by construction.
    if (!serverSimThread && native != GetCurrentThreadId()) {
        if (!threadWarned) Log("give: waiting: server input thread %lu, native give thread %lu; gives run only on the native give thread",
            GetCurrentThreadId(), native), threadWarned = true;
        return;
    }
    planDone.store(true, std::memory_order_release);
    // We are in the slot-68 post-call: the soldier's input has been applied,
    // so no frame of ours is inside a firing Update (0x6e9000) or a walk of
    // the server item vector (+0x2b8). The callers below are logged as evidence.
    void* frames[12]{}; const USHORT depth = RtlCaptureStackBackTrace(0, 12, frames, nullptr);
    char trace[12 * 11 + 1]{}; for (USHORT i = 0; i < depth; ++i) std::snprintf(trace + i * 11, 12, " 0x%08x", unsigned(reinterpret_cast<std::uintptr_t>(frames[i])));
    Log("give: running on game thread %lu, soldier 0x%08x stable for %u frames; stack%s", GetCurrentThreadId(), c.soldier, stableFrames, trace);
    for (unsigned s = 0; s < SlotCount; ++s)
        if (plan.slots[s].wanted && int(s) != plan.select && (plan.slots[s].remove || plan.slots[s].data)) GiveOne(c, s, plan.slots[s], false);
    if (plan.select >= 0) {
        const auto& r = plan.slots[plan.select];
        if (r.wanted && (r.remove || r.data)) GiveOne(c, unsigned(plan.select), r, true);
        else Log("give: Give.Select %d ignored: no resolved give for that slot (the plain selector 0x6d7c80 is not verified)", plan.select);
    }
    LogInventory("give: after");
    LogEntryActionNames();
    if (plan.select >= 0) { Chain after; if (ReadChain(after)) Log("give: select read-back: selected slot %u (wanted %d) %s", after.selected, plan.select, int(after.selected) == plan.select ? "ok" : "DIFFERS"); }
    Log("give: done");
    InterlockedExchange(&skipGameplaySeen, 1);
    Log(RestoreSoldierInput() ? "give: soldier input entry 0x%x restored" : "give: soldier input entry 0x%x changed; left as is", unsigned(SoldierInputSlot));
    // The firing-Update post-call stays hooked to watch the selection (below).
    if (plan.select >= 0) { watchStart = lastSelectionChange = GetTickCount64(); Log("give: watching slot %d selection", plan.select); }
    else if (ArsenalWanted()) Log("give: firing update stays hooked for the arsenal");
    else Log(RestoreSoldierUpdate() ? "give: soldier update entry 0x%x restored" : "give: soldier update entry 0x%x changed; left as is", unsigned(SoldierUpdateSlot));
}

// 3 of 8 SPAS launches (2026-10-08) logged "selected slot 1 ok" and later
// held the XM8 (slot 0) when the trace attached. After the give, the server
// copy's firing post-call checks the selection: a move away from Give.Select
// is logged and re-selected (at most 3 times) with the selector the give
// already uses. "give: holding" means 5 s without a move; the runner waits
// for it before starting a tape.
void WatchSelection(std::uintptr_t at) {
    if (!watchStart || watchFinished) return;
    Chain c; std::uint32_t firing = 0;
    if (!ReadChain(c) || !(c.selected < c.count && c.items[c.selected] && ReadU32(c.items[c.selected] + 0x10, firing) && at == firing)) return;
    const auto now = GetTickCount64();
    if (int(c.selected) != plan.select) {
        lastSelectionChange = now;
        Log("give: selection moved to slot %u, %.1f s after the give", c.selected, double(now - watchStart) / 1000.0);
        if (reselects < 3 && selectProven) {
            ++reselects;
            reinterpret_cast<SelectFn>(SelectSlot)(reinterpret_cast<void*>(c.inventory), plan.select, 1);
            Chain after; if (ReadChain(after)) Log("give: reselect %u: selected slot %u (wanted %d) %s", reselects, after.selected, plan.select, int(after.selected) == plan.select ? "ok" : "DIFFERS");
            return;
        }
    }
    // The slot can also be refilled with other data (AEK run 123705 held the
    // XM8 with slot 0 still selected): re-give it, within the same 3 retries.
    const auto& wanted = plan.slots[plan.select];
    std::uint32_t held = 0;
    if (int(c.selected) == plan.select && wanted.data && !wanted.remove && ReadU32(c.items[c.selected] + 4, held) && held != wanted.data) {
        lastSelectionChange = now;
        Log("give: slot %d holds data 0x%08x, not the given 0x%08x, %.1f s after the give", plan.select, held, unsigned(wanted.data), double(now - watchStart) / 1000.0);
        if (reselects < 3) { ++reselects; GiveOne(c, unsigned(plan.select), wanted, true); return; }
    } else if (int(c.selected) == plan.select && now - lastSelectionChange >= 5000) {
        watchFinished = true;
        Log("give: holding slot %d, 5 s without a move (reselects %u)", plan.select, reselects);
    }
    if (!watchFinished && now - watchStart >= 30000) { watchFinished = true; Log("give: selection NOT held after 30 s (reselects %u)", reselects); }
    if (watchFinished && !ArsenalWanted()) Log(RestoreSoldierUpdate() ? "give: soldier update entry 0x%x restored" : "give: soldier update entry 0x%x changed; left as is", unsigned(SoldierUpdateSlot));
}

// One arsenal step on the game thread, in the post-call of the server copy's
// selected firing Update. The selected item is the caller's and must not be
// replaced here, so the next gun goes into whichever of Primary1/Primary2 is
// not selected and the give selects it; the previous gun stays one switch away.
void ArsenalStep(std::uintptr_t at) {
    if (!arsenalReady.load(std::memory_order_acquire) || !arsenalRequest.load()) return;
    // A swap while the client is still equipping the previous gun (async
    // bundle with its mesh and 1P clips not loaded yet) faulted in the client
    // switch (live: 0xf691cf from 0x6ef5e4, 1 s after the previous swap).
    // Faster steps accumulate and apply as one jump.
    if (GetTickCount64() - arsenalLastSwap < ULONGLONG(arsenalMinIntervalMs)) return;
    Chain c; std::uint32_t firing = 0;
    if (!ReadChain(c) || !(c.selected < c.count && c.items[c.selected] && ReadU32(c.items[c.selected] + 0x10, firing) && at == firing)) return;
    const int step = arsenalRequest.exchange(0);
    if (!step) return;
    watchFinished = true; // the arsenal owns the selection from here on
    const int n = int(arsenal.size());
    if (arsenalIndex < 0 && !arsenalStart.empty()) {
        for (int i = 0; i < n; ++i) if (!_stricmp(arsenal[i].name.c_str(), arsenalStart.c_str())) { arsenalIndex = i - (step > 0 ? 1 : -1); break; }
        if (arsenalIndex < 0 && _stricmp(arsenal[0].name.c_str(), arsenalStart.c_str())) Log("arsenal: Arsenal.Start %s not in the list; starting at the top", arsenalStart.c_str());
    }
    arsenalIndex = arsenalIndex < 0 ? (step > 0 ? step - 1 : n + step) : arsenalIndex + step;
    arsenalIndex = ((arsenalIndex % n) + n) % n;
    const auto& e = arsenal[arsenalIndex];
    const unsigned slot = c.selected == 0 ? 1 : 0;
    Log("arsenal: %d/%d %s into slot %u (selected was %u)", arsenalIndex + 1, n, e.name.c_str(), slot, c.selected);
    GiveRequest r; r.wanted = true; r.name = e.name; r.data = e.data;
    GiveOne(c, slot, r, true);
    arsenalLastSwap = GetTickCount64();
}

// Hotkeys (only while a window of this process has focus) and named events.
DWORD WINAPI ArsenalInput(void*) {
    HANDLE events[2] = {CreateEventW(nullptr, FALSE, FALSE, L"Local\\BC2TestRange.ArsenalNext"),
                        CreateEventW(nullptr, FALSE, FALSE, L"Local\\BC2TestRange.ArsenalPrev")};
    if (!events[0] || !events[1]) Log("arsenal: named events unavailable (%lu); hotkeys only", GetLastError());
    bool nextDown = false, prevDown = false;
    for (;;) {
        if (events[0] && events[1]) {
            const DWORD w = WaitForMultipleObjects(2, events, FALSE, 15);
            if (w == WAIT_OBJECT_0) arsenalRequest.fetch_add(1);
            else if (w == WAIT_OBJECT_0 + 1) arsenalRequest.fetch_sub(1);
        } else Sleep(15);
        DWORD owner = 0; GetWindowThreadProcessId(GetForegroundWindow(), &owner);
        const bool focus = owner == GetCurrentProcessId();
        const bool next = focus && (GetAsyncKeyState(arsenalNextKey) & 0x8000), prev = focus && (GetAsyncKeyState(arsenalPrevKey) & 0x8000);
        if (next && !nextDown) arsenalRequest.fetch_add(1);
        if (prev && !prevDown) arsenalRequest.fetch_sub(1);
        nextDown = next; prevDown = prev;
    }
}

// "F1".."F24", "PageUp", "PageDown", "Home", "End", "Insert", "Delete", a
// single letter or digit, or a virtual-key code ("0x75"); 0 if unknown.
int ParseKey(const std::string& v) {
    if ((v[0] == 'F' || v[0] == 'f') && v.size() <= 3 && std::isdigit(static_cast<unsigned char>(v[1]))) {
        const int n = std::atoi(v.c_str() + 1); return n >= 1 && n <= 24 ? VK_F1 + n - 1 : 0;
    }
    static const std::pair<const char*, int> named[] = {{"PageUp", VK_PRIOR}, {"PageDown", VK_NEXT}, {"Home", VK_HOME}, {"End", VK_END}, {"Insert", VK_INSERT}, {"Delete", VK_DELETE}};
    for (auto& [name, vk] : named) if (!_stricmp(v.c_str(), name)) return vk;
    if (v.size() == 1 && std::isalnum(static_cast<unsigned char>(v[0]))) return std::toupper(static_cast<unsigned char>(v[0]));
    if (!v.rfind("0x", 0)) return int(std::strtol(v.c_str(), nullptr, 16));
    return 0;
}

bool ParseArsenal(const std::string& key, const std::string& value) {
    if (key.rfind("Arsenal.", 0)) return false;
    const std::string field = key.substr(8);
    if (field == "Gun") arsenalNames.push_back(value);
    else if (field == "All") arsenalAll = value == "1";
    else if (field == "Exclude") {
        arsenalExclude.clear();
        std::size_t at = 0;
        while ((at = value.find_first_not_of(" ,\t", at)) != std::string::npos) {
            const auto end = value.find_first_of(" ,\t", at);
            arsenalExclude.push_back(value.substr(at, end - at)); at = end;
        }
    } else if (field == "NextKey" || field == "PrevKey") {
        const int vk = ParseKey(value);
        if (!vk) Log("arsenal: %s %s is not a key; default kept", key.c_str(), value.c_str());
        else (field == "NextKey" ? arsenalNextKey : arsenalPrevKey) = vk;
    } else if (field == "Start") arsenalStart = value;
    else if (field == "First") {
        if (!arsenalFirstSet) arsenalFirst.clear(), arsenalFirstSet = true;
        arsenalFirst.push_back(value);
    }
    else if (field == "MinIntervalMs") arsenalMinIntervalMs = (std::max)(0, std::atoi(value.c_str()));
    else Log("arsenal: ignored unknown key %s %s", key.c_str(), value.c_str());
    return true;
}

void __fastcall SoldierInputHook(void* soldier, void*, std::uint32_t index) {
    reinterpret_cast<SoldierInputFn>(SoldierInput)(soldier, index);
    TryGive(reinterpret_cast<std::uintptr_t>(soldier));
}

// Single player never drains queued client input, so slot 68 never runs there
// (live 2026-10-08). Second server-thread point: vtable 0x1422f20 slot 60
// (entry 0x1423010) is 0x6afd00, thiscall(this, a, b), ret 8, the per-frame
// update that steps only the CURRENT weapon's firing (0x6e9000 at 0x6b0014)
// and walks no item vector. Its post-call holds no item pointer, so the give
// runs there for the local server soldier, on the native give thread only.
// 0x6e9000 firing Update, thiscall(firing, input, flag), ret 8. Post-call of the
// SERVER copy of the selected weapon only: the give fills an empty slot below
// the vector size, so nothing the caller still holds is freed or moved.
constexpr unsigned char SoldierUpdateCode[] = {0x83,0xec,0x08,0x56,0x8b,0xf1,0xd9,0x46,0x48,0x57,0x8b,0x7c,0x24,0x14,0xd8,0x67,0x18};
using SoldierUpdateFn = void(__thiscall*)(void* entity, std::uint32_t a, std::uint32_t b);
bool firstUpdateCall = true;
std::atomic<bool> nativeFiringOwner{false},nativeUiOnlyReady{false};
SoldierUpdateFn soldierUpdateOriginal = nullptr;
void __fastcall SoldierUpdateHook(void* entity, void*, std::uint32_t a, std::uint32_t b) {
    soldierUpdateOriginal(entity, a, b);
    if(nativeFiringOwner.load(std::memory_order_acquire))return;
    if (planDone.load(std::memory_order_acquire)) {
        WatchSelection(reinterpret_cast<std::uintptr_t>(entity));
        ArsenalStep(reinterpret_cast<std::uintptr_t>(entity));
        return;
    }
    if (!planResolved.load(std::memory_order_acquire)) return;
    Chain c;
    if (!ReadChain(c)) return;
    const auto at = reinterpret_cast<std::uintptr_t>(entity);
    if (firstUpdateCall) {
        firstUpdateCall = false; std::uint32_t vt = 0; ReadU32(at, vt);
        Log("give: soldier update slot runs on thread %lu, this 0x%08x vtable 0x%08x, local soldier 0x%08x (native give thread %lu)",
            GetCurrentThreadId(), unsigned(at), vt, c.soldier, nativeGiveThread.load());
    }
    std::uint32_t firing = 0;
    if (c.selected < c.count && c.items[c.selected] && ReadU32(c.items[c.selected] + 0x10, firing) && at == firing) TryGive(c.soldier, true);
}
bool RestoreSoldierUpdate() {
    return soldierUpdateOriginal && MH_DisableHook(reinterpret_cast<void*>(SoldierUpdate)) == MH_OK;
}

bool ParseGive(const std::string& key, const std::string& value) {
    if (key.rfind("Give.", 0)) return false;
    const std::string field = key.substr(5);
    int n = -1;
    if (field.size() == 5 && !field.compare(0, 4, "Slot") && field[4] >= '0' && field[4] < char('0' + SlotCount)) {
        auto& r = plan.slots[field[4] - '0'];
        r.wanted = true; r.name = value; r.remove = !_stricmp(value.c_str(), "none");
        plan.any = true;
    } else if (field == "Select" && std::sscanf(value.c_str(), "%d", &n) == 1 && n >= 0 && n < int(SlotCount)) plan.select = n;
    else if (field == "Loaded" && std::sscanf(value.c_str(), "%d", &n) == 1 && n >= -1) plan.loaded = n, plan.setLoaded = true; // -1 = keep native
    else if (field == "Reserve" && std::sscanf(value.c_str(), "%d", &n) == 1 && n >= -1) plan.reserve = n, plan.setReserve = true; // -1 = infinite
    else Log("give: ignored unknown or bad key %s %s", key.c_str(), value.c_str());
    return true;
}

bool InstallGive() {
    if (!GiveBuildMatches()) { Log("give: REFUSED: native give path not proven on this executable"); return false; }
    if (const MH_STATUS init = MH_Initialize(); init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) { Log("give: REFUSED: MinHook init failed"); return false; }
    if (std::memcmp(reinterpret_cast<const void*>(SoldierInput), SoldierInputCode, sizeof SoldierInputCode)
        || *reinterpret_cast<const std::uintptr_t*>(SoldierInputSlot) != SoldierInput) {
        Log("give: REFUSED: soldier input slot 0x%x / code 0x%x not proven", unsigned(SoldierInputSlot), unsigned(SoldierInput)); return false;
    }
    if (MH_CreateHook(reinterpret_cast<void*>(GiveWeapon), reinterpret_cast<void*>(&GiveHook), reinterpret_cast<void**>(&giveOriginal)) != MH_OK
        || MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        Log("give: REFUSED: hook install failed"); return false;
    }
    if (!SwapEntry(SoldierInputSlot, SoldierInput, reinterpret_cast<std::uintptr_t>(&SoldierInputHook))) {
        Log("give: REFUSED: soldier input entry 0x%x swap failed", unsigned(SoldierInputSlot));
        MH_DisableHook(reinterpret_cast<void*>(GiveWeapon)); return false;
    }
    if (std::memcmp(reinterpret_cast<const void*>(SoldierUpdate), SoldierUpdateCode, sizeof SoldierUpdateCode)
        || MH_CreateHook(reinterpret_cast<void*>(SoldierUpdate), reinterpret_cast<void*>(&SoldierUpdateHook), reinterpret_cast<void**>(&soldierUpdateOriginal)) != MH_OK
        || MH_EnableHook(reinterpret_cast<void*>(SoldierUpdate)) != MH_OK)
        Log("give: soldier update entry 0x%x not proven or not swapped; only the input slot is used", unsigned(SoldierUpdateSlot));
    else Log("give: firing update 0x%x hooked (server copy of the selected weapon runs the give)", unsigned(SoldierUpdate));
    Log("give: hooks installed on give 0x%x and soldier input entry 0x%x (0x%x)", unsigned(GiveWeapon), unsigned(SoldierInputSlot), unsigned(SoldierInput));
    return true;
}

bool ApplySettings(const std::vector<std::pair<std::string, std::string>>& pairs);

#include "BC2RenderResolution.h"

DWORD WINAPI Worker(void*) {
    logFile = _wfopen(Beside(L"BC2TestRange.log").c_str(), L"a");
    launchTicks = GetTickCount64();
    Log("BC2TestRange start pid=%lu", GetCurrentProcessId());
    if (!BuildMatches()) { Log("REFUSED: executable is not the expected build 795745 code"); return 1; }
    if (!renderResolution::Install()) return 1;
    auto all = ReadConfig();
    if (all.empty()) { Log("REFUSED: BC2TestRange.cfg missing or empty"); return 1; }
    // TestRange.* keys configure this DLL, Give.* keys the weapon give; only
    // known engine settings groups reach the registry. An unregistered group
    // makes the registry lookup kill BC2, so any other key refuses the run.
    std::vector<std::pair<std::string, std::string>> pairs;
    bool startLevel = false;
    const auto engineKey = [](const std::string& key) {
        for (const char* group : {"Client.", "Server.", "Game.", "Network.", "Physics."})
            if (key.rfind(group, 0) == 0) return true;
        return false;
    };
    for (auto& [key, value] : all) {
        if (key == "TestRange.Start") startLevel = value == "newSP";
        else if (key == "TestRange.Difficulty") startDifficulty = std::atoi(value.c_str());
        else if (key == "TestRange.WaitFrames") startWaitFrames = std::atoi(value.c_str());
        else if (key == "TestRange.SkipCutscene") skipArmed = value == "1";
        else if (key == "TestRange.SkipSeconds") skipSeconds = std::atoi(value.c_str());
        else if (key == "TestRange.TraceUi") traceUi = value == "1";
        else if (key == "TestRange.RegisterNetIds") netIdRegister = value == "1";
        else if (key == "TestRange.ReequipGuard") reequipGuard = value != "0";
        else if (key == "TestRange.ShaderOrder") shaderOrder = value != "0";
        else if (key == "TestRange.GapFill") gapFill = value != "0";
        else if (key == "TestRange.Mount") {
            const auto space = value.find_first_of(" \t");
            if (space == std::string::npos) { Log("REFUSED: TestRange.Mount needs \"<source> <target>\""); return 1; }
            mounts.emplace_back(value.substr(0, space), Trim(value.substr(space)));
        }
        else if (ParseGive(key, value)) {}
        else if (ParseArsenal(key, value)) {}
        else if (engineKey(key)) {
            if (key == "Client.Level") levelPath = value;
            pairs.emplace_back(key, value);
        }
        else { Log("REFUSED: unknown cfg key \"%s\"", key.c_str()); return 1; }
    }
    // Settings first: they race the menu's Client.LoadMenu read.
    const bool settings = pairs.empty() || ApplySettings(pairs);
    // Extra level bundles must be in place before newSP loads the level.
    if (traceUi && mounts.empty()) { LogGameExits(); TraceUi(); }
    if (!mounts.empty() && !ApplyMounts()) Log("mount: not every TestRange.Mount applied; weapons from them may not resolve");
    // The give hooks go in before the level can start, so the engine's own
    // spawn gives reveal its give thread.
    const bool give = (plan.any || ArsenalWanted()) && InstallGive();
    // A UI-only level fixture never has a give plan or firing hook to finish.
    // Publish this only after parsing the complete config and deciding ownership.
    if(!plan.any&&!ArsenalWanted())nativeUiOnlyReady.store(true,std::memory_order_release);
    if (give && ArsenalWanted()) CloseHandle(CreateThread(nullptr, 0, ArsenalInput, nullptr, 0, nullptr));
    bool armed = false;
    if (startLevel && !settings) Log("REFUSED: newSP not armed: settings did not apply");
    else if (startLevel) {
        // The UI manager exists before the menu; wait for it, then hook its update.
        for (int i = 0; i < 60000 && !*reinterpret_cast<void* volatile*>(UiManagerGlobal); ++i) Sleep(1);
        if (!*reinterpret_cast<void* volatile*>(UiManagerGlobal) || !ArmStart()) { Log("REFUSED: UI update hook site did not verify"); RestoreUiSlot("arm failed"); }
        else {
            if (skipArmed && !SkipCodeMatches()) { Log("REFUSED: skipCutscene 0x%x did not verify; cutscene skip off", unsigned(SkipCutscene)); skipArmed = 0; }
            InterlockedExchange(&startArmed, 1); armed = true;
            Log("settings applied; newSP armed (difficulty %d, after %d menu frames)", startDifficulty, startWaitFrames);
        }
    } else if (settings) Log("BC2TestRange done; settings applied. Start a new campaign to load Client.Level.");
    if (give && ResolvePlan()) { planResolved.store(true, std::memory_order_release); Log("give: plan queued for the game thread"); }
    // The log stays open while the UI detour or the game-thread give can write.
    if (!give && !armed && logFile) std::fclose(logFile), logFile = nullptr;
    return settings ? 0 : 1;
}

bool ApplySettings(const std::vector<std::pair<std::string, std::string>>& pairs) {
    void* registry = nullptr;
    // Injected at process start, this must beat the UI flow's single
    // Client.LoadMenu read (0x964faf), so poll tightly.
    for (int i = 0; i < 60000 && !(registry = *reinterpret_cast<void* volatile*>(SettingsGlobal)); ++i) Sleep(1);
    // The groups register right after the registry; a scalar setting is only
    // reachable once its group exists.
    auto get0 = reinterpret_cast<GetFn>(GetByName);
    static const char none[4]{};
    for (int i = 0; registry && i < 30000; ++i) {
        bool ready = true;
        for (auto& [key, value] : pairs) {
            void* field = get0(registry, key.c_str(), none);
            if (!field || field == none) { ready = false; break; }
        }
        if (ready) break;
        Sleep(1);
    }
    if (!registry) { Log("REFUSED: settings registry never appeared"); return false; }
    auto set = reinterpret_cast<SetFn>(SetByName);
    auto get = reinterpret_cast<GetFn>(GetByName);
    static const char empty[4]{};
    for (auto& [key, value] : pairs) {
        void* before = get(registry, key.c_str(), empty);
        std::string old = Describe(before, value);
        EaString k = View(key), v = View(value);
        set(registry, &k, &v);
        void* after = get(registry, key.c_str(), empty);
        // The by-name setter converts scalars but leaves eastl::string fields
        // unchanged. Build a fresh engine-owned string with the field's own
        // allocator and swap it in; the old buffer is left allocated.
        const std::string quoted = "\"" + value + "\"";
        if (after && Describe(after, value) != quoted && Describe(after, value).front() == '"') {
            auto* field = static_cast<EaString*>(after);
            EaString fresh{nullptr, nullptr, nullptr, field->allocator};
            reinterpret_cast<StringFn>(StringFromCString)(&fresh, value.c_str());
            field->begin = fresh.begin; field->end = fresh.end; field->capacity = fresh.capacity;
            Log("assigned string field %s directly", key.c_str());
        }
        Log("set %s = %s (before %s, after %s)", key.c_str(), value.c_str(), old.c_str(), Describe(after, value).c_str());
    }
    return true;
}
}

// Explicit x86 cdecl exports consumed by the native mod's matching interface.
// This changes hook ownership only; it does not relax executable signatures or
// select/give permissions. An older native build keeps the fixture's own hook.
#pragma comment(linker,"/EXPORT:FvrTestRangePrepareNative=_FvrTestRangePrepareNative")
#pragma comment(linker,"/EXPORT:FvrTestRangeFiringPost=_FvrTestRangeFiringPost")
extern "C" bool __cdecl FvrTestRangePrepareNative(std::uint32_t abi) {
    if(abi!=1||(!planDone.load(std::memory_order_acquire)&&!nativeUiOnlyReady.load(std::memory_order_acquire)))return false;
    if(nativeFiringOwner.load(std::memory_order_acquire))return true;
    nativeFiringOwner.store(true,std::memory_order_release);
    if(soldierUpdateOriginal){
        const auto status=MH_DisableHook(reinterpret_cast<void*>(SoldierUpdate));
        if(status!=MH_OK&&status!=MH_ERROR_DISABLED){nativeFiringOwner.store(false,std::memory_order_release);return false;}
    }
    Log("give: firing Update yielded to native owner (fixture ABI 1)");return true;
}
extern "C" void __cdecl FvrTestRangeFiringPost(std::uint32_t firing) {
    if(!nativeFiringOwner.load(std::memory_order_acquire)||!planDone.load(std::memory_order_acquire))return;
    WatchSelection(firing);ArsenalStep(firing);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance; DisableThreadLibraryCalls(instance);
        if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH) {
        // Never leave the vtable pointing into an unloaded DLL. No logging
        // here: another thread may have died holding the log lock.
        RestoreUiSlot("detach", true);
        RestoreSoldierInput();RestoreSoldierUpdate();
    }
    return TRUE;
}
