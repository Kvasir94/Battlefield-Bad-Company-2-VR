#include "Test.h"
#include "Bc2PumpHold.h"
#include <cmath>
#include <cstdio>
using namespace fvr::bc2;
namespace {
ReloadHoldInput Cohort(unsigned branch,unsigned cur,unsigned prev,unsigned next,float timer,int loaded=6,int reserve=20){
    ReloadHoldInput in;in.verified=true;in.branch=branch;
    in.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};in.identity.firing={0x50000,0x60000,0x70000};
    for(auto& b:in.branches){b.currentState=cur;b.previousState=prev;b.nextState=next;b.phaseTimer=timer;b.loaded=loaded;b.reserve=reserve;}
    return in;
}
constexpr std::int64_t Ms=1000000;
// A shot (6 -> 7, next 8) owes a pump. Copies are held only once their
// BoltActionDelay is nearly spent, and every copy is released by Rack().
int HoldsLateAndReleasesOnRack(){
    PumpHoldGate g;CHECK(!g.Evaluate(Cohort(2,7,6,8,.05f),true,1).hold);   // disabled by default
    g.Enable(true);
    CHECK(!g.Evaluate(Cohort(2,2,2,2,0),true,1*Ms).hold&&!g.Owed());        // idle: nothing owed
    CHECK(g.Evaluate(Cohort(2,7,6,8,.4f),true,2*Ms).hold&&g.Owed());        // held from the first Update at 7
    for(unsigned b=0;b<3;++b)CHECK(g.Evaluate(Cohort(b,7,6,8,.4f),true,3*Ms).hold); // all copies held
    CHECK(g.Rack()&&!g.Owed());
    // Racked: client0 first; a copy that comes before it waits (bounded).
    {const auto w=g.Evaluate(Cohort(1,7,6,8,.4f),true,4*Ms);CHECK(w.hold&&w.advance==0);}
    // Each copy then skips its remaining delay exactly once, then runs natively.
    for(unsigned b=0;b<3;++b){const auto d=g.Evaluate(Cohort(b,7,6,8,.4f),true,4*Ms);CHECK(!d.hold&&d.advance>.4f&&d.advance<.41f);}
    for(unsigned b=0;b<3;++b){const auto d=g.Evaluate(Cohort(b,7,6,8,.4f),true,5*Ms);CHECK(!d.hold&&d.advance==0);}
    CHECK(!g.Rack());
    // The next shot (one fewer round) owes a new pump.
    CHECK(g.Evaluate(Cohort(2,7,6,8,.05f,5),true,6*Ms).hold&&g.Owed());
    const auto s=g.Stats();CHECK(s.owed==2&&s.racked==1&&s.held==6&&s.advances==3&&s.orderWaits==1);
    // client0 never arrives at 7: the others go after the bounded wait.
    PumpHoldGate o;o.Enable(true);o.Evaluate(Cohort(2,7,6,8,.4f),true,1*Ms);CHECK(o.Rack());
    CHECK(o.Evaluate(Cohort(2,7,6,8,.4f),true,2*Ms).hold&&o.Evaluate(Cohort(2,7,6,8,.4f),true,2*Ms+PumpHoldGate::OrderWaitNs).advance>.4f);
    return 0;}
