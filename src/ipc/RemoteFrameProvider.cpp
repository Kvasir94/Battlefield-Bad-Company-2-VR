#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/runtime/LateLatch.h"
#include <algorithm>
#include <cstring>
namespace fvr::ipc {
namespace {
bool Changed(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    const runtime::PresentationRequirements& requested,const runtime::TrackingFrame& tracking)noexcept {
    return r.width!=requested.width||r.height!=requested.height||r.format!=requested.format||
        r.adapterLow!=requested.adapterLow||r.adapterHigh!=requested.adapterHigh||
        current.spaceGeneration!=tracking.spaceGeneration||current.predictedNs<tracking.predictedNs;
}
}
void RemoteFrameProvider::Suspend()noexcept {
    queueAfterFeedback_=false;
    cancelling_=channel_.CancelRequest()==ChannelResult::Busy;
    // Busy: a delivered pair awaits feedback. The channel cancelled every other
    // request already; their ids now poll as Invalid and are dropped then.
    if(!cancelling_)ClearPending();
}
bool RemoteFrameProvider::BeginNext(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& tracking)noexcept {
    if(InFlight()>=depth_)return false;
    const auto started=std::chrono::steady_clock::now();std::uint64_t id=0;
    if(channel_.BeginRequest(r,tracking,requestLifetimeMs_,&id)!=ChannelResult::Ok||!id)return false;
    auto free=std::find_if(pending_.begin(),pending_.end(),[](const Pending& p){return !p.id;});
    if(free==pending_.end()){channel_.CancelRequest();ClearPending();return false;} // Unreachable: the channel admits <= depth.
    if(InFlight())++stats_.overlapped;
    if(queueAfterFeedback_)++stats_.queuedWhileDelivered;
    *free={id,started,r,tracking};
    pendingRequirements_=r;pendingTracking_=tracking;++stats_.requested;return true;
}
void RemoteFrameProvider::PairConsumed(const graphics::PairTicket& ticket,bool consumed)noexcept {
    const auto result=channel_.Feedback(ticket,consumed);
    if(result!=ChannelResult::Ok&&result!=ChannelResult::Busy)return;
    if(queueAfterFeedback_){
        queueAfterFeedback_=false;
        // BeginRequest flushes deferred acknowledgment under its mutex first.
        // A successor is only CPU metadata until the producer reaps the old GPU
        // lease. The old pixels retain the tracking returned at completion.
        BeginNext(pendingRequirements_,completionTracking_);
    }
}
void RemoteFrameProvider::PrepareFrame(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current)noexcept {
    if(!queueAfterFeedback_)return;
    if(Changed(r,current,pendingRequirements_,pendingTracking_)||!current.focused||!current.headValid){Suspend();return;}
    completionTracking_=current;
}
bool RemoteFrameProvider::TryGetCompletedPair(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current,
    runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept {
    channel_.FlushFeedback();
    PrepareFrame(r,current);
    if(queueAfterFeedback_){
        // Delivered source remains GPU-owned. Depth 3+ still keeps one untaken
        // request queued (a different slot) so the game's next frame has one.
        KeepRequestQueued(r,current);
        return false;
    }
    bool changed=false;
    for(const auto& p:pending_)if(p.id&&Changed(r,current,p.requirements,p.tracking))changed=true;
    if(cancelling_||changed||!current.focused||!current.headValid){Suspend();if(cancelling_||!current.focused||!current.headValid)return false;}
    // Oldest first: the producer takes requests in id order, so a newer pair is
    // never ready while an older one is still rendering.
    while(true){
        auto oldest=pending_.end();
        for(auto it=pending_.begin();it!=pending_.end();++it)if(it->id&&(oldest==pending_.end()||it->id<oldest->id))oldest=it;
        if(oldest==pending_.end())break;
        runtime::TrackingFrame delivered{};
        const auto result=channel_.PollRequestId(oldest->id,d,ticket,&delivered);
        if(result==ChannelResult::Busy)break;
        const auto done=*oldest;*oldest={};
        if(result==ChannelResult::Timeout){++stats_.timeouts;continue;}
        if(result!=ChannelResult::Ok)continue; // Cancelled while a delivered pair was held.
        const auto us=std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-done.started).count());
        ++stats_.completed;stats_.totalLatencyUs+=us;if(us>stats_.maxLatencyUs)stats_.maxLatencyUs=us;if(us>50000)++stats_.completedAfter50Ms;
        // The pair is labelled with the sample actually rendered. A producer
        // late latch may only have advanced it (runtime::CanRelatch); anything
        // else keeps the request sample, which the ticket then fails to match.
        rendered=done.tracking;
        if(delivered.generation!=done.tracking.generation){
            if(runtime::CanRelatch(done.tracking,delivered)){rendered=delivered;++stats_.relatched;}else ++stats_.relatchRejected;
        }
        completionTracking_=current;queueAfterFeedback_=true;
        // Depth 3+: queue the successor now (another slot), not after the copy's
        // feedback, so the game's next frame already finds a request.
        KeepRequestQueued(r,current);
        return true;
    }
    if(InFlight()<depth_)BeginNext(r,current);
    return false;
}
bool RemoteFrameProvider::CompletedPairReady()noexcept {
    if(cancelling_||queueAfterFeedback_||!InFlight())return false;
    return channel_.CompletedReady();
}
void RemoteFrameProvider::KeepRequestQueued(const runtime::PresentationRequirements& r,const runtime::TrackingFrame& current)noexcept {
    // The channel admits it only when no other request is still untaken and a slot
    // is free; the producer late-latches the pose, so a queued request costs no
    // content latency. Depth 1..2 keep their original admission points.
    if(depth_<3||cancelling_||!current.focused||!current.headValid||InFlight()>=depth_)return;
    for(const auto& p:pending_)if(p.id&&Changed(r,current,p.requirements,p.tracking))return;
    BeginNext(r,current);
}
void RemoteFrameProducer::Close()noexcept {
    channel_.Close();if(step_==Step::Publishing||step_==Step::Waiting)provider_.PairConsumed(ticket_,false);step_=Step::Idle;
}
void RemoteFrameProducer::Pump()noexcept {
    if(step_==Step::Waiting){const auto outcome=channel_.PollOutcome();if(outcome==Outcome::Pending)return;
        provider_.PairConsumed(ticket_,outcome==Outcome::Consumed);step_=Step::Idle;}
    if(step_==Step::Skipping){if(channel_.Skip()==ChannelResult::Busy)return;step_=Step::Idle;}
    if(step_==Step::Idle){
        FrameRequest request{};if(channel_.TryTake(request)!=ChannelResult::Ok)return;
        runtime::PresentationRequirements requirements{};runtime::TrackingFrame tracking{};
        if(!Decode(request,requirements,tracking)||!provider_.TryGetPair(requirements,tracking,descriptor_,ticket_)){
            step_=Step::Skipping;if(channel_.Skip()!=ChannelResult::Busy)step_=Step::Idle;return;}
        step_=Step::Publishing;
    }
    if(step_==Step::Publishing){const auto result=channel_.Publish(descriptor_,ticket_);if(result==ChannelResult::Busy)return;
        if(result==ChannelResult::Ok)step_=Step::Waiting;
        else{provider_.PairConsumed(ticket_,false);step_=Step::Idle;}
    }
}
}
