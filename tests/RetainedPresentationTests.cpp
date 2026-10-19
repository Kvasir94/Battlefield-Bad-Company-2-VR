#include "Test.h"
#include "fvr/runtime/RetainedPresentation.h"
using namespace fvr;
int main(){
    runtime::PresentationRequirements req{100,120,29,19,4};runtime::TrackingFrame source{};
    source.generation=5;source.spaceGeneration=7;source.predictedNs=1000000000;source.focused=source.headValid=true;
    source.eyes[0].position.x=-.032f;source.eyes[1].position.x=.032f;
    source.fov={math::FovTangents{-1.2f,.8f,.9f,-1.1f},math::FovTangents{-.8f,1.2f,.9f,-1.1f}};
    graphics::TextureDescriptor d{};d.width=req.width;d.height=req.height;d.format=req.format;d.adapterLow=req.adapterLow;d.adapterHigh=req.adapterHigh;d.resourceEpoch=6;d.session[0]=1;
    graphics::PairTicket t{};t.resourceEpoch=d.resourceEpoch;t.session=d.session;t.sequence=1;t.frameId=4;t.spaceGeneration=source.spaceGeneration;t.trackingGeneration=source.generation;t.predictedNs=source.predictedNs;
    using runtime::RetentionContext;using runtime::RetainedPresentation;
    const auto at=[&](std::int64_t afterNs,bool focused=true,bool headValid=true,bool shouldRender=true){
        RetentionContext c;c.predictedNs=source.predictedNs+afterNs;c.spaceGeneration=source.spaceGeneration;c.focused=focused;c.headValid=headValid;c.shouldRender=shouldRender;return c;};
    RetainedPresentation retained;CHECK(!retained.Select(req,at(0)));CHECK(retained.Commit(req,source,d,t));
    const auto* held=retained.Select(req,at(11000000));CHECK(held);CHECK(held->generation==source.generation);
    CHECK(held->eyes[0].position.x==source.eyes[0].position.x&&held->fov[0].left==source.fov[0].left);
    // Repeated misses keep both original eyes, never the current head pose.
    for(int frame=2;frame<=22;++frame)CHECK(retained.Select(req,at(frame*11000000)));
    CHECK(!retained.Select(req,at(RetainedPresentation::MaxAgeNs+1)));
    CHECK(!retained.Select(req,at(0))); // Expiry cannot resurrect.
    // Dashboard: VISIBLE (not focused) keeps the world far beyond the focused bound,
    // with the original pose; only the age bound for that state or a reset ends it.
    CHECK(retained.Commit(req,source,d,t));
    CHECK(retained.Select(req,at(5000000000,false,false)));
    CHECK(retained.Select(req,at(RetainedPresentation::VisibleMaxAgeNs,false,false)));
    CHECK(!retained.Select(req,at(RetainedPresentation::VisibleMaxAgeNs+1,false,false)));
    CHECK(!retained.Select(req,at(0))); // Expiry cannot resurrect.
    // Kill switch: FVR_XR_RETAIN_VISIBLE=0 resets as soon as focus is gone.
    retained.SetRetainThroughGaps(false);CHECK(retained.Commit(req,source,d,t));
    CHECK(retained.Select(req,at(11000000)));CHECK(!retained.Select(req,at(22000000,false,true)));CHECK(!retained.Select(req,at(22000000)));
    retained.SetRetainThroughGaps(true);
    // Brief head loss keeps submitting the original pose; past the grace it is
    // withheld (not discarded) and regaining the head within the age bound resumes.
    CHECK(retained.Commit(req,source,d,t));
    CHECK(retained.Select(req,at(11000000,true,false)));
    CHECK(retained.Select(req,at(11000000+RetainedPresentation::HeadLossGraceNs,true,false)));
    CHECK(!retained.Select(req,at(11000000+RetainedPresentation::HeadLossGraceNs+1,true,false)));
    CHECK(retained.Select(req,at(150000000,true,true)));
    CHECK(retained.Select(req,at(160000000,true,false))); // A new loss restarts its grace.
    CHECK(!retained.Select(req,at(RetainedPresentation::MaxAgeNs+1,true,false)));CHECK(!retained.Select(req,at(160000000)));
    // Hard discontinuities reset permanently.
    for(unsigned transition=0;transition<6;++transition){
        CHECK(retained.Commit(req,source,d,t));auto current=at(11000000);auto changed=req;
        switch(transition){case 0:++current.spaceGeneration;break;case 1:current.predictedNs=source.predictedNs-1;break;
          case 2:++changed.width;break;case 3:++changed.adapterLow;break;case 4:current.shouldRender=false;break;case 5:++changed.format;break;}
        CHECK(!retained.Select(changed,current));CHECK(!retained.Select(req,at(11000000)));
    }
    // A pair whose pose is invalid is never retained; a mismatched ticket neither.
    auto invalid=source;invalid.eyes[1].orientation.w=NAN;CHECK(!retained.Commit(req,invalid,d,t));
    CHECK(retained.Commit(req,source,d,t));auto bad=t;++bad.trackingGeneration;
    CHECK(!retained.Commit(req,source,d,bad));CHECK(retained.Select(req,at(0)));
    retained.Reset();CHECK(!retained.Select(req,at(0)));return 0;
}
