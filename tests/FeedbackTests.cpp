#include "fvr/ipc/FeedbackProtocol.h"
#include "Test.h"
#include <iostream>
#include <limits>
#include <tuple>
using namespace fvr::interaction;using namespace fvr::ipc;
namespace {
constexpr std::int64_t Now=1000000000;
InputFrame Input(){InputFrame i;i.generation=20;i.spaceGeneration=7;i.predictedNs=3000000000;
    i.focused=i.headValid=true;i.hands[0].gripTracked=i.hands[1].gripTracked=true;return i;}
FeedbackEvent Event(){return {1,19,7,Now-1000000,Now+99000000,FeedbackKind::ReloadCapture,0};}
int DistinctCaptureAndReceiptWithNoReplay(){HapticFeedbackPolicy p;auto i=Input();auto e=Event();
    auto r=p.Update(i,Now,&e);CHECK(r.pulse&&r.pulse->hand==0&&r.pulse->durationNs==35000000&&Near(r.pulse->amplitude,.55f));
    CHECK(!p.Update(i,Now+1,&e).pulse);e.id++;e.kind=FeedbackKind::ReloadApplied;
    r=p.Update(i,Now+2,&e);CHECK(r.pulse&&r.pulse->durationNs==75000000&&Near(r.pulse->amplitude,.9f));
    CHECK(!p.Update(i,Now+3,&e).pulse);return 0;}
int FocusAndMenuBarrier(){HapticFeedbackPolicy p;auto i=Input();p.Update(i,Now);i.focused=false;++i.generation;
    CHECK(p.Update(i,Now+1).stop);auto e=Event();e.inputSequence=i.generation;
    CHECK(!p.Update(i,Now+2,&e).pulse);i.focused=true;++i.generation;e.id++;
    CHECK(!p.Update(i,Now+3,&e).pulse);e.id++;e.inputSequence=i.generation;
    CHECK(p.Update(i,Now+4,&e).pulse);return 0;}
int SessionEventSuspendBlocksOldInput(){HapticFeedbackPolicy p;auto i=Input();p.Update(i,Now);p.Suspend();
    auto e=Event();e.inputSequence=i.generation;CHECK(!p.Update(i,Now+1,&e).pulse);
    ++i.generation;++e.id;e.inputSequence=i.generation;CHECK(p.Update(i,Now+2,&e).pulse);return 0;}
int SpaceTrackingClockAndExpiry(){for(unsigned bad=0;bad<8;++bad){HapticFeedbackPolicy p;auto i=Input();auto e=Event();p.Update(i,Now);
    auto at=Now+1;
    if(bad==0)++e.space;if(bad==1)i.hands[0].gripTracked=false;if(bad==2)i.headValid=false;
    if(bad==3)at=e.deadlineNs;if(bad==4)e.inputSequence=i.generation+1;if(bad==5)e.observedNs=at+1;
    if(bad==6)at=Now-1;if(bad==7){++i.spaceGeneration;e.inputSequence=i.generation;}
    CHECK(!p.Update(i,at,&e).pulse);
    CHECK(!p.Update(Input(),Now+2,&e).pulse); // Rejected event cannot replay after recovery.
    }return 0;}
int WireLayoutAndMalformedInputs(){auto e=Event();FeedbackPacket wire;CHECK(EncodeFeedback(e,Now,wire));FeedbackEvent out;
    CHECK(DecodeFeedback(wire,Now,out)&&out.id==e.id&&out.observedNs==e.observedNs&&out.deadlineNs==e.deadlineNs);
    for(unsigned bad=0;bad<8;++bad){auto p=wire;if(bad==0)p.magic=0;if(bad==1)++p.version;if(bad==2)--p.bytes;if(bad==3)p.reserved=1;
        if(bad==4)p.kind=999;if(bad==5)p.hand=2;if(bad==6)p.event=0;if(bad==7)p.deadlineNs=p.observedNs+100000001;
        CHECK(!DecodeFeedback(p,Now,out)&&out.id==0);}
    CHECK(!DecodeFeedback(wire,e.deadlineNs,out));return 0;}
int SeatedIsDistinctAndCrossesTheWire(){HapticFeedbackPolicy p;auto i=Input();auto e=Event();e.kind=FeedbackKind::ReloadSeated;
    CHECK(ValidFeedback(e,Now));auto r=p.Update(i,Now,&e);CHECK(r.pulse&&r.pulse->durationNs==100000000&&Near(r.pulse->amplitude,.5f));
    // A capture right before the seat cannot rate-limit the seat click.
    HapticFeedbackPolicy q;auto c=Event();CHECK(q.Update(i,Now,&c).pulse);e.id=2;CHECK(q.Update(i,Now+1000000,&e).pulse);
    FeedbackPacket wire;CHECK(EncodeFeedback(e,Now,wire));FeedbackEvent out;CHECK(DecodeFeedback(wire,Now,out)&&out.kind==FeedbackKind::ReloadSeated);
    auto bad=e;bad.kind=FeedbackKind(18);CHECK(!ValidFeedback(bad,Now));bad.kind=FeedbackKind(0);CHECK(!ValidFeedback(bad,Now));
    // KF2 VR / AS2 set: grab 0.3/10 ms, rack 1.0/100 ms, gun-hand seat 0.35/30 ms,
    // failed insertion two 0.3/30 ms peaks 60 ms apart, gun fire 0.85/50 ms, dry fire 0.35/15 ms.
    HapticFeedbackPolicy k;auto g=Event();g.kind=FeedbackKind::ReloadGrab;auto gr=k.Update(i,Now,&g);CHECK(gr.pulse&&Near(gr.pulse->amplitude,.3f)&&gr.pulse->durationNs==10000000);
    auto rk=Event();rk.id=2;rk.kind=FeedbackKind::ReloadRack;auto rr=k.Update(i,Now,&rk);CHECK(rr.pulse&&Near(rr.pulse->amplitude,1.f)&&rr.pulse->durationNs==100000000);
    auto gs=Event();gs.id=3;gs.hand=1;gs.kind=FeedbackKind::ReloadGunSeat;auto sr=k.Update(i,Now,&gs);CHECK(sr.pulse&&sr.pulse->hand==1&&Near(sr.pulse->amplitude,.35f)&&sr.pulse->durationNs==30000000);
    auto fl=Event();fl.id=4;fl.kind=FeedbackKind::ReloadFailed;auto f1=k.Update(i,Now,&fl);CHECK(f1.pulse&&Near(f1.pulse->amplitude,.3f)&&f1.pulse->durationNs==30000000);
    CHECK(!k.Update(i,Now+30000000).pulse);auto f2=k.Update(i,Now+60000000);CHECK(f2.pulse&&f2.pulse->durationNs==30000000);
    CHECK(!k.Update(i,Now+70000000).pulse);
    for(const auto& [kind,amp,ns]:{std::tuple{FeedbackKind::PumpRear,.7f,45000000ll},std::tuple{FeedbackKind::PumpGunKnock,.3f,30000000ll},std::tuple{FeedbackKind::PumpForward,1.f,80000000ll},std::tuple{FeedbackKind::GunFire,.85f,50000000ll},std::tuple{FeedbackKind::DryFire,.35f,15000000ll}}){
        HapticFeedbackPolicy q;auto e=Event();e.kind=kind;const auto r=q.Update(i,Now,&e);CHECK(r.pulse&&Near(r.pulse->amplitude,amp)&&r.pulse->durationNs==ns);}
    auto hv=Event();hv.id=5;hv.kind=FeedbackKind::ReloadHover;auto hr=k.Update(i,Now+80000000,&hv);CHECK(hr.pulse&&Near(hr.pulse->amplitude,.1f)&&hr.pulse->durationNs==12000000);
    // Ready is a distinct receipt/confirmation cue; duplicate delivery cannot
    // restart its second click. No new wire layout or native object crosses IPC.
    {HapticFeedbackPolicy p;auto ready=Event();ready.hand=1;ready.kind=FeedbackKind::ReloadReady;
     FeedbackPacket packet;CHECK(EncodeFeedback(ready,Now,packet));FeedbackEvent back;
     CHECK(DecodeFeedback(packet,Now,back)&&back.kind==FeedbackKind::ReloadReady);
     const auto first=p.Update(i,Now,&ready);CHECK(first.pulse&&Near(first.pulse->amplitude,.18f));
     CHECK(!p.Update(i,Now+1000000,&ready).pulse);
     const auto second=p.Update(i,Now+40000000);CHECK(second.pulse&&second.pulse->eventId==ready.id);
     CHECK(!p.Update(i,Now+80000000).pulse);}
    // Stick weapon selection: a light tick on success, a double buzz on rejection.
    {HapticFeedbackPolicy s;auto ok=Event();ok.hand=1;ok.kind=FeedbackKind::SwitchSelected;CHECK(ValidFeedback(ok,Now));
     FeedbackPacket w;CHECK(EncodeFeedback(ok,Now,w));FeedbackEvent back;CHECK(DecodeFeedback(w,Now,back)&&back.kind==FeedbackKind::SwitchSelected);
     auto r1=s.Update(i,Now,&ok);CHECK(r1.pulse&&r1.pulse->hand==1&&Near(r1.pulse->amplitude,.25f)&&r1.pulse->durationNs==12000000);
     auto no=Event();no.id=2;no.hand=1;no.kind=FeedbackKind::SwitchRejected;auto r2=s.Update(i,Now+1000000,&no);
     CHECK(r2.pulse&&Near(r2.pulse->amplitude,.2f)&&r2.pulse->durationNs==25000000);
     CHECK(!s.Update(i,Now+30000000).pulse);auto r3=s.Update(i,Now+51000000);CHECK(r3.pulse&&r3.pulse->kind==FeedbackKind::SwitchRejected&&r3.pulse->hand==1);}
    return 0;}
int CaptureRateLimitDoesNotSuppressReceipt(){HapticFeedbackPolicy p;auto i=Input();auto e=Event();CHECK(p.Update(i,Now,&e).pulse);
    ++e.id;CHECK(!p.Update(i,Now+1000000,&e).pulse);++e.id;e.kind=FeedbackKind::ReloadApplied;
    CHECK(p.Update(i,Now+1000001,&e).pulse);return 0;}
int DelayedPulseOwnsItsEventAndHonorsCancellation(){
    for(unsigned cancel=0;cancel<7;++cancel){HapticFeedbackPolicy p;auto i=Input();auto e=Event();e.kind=FeedbackKind::ReloadFailed;
        const auto first=p.Update(i,Now,&e);CHECK(first.pulse&&first.pulse->eventId==e.id&&first.pulse->kind==e.kind);
        if(cancel==0)p.Suspend();
        if(cancel==1)i.focused=false;
        if(cancel==2)i.headValid=false;
        if(cancel==3)i.hands[0].gripTracked=false;
        if(cancel==4)++i.spaceGeneration;
        if(cancel==5)--i.generation;
        if(cancel==6){CHECK(!p.Update(i,Now-1).pulse);}
        CHECK(!p.Update(i,Now+60000000).pulse);
    }
    HapticFeedbackPolicy p;auto i=Input();auto e=Event();e.kind=FeedbackKind::ReloadFailed;
    CHECK(p.Update(i,Now,&e).pulse);
    // An unrelated rejected event arriving with the second peak must not own it.
    auto rejected=e;rejected.id=2;rejected.kind=FeedbackKind::ReloadCapture;++rejected.space;
    const auto second=p.Update(i,Now+60000000,&rejected);
    CHECK(second.pulse&&second.pulse->eventId==e.id&&second.pulse->kind==FeedbackKind::ReloadFailed);
    CHECK(!p.Update(i,Now+60000001).pulse);return 0;
}
}
int WeightedShotFeedback(){
    for(float weight:{0.f,.5f,1.f})for(bool support:{false,true}){
        auto e=Event();e.kind=FeedbackKind::GunFire;e.hand=support?0:1;e.recoilWeight=weight;e.recoilSupport=support;
        FeedbackPacket packet;CHECK(EncodeFeedback(e,Now,packet));FeedbackEvent decoded;
        CHECK(DecodeFeedback(packet,Now,decoded)&&decoded.recoilSupport==support&&std::abs(decoded.recoilWeight-weight)<.00002f);
        HapticFeedbackPolicy policy;const auto pulse=policy.Update(Input(),Now,&decoded).pulse;
        CHECK(pulse&&pulse->hand==e.hand&&Near(pulse->amplitude,(.35f+.65f*weight)*(support?.65f:1.f),.00002f));
        CHECK(std::abs(pulse->durationNs-std::llround((.025+.075*weight)*(support?.8:1.)*1000000000.))<2000);
        CHECK(!policy.Update(Input(),Now+1,&decoded).pulse);
    }
    auto e=Event();e.kind=FeedbackKind::GunFire;
    FeedbackPacket packet;CHECK(EncodeFeedback(e,Now,packet)&&packet.reserved==0);
    HapticFeedbackPolicy legacy;const auto pulse=legacy.Update(Input(),Now,&e).pulse;
    CHECK(pulse&&Near(pulse->amplitude,.85f)&&pulse->durationNs==50000000);
    for(float invalid:{-2.f,1.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        e.recoilWeight=invalid;CHECK(!EncodeFeedback(e,Now,packet));
    }
    e.recoilWeight=.5f;CHECK(EncodeFeedback(e,Now,packet));FeedbackEvent decoded;
    auto bad=packet;bad.reserved|=1u<<18;CHECK(!DecodeFeedback(bad,Now,decoded));
    bad=packet;bad.reserved&=~RecoilPresent;CHECK(!DecodeFeedback(bad,Now,decoded));
    bad=packet;bad.kind=unsigned(FeedbackKind::DryFire);CHECK(!DecodeFeedback(bad,Now,decoded));
    e.kind=FeedbackKind::ReloadCapture;CHECK(!EncodeFeedback(e,Now,packet));
    e.recoilWeight=-1.f;e.recoilSupport=true;CHECK(!EncodeFeedback(e,Now,packet));
    return 0;
}
int main(){if(DistinctCaptureAndReceiptWithNoReplay()||FocusAndMenuBarrier()||SessionEventSuspendBlocksOldInput()||SpaceTrackingClockAndExpiry()||WireLayoutAndMalformedInputs()||CaptureRateLimitDoesNotSuppressReceipt()||SeatedIsDistinctAndCrossesTheWire()||DelayedPulseOwnsItsEventAndHonorsCancellation()||WeightedShotFeedback())return 1;
    std::cout<<"Feedback: nine semantic/wire/safety groups passed\n";return 0;}
