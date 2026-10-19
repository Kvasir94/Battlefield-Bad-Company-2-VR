#pragma once
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/ipc/InputProtocol.h"
#include "fvr/interaction/Feedback.h"
#include <memory>
#include <string>
namespace fvr::ipc {
enum class ChannelResult {Ok,Busy,Invalid,Timeout,Closed};
// Request slots in the shared control block (wire v9). Pipeline depth is how many
// of them the host fills at once: 1 single exchange, 2 the original overlap, 3..4
// keep one untaken request queued so every game frame finds a request
// (docs/HEADSET-FRAME-DELIVERY-20261011.md).
inline constexpr unsigned FrameSlotCount=4;
// One host thread and one producer thread, in independent x64/x86 processes.
// A fresh random token identifies each session. No game pointers cross this API.
class FrameChannel {
public:
    FrameChannel();~FrameChannel();
    FrameChannel(const FrameChannel&)=delete;FrameChannel& operator=(const FrameChannel&)=delete;
    bool CreateHost() noexcept;
    bool ConnectProducer(const std::wstring& token) noexcept;
    const std::wstring& Token()const noexcept;
    bool Connected() noexcept;
    // Latest controls are independent of an in-flight render request. Never wait.
    ChannelResult PublishInput(const interaction::InputFrame&) noexcept;
    ChannelResult ReadInput(interaction::InputFrame&,std::int64_t* deadlineQpc=nullptr) noexcept;
    bool PublishFeedback(const interaction::FeedbackEvent&)noexcept;
    bool TakeFeedback(interaction::FeedbackEvent&)noexcept;
    void Close() noexcept;
    // Host: bounded rendezvous for this exact predicted-time sample. Game methods
    // never wait for a CPU mutex. A busy slot drops the current host request.
    ChannelResult RequestPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        unsigned budgetMs,graphics::TextureDescriptor&,graphics::PairTicket&) noexcept;
    // Host: nonblocking split rendezvous. A pending request keeps its immutable
    // sample across XR frames. Lifetime is 1..200 ms, independent of the 50 ms
    // blocking wait cap. Poll never consumes a newer sample's images.
    // Pipeline depth 2 (SetPipelineDepth): a second request may start in the other
    // slot once every in-flight request has been taken by the producer, so the
    // game renders frame N+1 while the host still owns frame N. `id` receives the
    // new request id. Depth 1 (default) is the original single-slot exchange.
    ChannelResult BeginRequest(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,unsigned lifetimeMs,std::uint64_t* id=nullptr) noexcept;
    // `rendered` receives the tracking the pair was actually rendered with: the
    // request sample, or a producer late-latched successor (runtime::CanRelatch).
    // Without an id: the oldest in-flight request.
    ChannelResult PollRequest(graphics::TextureDescriptor&,graphics::PairTicket&,runtime::TrackingFrame* rendered=nullptr) noexcept;
    ChannelResult PollRequestId(std::uint64_t id,graphics::TextureDescriptor&,graphics::PairTicket&,runtime::TrackingFrame* rendered=nullptr) noexcept;
    // Cancels every in-flight request; Busy while a delivered pair awaits feedback.
    ChannelResult CancelRequest() noexcept;
    // Host, 1..FrameSlotCount. Depth 3+ also admits requests while a delivered pair
    // awaits feedback (any free slot), still never more than one untaken request.
    void SetPipelineDepth(unsigned depth) noexcept;
    // Host: true when the oldest in-flight request has a published pair waiting
    // (Ready). Zero-wait try-lock; false when busy. Pace-to-content uses it.
    bool CompletedReady() noexcept;
    // Host: wait up to `ms` for any producer publication/skip signal (no lock held).
    // False on timeout or a closed channel. Never consumes a pair.
    bool WaitPublication(unsigned ms) noexcept;
    unsigned PipelineDepth()const noexcept;
    // Host: ask the producer for a DepthAttachment with each pair (FrameDepthRequested).
    void SetDepthRequested(bool) noexcept;
    // Host: the depth attachment delivered with exactly this color ticket.
    bool ReadDepth(const graphics::PairTicket&,graphics::DepthAttachment&) noexcept;
    ChannelResult Feedback(const graphics::PairTicket&,bool consumed) noexcept;
    // Host can drain deferred GPU feedback without requesting another frame.
    ChannelResult FlushFeedback() noexcept;
    // Producer: call at the verified render boundary, on the graphics thread.
    ChannelResult TryTake(FrameRequest&) noexcept;
    // A producer may hold two taken requests (pipeline depth 2): one published and
    // awaiting its outcome, one rendering. TryTake is Busy while one is rendering.
    // Publish/Skip act on the rendering request.
    ChannelResult Publish(const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame* props=nullptr,const runtime::TrackingFrame* rendered=nullptr,
        const graphics::DepthAttachment* depth=nullptr) noexcept;
    // Late-latch pose lane (optional; failure never affects the image transport).
    // Host: newest predicted view sample, once per XR frame. Producer: zero-wait read.
    bool PublishPose(const runtime::PresentationRequirements&,const runtime::TrackingFrame&) noexcept;
    bool ReadPose(runtime::TrackingFrame&) noexcept;
    bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame&)noexcept;
    ChannelResult Skip() noexcept;
    // Producer-local handoff after rendering has queued its GPU export. The
    // shared slot stays Rendering; nobody can consume it until PublishId.
    // Allows the other slot to render while this export's GPU event is pending.
    ChannelResult DeferRendering(std::uint64_t id) noexcept;
    ChannelResult PublishId(std::uint64_t id,const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::BodyPropFrame* props=nullptr,const runtime::TrackingFrame* rendered=nullptr,
        const graphics::DepthAttachment* depth=nullptr) noexcept;
    ChannelResult SkipId(std::uint64_t id) noexcept;
    // Without an id: the oldest published request.
    Outcome PollOutcome() noexcept;
    Outcome PollOutcome(std::uint64_t id) noexcept;
private:struct State;std::unique_ptr<State> state_;std::wstring token_;
};
}
