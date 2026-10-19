#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <cstring>

namespace fvr::graphics {
// Little-endian Windows wire types: no pointers, HANDLEs, bools, size_t or STL
// containers with dynamic storage. These are identical in x86 and x64 builds.
inline constexpr std::uint32_t TextureMagic=0x31525646; // FVR1
inline constexpr std::uint32_t TicketMagic=0x31545646;  // FVT1
inline constexpr std::uint32_t TextureProtocol=2;
enum class TextureSharing:std::uint32_t {NamedKeyed=0,LegacyFenced=1};
struct alignas(8) TextureDescriptor {
    std::uint32_t magic=TextureMagic, version=TextureProtocol, bytes=64, format=0;
    std::uint32_t width=0, height=0, adapterLow=0;
    std::int32_t adapterHigh=0;
    std::uint64_t resourceEpoch=0;
    std::array<std::uint8_t,16> session{};
    TextureSharing sharing=TextureSharing::NamedKeyed;std::uint32_t reserved=0;
};
struct FrameMetadata {
    std::uint64_t frameId=0, spaceGeneration=0, trackingGeneration=0;
    // Predicted display time in the shared OpenXR time domain, not wall time.
    std::int64_t predictedNs=0;
};
struct alignas(8) PairTicket {
    std::uint32_t magic=TicketMagic, version=TextureProtocol, bytes=96, reserved0=0;
    std::uint64_t resourceEpoch=0, sequence=0, frameId=0;
    std::uint64_t spaceGeneration=0, trackingGeneration=0;
    std::int64_t predictedNs=0;
    std::array<std::uint8_t,16> session{};
    std::array<std::uint64_t,2> reserved{};
};
static_assert(sizeof(TextureDescriptor)==64 && offsetof(TextureDescriptor,session)==40);
static_assert(sizeof(PairTicket)==96 && offsetof(PairTicket,sequence)==24 && offsetof(PairTicket,session)==64);
static_assert(std::is_trivially_copyable_v<TextureDescriptor> && std::is_trivially_copyable_v<PairTicket>);
// A GPU copy preserves bytes. It does not convert gamma or swap color channels.
// XR runtimes may return typeless backing textures for a typed swapchain format.
inline bool CopyCompatibleFormats(std::uint32_t a,std::uint32_t b) noexcept {
    // Group 3: the R32 family carrying [0,1] window depth (DepthAttachment).
    const auto group=[](std::uint32_t f){return (f==27||f==28||f==29)?1:(f==90||f==87||f==91)?2:(f==39||f==40||f==41)?3:0;};
    return group(a)!=0 && group(a)==group(b);
}
// v2 session bytes are a GUID for named resources, or two fixed-width legacy
// DXGI resource identifiers. They are never closed/duplicated as NT handles.
inline std::array<std::uint64_t,2> LegacyResourceIds(const TextureDescriptor& d)noexcept {std::array<std::uint64_t,2> ids{};std::memcpy(ids.data(),d.session.data(),16);return ids;}
inline bool Valid(const TextureDescriptor& d) noexcept {
    bool any=false;for(auto b:d.session)any|=b!=0;
    const auto legacy=LegacyResourceIds(d);
    const bool sharing=d.sharing==TextureSharing::NamedKeyed||
        (d.sharing==TextureSharing::LegacyFenced&&legacy[0]&&legacy[1]&&legacy[0]!=legacy[1]);
    // Resolved RGBA8/BGRA8; legacy storage is UNORM, semantic format is explicit.
    return d.magic==TextureMagic && d.version==TextureProtocol && d.bytes==sizeof(d) &&
        // R32_FLOAT (41) is only the depth attachment pair (named keyed sharing).
        (d.format==28 || d.format==29 || d.format==87 || d.format==91 || (d.format==41 && d.sharing==TextureSharing::NamedKeyed)) && d.width && d.width<=16384 &&
        d.height && d.height<=16384 && d.resourceEpoch && any && sharing && !d.reserved;
}
inline bool Valid(const PairTicket& t,const TextureDescriptor& d) noexcept {
    return Valid(d) && t.magic==TicketMagic && t.version==TextureProtocol && t.bytes==sizeof(t) &&
        t.resourceEpoch==d.resourceEpoch && t.session==d.session && t.sequence &&
        t.sequence<=0x7fffffffffffffffULL && t.frameId && t.spaceGeneration &&
        t.trackingGeneration && t.predictedNs>0 && !t.reserved0 && !t.reserved[0] && !t.reserved[1];
}
// Per-eye scene depth that travels with a color pair (XR_KHR_composition_layer_depth).
// A second, independent keyed pair in R32_FLOAT holding the game's window depth
// (D3D [0,1]: 0 at the near plane, 1 at the far plane; BC2 is not reversed-Z).
// The ticket carries the color ticket's frame metadata. Planes are metres.
inline constexpr std::uint32_t DepthFormat=41; // DXGI_FORMAT_R32_FLOAT
struct alignas(8) DepthAttachment {
    TextureDescriptor descriptor{};
    PairTicket ticket{};
    std::array<float,4> nearFar{}; // eye0 near, eye0 far, eye1 near, eye1 far (metres)
    std::uint32_t present=0,reserved=0;
};
static_assert(sizeof(DepthAttachment)==184 && offsetof(DepthAttachment,ticket)==64 && offsetof(DepthAttachment,nearFar)==160);
static_assert(std::is_trivially_copyable_v<DepthAttachment>);
inline bool DepthPlanesValid(float nearZ,float farZ) noexcept {
    // Finite, positive and ordered; a NaN fails every comparison.
    return nearZ>0.f && nearZ<1.0e6f && farZ>nearZ && farZ<1.0e7f;
}
// The attachment names its own resources, shares the color pair's size/adapter
// and carries exactly the color ticket's frame identity and tracking.
inline bool ValidDepth(const DepthAttachment& a,const TextureDescriptor& color,const PairTicket& colorTicket) noexcept {
    if(a.present!=1 || a.reserved || a.descriptor.format!=DepthFormat || !Valid(a.ticket,a.descriptor) || !Valid(colorTicket,color))return false;
    if(a.descriptor.width!=color.width || a.descriptor.height!=color.height || a.descriptor.adapterLow!=color.adapterLow ||
       a.descriptor.adapterHigh!=color.adapterHigh || a.descriptor.session==color.session)return false;
    if(a.ticket.frameId!=colorTicket.frameId || a.ticket.spaceGeneration!=colorTicket.spaceGeneration ||
       a.ticket.trackingGeneration!=colorTicket.trackingGeneration || a.ticket.predictedNs!=colorTicket.predictedNs)return false;
    return DepthPlanesValid(a.nearFar[0],a.nearFar[1]) && DepthPlanesValid(a.nearFar[2],a.nearFar[3]);
}
}