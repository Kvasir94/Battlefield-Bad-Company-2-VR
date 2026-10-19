#pragma once
#include "fvr/ipc/FrameChannel.h"
#include "fvr/runtime/FrameProvider.h"
#include <array>
#include <chrono>
namespace fvr::ipc {
struct AsyncDeliveryStats {
    std::uint64_t requested=0,completed=0,timeouts=0;
    std::uint64_t totalLatencyUs=0,maxLatencyUs=0,completedAfter50Ms=0;
    std::uint64_t relatched=0,relatchRejected=0; // Pairs carrying a producer late-latched sample.
    std::uint64_t overlapped=0; // Requests started while another was still in flight (pipeline depth 2+).
    std::uint64_t queuedWhileDelivered=0; // Depth 3+: requests started while the host still held a delivered pair.
};
class RemoteFrameProvider final:public runtime::IFrameProvider {
public:
    bool Create()noexcept{
        ClearPending();cancelling_=queueAfterFeedback_=false;stats_={};
        const bool created=channel_.CreateHost();channel_.SetPipelineDepth(depth_);channel_.SetDepthRequested(depthRequested_);return created;
    }
    const std::wstring& Token()const noexcept{return channel_.Token();}
    bool Connected()noexcept{return channel_.Connected();}
    void Close()noexcept{channel_.Close();ClearPending();cancelling_=queueAfterFeedback_=false;}
    ChannelResult FlushFeedback()noexcept{return channel_.FlushFeedback();}
    // Blocking wait budget only. Async rendering spans multiple native frames.
    bool SetBudget(unsigned ms)noexcept{if(!ms||ms>50)return false;budgetMs_=ms;return true;}
    // A bounded 150 ms lifetime permits render + GPU-fence handoff at 60 Hz.
    // Calls still never wait; original pose and 250 ms presentation age are unchanged.
    bool SetRequestLifetime(unsigned ms)noexcept{if(!ms||ms>200)return false;requestLifetimeMs_=ms;return true;}
    unsigned RequestLifetimeMs()const noexcept{return requestLifetimeMs_;}
    // Requests in flight at once: 1 is the original exchange; 2 lets the game render
    // frame N+1 while the host still owns frame N; 3..4 (BC2XrHost default 4) also
    // keep one untaken request queued at all times, so a game frame never starts
    // without one. FVR_XR_PIPELINE_DEPTH=1|2 restores the older modes. Library default 1.
    bool SetPipelineDepth(unsigned depth)noexcept{if(depth<1||depth>FrameSlotCount)return false;depth_=depth;channel_.SetPipelineDepth(depth);return true;}
    unsigned PipelineDepth()const noexcept{return depth_;}
    const AsyncDeliveryStats& Statistics()const noexcept{return stats_;}
    bool TryGetPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t,
        graphics::TextureDescriptor& d,graphics::PairTicket& p)noexcept override {
        return channel_.RequestPair(r,t,budgetMs_,d,p)==ChannelResult::Ok;
    }
    bool TryGetCompletedPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override;
    bool ReadBodyProps(const graphics::PairTicket& t,graphics::BodyPropFrame& p)noexcept override{return channel_.ReadBodyProps(t,p);}
    bool ReadDepth(const graphics::PairTicket& t,graphics::DepthAttachment& d)noexcept override{return channel_.ReadDepth(t,d);}
    void RequestDepth(bool wanted)noexcept override{depthRequested_=wanted;channel_.SetDepthRequested(wanted);}
    void UpdateInput(const interaction::InputFrame& input)noexcept override{channel_.PublishInput(input);}
    void PublishPose(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& t)noexcept override{channel_.PublishPose(r,t);}
    void PrepareFrame(const runtime::PresentationRequirements&,const runtime::TrackingFrame&)noexcept override;
    bool TakeFeedback(interaction::FeedbackEvent& event)noexcept override{return channel_.TakeFeedback(event);}
    bool PacesContent()noexcept override{return true;}
    bool CompletedPairReady()noexcept override;
    bool WaitForPublication(unsigned ms)noexcept override{return channel_.WaitPublication(ms);}
    void KeepRequestQueued(const runtime::PresentationRequirements&,const runtime::TrackingFrame&)noexcept override;
    void Suspend()noexcept override;
    void PairConsumed(const graphics::PairTicket&,bool consumed)noexcept override;
private:
    // One in-flight request: its channel id and the exact sample it was issued with.
    struct Pending {
        std::uint64_t id=0;std::chrono::steady_clock::time_point started{};
        runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
    };
    FrameChannel channel_;unsigned budgetMs_=8,requestLifetimeMs_=150,depth_=1;bool depthRequested_=false;
    AsyncDeliveryStats stats_{};
    std::array<Pending,FrameSlotCount> pending_{};
    bool cancelling_=false;
    bool queueAfterFeedback_=false;
    runtime::TrackingFrame completionTracking_{};
    // The most recently requested sample (successor requests and change checks).
    runtime::PresentationRequirements pendingRequirements_{};
    runtime::TrackingFrame pendingTracking_{};
    void ClearPending()noexcept{pending_={};}
    unsigned InFlight()const noexcept{unsigned n=0;for(const auto& p:pending_)n+=p.id?1u:0u;return n;}
    bool BeginNext(const runtime::PresentationRequirements&,const runtime::TrackingFrame&)noexcept;
};
// The local provider must outlive this object. Pump only on its graphics thread,
// at a verified render boundary. Never advances native simulation by itself.
class RemoteFrameProducer {
public:
    explicit RemoteFrameProducer(runtime::IFrameProvider& provider):provider_(provider){}
    ~RemoteFrameProducer(){Close();}
    bool Connect(const std::wstring& token)noexcept{Close();return channel_.ConnectProducer(token);}
    bool Connected()noexcept{return channel_.Connected();}
    void Pump()noexcept;
    void Close()noexcept;
private:
    enum class Step{Idle,Skipping,Publishing,Waiting};
    FrameChannel channel_;runtime::IFrameProvider& provider_;Step step_=Step::Idle;
    graphics::TextureDescriptor descriptor_{};graphics::PairTicket ticket_{};
};
}
