#include "fvr/interaction/ChargingHandleRack.h"
#include "fvr/interaction/PistolSlide.h"
#include "fvr/interaction/PhysicalWeaponState.h"
#include "Test.h"
#include <cstdio>
#include <limits>
using namespace fvr;using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
ChargingHandleProfile Profile(){ChargingHandleProfile p;p.id=1;p.revision=1;p.handleMeters={0,.05f,-.1f};p.pullDirection={0,0,-1};
 p.strokeMeters=.06f;p.grabRadiusMeters=.04f;p.rearToleranceMeters=.006f;p.lateralReleaseMeters=.05f;return p;}
struct Driver {
 ChargingHandleRack rack{Profile()};std::uint64_t sequence=0;std::int64_t now=1000*Ms;
 ChargingHandleResult Send(bool grip,float z,float x=0,std::int64_t dt=11*Ms,bool tracked=true){
  ChargingHandleSample s;s.sequence=++sequence;now+=dt;s.observedNs=now;s.grip=grip;s.tracked=tracked;
  for(unsigned i=0;i<4;++i)s.weaponFromHand.values[i][i]=1;s.weaponFromHand.values[3]={x,.05f,z,1};return rack.Update(s);}
};
int ProfileValidation(){
 CHECK(ValidChargingHandleProfile(Profile()));CHECK(!ChargingHandleRack{}.Valid());
 auto p=Profile();p.pullDirection={0,0,-2};CHECK(!ValidChargingHandleProfile(p));
 p=Profile();p.strokeMeters=0;CHECK(!ValidChargingHandleProfile(p));
 p=Profile();p.rearToleranceMeters=p.strokeMeters;CHECK(!ValidChargingHandleProfile(p));
 p=Profile();p.handleMeters[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!ValidChargingHandleProfile(p));
 p=Profile();p.motion.maxGapNs=0;CHECK(!ValidChargingHandleProfile(p));
 p=Profile();p.id=0;CHECK(!ValidChargingHandleProfile(p));
 Driver bad;bad.rack=ChargingHandleRack{};auto r=bad.Send(true,-.1f);CHECK(r.reason==ChargingHandleReason::InvalidProfile&&!r.racked);
 return 0;
}
int FullStrokeRacksOnce(){
 Driver d;CHECK(!d.Send(false,-.1f).grabbed); // neutral first
 auto r=d.Send(true,-.1f);CHECK(r.grabbed&&r.phase==ChargingHandlePhase::Grabbed);
 for(float z=-.11f;z>-.15f;z-=.01f){r=d.Send(true,z);CHECK(!r.racked&&!r.cancelled);}
 CHECK(r.travel>.5f&&r.travel<1);
 r=d.Send(true,-.155f);CHECK(r.racked&&r.phase==ChargingHandlePhase::Idle&&r.travel==1);
 // Holding at the rear does not rack again; a new stroke needs a new grip edge.
 r=d.Send(true,-.16f);CHECK(!r.racked&&!r.grabbed);r=d.Send(true,-.1f);CHECK(!r.grabbed);
 r=d.Send(false,-.1f);r=d.Send(true,-.1f);CHECK(r.grabbed);
 return 0;
}
int GripEdgeMustStartAtHandle(){
 Driver d;d.Send(false,-.3f);
 auto r=d.Send(true,-.3f);CHECK(!r.grabbed); // grip far away
 r=d.Send(true,-.1f);CHECK(!r.grabbed);      // sliding a closed fist onto the handle is not a grab
 r=d.Send(false,-.1f);r=d.Send(true,-.1f);CHECK(r.grabbed);
 return 0;
}
int EarlyReleaseOffAxisAndJumpsCancel(){
 Driver d;d.Send(false,-.1f);d.Send(true,-.1f);auto r=d.Send(true,-.13f);
 r=d.Send(false,-.13f);CHECK(r.cancelled&&r.reason==ChargingHandleReason::Released&&!r.racked);
 Driver off;off.Send(false,-.1f);off.Send(true,-.1f);r=off.Send(true,-.12f,.03f);CHECK(!r.cancelled);
 r=off.Send(true,-.13f,.06f);CHECK(r.cancelled&&r.reason==ChargingHandleReason::OffAxis);
 Driver jump;jump.Send(false,-.1f);jump.Send(true,-.1f);
 r=jump.Send(true,-.3f);CHECK(r.cancelled&&r.reason==ChargingHandleReason::MotionRejected&&!r.racked); // 20 cm in 11 ms
 Driver gap;gap.Send(false,-.1f);gap.Send(true,-.1f);
 r=gap.Send(true,-.12f,0,151*Ms);CHECK(r.cancelled&&r.reason==ChargingHandleReason::MotionRejected);
 Driver lost;lost.Send(false,-.1f);lost.Send(true,-.1f);
 r=lost.Send(true,-.12f,0,11*Ms,false);CHECK(r.cancelled&&r.reason==ChargingHandleReason::TrackingLost);
 return 0;
}
int DuplicatesCannotAdvance(){
 Driver d;d.Send(false,-.1f);d.Send(true,-.1f);
 ChargingHandleSample s;s.sequence=d.sequence;s.observedNs=d.now+5*Ms;s.grip=true;s.tracked=true;
 for(unsigned i=0;i<4;++i)s.weaponFromHand.values[i][i]=1;s.weaponFromHand.values[3]={0,.05f,-.2f,1};
 const auto r=d.rack.Update(s);CHECK(!r.racked&&!r.cancelled&&r.phase==ChargingHandlePhase::Grabbed);
 return 0;
}
}
// Adapter aborts keep their reason and report a grabbed stroke as cancelled.
int AbortCarriesReason(){
 Driver d;d.Send(false,-.1f);CHECK(d.Send(true,-.1f).grabbed&&d.rack.Phase()==ChargingHandlePhase::Grabbed);
 const auto r=d.rack.Abort(ChargingHandleReason::HandBusy);
 CHECK(r.cancelled&&r.reason==ChargingHandleReason::HandBusy&&d.rack.Phase()==ChargingHandlePhase::Idle);
 const auto idle=d.rack.Abort(ChargingHandleReason::NotOwed);CHECK(!idle.cancelled&&idle.reason==ChargingHandleReason::NotOwed);
 return 0;}
