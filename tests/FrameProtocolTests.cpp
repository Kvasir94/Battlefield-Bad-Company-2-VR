#include "Test.h"
#include "fvr/ipc/FrameProtocol.h"
#include "fvr/runtime/LateLatch.h"
#include <cstring>
#include <limits>
using namespace fvr;
int main(){
    runtime::PresentationRequirements requirements{100,120,29,0x87654321,-3};runtime::TrackingFrame tracking{};
    tracking.generation=0x123456789abcdef0ULL;tracking.spaceGeneration=0x100000007ULL;tracking.predictedNs=10000000000;
    tracking.worldUnitsPerMeter=2.5f;
    tracking.focused=tracking.headValid=true;tracking.referenceHead.position.y=1.6f;tracking.head=tracking.referenceHead;tracking.head.position.x=.2f;
    tracking.eyes={tracking.head,tracking.head};tracking.eyes[0].position.x-=.032f;tracking.eyes[1].position.x+=.032f;
    tracking.fov={math::FovTangents{-1.2f,.9f,1.1f,-1.f},math::FovTangents{-.9f,1.2f,1.1f,-1.f}};
    ipc::FrameRequest request{};CHECK(ipc::Encode(requirements,tracking,0x100000001ULL,1000,request));
    runtime::PresentationRequirements decoded{};runtime::TrackingFrame sample{};CHECK(ipc::Decode(request,decoded,sample));
    CHECK(sample.worldUnitsPerMeter==2.5f);
    auto invalidScale=request;invalidScale.worldUnitsPerMeter=0;CHECK(!ipc::Decode(invalidScale,decoded,sample));
    invalidScale=request;invalidScale.reservedScale=1;CHECK(!ipc::Decode(invalidScale,decoded,sample));
    CHECK(decoded.adapterHigh==-3&&decoded.adapterLow==requirements.adapterLow&&sample.generation==tracking.generation&&sample.predictedNs==tracking.predictedNs);
    CHECK(sample.eyes[0].position.x==tracking.eyes[0].position.x&&sample.referenceHead.position.y==1.6f&&sample.fov[1].right==1.2f);
    auto corrupt=request;corrupt.bytes--;CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.flags=1;CHECK(!ipc::Decode(corrupt,decoded,sample));
    corrupt=request;corrupt.referenceHead.values[6]=0;CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.eyes[1].values[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!ipc::Decode(corrupt,decoded,sample));
    corrupt=request;corrupt.fov[0][1]=corrupt.fov[0][0];CHECK(!ipc::Decode(corrupt,decoded,sample));corrupt=request;corrupt.reserved[1]=1;CHECK(!ipc::Decode(corrupt,decoded,sample));
    corrupt=request;corrupt.version=2;CHECK(!ipc::Decode(corrupt,decoded,sample));
    tracking.focused=false;CHECK(!ipc::Encode(requirements,tracking,1,1000,corrupt));tracking.focused=true;
    graphics::TextureDescriptor d{};d.width=requirements.width;d.height=requirements.height;d.format=requirements.format;d.adapterLow=requirements.adapterLow;d.adapterHigh=requirements.adapterHigh;d.resourceEpoch=4;d.session[0]=7;
    graphics::PairTicket ticket{};ticket.session=d.session;ticket.resourceEpoch=d.resourceEpoch;ticket.sequence=1;ticket.frameId=0x100000009ULL;
    ticket.trackingGeneration=tracking.generation;ticket.spaceGeneration=tracking.spaceGeneration;ticket.predictedNs=tracking.predictedNs;
    ipc::FrameSlot slot{};ipc::FrameRequest taken{};graphics::TextureDescriptor got{};graphics::PairTicket pair{};
    CHECK(ipc::Begin(slot,request,1));CHECK(!ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(!ipc::Take(slot,3,taken));
    auto badTicket=ticket;--badTicket.predictedNs;CHECK(!ipc::Publish(slot,request.requestId,5,d,badTicket));CHECK(!ipc::Publish(slot,1,5,d,ticket));
    CHECK(ipc::Publish(slot,request.requestId,5,d,ticket));CHECK(!ipc::Publish(slot,request.requestId,6,d,ticket));
    CHECK(ipc::Deliver(slot,request.requestId,6,got,pair));CHECK(!ipc::Deliver(slot,request.requestId,6,got,pair));
    // A timeout cannot reclaim a texture that the GPU consumer already owns.
    ipc::Cancel(slot,request.requestId);CHECK(ipc::Reap(slot,request.requestId,2000)==ipc::Outcome::Pending);
    CHECK(!ipc::Acknowledge(slot,2,true));CHECK(ipc::Acknowledge(slot,request.requestId,true));CHECK(ipc::Reap(slot,request.requestId,2000)==ipc::Outcome::Consumed);CHECK(slot.phase==ipc::Phase::Idle);
    // Timeout before take; timeout during render; timeout after publish.
    CHECK(ipc::Begin(slot,request,1));CHECK(!ipc::Take(slot,1000,taken));CHECK(slot.phase==ipc::Phase::Idle);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));ipc::Cancel(slot,request.requestId);
    CHECK(slot.phase==ipc::Phase::Cancelled);CHECK(!ipc::Begin(slot,request,3));CHECK(!ipc::Publish(slot,request.requestId,4,d,ticket));CHECK(slot.phase==ipc::Phase::Idle);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));
    CHECK(!ipc::Deliver(slot,request.requestId,1000,got,pair));CHECK(ipc::Reap(slot,request.requestId,1000)==ipc::Outcome::Discarded);
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));
    CHECK(ipc::Reap(slot,request.requestId,1000)==ipc::Outcome::Discarded);
    // Skip a frame without a GPU pair, then reject the previous frame's late write.
    CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));ipc::Skip(slot,request.requestId);
    auto next=request;++next.requestId;CHECK(ipc::Begin(slot,next,3));CHECK(!ipc::Publish(slot,request.requestId,4,d,ticket));CHECK(slot.phase==ipc::Phase::Requested);
    slot.phase=ipc::Phase::Closed;CHECK(!ipc::Begin(slot,request,1));CHECK(ipc::Reap(slot,next.requestId,2)==ipc::Outcome::Discarded);
    // A successor cannot take over the GPU lease before acknowledgment/reaping.
    slot={};CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));
    CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));CHECK(ipc::Deliver(slot,request.requestId,4,got,pair));
    CHECK(!ipc::Begin(slot,next,5));CHECK(ipc::Acknowledge(slot,request.requestId,true));
    CHECK(!ipc::Acknowledge(slot,request.requestId,false));CHECK(ipc::Begin(slot,next,6));
    CHECK(slot.request.requestId==request.requestId&&slot.phase==ipc::Phase::Complete);
    CHECK(!ipc::Begin(slot,next,7));CHECK(!ipc::Take(slot,8,taken));
    CHECK(ipc::Reap(slot,request.requestId,9)==ipc::Outcome::Consumed);
    CHECK(slot.phase==ipc::Phase::Requested&&slot.request.requestId==next.requestId&&!slot.queued.requestId);
    CHECK(!ipc::Acknowledge(slot,request.requestId,true));CHECK(ipc::Take(slot,10,taken));
    CHECK(taken.requestId==next.requestId&&taken.predictedNs==next.predictedNs);
    CHECK(ipc::Reap(slot,request.requestId,11)==ipc::Outcome::Discarded);CHECK(slot.phase==ipc::Phase::Rendering);
    // Cancelling a queued successor preserves the previous consumed outcome.
    slot={};CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));
    CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));CHECK(ipc::Deliver(slot,request.requestId,4,got,pair));
    CHECK(ipc::Acknowledge(slot,request.requestId,true));CHECK(ipc::Begin(slot,next,5));
    ipc::Cancel(slot,next.requestId);CHECK(!slot.queued.requestId&&slot.phase==ipc::Phase::Complete);
    CHECK(ipc::Reap(slot,request.requestId,6)==ipc::Outcome::Consumed);CHECK(slot.phase==ipc::Phase::Idle);
    CHECK(ipc::Begin(slot,next,7)); // Queue after reaping uses the normal idle path.
    // A successor's deadline is immutable even while awaiting old lease reaping.
    slot={};CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));
    CHECK(ipc::Publish(slot,request.requestId,3,d,ticket));CHECK(ipc::Deliver(slot,request.requestId,4,got,pair));
    CHECK(ipc::Acknowledge(slot,request.requestId,false));CHECK(ipc::Begin(slot,next,5));
    CHECK(ipc::Reap(slot,request.requestId,1000)==ipc::Outcome::Discarded);
    CHECK(slot.phase==ipc::Phase::Idle&&!slot.queued.requestId);CHECK(!ipc::Take(slot,1001,taken));
    // Late latch: a newer sample of the same space/reference relabels the pair.
    {auto fresh=tracking;fresh.generation+=3;fresh.predictedNs+=11000000;fresh.eyes[0].position.z-=.01f;fresh.fov[0].left=-1.25f;
        CHECK(runtime::CanRelatch(tracking,fresh));
        auto older=fresh;older.generation=tracking.generation;CHECK(!runtime::CanRelatch(tracking,older));
        auto moved=fresh;++moved.spaceGeneration;CHECK(!runtime::CanRelatch(tracking,moved));
        auto recentered=fresh;recentered.referenceHead.position.x+=.1f;CHECK(!runtime::CanRelatch(tracking,recentered));
        auto rescaled=fresh;rescaled.worldUnitsPerMeter=1;CHECK(!runtime::CanRelatch(tracking,rescaled));
        auto backwards=fresh;backwards.predictedNs=tracking.predictedNs-1;CHECK(!runtime::CanRelatch(tracking,backwards));
        auto far=fresh;far.predictedNs=tracking.predictedNs+runtime::MaxRelatchAdvanceNs+1;CHECK(!runtime::CanRelatch(tracking,far));
        auto latched=ticket;latched.trackingGeneration=fresh.generation;latched.predictedNs=fresh.predictedNs;
        runtime::TrackingFrame rendered{};
        slot={};CHECK(ipc::Begin(slot,request,1));CHECK(ipc::Take(slot,2,taken));
        CHECK(!ipc::Publish(slot,request.requestId,3,d,ticket,&fresh)); // Ticket must name the latched sample.
        CHECK(!ipc::Publish(slot,request.requestId,3,d,latched,&moved));
        CHECK(!ipc::Publish(slot,request.requestId,3,d,latched));        // Or the request, not a mix.
        CHECK(ipc::Publish(slot,request.requestId,3,d,latched,&fresh));
        CHECK(ipc::RenderedTracking(slot,rendered)&&rendered.generation==fresh.generation&&rendered.predictedNs==fresh.predictedNs);
        CHECK(rendered.eyes[0].position.z==fresh.eyes[0].position.z&&rendered.fov[0].left==-1.25f);
        CHECK(slot.request.requestId==request.requestId&&slot.request.deadlineQpc==request.deadlineQpc);
        CHECK(ipc::Deliver(slot,request.requestId,4,got,pair)&&pair.trackingGeneration==fresh.generation);
        CHECK(ipc::RenderedTracking(slot,rendered)&&rendered.generation==fresh.generation);
        CHECK(ipc::Acknowledge(slot,request.requestId,true));CHECK(!ipc::RenderedTracking(slot,rendered));
        CHECK(ipc::Reap(slot,request.requestId,5)==ipc::Outcome::Consumed);}
    // Depth is optional and belongs to the exact color identity. Invalid or
    // unrequested depth cannot fail or contaminate a valid color publication.
    {
        graphics::DepthAttachment depth{};depth.descriptor=d;depth.descriptor.format=graphics::DepthFormat;
        depth.descriptor.session[0]=8;depth.ticket=ticket;depth.ticket.session=depth.descriptor.session;
        depth.nearFar={.1f,500.f,.2f,400.f};depth.present=1;
        CHECK(graphics::ValidDepth(depth,d,ticket));
        auto bad=depth;bad.nearFar[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!graphics::ValidDepth(bad,d,ticket));
        bad=depth;bad.nearFar[3]=std::numeric_limits<float>::infinity();CHECK(!graphics::ValidDepth(bad,d,ticket));
        bad=depth;++bad.ticket.frameId;CHECK(!graphics::ValidDepth(bad,d,ticket));
        bad=depth;++bad.descriptor.width;CHECK(!graphics::ValidDepth(bad,d,ticket));
        bad=depth;++bad.descriptor.adapterHigh;CHECK(!graphics::ValidDepth(bad,d,ticket));
        bad=depth;bad.descriptor.session=d.session;bad.ticket.session=d.session;CHECK(!graphics::ValidDepth(bad,d,ticket));
        auto depthRequest=request;depthRequest.flags|=ipc::FrameDepthRequested;
        corrupt=depthRequest;corrupt.flags|=8;CHECK(!ipc::Decode(corrupt,decoded,sample));
        for(unsigned mode=0;mode<3;++mode){
            slot={};CHECK(ipc::Begin(slot,mode?depthRequest:request,1));CHECK(ipc::Take(slot,2,taken));
            CHECK(ipc::Publish(slot,request.requestId,3,d,ticket,nullptr,mode==2?&bad:&depth));
            graphics::DepthAttachment received{};
            CHECK(ipc::Deliver(slot,request.requestId,4,got,pair,&received));CHECK(received.present==(mode==1?1u:0u));
            CHECK(pair.frameId==ticket.frameId&&pair.trackingGeneration==ticket.trackingGeneration);
        }
        // Late-latched color and depth must both identify the new tracking sample.
        auto fresh=tracking;++fresh.generation;fresh.predictedNs+=11000000;
        auto latched=ticket;latched.trackingGeneration=fresh.generation;latched.predictedNs=fresh.predictedNs;
        depth.ticket.trackingGeneration=fresh.generation;depth.ticket.predictedNs=fresh.predictedNs;
        slot={};CHECK(ipc::Begin(slot,depthRequest,1));CHECK(ipc::Take(slot,2,taken));
        CHECK(ipc::Publish(slot,request.requestId,3,d,latched,&fresh,&depth));
        graphics::DepthAttachment received{};CHECK(ipc::Deliver(slot,request.requestId,4,got,pair,&received));
        CHECK(received.present&&received.ticket.trackingGeneration==fresh.generation);
        CHECK(slot.request.flags&ipc::FrameDepthRequested);
    }
    // Overscan widens tangents, caps at the culling limit and never shrinks.
    {const math::FovTangents fov{-1.f,.8f,1.4f,-1.2f};const auto wide=runtime::OverscanFov(fov,.1f,1.45f);
        CHECK(Near(wide.left,-1.1f)&&Near(wide.right,.88f)&&Near(wide.up,1.45f)&&Near(wide.down,-1.32f));
        const auto beyond=runtime::OverscanFov(math::FovTangents{-1.f,1.f,1.6f,-1.f},.1f,1.45f);CHECK(beyond.up==1.6f);
        const auto off=runtime::OverscanFov(fov,0,1.45f);CHECK(off.left==fov.left&&off.up==fov.up);}
    // Presentation lead: zero until measured, then the average age, capped.
    {runtime::PresentationLead lead;CHECK(lead.LeadNs()==0);for(int i=0;i<3;++i)lead.Observe(20000000);CHECK(lead.LeadNs()==0);
        lead.Observe(20000000);CHECK(lead.LeadNs()==20000000);lead.Observe(-1);lead.Observe(900000000);CHECK(lead.Samples()==4);
        for(int i=0;i<200;++i)lead.Observe(200000000);CHECK(lead.LeadNs()==runtime::PresentationLead::MaxLeadNs);}
    return 0;
}
