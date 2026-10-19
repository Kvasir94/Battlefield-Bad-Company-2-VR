#include "Bc2MagazinePhysicalProbe.h"
#include "Bc2PhysicalReloadProbe.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
std::optional<math::Matrix4> Controller(const math::Pose& p)noexcept {const auto v=math::MakeLhViewFromOpenXRPose(p);return v?InverseRigid(*v):std::nullopt;}
// Identity recenter reference and a head that has not moved since the first
// frame (a constant pitch lets image runs see a low magazine well).
bool Static(const InputFrame& s,const math::Pose& head)noexcept {const auto a=Controller(s.referenceHead),b=Controller(s.head),h=Controller(head);return a&&b&&h&&
 Distance(*a,Identity())<.0001f&&Angle(*a,Identity())<.001f&&Distance(*b,*h)<.0001f&&Angle(*b,*h)<.001f;}
math::Pose Step(const math::Pose& from,const math::Pose& target)noexcept {
 auto out=target;const float d=std::hypot(target.position.x-from.position.x,target.position.y-from.position.y,target.position.z-from.position.z);
 const float t=d>.01f?.01f/d:1;out.position={from.position.x+(target.position.x-from.position.x)*t,
 from.position.y+(target.position.y-from.position.y)*t,from.position.z+(target.position.z-from.position.z)*t};
 auto q=target.orientation;const auto a=from.orientation;float dot=a.x*q.x+a.y*q.y+a.z*q.z+a.w*q.w;
 if(dot<0){q={-q.x,-q.y,-q.z,-q.w};dot=-dot;}
 // 0.12 rad per raw sample (consumer limit 1.05): the 135 deg start turn took
 // the whole 4.5 s approach budget at 0.05 rad (run 071440).
 const float angle=2*std::acos(std::clamp(dot,0.f,1.f)),u=angle>.12f?.12f/angle:1;
 q={a.x+(q.x-a.x)*u,a.y+(q.y-a.y)*u,a.z+(q.z-a.z)*u,a.w+(q.w-a.w)*u};
 const float n=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);out.orientation={q.x/n,q.y/n,q.z/n,q.w/n};return out;
}
bool Fresh(const Bc2AmmoReserveLease& r,const ReloadStateOwner& owner,std::int64_t now)noexcept {
 return r.verified&&r.identity.owner==owner&&r.observedNs>0&&r.observedNs<=now&&r.deadlineNs>now&&r.deadlineNs-r.observedNs<=200000000;
}
}
void Bc2MagazinePhysicalProbe::PhaseTo(Phase p,std::int64_t now)noexcept {
 phase_=p;phaseAt_=now;alignedAt_=0;
 if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(p),failure_,now};else ++dropped_;
}
void Bc2MagazinePhysicalProbe::Fail(unsigned why,std::int64_t now)noexcept {if(CancelConsumer())return;failure_=why;PhaseTo(Phase::Failed,now);}
void Bc2MagazinePhysicalProbe::Prepare(InputFrame& in,const ReloadStateOwner& owner,std::string_view asset,const MagazineRawContact& raw,
 const MagazinePhysicalProbeState& state,std::int64_t observed,std::int64_t deadline,std::int64_t now)noexcept {
 if(!enabled_)return;
 if(Skipped())return; // unsupported weapon: the input passes through untouched
 if(CancelConsumer()){in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=0;return;}
 if(!first_){first_=phaseAt_=now;owner_=owner;command_=in.hands[0].grip;baseline_=packs_;asset_=std::string(asset);head_=in.head;
  const auto support=MagazineSupportFor(asset);support_=support.status;
  if(support.status==MagazineSupportStatus::Supported&&support.profile&&support.profile->geometry)geometry_=support.profile->geometry;
  else {PhaseTo(Phase::Skipped,now);return;}
  if(rackLeg_)handle_=FindChargingHandle(asset);}
 if(now<lastNow_||now-first_>=30000000000ll)Fail(1,now);lastNow_=now;
 if(owner!=owner_||asset!=asset_)Fail(2,now);
 // One stale or late gather (a >100 ms stall) is common live; leave that frame
 // untouched and fail only on a sustained run (run 081350 failed on one).
 const bool frameValid=ValidInput(in)&&in.focused&&in.headValid&&Static(in,head_)&&in.hands[0].gripTracked&&in.hands[1].gripTracked&&
  in.hands[1].aimTracked&&observed>0&&observed<=now&&deadline>now&&deadline-observed<=100000000;
 // Still pose the hand: returning the receiver's static left pose for one
 // frame is a 0.3 m jump (run 085012 cancelled pose_jump).
 if(!frameValid){++invalidFrames_;if(++invalidRun_>5)Fail(2,now);
  in.hands[0].grip=command_;in.hands[0].aim=command_;if(lastInput_)in.hands[0].squeeze=lastInput_->hands[0].squeeze;return;}
 invalidRun_=0;
 if(lastInput_&&in.generation<=lastInput_->generation){
  if(in.generation<lastInput_->generation)Fail(3,now);
  in.hands[0].grip=lastInput_->hands[0].grip;in.hands[0].aim=lastInput_->hands[0].aim;
  in.hands[0].squeeze=CancelConsumer()?0:lastInput_->hands[0].squeeze;return;
 }
 const bool authority=state.physicalState&&state.physicalState->initialized;
 const bool mechanicsHeld=authority?state.active:state.nativeHolding;
 const int loaded=authority?int(state.physicalState->loaded):state.reserve?state.reserve->loaded:-1;
 const int reserve=authority?int(state.physicalState->looseReserve):state.reserve?state.reserve->reserve:-1;
 const int capacity=authority?int(state.physicalState->config.feedCapacity):state.reserve?state.reserve->capacity:-1;
 if(state.submitted>1||state.completed>1||state.completed>state.submitted||
    (originalReturn_&&!authority&&(state.submitted||state.completed||state.acquired)))Fail(4,now);
 // Before anything is held, a cancel that re-arms (ResetUnstarted) is not a
 // failure: hold still and resume once the consumer is Attached again.
 const bool waitRearm=state.phase==DetachableMagazinePhase::Cancelled&&(phase_==Phase::ApproachMagazine||phase_==Phase::Grip)&&
  !state.submitted&&!state.acquired;
 if(waitRearm&&!rearmWaitAt_)rearmWaitAt_=now;else if(!waitRearm)rearmWaitAt_=0;
 if(waitRearm&&now-rearmWaitAt_>=5000000000ll)Fail(5,now);
 // A client copy re-synced by the engine at begin (11->2->10->11, run 134755)
 // reads Cancelled for a frame or two; only a Cancelled lasting 50 ms fails.
 else if(state.phase==DetachableMagazinePhase::Cancelled&&phase_!=Phase::Warmup&&!waitRearm){
  if(!cancelSeenAt_){cancelSeenAt_=now;++transientCancels_;}
  if(now-cancelSeenAt_>=50000000ll)Fail(5,now);
 }
 if(state.phase!=DetachableMagazinePhase::Cancelled)cancelSeenAt_=0;
 if(waitRearm){++rearmWaits_;if(phase_==Phase::Grip)PhaseTo(Phase::ApproachMagazine,now);phaseAt_=now; // approach timeout restarts on resume
  in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=0;return;}
 const Input* original=nullptr;
 if(raw.valid&&raw.owner==owner&&raw.rigFingerprint==geometry_->rigFingerprint)
  for(const auto& h:history_)if(h&&h->owner==owner&&h->frame.generation==raw.inputEvidence.sequence&&h->observed==raw.inputEvidence.observedNs&&
   h->deadline==raw.inputEvidence.deadlineNs&&h->deadline>now){original=&*h;break;}
 const bool newRaw=original&&raw.inputEvidence.sequence>lastRaw_;
 if(phase_==Phase::Warmup&&now-first_>=6000000000ll){
  // Opt-in preparation: a full magazine cannot be reloaded, so hold the real
  // right trigger (input only) until the reader sees a partial magazine, then
  // release it for 500 ms before approaching. Never touches counts.
  // The trigger itself is applied by Gameplay before Fire is derived (NoteShot).
  if((lastShotAt_&&now-lastShotAt_<500000000ll)||!DryFireComplete()){
   // Burst or dry pulls in progress, or settling after release.
  }else if(state.phase!=DetachableMagazinePhase::Cancelled&&(authority||(state.reserve&&Fresh(*state.reserve,owner,now)&&state.reserve->reloadInputReady))&&
   (loaded>0||(dryPulls_&&loaded==0))&&loaded<capacity&&reserve>0&&newRaw&&raw.nativeMagazineAttached){
   loadedBefore_=loaded;reserveBefore_=reserve;capacityBefore_=capacity;
   physicalAuthority_=authority;
   if(authority){const auto& p=*state.physicalState;physicalIdentity_=p.identity;chamberBefore_=p.chamber;
    admittedBefore_=p.admitted;spentBefore_=p.spent;discardedBefore_=p.discarded;}
   expectedUnits_=originalReturn_?0:std::min(authority?capacityBefore_:capacityBefore_-loadedBefore_,reserveBefore_);
   PhaseTo(eject_?Phase::Eject:Phase::ApproachMagazine,now);
  }else if(now-first_>=(prepareShots_?16000000000ll:10000000000ll))Fail(6,now);
 }
 if(forcedCancel_&&!forcedDone_&&(phase_==Phase::ApproachMagazine||phase_==Phase::Grip)){
  const bool begun=state.acquired||state.phase==DetachableMagazinePhase::PreparingRemoval||state.phase==DetachableMagazinePhase::Pulling;
  if(forcedCancel_==2&&begun&&!state.nativeHolding){forcedDone_=true;forcedAt_=now;PhaseTo(Phase::ForcedCancel,now);}
  else if(forcedCancel_==1&&state.nativeHolding){forcedDone_=true;forcedAt_=now;PhaseTo(Phase::ForcedCancel,now);}
  else if(forcedCancel_==3&&state.nativeHolding){forcedDone_=true;forcedAt_=now;untrackUntil_=now+300000000ll;}
  else if(forcedCancel_==4&&begun&&!state.nativeHolding){forcedDone_=true;forcedAt_=now;untrackUntil_=now+300000000ll;}
  else if(forcedCancel_==5&&state.nativeHolding){forcedDone_=true;forcedAt_=now;headLostUntil_=now+1000000000ll;}
 }
 if(headLostUntil_&&now<headLostUntil_){in.headValid=false;in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=0;return;}
 if(untrackUntil_&&now<untrackUntil_){in.hands[0].gripTracked=in.hands[0].aimTracked=false;in.hands[0].squeeze=0;return;}
 float along=.1f;bool solve=false;
 if(phase_==Phase::ApproachMagazine||phase_==Phase::Grip){along=.1f;solve=true;}
 if(phase_==Phase::Pull){along=.1f-.12f*std::clamp(float(now-phaseAt_)/900000000.f,0.f,1.f);solve=true;}
 if(phase_==Phase::Release){along=-.02f;solve=true;}
 if(phase_==Phase::ApproachRail){along=-.11f;solve=true;}
 if(phase_==Phase::Enter){along=-.025f;solve=true;}
 if(phase_==Phase::Stroke){along=-.025f+.125f*std::clamp(float(now-phaseAt_)/1200000000.f,0.f,1.f);solve=true;}
 if(phase_==Phase::SettleRelease||(phase_==Phase::WaitAck&&releasedMidStroke_)){along=releaseAlong_;solve=true;} // Hand let go and stays put.
 else if(phase_==Phase::WaitAck){along=.1f;solve=true;}
 if(solve&&newRaw&&!CancelConsumer()){
  const auto p=geometry_->interaction.insertion;
  const auto desired=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,along),Multiply(p.weaponFromEntry,raw.weaponWorldMeters))));
  if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
   command_=Step(command_,*control);lastRaw_=raw.inputEvidence.sequence;
   const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
   if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
   const bool settled=alignedAt_&&now-alignedAt_>=100000000;
   if(phase_==Phase::ApproachMagazine&&settled&&original->frame.hands[0].squeeze<=.35f)PhaseTo(Phase::Grip,now);
   else if(phase_==Phase::Grip&&mechanicsHeld&&state.phase==DetachableMagazinePhase::Pulling){if(flickLeg_&&!authority&&!flickAt_)flickAt_=now;PhaseTo(Phase::Pull,now);}
   else if(phase_==Phase::Pull&&state.phase==DetachableMagazinePhase::RemovedHeld&&settled&&removedPair_){
    if(flickLeg_&&authority&&!flickAt_)flickAt_=now;
    PhaseTo(originalReturn_?Phase::ApproachRail:(carryChallenge_?Phase::Carry:Phase::Release),now);}
   else if(phase_==Phase::Release&&state.phase==DetachableMagazinePhase::WellEmpty&&!state.ownsHand&&hiddenPair_&&
    original->frame.hands[0].squeeze<=.35f&&now-phaseAt_>=700000000)PhaseTo(Phase::Pouch,now);
   else if(phase_==Phase::ApproachRail&&settled)PhaseTo(Phase::Enter,now);
   else if(phase_==Phase::Stroke&&releaseMidStroke_&&state.phase==DetachableMagazinePhase::Guided&&along>=.055f){
    releaseAlong_=along;releasedMidStroke_=true;PhaseTo(Phase::SettleRelease,now);}
   else if(phase_==Phase::Enter&&settled&&(originalReturn_&&!authority?(state.phase==DetachableMagazinePhase::RemovedHeld&&removedPair_):(state.phase==DetachableMagazinePhase::Guided&&replacementPair_)))PhaseTo(Phase::Stroke,now);
   if(now-rowAt_>=100000000){rowAt_=now;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase_),0,now,in.generation,
    raw.inputEvidence.sequence,state.cycle,position,angle,along,state.reserve?state.reserve->loaded:-1,state.reserve?state.reserve->reserve:-1,packs_.pairs};else ++dropped_;}
  }else Fail(7,now);
 }
 if(phase_==Phase::Carry&&!CancelConsumer()){
  const unsigned missing=(!state.active?1u:0u)|(!state.ownsHand?2u:0u)|
   (state.phase!=DetachableMagazinePhase::RemovedHeld?4u:0u)|(!removedPair_?8u:0u)|(!mechanicsHeld?16u:0u);
  if(mechanicsHeld)carryWaitAt_=0;
  if(missing&15u){carryFailureFlags_=missing;carryFailureAt_=now;Fail(17,now);}
  else if(!mechanicsHeld){
   // Prepare precedes consumer Tick: its previous positive lease may expire
   // between packets. Keep only the existing command while real Tick rereads.
   if(!carryWaitAt_)carryWaitAt_=now;++carryWaits_;
   if(now-carryWaitAt_>=50000000ll){carryFailureFlags_=missing|32u;carryFailureAt_=now;Fail(20,now);}
  }else if(newRaw){
   carryWaitAt_=0;
   if(!carryStart_)carryStart_=raw.rawLeftWristWorldMeters;
   auto desired=*carryStart_;
   if(!carryReturning_){
    auto rotation=Identity();const float c=std::cos(.65f),s=std::sin(.65f);
    rotation.values[0][0]=rotation.values[2][2]=c;rotation.values[0][2]=-s;rotation.values[2][0]=s;
    desired=Multiply(rotation,*carryStart_);
    desired.values[3]=carryStart_->values[3];desired.values[3][0]+=.12f;desired.values[3][1]+=.04f;
   }
   if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
    // Deliberate fresh off-rail command, rather than the rail's 1cm smoother.
    command_=*control;lastRaw_=raw.inputEvidence.sequence;++carrySamples_;
    if(carryPrevious_){carryStepTranslation_=std::max(carryStepTranslation_,Distance(raw.rawLeftWristWorldMeters,*carryPrevious_));
     carryStepAngle_=std::max(carryStepAngle_,Angle(raw.rawLeftWristWorldMeters,*carryPrevious_));}
    carryPrevious_=raw.rawLeftWristWorldMeters;
    carryDisplacement_=std::max(carryDisplacement_,Distance(raw.rawLeftWristWorldMeters,*carryStart_));
    carryAngle_=std::max(carryAngle_,Angle(raw.rawLeftWristWorldMeters,*carryStart_));
    const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
    if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
    if(alignedAt_&&now-alignedAt_>=100000000){
     if(!carryReturning_&&carryDisplacement_>=.10f&&carryAngle_>=.5f){carryExercised_=true;carryReturning_=true;alignedAt_=0;}
     else if(carryReturning_)PhaseTo(Phase::Release,now);
    }
    if(now-rowAt_>=100000000){rowAt_=now;if(rowCount_<rows_.size())rows_[rowCount_++]={unsigned(phase_),0,now,in.generation,
     raw.inputEvidence.sequence,state.cycle,position,angle,0,state.reserve?state.reserve->loaded:-1,state.reserve?state.reserve->reserve:-1,packs_.pairs};else ++dropped_;}
   }else Fail(7,now);
  }
 }
 // Eject: the hand stays where it is; the well must empty without a hand unseat.
 if(phase_==Phase::Eject&&state.phase==DetachableMagazinePhase::WellEmpty&&!state.ownsHand&&!state.acquired&&now-phaseAt_>=300000000ll)PhaseTo(Phase::Pouch,now);
 if(phase_==Phase::Pouch||phase_==Phase::GrabReplacement){
  math::Pose pouch;const auto c=Bc2MagazinePhysicalReload::DefaultPouch();pouch.position={c.pouchCenterMeters[0],c.pouchCenterMeters[1],-c.pouchCenterMeters[2]};
  command_=Step(command_,pouch);
  if(phase_==Phase::Pouch&&newRaw&&original->frame.hands[0].squeeze<=.35f){
   const auto actual=PhysicalReloadPouchPose(original->frame),target=Controller(pouch);
   if(actual&&target&&Distance(*actual,*target)<.01f&&Angle(*actual,*target)<.05f){
    if(!alignedAt_)alignedAt_=now;if(now-alignedAt_>=100000000)PhaseTo(Phase::GrabReplacement,now);
   }else alignedAt_=0;
  }
  if(phase_==Phase::GrabReplacement&&state.acquired==1&&state.ownsHand&&state.phase==DetachableMagazinePhase::ReplacementHeld)PhaseTo(Phase::ApproachRail,now);
 }
 if((originalReturn_&&!authority?state.originalReturning:state.submitted==1)&&((phase_>=Phase::ApproachRail&&phase_<=Phase::Stroke)||phase_==Phase::SettleRelease))PhaseTo(Phase::WaitAck,now);
 if(phase_==Phase::WaitBaseline&&receiptVerified_&&!state.active&&!state.retiring&&!state.pending&&!state.blocksEquipment&&
  state.phase==DetachableMagazinePhase::Attached&&(removedPair_||eject_)&&(originalReturn_?originalReturns_==1:(hiddenPair_&&replacementPair_))){
  PhaseTo(rackLeg_&&(handle_||boltRelease_)?Phase::RackFire1:Phase::Done,now);
  if(returnThenReplace_&&!secondCycle_){
   // Restart only the synthetic driver. Never reset consumer/native counters,
   // ownership, ammo, or renderer receipts. The same exact owner and original
   // 30-second outer deadline cover both actual interactions.
   try {
    std::ostringstream evidence;Report(evidence);
    Bc2MagazinePhysicalProbe next(true,false,true);
    next.returnThenReplace_=true;next.secondCycle_=true;next.firstCycleReport_=evidence.str();
    next.first_=first_;next.phaseAt_=now;next.lastNow_=now;next.owner_=owner_;
    next.head_=head_;next.asset_=asset_;next.support_=support_;next.geometry_=geometry_;
    next.command_=command_;next.packs_=packs_;next.baseline_=packs_;
    *this=std::move(next);
   }catch(...){phase_=Phase::WaitBaseline;Fail(19,now);}
  }
 }
 // Rack leg: the palm (wrist through the magazine grasp) on the handle, then
 // along the pull direction. Orientation is the seated magazine's.
 if(phase_>=Phase::RackFire1&&phase_<=Phase::RackFire2&&!CancelConsumer()){
  const auto t=now-phaseAt_;
  if(phase_==Phase::RackFire1&&t>=600000000ll)PhaseTo(boltRelease_?Phase::BoltRelease:Phase::RackApproach,now);
  else if(phase_==Phase::RackFire2&&t>=600000000ll)PhaseTo(Phase::Done,now);
  else if(phase_!=Phase::RackFire1&&phase_!=Phase::RackFire2&&newRaw){
   const auto& h=*handle_;const auto p=geometry_->interaction.insertion;
   const float stroke=phase_==Phase::RackPull?std::clamp(float(t)/500000000.f,0.f,1.f)*(h.strokeMeters+.005f):(phase_==Phase::RackRelease?h.strokeMeters+.005f:0.f);
   auto item=Multiply(*InverseRigid(p.itemFromInsertion),Multiply(TravelPose(p,0),p.weaponFromEntry));
   for(unsigned a=0;a<3;++a)item.values[3][a]=h.handleMeters[a]+h.pullDirection[a]*stroke;
   // After the release the hand drops 12 cm below the handle line, so images
   // show the handle springing back instead of the back of the hand.
   if(phase_==Phase::RackRelease)item.values[3][1]-=std::clamp(float(t)/300000000.f,0.f,1.f)*.12f;
   const auto desired=Multiply(p.itemFromHand,Multiply(item,raw.weaponWorldMeters));
   if(const auto control=PhysicalReloadProbeController(raw,original->frame,desired)){
    command_=Step(command_,*control);lastRaw_=raw.inputEvidence.sequence;
    const float position=Distance(raw.rawLeftWristWorldMeters,desired),angle=Angle(raw.rawLeftWristWorldMeters,desired);
    if(position<.003f&&angle<.05f){if(!alignedAt_)alignedAt_=now;}else alignedAt_=0;
    const bool settled=alignedAt_&&now-alignedAt_>=100000000;
    if(phase_==Phase::RackApproach&&settled)PhaseTo(Phase::RackGrip,now);
    else if(phase_==Phase::RackGrip&&t>=300000000ll)PhaseTo(Phase::RackPull,now);
    else if(phase_==Phase::RackPull&&t>=600000000ll)PhaseTo(Phase::RackRelease,now);
    else if(phase_==Phase::RackRelease&&t>=600000000ll)PhaseTo(Phase::RackFire2,now);
   }else Fail(7,now);
  }
 }
 if(phase_==Phase::BoltRelease&&!CancelConsumer()&&now-phaseAt_>=600000000ll)PhaseTo(Phase::RackFire2,now);
 const auto elapsed=now-phaseAt_;
 if(phase_==Phase::RackApproach&&elapsed>4500000000ll)Fail(23,now);
 if((phase_==Phase::ApproachMagazine||phase_==Phase::Pouch||phase_==Phase::ApproachRail)&&elapsed>4500000000ll)Fail(8,now);
 // Grip may wait for the consumer's begin (deferred reads); give it 8 s (run 081216 began at 3.9 s).
 if(phase_==Phase::Grip&&elapsed>8000000000ll)Fail(9,now);
 if((phase_==Phase::WaitAck||phase_==Phase::WaitBaseline)&&elapsed>4000000000ll)Fail(9,now);
 if((phase_==Phase::Pull||phase_==Phase::Enter||phase_==Phase::Stroke)&&elapsed>3500000000ll)Fail(10,now);
 if((phase_==Phase::Release||phase_==Phase::GrabReplacement)&&elapsed>1500000000ll)Fail(11,now);
 if(phase_==Phase::Carry&&elapsed>1500000000ll)Fail(18,now);
 if(phase_==Phase::SettleRelease&&elapsed>1000000000ll)Fail(21,now); // Settle is <= 250 ms by design.
 if(phase_==Phase::Eject&&elapsed>3000000000ll)Fail(22,now);
 // If the consumer is still Attached (NeedNeutral) after 400 ms of grip,
 // open the hand for 150 ms of every 600 ms so it sees a neutral packet first.
 // Original-magazine grab only (never from GrabReplacement on), and only after
 // a real stall: run 073103 regripped 10 frames and the cycle then oscillated.
 const bool regrip=phase_==Phase::Grip&&state.phase==DetachableMagazinePhase::Attached&&!state.acquired&&!state.submitted&&
  elapsed>1500000000ll&&(elapsed-1500000000ll)%800000000ll<150000000ll;
 if(regrip)++regripFrames_;
 const bool squeezing=!regrip&&(phase_==Phase::Carry||phase_==Phase::RackGrip||phase_==Phase::RackPull||(phase_>=Phase::Grip&&phase_<=Phase::Pull)||
  (phase_>=Phase::GrabReplacement&&phase_<=Phase::WaitAck&&!(phase_==Phase::WaitAck&&releasedMidStroke_)));
 in.hands[0].grip=command_;in.hands[0].aim=command_;in.hands[0].squeeze=squeezing?(carryChallenge_&&(phase_==Phase::Pull||phase_==Phase::Carry)?.6f:1.f):0.f;
 lastInput_=in;history_[historyNext_]=Input{in,owner,observed,deadline};historyNext_=(historyNext_+1)%history_.size();
}
void Bc2MagazinePhysicalProbe::Observe(const MagazinePhysicalProbeState& s,const MagazinePackCounters& packs,std::int64_t now)noexcept {
 if(!enabled_||CancelConsumer())return;
 if(packs.copies<packs_.copies||packs.pairs<packs_.pairs||packs.fallbacks<packs_.fallbacks){Fail(12,now);return;}
 for(unsigned n=0;n<4;++n)if(packs.roleCopies[n]<packs_.roleCopies[n]||packs.rolePairs[n]<packs_.rolePairs[n]){Fail(12,now);return;}
 packs_=packs;submitted_=s.submitted;acquired_=s.acquired;
 if(s.physicalState){
  const auto& state=*s.physicalState;
  if(!state.initialized||state.live+state.spent+state.discarded!=state.admitted){Fail(13,now);return;}
  const auto pair=[&](unsigned role){if(!phasePairs_[role])phasePairs_[role]=packs.rolePairs[role];
   return packs.rolePairs[role]>*phasePairs_[role]&&packs.rolePairs[role]>baseline_.rolePairs[role];};
  if(s.phase==DetachableMagazinePhase::RemovedHeld&&(pair(1)||pair(2)))removedPair_=true;
  if(s.phase==DetachableMagazinePhase::WellEmpty&&pair(3))hiddenPair_=true;
  if((s.phase==DetachableMagazinePhase::Guided||s.phase==DetachableMagazinePhase::AwaitingSeat)&&pair(2))replacementPair_=true;
  if(s.completed>completed_){
   const int expectedLoaded=originalReturn_?loadedBefore_:expectedUnits_+chamberBefore_;
   if(!physicalAuthority_||state.identity!=physicalIdentity_||s.completed!=1||s.submitted!=1||
    (originalReturn_?s.acquired!=0:s.acquired!=1)||!state.insertedMagazine||
    int(state.loaded)!=expectedLoaded||int(state.looseReserve)!=reserveBefore_-expectedUnits_||
    state.admitted!=admittedBefore_||state.spent!=spentBefore_||state.discarded!=discardedBefore_){Fail(13,now);return;}
   completed_=s.completed;originalReturns_=originalReturn_?1u:0u;receiptVerified_=true;PhaseTo(Phase::WaitBaseline,now);
  }else if(s.completed<completed_||s.completed>s.submitted||s.submitted>1||s.acquired>1)Fail(4,now);
  return;
 }
 if(originalReturn_){
  if(s.submitted||s.completed||s.acquired||s.originalReturns>1||s.originalReturns<originalReturns_){Fail(4,now);return;}
  if(s.original){
   if(original_&&*original_!=*s.original){Fail(14,now);return;}
   if(!original_){original_=s.original;originalCycle_=s.cycle;}
  }
  if(s.reserve&&Fresh(*s.reserve,owner_,now)&&loadedBefore_>=0&&
    (s.reserve->loaded!=loadedBefore_||s.reserve->reserve!=reserveBefore_||s.reserve->capacity!=capacityBefore_)){Fail(15,now);return;}
  if(s.originalReturns>originalReturns_){
   const auto& receipt=s.originalReceipt;
   if(!original_||!receipt||!receipt->verified||receipt->original!=*original_||receipt->cycle!=originalCycle_||
     !receipt->seat||!receipt->retirement||receipt->observedNs<=0||receipt->observedNs>now||receipt->deadlineNs<=now||
     receipt->deadlineNs-receipt->observedNs>200000000||!removedPair_||(phase_!=Phase::WaitAck&&phase_!=Phase::Stroke)||
     !s.reserve||!Fresh(*s.reserve,owner_,now)||!s.reserve->reloadInputReady||
     s.reserve->loaded!=loadedBefore_||s.reserve->reserve!=reserveBefore_||s.reserve->capacity!=capacityBefore_){Fail(16,now);return;}
   originalReceipt_=receipt;originalReturns_=s.originalReturns;receiptVerified_=true;PhaseTo(Phase::WaitBaseline,now);
  }
 }
 const auto newPair=[&](unsigned role){if(!phasePairs_[role])phasePairs_[role]=packs.rolePairs[role];
  return packs.rolePairs[role]>*phasePairs_[role]&&packs.rolePairs[role]>baseline_.rolePairs[role];};
 if(s.phase==DetachableMagazinePhase::RemovedHeld&&newPair(1))removedPair_=true;
 if(s.phase==DetachableMagazinePhase::WellEmpty&&newPair(3))hiddenPair_=true;
 if((s.phase==DetachableMagazinePhase::Guided||s.phase==DetachableMagazinePhase::AwaitingSeat)&&newPair(2))replacementPair_=true;
 if(s.completed>completed_){
  if(s.completed!=1||s.submitted!=1||!s.reserve||!Fresh(*s.reserve,owner_,now)||expectedUnits_<=0||
   s.reserve->capacity!=capacityBefore_||s.reserve->loaded!=loadedBefore_+expectedUnits_||s.reserve->reserve!=reserveBefore_-expectedUnits_){Fail(13,now);return;}
  completed_=s.completed;receiptVerified_=true;PhaseTo(Phase::WaitBaseline,now);
 }else if(s.completed<completed_||s.completed>s.submitted||s.submitted>1||s.acquired>1)Fail(4,now);
}
void Bc2MagazinePhysicalProbe::Report(std::ostream& o)const {
 const auto precision=o.precision();o.precision(std::numeric_limits<float>::max_digits10);
 o<<"{\"enabled\":"<<(enabled_?"true":"false")<<",\"synthetic_input\":true,\"headset_verified\":false,\"eye_textures_verified\":false"
  <<",\"prepare_shots\":{\"enabled\":"<<(prepareShots_?"true":"false")<<",\"window_frames\":"<<prepWindow_<<",\"full_frames\":"<<prepFull_
  <<",\"pressed_frames\":"<<prepPressed_<<",\"bursts\":"<<preparedBursts_<<",\"dry_pulls\":"<<dryPulls_<<",\"dry_pressed_frames\":"<<dryPressed_
  <<",\"dry_done\":"<<(dryDone_?"true":"false")<<'}'<<",\"rearm_wait_frames\":"<<rearmWaits_<<",\"regrip_frames\":"<<regripFrames_
  <<",\"return_then_replace\":"<<(returnThenReplace_?"true":"false")<<",\"second_cycle\":"<<(secondCycle_?"true":"false")
  <<",\"first_cycle\":"<<(firstCycleReport_.empty()?"null":firstCycleReport_)
  <<",\"original_return_fixture\":"<<(originalReturn_?"true":"false")
  <<",\"asset\":\""<<asset_<<"\",\"magazine_support\":\""<<MagazineSupportName(support_)<<"\",\"skipped\":"<<(Skipped()?"true":"false")
  <<",\"transient_cancels\":"<<transientCancels_<<",\"rack_leg\":"<<(rackLeg_?"true":"false")<<",\"rack_handle\":"<<(handle_?"true":"false")<<",\"rack_press_frames\":"<<rackPressFrames_<<",\"bolt_release_leg\":"<<(boltRelease_?"true":"false")<<",\"forced_cancel\":"<<forcedCancel_<<",\"forced_at_ns\":"<<forcedAt_
  <<",\"eject\":"<<(eject_?"true":"false")<<",\"eject_press_frames\":"<<ejectPressFrames_
  <<",\"release_mid_stroke\":"<<(releaseMidStroke_?"true":"false")<<",\"released_mid_stroke\":"<<(releasedMidStroke_?"true":"false")<<",\"release_rail_m\":"<<releaseAlong_
  <<",\"carry_challenge\":"<<(carryChallenge_?"true":"false")<<",\"carry_exercised\":"<<(carryExercised_?"true":"false")
  <<",\"carry_displacement_m\":"<<carryDisplacement_<<",\"carry_angle_rad\":"<<carryAngle_<<",\"carry_raw_samples\":"<<carrySamples_
  <<",\"carry_wait_packets\":"<<carryWaits_<<",\"carry_wait_started_ns\":"<<carryWaitAt_
  <<",\"carry_failure_flags\":"<<carryFailureFlags_<<",\"carry_failure_ns\":"<<carryFailureAt_
  <<",\"carry_max_raw_step_m\":"<<carryStepTranslation_<<",\"carry_max_raw_step_rad\":"<<carryStepAngle_
  <<",\"original_returns\":"<<originalReturns_<<",\"acquired\":"<<acquired_
  <<",\"original_item\":"<<(original_?original_->item.id:0)<<",\"original_generation\":"<<(original_?original_->item.generation:0)
  <<",\"original_rounds\":"<<(original_?original_->rounds:0)<<",\"original_cycle\":"<<originalCycle_
  <<",\"original_return_seat\":"<<(originalReceipt_?originalReceipt_->seat:0)<<",\"original_return_retirement\":"<<(originalReceipt_?originalReceipt_->retirement:0)
  <<",\"physical_authority\":"<<(physicalAuthority_?"true":"false")
  <<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_<<",\"actual_consumer_completed\":"<<(phase_==Phase::Done?"true":"false")
  <<",\"native_receipt\":"<<(receiptVerified_?"true":"false")<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_
  <<",\"loaded_before\":"<<loadedBefore_<<",\"reserve_before\":"<<reserveBefore_<<",\"expected_units\":"<<expectedUnits_
  <<",\"removed_pair\":"<<removedPair_<<",\"hidden_pair\":"<<hiddenPair_<<",\"replacement_pair\":"<<replacementPair_
  <<",\"copies\":"<<packs_.copies<<",\"pairs\":"<<packs_.pairs<<",\"fallbacks\":"<<packs_.fallbacks<<",\"row_dropped\":"<<dropped_<<",\"rows\":[";
 for(unsigned n=0;n<rowCount_;++n){const auto& r=rows_[n];if(n)o<<',';o<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now
  <<",\"input\":"<<r.input<<",\"raw\":"<<r.raw<<",\"cycle\":"<<r.cycle<<",\"position_error_m\":"<<r.positionError
  <<",\"angle_error_rad\":"<<r.angleError<<",\"rail_m\":"<<r.rail<<",\"loaded\":"<<r.loaded<<",\"reserve\":"<<r.reserve<<",\"pairs\":"<<r.pairs<<'}';}
 o<<"]}";o.precision(precision);
}
} // namespace fvr::bc2
