#include "fvr/ipc/StagedFrameProducer.h"
#include "fvr/runtime/LateLatch.h"
#include <Windows.h>
namespace fvr::ipc {
unsigned StagedFrameProducer::OverlapFromEnvironment()noexcept {
    // Same switch as the host: FVR_XR_PIPELINE_DEPTH=1 keeps one request in flight,
    // =2 the original single overlap; anything else lets the producer hold every
    // slot the host fills (FrameSlotCount, one of them rendering).
    wchar_t value[8]{};const auto length=GetEnvironmentVariableW(L"FVR_XR_PIPELINE_DEPTH",value,8);
    if(length==1&&value[0]==L'1')return 0;
    if(length==1&&value[0]==L'2')return 1;
    return FrameSlotCount-1;
}
StagedFrameProducer::Overlapped* StagedFrameProducer::FreeOverlap()noexcept {
    unsigned used=0;Overlapped* free=nullptr;
    for(auto& entry:overlapped_){if(entry.requestId)++used;else if(!free)free=&entry;}
    return used<overlapLimit_?free:nullptr;
}
StagedFrameProducer::Overlapped* StagedFrameProducer::OverlapOf(std::uint64_t requestId,Step step)noexcept {
    if(!requestId)return nullptr;
    for(auto& entry:overlapped_)if(entry.requestId==requestId&&entry.step==step)return &entry;
    return nullptr;
}
bool StagedFrameProducer::Connect(const std::wstring& token)noexcept {
    CloseGraphics();if(generation_==UINT64_MAX)return false;
    ++generation_;return channel_.ConnectProducer(token);
}
void StagedFrameProducer::CloseGraphics()noexcept {
    // Caller has stopped both hooks; no callback may race destruction/connection.
    DrainFeedbackGraphics();
    channel_.Close();
    for(auto& entry:overlapped_){
        if(entry.requestId&&(entry.step==Step::Publishing||entry.step==Step::Waiting))feedback_.PairConsumed(entry.ticket,false);
        entry={};
    }
    if(step_==Step::Publishing||step_==Step::Waiting)feedback_.PairConsumed(ticket_,false);
    step_=Step::Idle;lease_={};ticket_={};descriptor_={};depth_={};
}
bool StagedFrameProducer::Matches(const FrameLease& lease)const noexcept {
    return lease.channelGeneration==generation_&&lease.requestId&&lease.requestId==lease_.requestId&&lease.native==lease_.native;
}
bool StagedFrameProducer::TryBegin(const NativeFrameKey& native,FrameLease& out)noexcept {
    if(!native.owner||!native.frameId||!native.deviceEpoch)return false;
    Lock lock(gate_);if(!lock||deferredCount_)return false;
    if(native.owner==lastNative_.owner&&native.deviceEpoch==lastNative_.deviceEpoch&&native.frameId<=lastNative_.frameId)return false;
    // CPU-only reaping can promote a queued request before this frame's
    // visibility work. GPU release/reset remains on the graphics thread.
    for(auto& entry:overlapped_)if(entry.requestId&&entry.step==Step::Waiting){
        const auto outcome=channel_.PollOutcome(entry.requestId);
        if(outcome!=Outcome::Pending){Defer(entry.ticket,outcome==Outcome::Consumed);entry={};}
    }
    if(step_==Step::Waiting){
        const auto outcome=channel_.PollOutcome(lease_.requestId);
        if(outcome==Outcome::Pending){
            // Pipelining: the host still owns this pair; render the next request
            // meanwhile and collect this outcome separately.
            auto* free=FreeOverlap();if(!free)return false;
            *free={};free->requestId=lease_.requestId;free->ticket=ticket_;
        }else Defer(ticket_,outcome==Outcome::Consumed);
        step_=Step::Idle;lease_={};ticket_={};descriptor_={};depth_={};
    }
    if(step_!=Step::Idle)return false;
    FrameRequest request{};if(channel_.TryTake(request)!=ChannelResult::Ok)return false;
    FrameLease next{};next.channelGeneration=generation_;next.requestId=request.requestId;next.native=native;
    next.depthRequested=(request.flags&FrameDepthRequested)!=0;
    if(!Decode(request,next.requirements,next.tracking)){step_=Step::Skipping;return false;}
    bodyProps_={};bodyProps_.frameId=native.frameId;bodyProps_.spaceGeneration=next.tracking.spaceGeneration;bodyProps_.trackingGeneration=next.tracking.generation;
    lease_=next;lastNative_=native;relatched_=false;depth_={};step_=Step::Rendering;out=next;return true;
}
void StagedFrameProducer::DrainFeedbackGraphics()noexcept {
    const auto count=deferredCount_;const auto deferred=deferred_;
    deferredCount_=0;deferred_={};
    for(unsigned i=0;i<count;++i)feedback_.PairConsumed(deferred[i].ticket,deferred[i].consumed);
}
bool StagedFrameProducer::TryDrainFeedbackGraphics()noexcept {
    Lock lock(gate_);if(!lock)return false;
    DrainFeedbackGraphics();return true;
}
ChannelResult StagedFrameProducer::SetBodyProps(const FrameLease& lease,unsigned eye,const graphics::BodyPropEye& props)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||eye>1||props.count>graphics::MaxBodyProps)return ChannelResult::Invalid;
    bodyProps_.eyes[eye]=props;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Submit(const FrameLease& lease,const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket,const graphics::DepthAttachment* depth)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(auto* old=OverlapOf(lease.requestId,Step::Rendering)){
        if(lease.channelGeneration!=old->lease.channelGeneration||lease.native!=old->lease.native||
            ticket.frameId!=old->lease.native.frameId||!runtime::PairMatchesFrame(old->lease.requirements,old->lease.tracking,descriptor,ticket))return ChannelResult::Invalid;
        old->descriptor=descriptor;old->ticket=ticket;
        old->depth=depth&&old->lease.depthRequested&&graphics::ValidDepth(*depth,descriptor,ticket)?*depth:graphics::DepthAttachment{};
        old->step=Step::Publishing;return ChannelResult::Ok;
    }
    if(step_!=Step::Rendering||!Matches(lease)||ticket.frameId!=lease_.native.frameId||
       !runtime::PairMatchesFrame(lease_.requirements,lease_.tracking,descriptor,ticket))return ChannelResult::Invalid;
    descriptor_=descriptor;ticket_=ticket;
    depth_=depth&&lease_.depthRequested&&graphics::ValidDepth(*depth,descriptor,ticket)?*depth:graphics::DepthAttachment{};
    step_=Step::Publishing;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Relatch(FrameLease& lease,const runtime::TrackingFrame& tracking)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease)||!runtime::CanRelatch(lease_.tracking,tracking))return ChannelResult::Invalid;
    lease_.tracking=tracking;lease_.tracking.focused=lease_.tracking.headValid=true;
    bodyProps_.trackingGeneration=tracking.generation;relatched_=true;lease.tracking=lease_.tracking;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::Cancel(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    if(auto* old=OverlapOf(lease.requestId,Step::Rendering)){
        if(lease.channelGeneration!=old->lease.channelGeneration||lease.native!=old->lease.native)return ChannelResult::Invalid;
        old->step=Step::Skipping;return ChannelResult::Ok;
    }
    if(step_!=Step::Rendering||!Matches(lease))return ChannelResult::Invalid;
    step_=Step::Skipping;return ChannelResult::Ok;
}
ChannelResult StagedFrameProducer::DeferGpu(const FrameLease& lease)noexcept {
    Lock lock(gate_);if(!lock)return ChannelResult::Busy;
    auto* free=FreeOverlap();if(!free)return ChannelResult::Busy;
    if(step_!=Step::Rendering||!Matches(lease))return ChannelResult::Invalid;
    const auto result=channel_.DeferRendering(lease.requestId);if(result!=ChannelResult::Ok)return result;
    *free={};free->requestId=lease.requestId;free->step=Step::Rendering;
    free->lease=lease_;free->props=bodyProps_;free->relatched=relatched_;
    step_=Step::Idle;lease_={};ticket_={};descriptor_={};depth_={};return ChannelResult::Ok;
}
void StagedFrameProducer::PumpGraphics()noexcept {
    Lock lock(gate_);if(!lock)return;
    DrainFeedbackGraphics();
    // Oldest request first: the host takes pairs in request order.
    std::array<Overlapped*,FrameSlotCount-1> order{};unsigned count=0;
    for(auto& entry:overlapped_)if(entry.requestId)order[count++]=&entry;
    for(unsigned i=1;i<count;++i)for(unsigned j=i;j>0&&order[j]->requestId<order[j-1]->requestId;--j)std::swap(order[j],order[j-1]);
    for(unsigned i=0;i<count;++i){
        auto& old=*order[i];
        if(old.step==Step::Skipping){
            if(channel_.SkipId(old.requestId)!=ChannelResult::Busy)old={};
            continue;
        }
        if(old.step==Step::Publishing){
            const auto result=channel_.PublishId(old.requestId,old.descriptor,old.ticket,&old.props,old.relatched?&old.lease.tracking:nullptr,old.depth.present?&old.depth:nullptr);
            if(result==ChannelResult::Ok)old.step=Step::Waiting;
            else if(result!=ChannelResult::Busy){feedback_.PairConsumed(old.ticket,false);old={};continue;}
        }
        if(old.step==Step::Waiting){const auto outcome=channel_.PollOutcome(old.requestId);
            if(outcome!=Outcome::Pending){const auto ticket=old.ticket;old={};feedback_.PairConsumed(ticket,outcome==Outcome::Consumed);}
        }
    }
    if(step_==Step::Skipping){if(channel_.Skip()!=ChannelResult::Busy){step_=Step::Idle;lease_={};}return;}
    if(step_==Step::Publishing){const auto result=channel_.Publish(descriptor_,ticket_,&bodyProps_,relatched_?&lease_.tracking:nullptr,depth_.present?&depth_:nullptr);
        if(result==ChannelResult::Busy)return;
        if(result==ChannelResult::Ok)step_=Step::Waiting;
        else{feedback_.PairConsumed(ticket_,false);step_=Step::Idle;lease_={};return;}
    }
    if(step_==Step::Waiting){const auto outcome=channel_.PollOutcome(lease_.requestId);if(outcome==Outcome::Pending)return;
        feedback_.PairConsumed(ticket_,outcome==Outcome::Consumed);step_=Step::Idle;lease_={};
    }
}
}
