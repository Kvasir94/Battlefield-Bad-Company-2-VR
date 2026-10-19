#pragma once
#include <Windows.h>
#include <cstdint>
namespace fvr::bc2::testRangeHandoff {
// Versioned interface to our own fixture DLL (not a guessed game signature).
// A successful prepare yields ONLY its 0x6e9000 detour. All normal native
// executable/vtable checks still run against the restored game bytes.
using PrepareFn=bool(__cdecl*)(std::uint32_t);
using PostFn=void(__cdecl*)(std::uint32_t);
inline PostFn firingPost=nullptr;
inline bool Prepare()noexcept {
    const auto fixture=GetModuleHandleW(L"BC2TestRange.dll");if(!fixture)return true;
    const auto prepare=reinterpret_cast<PrepareFn>(GetProcAddress(fixture,"FvrTestRangePrepareNative"));
    const auto post=reinterpret_cast<PostFn>(GetProcAddress(fixture,"FvrTestRangeFiringPost"));
    // An old fixture retains its old behavior; exact native guards decide
    // whether its live code is compatible. Never chain an unknown detour.
    if(!prepare&&!post)return true;
    if(!prepare||!post||!prepare(1))return false;
    firingPost=post;return true;
}
struct FiringPostScope {
    std::uint32_t firing;
    ~FiringPostScope(){if(firingPost)firingPost(firing);}
};
}
