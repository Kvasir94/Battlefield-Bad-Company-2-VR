#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <array>
#include <atomic>
namespace fvr::ipc {
// Local identity, never a wire pointer. The engine adapter supplies generations
// for world ownership and the graphics device, plus its native frame serial.
struct NativeFrameKey {
    std::uint64_t owner=0,frameId=0,deviceEpoch=0;
    bool operator==(const NativeFrameKey&)const=default;
};
struct FrameLease {
    std::uint64_t channelGeneration=0,requestId=0;
    NativeFrameKey native{};
    runtime::PresentationRequirements requirements{};
    runtime::TrackingFrame tracking{};
    bool depthRequested=false; // The host asked for a DepthAttachment (FrameDepthRequested).
};
// One request can span native visibility and graphics callbacks on different
// threads. No native calls, simulation updates or GPU work occur in TryBegin.
// Runtime operations use try-locks only; contention drops/defer work.
class StagedFrameProducer {
public:
    explicit StagedFrameProducer(runtime::IFrameProvider& feedback):feedback_(feedback),overlapLimit_(OverlapFromEnvironment()){}
    // Leases the producer may hold besides the rendering one: FVR_XR_PIPELINE_DEPTH=1
    // gives 0, =2 gives 1 (the original overlap), otherwise FrameSlotCount-1.
    static unsigned OverlapFromEnvironment()noexcept;
    unsigned OverlapLimit()const noexcept{return overlapLimit_;}
    ~StagedFrameProducer(){CloseGraphics();}
    StagedFrameProducer(const StagedFrameProducer&)=delete;
    StagedFrameProducer& operator=(const StagedFrameProducer&)=delete;
    // Setup/teardown only with both callback threads quiescent, on graphics thread.
    bool Connect(const std::wstring&)noexcept;
    void CloseGraphics()noexcept;
    // Once taken, a frame cannot be retried, even after cancellation/reconnect.
    bool TryBegin(const NativeFrameKey&,FrameLease&)noexcept;
    ChannelResult ReadInput(interaction::InputFrame& out,std::int64_t* deadlineQpc=nullptr)noexcept {
        if(deadlineQpc)*deadlineQpc=0;out={};Lock lock(gate_);return lock?channel_.ReadInput(out,deadlineQpc):ChannelResult::Busy;
    }
    bool PublishFeedback(const interaction::FeedbackEvent& e)noexcept {Lock lock(gate_);return lock&&channel_.PublishFeedback(e);}
    // Late latch: newest host view sample (zero-wait; false when busy/absent/expired).
    bool ReadPose(runtime::TrackingFrame& out)noexcept {out={};Lock lock(gate_);return lock&&channel_.ReadPose(out);}
    // Replace the rendering lease's sample before its cameras are written. Only a
    // runtime::CanRelatch successor; the published pair then carries it to the host.
    // Ok updates `lease` too. Call on the thread that owns the lease's camera write.
    ChannelResult Relatch(FrameLease& lease,const runtime::TrackingFrame& tracking)noexcept;
    // Ok transfers ticket ownership. On Busy/Invalid caller still owns the pair
    // and must retry or reclaim it. Submit ONLY after exact native restoration.
    // `depth` (optional) travels with the pair; it must carry the ticket's identity.
    ChannelResult Submit(const FrameLease&,const graphics::TextureDescriptor&,const graphics::PairTicket&,
        const graphics::DepthAttachment* depth=nullptr)noexcept;
    ChannelResult Cancel(const FrameLease&)noexcept;
    // Graphics only, after both eye copies are queued and native state restored.
    // Does not publish pixels. Submit still requires GPU completion; the sealed
    // lease, late-latched pose and props survive while the next frame renders.
    ChannelResult DeferGpu(const FrameLease&)noexcept;
    ChannelResult SetBodyProps(const FrameLease&,unsigned eye,const graphics::BodyPropEye&)noexcept;
    // Graphics thread only, before any successor capture/resource mutation.
    // False means the gate is busy: caller MUST defer/drop capture, never write
    // a resource whose prior GPU acknowledgment/reset has not been drained.
    bool TryDrainFeedbackGraphics()noexcept;
    // Graphics thread only: publish and return GPU ownership feedback exactly once.
    void PumpGraphics()noexcept;
private:
    enum class Step{Idle,Rendering,Skipping,Publishing,Waiting};
    struct Lock {
        std::atomic_flag& flag;bool held;
        explicit Lock(std::atomic_flag& f):flag(f),held(!f.test_and_set(std::memory_order_acquire)){}
        ~Lock(){if(held)flag.clear(std::memory_order_release);}
        explicit operator bool()const{return held;}
    };
    bool Matches(const FrameLease&)const noexcept;
    void DrainFeedbackGraphics()noexcept;
    FrameChannel channel_;runtime::IFrameProvider& feedback_;
    std::atomic_flag gate_=ATOMIC_FLAG_INIT;
    Step step_=Step::Idle;std::uint64_t generation_=0;
    NativeFrameKey lastNative_{};
    graphics::BodyPropFrame bodyProps_{};
    FrameLease lease_{};graphics::TextureDescriptor descriptor_{};graphics::PairTicket ticket_{};
    graphics::DepthAttachment depth_{};
    bool relatched_=false;
    // Pipelining: leases whose GPU export, publication or host outcome is still
    // pending while the next lease renders (up to overlapLimit_ of them).
    struct Overlapped {
        std::uint64_t requestId=0;graphics::PairTicket ticket{};
        Step step=Step::Waiting;FrameLease lease{};graphics::TextureDescriptor descriptor{};
        graphics::BodyPropFrame props{};graphics::DepthAttachment depth{};bool relatched=false;
    };
    std::array<Overlapped,FrameSlotCount-1> overlapped_{};unsigned overlapLimit_=FrameSlotCount-1;
    Overlapped* FreeOverlap()noexcept;
    Overlapped* OverlapOf(std::uint64_t requestId,Step step)noexcept;
    // Outcomes reaped on the visibility thread, released on the graphics thread.
    struct Deferred {graphics::PairTicket ticket{};bool consumed=false;};
    std::array<Deferred,FrameSlotCount> deferred_{};unsigned deferredCount_=0;
    void Defer(const graphics::PairTicket& ticket,bool consumed)noexcept{if(deferredCount_<deferred_.size())deferred_[deferredCount_++]={ticket,consumed};}
};
}