// Never held: not SPAS, last shell (native skips the pump), reload input,
// a copy that left 7/8, or the safety timeout.
int ReleasesWithoutRack(){
    PumpHoldGate g;g.Enable(true);
    CHECK(!g.Evaluate(Cohort(2,7,6,8,.05f),false,1).hold&&!g.Owed());
    CHECK(!g.Evaluate(Cohort(2,7,6,8,.05f,0),true,2).hold&&!g.Owed());
    CHECK(g.Evaluate(Cohort(2,7,6,8,.05f,6),true,3).hold);
    auto reload=Cohort(1,7,6,8,.05f,6);reload.context.reloadRequested=true;CHECK(!g.Evaluate(reload,true,4).hold&&!g.Owed());
    CHECK(g.Evaluate(Cohort(2,7,6,8,.05f,4),true,5).hold);
    CHECK(!g.Evaluate(Cohort(2,1,7,1,0,4),true,6).hold&&!g.Owed());           // server sprint-interrupted to 1
    CHECK(g.Evaluate(Cohort(2,7,6,8,.05f,3),true,10).hold);
    CHECK(!g.Evaluate(Cohort(0,7,6,8,.05f,3),true,10+PumpHoldGate::TimeoutNs+1).hold&&!g.Owed());
    CHECK(g.Evaluate(Cohort(0,7,6,8,.05f,3),true,11+PumpHoldGate::TimeoutNs).advance==0); // a timeout never fast-forwards
    const auto s=g.Stats();CHECK(s.reloadReleases==1&&s.stateReleases==1&&s.timeouts==1);
    // After the server copy leaves 7, a client copy still at 7 (prev 6) for the
    // same shot owes nothing.
    // Copies arrive at 7 a few ms apart: a server Update still at 6 after a
    // client owed the pump must not release it (live run 083755).
    PumpHoldGate a;a.Enable(true);CHECK(a.Evaluate(Cohort(0,7,6,8,.5f,6),true,1).hold);
    CHECK(!a.Evaluate(Cohort(2,6,5,6,.0f,6),true,2).hold&&a.Owed());
    CHECK(a.Evaluate(Cohort(2,7,6,8,.5f,6),true,3).hold&&a.Evaluate(Cohort(1,7,6,8,.5f,6),true,3).hold);
    a.OwnerCleared();CHECK(!a.Owed()&&a.Stats().ownerResets==1&&!a.Evaluate(Cohort(2,7,6,8,.5f,6),true,4).hold);
    PumpHoldGate h;h.Enable(true);CHECK(h.Evaluate(Cohort(2,7,6,8,.05f,6),true,1).hold);
    h.Evaluate(Cohort(2,8,7,1,.2f,6),true,2);
    CHECK(!h.Evaluate(Cohort(2,1,8,2,0,6),true,3).hold&&!h.Owed());
    CHECK(!h.Evaluate(Cohort(0,7,6,8,.05f,6),true,4).hold&&!h.Owed()&&h.Stats().owed==1);
    return 0;}
// KF2 VR stroke: rear stop at 95% then forward lock at 5%; a release, a gap
// or an implausible step restarts it.
int StrokeNeedsRearThenForward(){
    PumpStroke p;std::int64_t t=0;const auto step=[&](bool grip,float along){t+=11*Ms;return p.Update(grip,along,t);};
    CHECK(!step(true,0).completed);
    for(float a:{.01f,.02f,.03f,.04f,.05f})CHECK(!step(true,a).rearStop);
    CHECK(step(true,.058f).rearStop);
    for(float a:{.045f,.03f,.015f})CHECK(!step(true,a).completed);
    CHECK(step(true,.002f).completed);
    // Forward first never completes; letting go resets.
    PumpStroke q;t=0;const auto stepq=[&](bool grip,float along){t+=11*Ms;return q.Update(grip,along,t);};
    stepq(true,0);for(float a:{-.02f,-.04f,0.f})CHECK(!stepq(true,a).completed);
    for(float a:{.02f,.04f,.058f})stepq(true,a);CHECK(!stepq(false,.03f).completed&&q.Phase()==PumpStrokePhase::Idle);
    // A tracking jump restarts from the new position.
    PumpStroke r;t=0;const auto stepr=[&](float along){t+=11*Ms;return r.Update(true,along,t);};
    stepr(0);stepr(.03f);CHECK(!stepr(.2f).rearStop);CHECK(!stepr(.21f).rearStop);
    // Duplicate tracked packets preserve the rear latch, but never replay it.
    PumpStroke repeated;repeated.Update(true,0,11*Ms);
    CHECK(repeated.Update(true,.058f,22*Ms).rearStop);
    const auto duplicate=repeated.Update(true,.058f,22*Ms);
    CHECK(!duplicate.rearStop&&!duplicate.completed&&duplicate.phase==PumpStrokePhase::Forward);
    CHECK(repeated.Update(true,.002f,33*Ms).completed);
    // Find the forward stop before pulling, as KF2 does. The approach is not
    // extra travel that must be paid back before the rear stop can be reached.
    PumpStroke approach;approach.Update(true,0,11*Ms);approach.Update(true,-.04f,22*Ms);
    CHECK(approach.Update(true,.018f,33*Ms).rearStop);
    CHECK(approach.Update(true,-.038f,44*Ms).completed);
    // A native-size SPAS stroke can be fast; the KF2 continuity allowance
    // admits a deliberate pull without the old 8 cm per-sample cliff.
    PumpStroke native{PumpStrokeConfig{.09495844f}};native.Update(true,0,11*Ms);
    CHECK(native.Update(true,.094f,22*Ms).rearStop);
    CHECK(native.Update(true,.002f,33*Ms).completed);
    PumpStrokeConfig scaledConfig{.09495844f};scaledConfig.travelScale=1.35f;
    PumpStroke scaled{scaledConfig};scaled.Update(true,0,11*Ms);
    CHECK(scaled.Update(true,.068f,22*Ms).rearStop); // Full native motion from a shorter deliberate throw.
    CHECK(scaled.Update(true,.002f,33*Ms).completed);return 0;}
}
// KF2: the stroke may start right after the shot. A rack while the server
// copy is still in its shot states settles that shot: no copy owes it, and
// each skips its remaining delay once at 7 (live run 091024: 6 lasted 0.7 s).
int EarlyRackDuringShotRecovery(){
    PumpHoldGate g;g.Enable(true);
    CHECK(!g.Expected()&&!g.Rack());                                         // nothing fired: no bank
    CHECK(!g.Evaluate(Cohort(2,6,5,7,.3f,5),true,1*Ms).hold&&g.Expected()&&!g.Owed());
    CHECK(g.Rack()&&!g.Expected());                                          // banked for this shot
    for(unsigned b=0;b<3;++b){const auto d=g.Evaluate(Cohort(b,7,6,8,.5f,5),true,2*Ms);CHECK(!d.hold&&d.advance>.5f&&!g.Owed());}
    CHECK(!g.Evaluate(Cohort(2,7,6,8,.4f,5),true,3*Ms).hold&&!g.Owed());
    // The last shell: native skips the pump, so nothing is expected.
    PumpHoldGate z;z.Enable(true);CHECK(!z.Evaluate(Cohort(2,6,5,7,.3f,0),true,1*Ms).hold&&!z.Expected());
    // A reload during the recovery forgets the expectation.
    PumpHoldGate r;r.Enable(true);r.Evaluate(Cohort(2,6,5,7,.3f,5),true,1*Ms);CHECK(r.Expected());
    r.Evaluate(Cohort(2,10,6,11,.3f,5),true,2*Ms);CHECK(!r.Expected()&&!r.Rack());
    const auto s=g.Stats();CHECK(s.early==1&&s.racked==1&&s.owed==0&&s.advances==3);return 0;}
