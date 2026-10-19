#pragma once
#include "Bc2ReloadState.h"

namespace fvr::bc2::physicalWeaponNative {
// Callbacks run on native simulation threads and must be bounded/nonblocking.
// Ownership is the caller's responsibility: unknown/non-player firing objects
// must permit fire and must not block reload. acceptedShot must similarly filter
// to the currently identified authoritative player copy before changing a ledger.
struct Callbacks {
    void* context=nullptr;
    bool (*permitFire)(void*,std::uint32_t firing)noexcept=nullptr;
    void (*acceptedShot)(void*,std::uint32_t firing)noexcept=nullptr;
    bool (*blockReload)(void*,std::uint32_t firing)noexcept=nullptr;
};
// Checks pinned build 795745 disk/live code, then creates three DISABLED MinHook
// detours. MinHook must already be initialized. No game state is read or written.
// Install before the application's global MH_EnableHook(MH_ALL_HOOKS).
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t image,
    const ReloadStateMemory&,const Callbacks&);
// Enables these hooks (also accepts already enabled by global startup), then
// publishes policy. Lifecycle calls must be serialized and outside callbacks.
bool Start()noexcept;
// Turns off policy, disables hooks and waits up to two seconds for callbacks.
// Retains trampolines and callback storage for process lifetime: no live remove,
// reinstall or DLL unload is supported. A false result requires retaining all
// callback-owned objects and retrying Stop before destroying any of them.
// Even after success the DLL must stay loaded until native threads have stopped;
// a thread may still be completing a restored mid-function trampoline tail.
bool Stop()noexcept;
}
