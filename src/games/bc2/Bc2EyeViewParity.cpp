#include "Bc2EyeViewParity.h"
#include "Bc2PatternScan.h"
#include <Windows.h>
#include <atomic>
#include <cstring>
namespace fvr::bc2::eyeParity {
namespace {
// How the getter hands the value back and the setter takes it (all thiscall,
// setters `ret 4`, verified per body below; DIS in BC2_EYE_PARITY_POST_RE.md).
enum class Kind:unsigned char {
    Pointer,   // getter returns &view->field, setter copies from a pointer
    Reference, // getter returns the stored object pointer, setter Release/AddRef
    Dword,     // getter returns EAX, setter takes a dword by value
    Byte,      // getter returns AL, setter takes a byte by value
    Float,     // getter returns ST0 (FLD), setter takes a float by value (SSE load)
    Out        // getter writes to an out pointer, setter copies from a pointer
};
struct Accessor {unsigned char set,get;Kind kind;std::uint16_t field,bytes;const char* setBody;const char* getBody;};
// Mirror order follows 0x5de070 where it sets the same slot.
constexpr Accessor accessors[]{
    {21,22,Kind::Pointer,0x1530,12,"55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 99 30 15 00 00 D9 40 04 D9 99 34 15 00 00 D9 40 08 D9 99 38 15 00 00 8B E5 5D C2 04 00","8D 81 30 15 00 00 C3"},
    {23,24,Kind::Reference,0x1540,4,"56 8B F1 8B 8E 40 15 00 00 57 8B 7C 24 0C 3B F9 74 27 85 C9 74 11 8B 01 8B 50 04 FF D2 C7 86 40 15 00 00 00 00 00 00 85 FF 89 BE 40 15 00 00 74 08 8B 07 8B 10 8B CF FF D2 5F 5E C2 04 00","8B 81 40 15 00 00 C3"},
    {25,26,Kind::Dword,0x1544,4,"8B 44 24 04 89 81 44 15 00 00 C2 04 00","8B 81 44 15 00 00 C3"},
    {27,28,Kind::Reference,0x1548,4,"56 8B F1 8B 8E 48 15 00 00 57 8B 7C 24 0C 3B F9 74 27 85 C9 74 11 8B 01 8B 50 04 FF D2 C7 86 48 15 00 00 00 00 00 00 85 FF 89 BE 48 15 00 00 74 08 8B 07 8B 10 8B CF FF D2 5F 5E C2 04 00","8B 81 48 15 00 00 C3"},
    {29,30,Kind::Dword,0x154c,4,"8B 44 24 04 89 81 4C 15 00 00 C2 04 00","8B 81 4C 15 00 00 C3"},
    {31,32,Kind::Float,0x1550,4,"F3 0F 10 44 24 04 F3 0F 11 81 50 15 00 00 C2 04 00","D9 81 50 15 00 00 C3"},
    {33,34,Kind::Out,0x1600,16,"55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 99 00 16 00 00 D9 40 04 D9 99 04 16 00 00 D9 40 08 D9 99 08 16 00 00 D9 40 0C D9 99 0C 16 00 00 8B E5 5D C2 04 00",
        "8B 44 24 04 D9 81 00 16 00 00 D9 18 D9 81 04 16 00 00 D9 58 04 D9 81 08 16 00 00 D9 58 08 D9 81 0C 16 00 00 D9 58 0C C2 04 00"},
    {35,36,Kind::Out,0x1610,8,"8B 44 24 04 8B 10 89 91 10 16 00 00 8B 40 04 89 81 14 16 00 00 C2 04 00","8B 91 10 16 00 00 8B 44 24 04 8B 89 14 16 00 00 89 10 89 48 04 C2 04 00"},
    {37,38,Kind::Pointer,0x1560,0x70,"81 C1 60 15 00 00 E9 ?? ?? ?? ??","8D 81 60 15 00 00 C3"},
    {41,42,Kind::Byte,0x1648,1,"8A 44 24 04 88 81 48 16 00 00 C2 04 00","8A 81 48 16 00 00 C3"},
    {44,43,Kind::Out,0x1620,16,"55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 99 20 16 00 00 D9 40 04 D9 99 24 16 00 00 D9 40 08 D9 99 28 16 00 00 D9 40 0C D9 99 2C 16 00 00 8B E5 5D C2 04 00",
        "8B 44 24 04 D9 81 20 16 00 00 D9 18 D9 81 24 16 00 00 D9 58 04 D9 81 28 16 00 00 D9 58 08 D9 81 2C 16 00 00 D9 58 0C C2 04 00"},
    {48,47,Kind::Float,0x1640,4,"F3 0F 10 44 24 04 F3 0F 11 81 40 16 00 00 C2 04 00","D9 81 40 16 00 00 C3"},
    {50,49,Kind::Out,0x1630,12,"55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 99 30 16 00 00 D9 40 04 D9 99 34 16 00 00 D9 40 08 D9 99 38 16 00 00 8B E5 5D C2 04 00",
        "8B 44 24 04 D9 81 30 16 00 00 D9 18 D9 81 34 16 00 00 D9 58 04 D9 81 38 16 00 00 D9 58 08 C2 04 00"},
    {51,52,Kind::Float,0x1644,4,"F3 0F 10 44 24 04 F3 0F 11 81 44 16 00 00 C2 04 00","D9 81 44 16 00 00 C3"},
    {53,54,Kind::Out,0x1650,16,"55 8B EC 83 E4 F0 8B 45 08 D9 00 D9 99 50 16 00 00 D9 40 04 D9 99 54 16 00 00 D9 40 08 D9 99 58 16 00 00 D9 40 0C D9 99 5C 16 00 00 8B E5 5D C2 04 00",
        "8B 44 24 04 D9 81 50 16 00 00 D9 18 D9 81 54 16 00 00 D9 58 04 D9 81 58 16 00 00 D9 58 08 D9 81 5C 16 00 00 D9 58 0C C2 04 00"},
    {55,56,Kind::Pointer,0x15d0,0x26,"55 8B EC 83 E4 F0 8B 45 08 8B 10 89 91 D0 15 00 00 D9 40 04 D9 99 D4 15 00 00 D9 40 10 D9 99 E0 15 00 00 D9 40 14 D9 99 E4 15 00 00 D9 40 18 D9 99 E8 15 00 00 D9 40 1C D9 99 EC 15 00 00 D9 40 20 D9 99 F0 15 00 00 8A 50 24 88 91 F4 15 00 00 8A 40 25 88 81 F5 15 00 00 8B E5 5D C2 04 00",
        "8D 81 D0 15 00 00 C3"},
    {57,58,Kind::Float,0x1660,4,"F3 0F 10 44 24 04 F3 0F 11 81 60 16 00 00 C2 04 00","D9 81 60 16 00 00 C3"},
};
// Slot 37 tail-jumps (this+0x1560) to this colour-block copy, which copies
// +0,+4,+8,+0x10..+0x20 and calls a 4x4 x87 copy for +0x30. No SSE alignment.
constexpr const char* colourCopyBody="55 8B EC 83 E4 F0 8B 45 08 83 EC 0C 56 8B F1 8B 08 89 0E 8B 50 04 89 56 04 D9 40 08 D9 5E 08 83 C0 30 D9 40 E0 50 D9 5E 10 8D 4E 30 D9 40 E4 D9 5E 14 D9 40 E8 D9 5E 18 D9 40 EC D9 5E 1C D9 40 F0 D9 5E 20 E8 ?? ?? ?? ?? 8B C6 5E 8B E5 5D C2 04 00";
constexpr std::uint32_t colourCopyCall=0x44;
constexpr const char* matrixCopyBody="55 8B EC 83 E4 F0 8B C1 8B 4D 08 D9 01 D9 18 D9 41 04 D9 58 04 D9 41 08 D9 58 08 D9 41 0C D9 58 0C D9 41 10 D9 58 10 D9 41 14 D9 58 14 D9 41 18 D9 58 18 D9 41 1C D9 58 1C D9 41 20 D9 58 20 D9 41 24 D9 58 24 D9 41 28 D9 58 28 D9 41 2C D9 58 2C D9 41 30 D9 58 30 D9 41 34 D9 58 34 D9 41 38 D9 58 38 D9 41 3C D9 58 3C 8B E5 5D C2 04 00";

struct Runtime {
    Candidates c{};std::uintptr_t image=0;bool bound=false;
    std::uint32_t initialisedEye=0;std::atomic<std::uint64_t> mirrors=0,initialised=0;std::atomic<std::uint32_t> lastEye=0;
} runtime;
std::atomic<int> enabledCache{-1};

bool Read(std::uintptr_t at,void* out,std::size_t n)noexcept{
    if(at<0x10000||n>0x1000||at>UINT32_MAX-n)return false;SIZE_T got=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(at),out,n,&got)&&got==n;
}
template<class T> T Fn(unsigned slot)noexcept{return reinterpret_cast<T>(runtime.image+runtime.c.slot[slot]);}
}