// Along the gun hand's aim axis, positive toward the shooter, whatever the
// hand's offset: identity aim (forward -Z), and yawed 90 degrees left.
int AlongFollowsTheAimAxis(){
    fvr::math::Pose aim;aim.position={0,-.25f,-.45f};
    CHECK(std::fabs(PumpAlong({-.2f,-.4f,-.3f},aim)-.15f)<1e-5f&&std::fabs(PumpAlong({-.2f,-.4f,-.2f},aim)-.25f)<1e-5f);
    aim.orientation={0,.70710678f,0,.70710678f}; // +90 deg about Y: forward -X, back +X
    CHECK(std::fabs(PumpAlong({.1f,-.25f,-.45f},aim)-.1f)<1e-4f);return 0;}
// Zone: forward of the gun hand along the aim axis and near the barrel line.
int ZoneIsTheForeEnd(){
    fvr::math::Pose aim;aim.position={0,-.25f,-.45f};
    CHECK(PumpInZone({0,-.27f,-.75f},aim));                  // 30 cm forward, 2 cm below
    CHECK(!PumpInZone({0,-.25f,-.42f},aim));                 // at the gun hand
    CHECK(!PumpInZone({0,-.25f,-.2f},aim));                  // behind it (toward the shooter)
    CHECK(!PumpInZone({-.3f,-.25f,-.75f},aim));              // 30 cm to the side (pouch/sling)
    CHECK(std::fabs(PumpLateral({-.3f,-.25f,-.75f},aim)-.3f)<1e-5f);
    aim.orientation={0,.70710678f,0,.70710678f};             // forward -X
    CHECK(PumpInZone({-.3f,-.25f,-.45f},aim)&&!PumpInZone({0,-.25f,-.75f},aim));
    // OpenXR convention (forward -Z), origin at the right grip: a left grip
    // 35 cm forward and 2 cm below the gun hand is on the fore-end; the
    // regression tape's left grip (0.15 m behind, 0.25 m off-axis) is not.
    const fvr::math::Pose grip{{.1f,-.3f,-.3f},{0,0,0,1}};
    CHECK(PumpInZone({.1f,-.32f,-.65f},grip)&&std::fabs(PumpAlong({.1f,-.32f,-.65f},grip)+.35f)<1e-5f);
    const fvr::math::Pose tapeGrip{{0,-.25f,-.45f},{0,0,0,1}};
    CHECK(!PumpInZone({-.2f,-.4f,-.3f},tapeGrip));return 0;}
