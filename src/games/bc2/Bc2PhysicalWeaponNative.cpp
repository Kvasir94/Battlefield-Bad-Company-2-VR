#include "Bc2PhysicalWeaponNative.h"
#include "fvr/engine/BindingValidation.h"
#include <algorithm>
#include <array>
#include <cstring>

#if defined(_M_IX86)
#include <Windows.h>
#include <MinHook.h>
#include <atomic>

namespace fvr::bc2::physicalWeaponNative {
namespace {
constexpr std::uint32_t imageBase=0x400000;
struct Proof {std::uint32_t va,size;std::uint64_t hash;};
// Fresh static decode of c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162.
// Entire functions, including the Step dispatch targets and both accepted-shot
// interface construction and dispatch. No signature-only ABI assumptions.
constexpr std::array proofs{
    Proof{0x6dd960,0xe5,0x7029ff5070647d64ull}, // ShotNotify, ECX=firing, (float,ctx*), ret8
    Proof{0x6d7040,0x6b,0x3b4a65224e6e4d9dull}, // accepted shot, ECX=firing+4, (), ret
    Proof{0x6e62d0,0x6c4,0x71118b4d657473d3ull}, // Step, ECX=firing, (ctx*,float), ret8
    Proof{0x6f1780,0x1a1,0xdab88c158da90da5ull}, // installs firing+4 secondary vtable
    Proof{0x57afc0,0x90,0xc2e0b6bb4212696eull}, // success-only server callback dispatch
    Proof{0x534290,0x61,0x583e820566fd3a59ull} // stores firing+4 in server effects+d4
};
constexpr std::array<std::uint32_t,3> targets{0x6dd960,0x6d7040,0x6e654c};
constexpr std::array<const char*,3> signatures{
    "51 56 8B F1 8B 86 98 00 00 00 83 F8 FF",
    "56 8B F1 57 83 CF FF 01 BE 80 00 00 00 8B 86 80 00 00 00",
    "80 7B 24 00 74 2C 80 7B 28 00"
};
using ShotFn=void(__thiscall*)(void*,float,void*);
using AcceptedFn=void(__thiscall*)(void*);
ShotFn originalShot=nullptr;
AcceptedFn originalAccepted=nullptr;
void* originalGate=nullptr;
void* gateSkip=reinterpret_cast<void*>(0x6e657e);
std::array<void*,3> hooks{};
Callbacks callbacks{};
std::atomic<bool> policy{false};
std::atomic<unsigned> active{0};
bool installed=false,retired=false;

struct Invocation {
    Invocation()noexcept{active.fetch_add(1,std::memory_order_acq_rel);}
    ~Invocation(){active.fetch_sub(1,std::memory_order_acq_rel);}
};

std::uint64_t Hash(std::span<const std::byte> bytes)noexcept{
    std::uint64_t h=14695981039346656037ull;
    for(const auto b:bytes){h^=std::to_integer<unsigned char>(b);h*=1099511628211ull;}
    return h;
}
std::optional<std::size_t> FileOffset(std::span<const std::byte> bytes,
    const engine::PeImage& pe,std::uint32_t va,std::size_t count,bool executable)noexcept{
    if(va<imageBase)return {};
    const auto rva=va-imageBase;
    for(const auto& s:pe.sections){
        if(rva<s.rva||(executable&&!(s.flags&0x20000000u)))continue;
        const auto delta=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+delta;
        if(delta<=s.rawSize&&count<=s.rawSize-delta&&at<=bytes.size()&&count<=bytes.size()-at)
            return std::size_t(at);
    }
    return {};
}
bool Unique(std::span<const std::byte> bytes,const engine::PeImage& pe,
    const char* signature,std::uint32_t expected){
    const auto pattern=engine::ParsePattern(signature);if(!pattern||pattern->empty())return false;
    unsigned count=0;
    for(const auto& s:pe.sections){
        if(!(s.flags&0x20000000u)||s.rawOffset>bytes.size()||s.rawSize>bytes.size()-s.rawOffset||pattern->size()>s.rawSize)continue;
        for(std::size_t i=0;i<=s.rawSize-pattern->size();++i){
            bool match=true;
            for(std::size_t j=0;j<pattern->size();++j)
                if(!(*pattern)[j].wildcard&&std::to_integer<unsigned char>(bytes[s.rawOffset+i+j])!=(*pattern)[j].value){match=false;break;}
            if(match){if(imageBase+std::uint64_t(s.rva)+i!=expected||++count!=1)return false;}
        }
    }
    return count==1;
}
template<std::size_t N>bool Exact(std::span<const std::byte> bytes,const engine::PeImage& pe,
    const ReloadStateMemory& memory,std::uint32_t va,const std::array<unsigned char,N>& expected,bool executable=true){
    const auto offset=FileOffset(bytes,pe,va,N,executable);if(!offset)return false;
    std::array<unsigned char,N> live{};
    return std::memcmp(bytes.data()+*offset,expected.data(),N)==0&&
        memory.read(memory.context,va,live.data(),N)&&live==expected;
}
bool Verify(std::span<const std::byte> bytes,const engine::PeImage& pe,
    std::uintptr_t base,const ReloadStateMemory& memory){
    if(base!=imageBase||pe.machine!=0x14c||pe.timestamp!=1314210909u||pe.imageSize!=0x1913000||!memory.read)return false;
    std::array<std::byte,0x6c4> live{};
    for(const auto& p:proofs){
        const auto offset=FileOffset(bytes,pe,p.va,p.size,true);
        if(!offset||p.size>live.size()||Hash(bytes.subspan(*offset,p.size))!=p.hash||
           !memory.read(memory.context,p.va,live.data(),p.size)||Hash(std::span(live).first(p.size))!=p.hash)return false;
    }
    for(unsigned n=0;n<targets.size();++n)if(!Unique(bytes,pe,signatures[n],targets[n]))return false;
    // Secondary interface slot0 -> accepted-shot function. Server listener+c ->
    // forwarding function whose full body proves shoot success precedes slot0.
    // Factory code explicitly passes firing+4 into the checked effects ctor.
    return Exact(bytes,pe,memory,0x1425804,std::array<unsigned char,4>{0x40,0x70,0x6d,0x00},false)&&
        Exact(bytes,pe,memory,0x1416b1c,std::array<unsigned char,4>{0xc0,0xaf,0x57,0x00},false)&&
        Exact(bytes,pe,memory,0x55c179,std::array<unsigned char,17>{0x8d,0x51,0x04,0x8b,0x4c,0x24,0x0c,0x52,0x51,0x56,0x8b,0xc8,0xe8,0x06,0x81,0xfd,0xff});
}

void __fastcall Shot(void* self,void*,float power,void* context){
    Invocation invocation;
    // Final barrier also catches automatic/burst shots already scheduled before
    // magazine removal. CanFire is only checked AFTER these native shot calls.
    if(policy.load(std::memory_order_acquire)&&!callbacks.permitFire(callbacks.context,std::uint32_t(self)))return;
    originalShot(self,power,context);
}
void __fastcall Accepted(void* self,void*){
    Invocation invocation;
    originalAccepted(self);
    if(policy.load(std::memory_order_acquire)&&std::uintptr_t(self)>=4)
        callbacks.acceptedShot(callbacks.context,std::uint32_t(self)-4);
}
bool __cdecl BlockReload(std::uint32_t firing)noexcept{
    Invocation invocation;
    return policy.load(std::memory_order_acquire)&&callbacks.blockReload(callbacks.context,firing);
}
// This is inside Step, NOT a function entry. Preserve every register, flags,
// x87 stack/control word and SSE state across the C++ policy callback. The
// function's own prologue already wrote timer=delta, allowing Update's next
// Tick to consume time. Skipping the whole Step would leave an infinite loop.
__declspec(naked) void ReloadEntry(){
    __asm {
        pushfd
        pushad
        mov ebp, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push esi
        call BlockReload
        add esp, 4
        test al, al
        jnz skip_reload
        fxrstor [esp]
        mov esp, ebp
        popad
        popfd
        jmp dword ptr [originalGate]
    skip_reload:
        fxrstor [esp]
        mov esp, ebp
        popad
        popfd
        jmp dword ptr [gateSkip]
    }
}
}

bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t base,
    const ReloadStateMemory& memory,const Callbacks& api){
    if(installed||retired||!api.permitFire||!api.acceptedShot||!api.blockReload||!Verify(bytes,pe,base,memory))return false;
    callbacks=api;
    const std::array<void*,3> detours{reinterpret_cast<void*>(Shot),reinterpret_cast<void*>(Accepted),reinterpret_cast<void*>(ReloadEntry)};
    const std::array<void**,3> originals{reinterpret_cast<void**>(&originalShot),reinterpret_cast<void**>(&originalAccepted),&originalGate};
    for(unsigned n=0;n<targets.size();++n){
        auto* target=reinterpret_cast<void*>(targets[n]);
        if(MH_CreateHook(target,detours[n],originals[n])!=MH_OK){
            // These newly created hooks have never been enabled; no trampoline
            // can be executing. Existing unrelated hooks are untouched.
            for(auto& hook:hooks)if(hook){MH_RemoveHook(hook);hook=nullptr;}
            originalShot=nullptr;originalAccepted=nullptr;originalGate=nullptr;callbacks={};return false;
        }
        hooks[n]=target;
    }
    installed=true;return true;
}
bool Start()noexcept{
    if(!installed||retired)return false;
    for(auto* hook:hooks){
        const auto result=MH_EnableHook(hook);
        if(result!=MH_OK&&result!=MH_ERROR_ENABLED){Stop();return false;}
    }
    policy.store(true,std::memory_order_release);return true;
}
bool Stop()noexcept{
    policy.store(false,std::memory_order_release);
    if(!installed)return true;
    retired=true;bool disabled=true;
    for(auto* hook:hooks){const auto result=MH_DisableHook(hook);disabled&=result==MH_OK||result==MH_ERROR_DISABLED;}
    const auto deadline=GetTickCount64()+2000;
    while(active.load(std::memory_order_acquire)&&GetTickCount64()<deadline)Sleep(1);
    // Deliberately retain all original trampolines even when drained. The
    // mid-function assembly path can be just past its C++ callback and needs
    // its trampoline to finish. Retaining it also avoids racing disabled calls.
    return disabled&&active.load(std::memory_order_acquire)==0;
}
}
#else
namespace fvr::bc2::physicalWeaponNative {
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,const ReloadStateMemory&,const Callbacks&){return false;}
bool Start()noexcept{return false;}
bool Stop()noexcept{return true;}
}
#endif