std::optional<Candidates> Discover(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t vtable){
    const auto base=scan::Base(b,pe);if(!base||!vtable)return {};
    const auto table=scan::At(b,pe,vtable,60*4);if(!table)return {};
    Candidates c{};c.vtable=vtable;
    const auto slotRva=[&](unsigned s)->std::optional<std::uint32_t>{const auto va=scan::U(b,*table+s*4);if(va<base)return {};return va-base;};
    for(const auto& a:accessors){
        const auto set=slotRva(a.set),get=slotRva(a.get);if(!set||!get)return {};
        const auto setLength=scan::Match(b,pe,*set,a.setBody),getLength=scan::Match(b,pe,*get,a.getBody);if(!setLength||!getLength)return {};
        c.slot[a.set]=*set;c.slot[a.get]=*get;c.length[a.set]=std::uint16_t(*setLength);c.length[a.get]=std::uint16_t(*getLength);
        if(a.set==37){
            const auto copy=scan::Rel32(b,pe,*set+6,0xe9);if(!copy||!scan::Match(b,pe,*copy,colourCopyBody))return {};
            const auto matrix=scan::Rel32(b,pe,*copy+colourCopyCall,0xe8);if(!matrix||!scan::Match(b,pe,*matrix,matrixCopyBody))return {};
            c.colourCopy=*copy;c.matrixCopy=*matrix;
        }
    }
    return c.colourCopy&&c.matrixCopy?std::optional<Candidates>(c):std::nullopt;
}