int FullPositionContinuity(){
    using fvr::math::Vec3;
    constexpr std::int64_t Ms=1000000;
    PumpStroke p;
    p.Update(true,Vec3{0,0,0},11*Ms);
    CHECK(p.Update(true,Vec3{0,0,.058f},22*Ms).rearStop);
    // The scalar bore projection is unchanged, but this is a tracking jump.
    const auto jump=p.Update(true,Vec3{.4f,0,.058f},33*Ms);
    CHECK(!jump.rearStop&&!jump.completed&&p.Phase()==PumpStrokePhase::Idle);
    CHECK(!p.Update(true,Vec3{.4f,0,.058f},44*Ms).completed);
    CHECK(!p.Update(true,Vec3{.4f,0,0},55*Ms).completed);
    CHECK(p.Update(true,Vec3{.4f,0,.058f},66*Ms).rearStop);
    CHECK(p.Update(true,Vec3{.4f,0,.002f},77*Ms).completed);
    // Changed 3D position on a repeated packet cannot borrow its rear latch.
    PumpStroke duplicate;duplicate.Update(true,Vec3{0,0,0},11*Ms);
    CHECK(duplicate.Update(true,Vec3{0,0,.058f},22*Ms).rearStop);
    CHECK(!duplicate.Update(true,Vec3{0,.01f,.058f},22*Ms).completed);
    CHECK(duplicate.Phase()==PumpStrokePhase::Idle);
    for(const fvr::math::Pose gun:{fvr::math::Pose{{.1f,-.3f,-.3f},{0,0,0,1}},
        fvr::math::Pose{{.1f,-.3f,-.3f},{0,.70710678f,0,.70710678f}}}){
        const Vec3 support{-.2f,-.32f,-.65f};const auto local=PumpPosition(support,gun);
        CHECK(local&&std::abs(local->z-PumpAlong(support,gun))<1e-5f);
        auto shiftedGun=gun;shiftedGun.position.x+=2;shiftedGun.position.y+=1;shiftedGun.position.z-=3;
        const auto moved=PumpPosition({support.x+2,support.y+1,support.z-3},shiftedGun);
        CHECK(moved&&std::hypot(local->x-moved->x,local->y-moved->y,local->z-moved->z)<1e-5f);
    }
    return 0;
}
PumpPresentationOwner PresentationOwner(){
    PumpPresentationOwner owner;owner.weapon={1,2,3,4};owner.hand={1,2,5,6};
    owner.equipment.weapon=3;owner.equipment.data=7;owner.equipment.persistence=8;
    return owner;
}
int PresentationCannotCarryStrokeAcrossOwners(){
    PumpPresentationState presentation;auto owner=PresentationOwner();
    CHECK(presentation.Observe(owner));
    presentation.stroke.Update(true,0.f,11*Ms);
    CHECK(presentation.stroke.Update(true,.058f,22*Ms).rearStop);
    presentation.zoneHeld=presentation.handLatched=true;presentation.travel=1;presentation.settleUntilNs=1000*Ms;
    CHECK(!presentation.Observe(owner)&&presentation.stroke.Phase()==PumpStrokePhase::Forward);
    // The same physical SPAS reselected under a new equipment or space epoch
    // needs a new complete stroke. No reset submits/completes a native cycle.
    const auto reset=[&](const PumpPresentationOwner& next){
        return presentation.Observe(next)&&presentation.stroke.Phase()==PumpStrokePhase::Idle&&
            !presentation.zoneHeld&&!presentation.handLatched&&presentation.travel==0&&!presentation.settleUntilNs;
    };
    auto next=owner;++next.hand.equipGeneration;CHECK(reset(next));
    CHECK(!presentation.stroke.Update(true,.002f,33*Ms).completed);
    owner=next;++next.hand.space;CHECK(reset(next));
    owner=next;++next.equipment.data;CHECK(reset(next));
    owner=next;++next.weapon.weaponGeneration;CHECK(reset(next));
    owner=next;++next.weapon.weapon;CHECK(reset(next));
    presentation.Reset();CHECK(!presentation.Matches(next)&&presentation.stroke.Phase()==PumpStrokePhase::Idle);
    return 0;
}
int PendingCommandsStayWithTheirPhysicalWeapon(){
    PumpCommandQueue commands;auto spas=PresentationOwner(),other=spas;++other.weapon.weapon;
    CHECK(commands.CanSubmit(spas.weapon)&&commands.Remember(101,spas));
    CHECK(commands.PendingFor(spas.weapon)&&!commands.CanSubmit(spas.weapon));
    CHECK(!commands.PendingFor(other.weapon)&&commands.CanSubmit(other.weapon));
    CHECK(commands.Remember(102,other));
    // Resetting/transferring presentation does not discard a pending receipt.
    PumpPresentationState presentation;presentation.Observe(spas);presentation.Reset();presentation.Observe(other);
    CHECK(commands.PendingFor(spas.weapon)&&commands.PendingFor(other.weapon));
    auto reselected=spas;++reselected.hand.equipGeneration;
    CHECK(commands.PendingFor(reselected.weapon)&&commands.Entries()[0].owner!=reselected);
    commands.Consumed(101);CHECK(!commands.PendingFor(spas.weapon)&&commands.PendingFor(other.weapon));
    auto replacement=other;++replacement.weapon.weaponGeneration;
    CHECK(!commands.PendingFor(replacement.weapon)&&commands.CanSubmit(replacement.weapon));
    commands.Consumed(102);CHECK(!commands.PendingFor(other.weapon));
    return 0;
}
int RearLatchIsNotForwardMotion(){
    PumpStroke stroke;stroke.Update(true,0.f,11*Ms);
    const auto rear=stroke.Update(true,.058f,22*Ms);
    CHECK(rear.rearStop&&rear.phase==PumpStrokePhase::Forward&&!rear.returning);
    CHECK(stroke.PoseFraction(rear.travel)==1);
    CHECK(!stroke.Update(true,.058f,33*Ms).returning);
    CHECK(!stroke.Update(true,.0575f,44*Ms).returning); // sub-millimetre noise at rear
    CHECK(stroke.Update(true,.055f,55*Ms).returning);
    CHECK(stroke.Update(true,.057f,66*Ms).returning); // return branch cannot chatter
    CHECK(stroke.Update(true,.002f,77*Ms).completed);
    CHECK(stroke.PoseFraction(.05f)==0&&stroke.PoseFraction(.95f)==1);
    stroke.Reset();CHECK(!stroke.Returning());
    return 0;
}
int OnlyOwedOrFeedablePumpsAreExpected(){
    using namespace fvr::interaction;
    PhysicalWeaponState state;const auto owner=PresentationOwner().weapon;
    CHECK(state.Initialize(owner,{PhysicalWeaponFamily::TubePump,6,6},1,17,true,true));
    CHECK(!PhysicalPumpExpected(state.Snapshot())); // ready chamber: no free pumping
    CHECK(state.ReconcileAcceptedShots(owner,1,0,17).Applied());
    CHECK(PhysicalPumpExpected(state.Snapshot())); // one real pump after last shot
    PumpPresentationState presentation;presentation.Observe(PresentationOwner());presentation.Reset();
    CHECK(PhysicalPumpExpected(state.Snapshot())); // presentation reset cannot settle owed state
    const auto command=[&](PhysicalWeaponCommandKind kind){return PhysicalWeaponCommand{owner,state.NextCommandId(),kind};};
    CHECK(state.Apply(command(PhysicalWeaponCommandKind::CycleAction)).Applied());
    CHECK(!state.Snapshot().pumpOwed&&state.Snapshot().needsRack&&!state.Snapshot().feed);
    CHECK(!PhysicalPumpExpected(state.Snapshot())); // empty needsRack is not endless dry pumping
    CHECK(state.Apply(command(PhysicalWeaponCommandKind::InsertRound)).Applied());
    CHECK(PhysicalPumpExpected(state.Snapshot())); // a newly loaded tube needs chambering
    CHECK(!PhysicalPumpExpected(state.Snapshot(),true));
    CHECK(state.Apply(command(PhysicalWeaponCommandKind::CycleAction)).Applied());
    CHECK(state.Snapshot().chamber==1&&!PhysicalPumpExpected(state.Snapshot()));
    CHECK(state.Conserved());
    return 0;
}
int main(){
    if(HoldsLateAndReleasesOnRack()||ReleasesWithoutRack()||StrokeNeedsRearThenForward()||EarlyRackDuringShotRecovery()||AlongFollowsTheAimAxis()||ZoneIsTheForeEnd()||FullPositionContinuity()||
       PresentationCannotCarryStrokeAcrossOwners()||PendingCommandsStayWithTheirPhysicalWeapon()||RearLatchIsNotForwardMotion()||OnlyOwedOrFeedablePumpsAreExpected())return 1;
    std::puts("Bc2PumpHold: SPAS pump gate (owed/held/racked/released) and KF2 pump stroke passed; native hold and headset unverified.");return 0;
}