int PistolSlideAuthoredSequence(){
 const PhysicalWeaponIdentity owner{1,2,3,4};
 PhysicalWeaponState state;CHECK(state.Initialize(owner,{PhysicalWeaponFamily::ClosedBoltMagazine,15,16,true},3,30,true,true));
 PistolSlide slide{Profile()};std::uint64_t seq=0;std::int64_t now=Ms;
 const auto send=[&](bool grip,float z,bool tracked=true){
  ChargingHandleSample s;s.sequence=++seq;now+=11*Ms;s.observedNs=now;s.grip=grip;s.tracked=tracked;
  for(unsigned i=0;i<4;++i)s.weaponFromHand.values[i][i]=1;s.weaponFromHand.values[3]={0,.05f,z,1};
  return slide.Update(s,state.Snapshot().actionLocked);};
 const auto commit=[&](PhysicalWeaponCommandKind k){return state.Apply({owner,state.NextCommandId(),k}).Applied();};
 send(false,-.1f);CHECK(send(true,-.1f).grabbed);
 auto r=send(true,-.13f);CHECK(r.held&&!r.open&&!r.close);
 r=send(true,-.155f);CHECK(r.open&&r.held&&!r.close);CHECK(commit(PhysicalWeaponCommandKind::OpenAction));
 CHECK(!state.CanFire()&&state.Snapshot().chamber==0&&state.Snapshot().feed==2&&state.Snapshot().discarded==1);
 for(unsigned i=0;i<8;++i){r=send(true,-.16f);CHECK(r.held&&!r.open&&!r.close);}
 CHECK(state.Snapshot().feed==2); // Holding rear never feeds.
 r=send(false,-.16f);CHECK(r.close&&!r.held);CHECK(commit(PhysicalWeaponCommandKind::CloseAction));
 CHECK(state.CanFire()&&state.Snapshot().feed==1&&state.Snapshot().chamber==1&&state.Conserved());
 CHECK(state.ReconcileAcceptedShots(owner,1,0,30).Applied());CHECK(state.Snapshot().actionLocked);
 // Locked-open slide must be acquired at its actual rearward position.
 send(false,-.16f);r=send(true,-.16f);CHECK(r.grabbed&&r.open&&r.travel==1);
 CHECK(commit(PhysicalWeaponCommandKind::OpenAction));r=send(false,-.16f);CHECK(r.close);
 CHECK(commit(PhysicalWeaponCommandKind::CloseAction));CHECK(state.Snapshot().actionLocked&&!state.CanFire());
 // A short pull/release cannot eject a live round; a tracking interruption
 // after opening requests spring return without granting chamber readiness.
 slide.Reset();PhysicalWeaponState fresh;CHECK(fresh.Initialize(owner,{PhysicalWeaponFamily::ClosedBoltMagazine,15,16,true},3,0,true,true));state=fresh;
 send(false,-.1f);send(true,-.1f);send(true,-.12f);r=send(false,-.12f);CHECK(!r.open&&!r.close&&state.Snapshot().loaded==3);
 send(false,-.1f);send(true,-.1f);send(true,-.13f);r=send(true,-.155f);CHECK(r.open);CHECK(commit(PhysicalWeaponCommandKind::OpenAction));
 r=send(true,-.155f,false);CHECK(r.cancelled&&r.close&&!state.CanFire());
 CHECK(commit(PhysicalWeaponCommandKind::CloseAction));CHECK(state.Conserved()&&state.CanFire());
 return 0;
}
int main(){
 if(ProfileValidation()||FullStrokeRacksOnce()||GripEdgeMustStartAtHandle()||EarlyReleaseOffAxisAndJumpsCancel()||DuplicatesCannotAdvance()||AbortCarriesReason()||PistolSlideAuthoredSequence())return 1;
 std::puts("ChargingHandleRack: 7 groups passed.");return 0;
}
