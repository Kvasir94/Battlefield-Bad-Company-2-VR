// Loads BC2TestRange.dll into a running BFBC2Game.exe: BC2TestRangeInject <pid> <dll>.
// Built x86 so the LoadLibraryW address matches the 32-bit game.
#include <windows.h>
#include <cstdio>
#include <cwchar>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) { std::fwprintf(stderr, L"usage: BC2TestRangeInject <pid> <full dll path>\n"); return 2; }
    const DWORD pid = std::wcstoul(argv[1], nullptr, 10);
    wchar_t dll[MAX_PATH]{};
    if (!pid || !GetFullPathNameW(argv[2], MAX_PATH, dll, nullptr) || GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        std::fwprintf(stderr, L"bad pid or dll path\n"); return 2;
    }
    HANDLE process = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                                 PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid);
    if (!process) { std::fwprintf(stderr, L"OpenProcess failed %lu\n", GetLastError()); return 1; }
    wchar_t image[MAX_PATH]{}; DWORD length = MAX_PATH;
    if (!QueryFullProcessImageNameW(process, 0, image, &length) || !wcsstr(_wcslwr(image), L"\\bfbc2game.exe")) {
        std::fwprintf(stderr, L"target is not BFBC2Game.exe\n"); CloseHandle(process); return 1;
    }
    const SIZE_T bytes = (wcslen(dll) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(process, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int result = 1;
    if (remote && WriteProcessMemory(process, remote, dll, bytes, nullptr)) {
        auto load = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
        if (HANDLE thread = CreateRemoteThread(process, nullptr, 0, load, remote, 0, nullptr)) {
            WaitForSingleObject(thread, 15000);
            DWORD module = 0; GetExitCodeThread(thread, &module);
            result = module ? 0 : 1;
            std::wprintf(L"{\"pid\":%lu,\"loaded\":%s}\n", pid, module ? L"true" : L"false");
            CloseHandle(thread);
        } else std::fwprintf(stderr, L"CreateRemoteThread failed %lu\n", GetLastError());
    } else std::fwprintf(stderr, L"remote allocation failed %lu\n", GetLastError());
    if (remote) VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    CloseHandle(process);
    return result;
}
