#pragma once
#include "Bc2NativeSkinRequestRegistry.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <iosfwd>
struct ID3D11Device;
struct ID3D11Buffer;

namespace fvr::bc2::nativeSkinGeometry {
inline constexpr unsigned MaxSections=8;
// False means pending OR mismatch. The callback must not drive an immediate
// context on the native collector thread. It may queue graphics-thread readback.
using VerifySource=bool(*)(void*,ID3D11Buffer*,std::uint32_t,std::uint32_t,
    const std::array<std::byte,32>&);
// Call before enabling submission hooks. Atomic immutable set: a bad pack
// rejects the whole set. At most32 packs and64MiB of input; no runtime file I/O.
bool ConfigureSet(std::span<const std::span<const std::byte>> packs,std::uint64_t expectedRig)noexcept;
// Independent immutable pools; a prop pack can never enter ordinary Replace.
struct PropPack {std::span<const std::byte> bytes;std::uint64_t rig=0;unsigned bone=0;};
bool ConfigureProps(std::span<const PropPack>)noexcept;
// Backward-compatible single-pack wrapper.
bool Configure(std::span<const std::byte> bytes,std::uint64_t expectedRig)noexcept;
// Call once at the native frame boundary, never once per eye. GPU allocation
// failure is sticky through that frame so the other eye cannot retry it.
void BeginFrame()noexcept;
// Bounded aggregate rejection counters and first observed mismatch per gate.
void Report(std::ostream&);
// Only these shallow views are synchronous. Never retain or queue View itself.
// Native 970870 consumes it synchronously and queues persistent geometry only.
struct View {
    alignas(16) std::array<std::byte,0x64> lod{};
    std::array<std::byte,0x38> container{};
    std::array<std::array<std::byte,0x2c>,MaxSections> parts{};
    std::array<std::array<std::uint8_t,MaxSections>,4> buckets{};
    unsigned Lod()const noexcept;
};
// The caller proves the CURRENT selected-character request scope and rechecks
// it before dispatch. Device must be independently identified as native DX11.
// graphicsThreadId is the thread observed at the native immediate-context draw
// boundary. Cache creation/COM methods run only there. Workers may reuse an
// exact current native snapshot matching a pinned immutable entry; no COM call
// is made on that path. New entries become usable at the next BeginFrame.
// No native object is written. Failure leaves out unusable and means original.
bool Replace(unsigned lod,unsigned variant,unsigned paletteCount,
    const NativeSkinOwner&,ID3D11Device*,VerifySource,void* verifyContext,View& out,unsigned graphicsThreadId)noexcept;
bool ReplaceProp(unsigned pack,unsigned lod,unsigned variant,unsigned paletteCount,
    const NativeSkinOwner&,ID3D11Device*,VerifySource,void*,View&,unsigned graphicsThreadId)noexcept;
}