std::vector<std::pair<std::uint32_t,std::uint32_t>> LiveSpans(const Candidates& c){
    std::vector<std::pair<std::uint32_t,std::uint32_t>> spans;
    for(unsigned s=0;s<c.slot.size();++s)if(c.slot[s])spans.emplace_back(c.slot[s],c.length[s]);
    // Slot 37's rel32 is part of its body; the copy targets are verified whole.
    if(const auto n=engine::ParsePattern(colourCopyBody))spans.emplace_back(c.colourCopy,std::uint32_t(n->size()));
    if(const auto n=engine::ParsePattern(matrixCopyBody))spans.emplace_back(c.matrixCopy,std::uint32_t(n->size()));
    return spans;
}

bool EnabledByEnvironment()noexcept{
    auto cached=enabledCache.load(std::memory_order_acquire);
    if(cached<0){char value[4]{};cached=(GetEnvironmentVariableA("FVR_BC2_EYE_PARITY",value,sizeof(value))&&value[0]=='0')?0:1;enabledCache.store(cached,std::memory_order_release);}
    return cached==1;
}
void Bind(const Candidates& c,std::uintptr_t imageBase)noexcept{runtime.c=c;runtime.image=imageBase;runtime.bound=imageBase&&c.colourCopy;}
bool Bound()noexcept{return runtime.bound;}

bool Mirror(std::uintptr_t main,std::uintptr_t eye)noexcept{
    if(!runtime.bound||!main||!eye||main==eye)return false;
    auto* const e=reinterpret_cast<void*>(eye);auto* const m=reinterpret_cast<void*>(main);
    if(runtime.initialisedEye!=eye){
        // The WorldRenderView ctor leaves +0x1600 and +0x1610 uninitialised
        // (DIS); only DynamicPostProcess writes them. Start the eye from zero
        // through its own setters before the first mirror.
        alignas(16) const float zero[4]{};
        Fn<void(__thiscall*)(void*,const void*)>(33)(e,zero);Fn<void(__thiscall*)(void*,const void*)>(35)(e,zero);
        runtime.initialisedEye=eye;runtime.initialised.fetch_add(1,std::memory_order_relaxed);
    }
    for(const auto& a:accessors){
        switch(a.kind){
        case Kind::Pointer:Fn<void(__thiscall*)(void*,const void*)>(a.set)(e,Fn<const void*(__thiscall*)(void*)>(a.get)(m));break;
        case Kind::Reference:Fn<void(__thiscall*)(void*,void*)>(a.set)(e,Fn<void*(__thiscall*)(void*)>(a.get)(m));break;
        case Kind::Dword:Fn<void(__thiscall*)(void*,std::uint32_t)>(a.set)(e,Fn<std::uint32_t(__thiscall*)(void*)>(a.get)(m));break;
        case Kind::Byte:Fn<void(__thiscall*)(void*,unsigned char)>(a.set)(e,Fn<unsigned char(__thiscall*)(void*)>(a.get)(m));break;
        case Kind::Float:Fn<void(__thiscall*)(void*,float)>(a.set)(e,Fn<float(__thiscall*)(void*)>(a.get)(m));break;
        case Kind::Out:{alignas(16) float value[4]{};Fn<void(__thiscall*)(void*,void*)>(a.get)(m,value);Fn<void(__thiscall*)(void*,const void*)>(a.set)(e,value);break;}
        }
    }
    // Raw fields written directly by 0x5de070 on the main view (DIS): dword
    // +4/+8, byte +0xc, dwords +0x10..+0x1c. Plain values; refcount is +0x20.
    struct Raw {std::uint32_t a4,a8;unsigned char c;unsigned char pad[3];std::uint32_t d[4];} raw{};
    static_assert(sizeof(Raw)==0x1c);
    if(Read(main+4,&raw,sizeof raw)){
        auto* const out=reinterpret_cast<unsigned char*>(eye);
        std::memcpy(out+4,&raw.a4,4);std::memcpy(out+8,&raw.a8,4);out[0xc]=raw.c;std::memcpy(out+0x10,raw.d,16);
    }
    runtime.mirrors.fetch_add(1,std::memory_order_relaxed);runtime.lastEye.store(std::uint32_t(eye),std::memory_order_relaxed);
    return true;
}

Status Snapshot()noexcept{return {EnabledByEnvironment(),runtime.bound,runtime.mirrors.load(),runtime.initialised.load(),runtime.lastEye.load()};}
}
