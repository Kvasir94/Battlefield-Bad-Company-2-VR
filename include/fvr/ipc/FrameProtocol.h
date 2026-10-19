#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
namespace fvr::ipc {
inline constexpr std::uint32_t FrameMagic=0x31514646; // FFQ1, distinct from GPU protocol
// Request flag: the host wants a DepthAttachment with the pair (optional; a pair
// without one is still valid).
inline constexpr std::uint32_t FrameDepthRequested=4;
// Explicit wire poses: xyz metres, then quaternion xyzw. Never serialize C++ bools.
struct PoseWire {std::array<float,7> values{};};
struct alignas(8) FrameRequest {
    // flags: 3, plus FrameDepthRequested when the host submits depth layers.
    std::uint32_t magic=FrameMagic,version=3,bytes=248,flags=3;
    std::uint64_t requestId=0;
    std::int64_t deadlineQpc=0;
    std::uint32_t width=0,height=0,format=0,adapterLow=0;
    std::int32_t adapterHigh=0;
    std::uint32_t reserved0=0;
    std::uint64_t trackingGeneration=0,spaceGeneration=0;
    std::int64_t predictedNs=0;
    PoseWire referenceHead{},head{};
    std::array<PoseWire,2> eyes{};
    std::array<std::array<float,4>,2> fov{};
    float worldUnitsPerMeter=1;std::uint32_t reservedScale=0;
    std::array<std::uint64_t,2> reserved{};
};
static_assert(sizeof(FrameRequest)==248 && offsetof(FrameRequest,referenceHead)==80 && offsetof(FrameRequest,fov)==192);
static_assert(std::is_trivially_copyable_v<FrameRequest>);
bool Encode(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
    std::uint64_t requestId,std::int64_t deadlineQpc,FrameRequest&) noexcept;
bool Decode(const FrameRequest&,runtime::PresentationRequirements&,runtime::TrackingFrame&) noexcept;
enum class Phase:std::uint32_t {Idle,Requested,Rendering,Ready,Delivered,Complete,Cancelled,Closed};
enum class Outcome {Pending,Consumed,Discarded};
// Only access under the interprocess mutex; no unaligned x86 atomic assumptions.
struct alignas(8) FrameSlot {
    Phase phase=Phase::Idle;
    std::uint32_t consumed=0;
    FrameRequest request{};
    graphics::TextureDescriptor descriptor{};
    graphics::PairTicket ticket{};
    std::uint64_t reserved=0;
    // CPU-only successor accepted after explicit acknowledgment. The producer
    // retains the old outcome/lease until Reap promotes this immutable request.
    FrameRequest queued{};
    // Optional scene depth for the published pair (present=0 when absent).
    graphics::DepthAttachment depth{};
};
static_assert(sizeof(FrameSlot)==856 && offsetof(FrameSlot,ticket)==320 && offsetof(FrameSlot,depth)==672);
static_assert(std::is_trivially_copyable_v<FrameSlot>);
bool Begin(FrameSlot&,const FrameRequest&,std::int64_t now) noexcept;
bool Take(FrameSlot&,std::int64_t now,FrameRequest&) noexcept;
// `rendered`, when given, is a late-latched successor sample (runtime::CanRelatch)
// that replaces the request's poses: the ticket must match it, and the host
// receives it back as the pair's rendered tracking.
bool Publish(FrameSlot&,std::uint64_t id,std::int64_t now,
    const graphics::TextureDescriptor&,const graphics::PairTicket&,
    const runtime::TrackingFrame* rendered=nullptr,const graphics::DepthAttachment* depth=nullptr) noexcept;
// Decoded tracking the published pair was rendered with (request or relatched).
bool RenderedTracking(const FrameSlot&,runtime::TrackingFrame&) noexcept;
bool Deliver(FrameSlot&,std::uint64_t id,std::int64_t now,
    graphics::TextureDescriptor&,graphics::PairTicket&,graphics::DepthAttachment* depth=nullptr) noexcept;
void Cancel(FrameSlot&,std::uint64_t id) noexcept;
void Skip(FrameSlot&,std::uint64_t id) noexcept;
bool Acknowledge(FrameSlot&,std::uint64_t id,bool consumed) noexcept;
Outcome Reap(FrameSlot&,std::uint64_t id,std::int64_t now) noexcept;
}
