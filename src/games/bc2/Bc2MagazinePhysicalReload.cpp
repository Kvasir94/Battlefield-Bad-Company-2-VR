#include "Bc2MagazinePhysicalReload.h"
#include "Bc2ReloadFeedback.h"
#include <limits>
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
bool SameInput(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
 return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
 a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool Fresh(std::int64_t a,std::int64_t b,std::int64_t now)noexcept{return a>0&&a<=now&&b>now&&b-a<=200000000;}
bool CanSupply(DetachableMagazinePhase p)noexcept{return p==DetachableMagazinePhase::WellEmpty||
 p==DetachableMagazinePhase::ReplacementHeld||p==DetachableMagazinePhase::Guided;}
}
interaction::AmmoSupplyConfig Bc2MagazinePhysicalReload::DefaultPouch()noexcept {
 return {InteractionHand::Left,0x4243324d41474full,{0x4243324d504f55ull,1},{-.23f,-.55f,.02f},.18f,200000000};
}
Bc2MagazinePhysicalReload::Bc2MagazinePhysicalReload(bool enabled,MagazinePhysicalApi api,AmmoSupplyConfig pouch)noexcept
 :supply_(pouch),pouch_(pouch),api_(api),enabled_(enabled){}
ReloadKeepAliveResult Bc2MagazinePhysicalReload::Keep(const ReloadCycleControl& c,std::int64_t now)noexcept {
 const auto result=api_.keepObserved?api_.keepObserved(api_.context,c):
  api_.keep(api_.context,c)?ReloadKeepAliveResult::Accepted:ReloadKeepAliveResult::Rejected;
 if(result==ReloadKeepAliveResult::Accepted)acceptedControlDeadline_=c.deadlineNs;
 if(result==ReloadKeepAliveResult::Deferred&&!ReloadKeepAliveDeferredWithinOriginalDeadline(result,now,acceptedControlDeadline_))return ReloadKeepAliveResult::Rejected;
 return result;
}
bool Bc2MagazinePhysicalReload::Api()const noexcept{return api_.reserve&&api_.identity&&api_.start&&api_.inspectStart&&api_.keep&&api_.lease&&
 api_.gate&&api_.submit&&api_.ack&&api_.cancel&&api_.retire;}
std::int64_t Bc2MagazinePhysicalReload::Now(std::int64_t fallback)const noexcept{return api_.clock?api_.clock(api_.context):fallback;}
void Bc2MagazinePhysicalReload::PublishReplacementFrame(MagazineTracking& tracking,const MagazinePhysicalSample& s,bool canCapture)noexcept {
 if(tracking.target&&tracking.target->role==MagazinePropRole::Removed&&last_.insertion.phase==ReloadInsertionPhase::Free){
  replacementFrame_.reset();const auto& v=*tracking.target;
  if(!removalFrame_||removalFrame_->owner!=v.owner||removalFrame_->item!=v.item||removalFrame_->handClaim!=v.handClaim||
   removalFrame_->gunClaim!=v.gunClaim||removalFrame_->inputSequence!=v.inputSequence||removalFrame_->cycle!=v.nativeCycle||
   removalFrame_->observedNs!=v.observedNs||removalFrame_->deadlineNs!=v.deadlineNs){
   removalFrame_.reset();if(canCapture)removalFrame_=MagazineRemovalFrame(tracking,s.raw);
  }
  if(removalFrame_)tracking.removalFrame=removalFrame_;else tracking.target.reset();return;
 }
 removalFrame_.reset();
 if(!tracking.target||tracking.target->role!=MagazinePropRole::Replacement||last_.insertion.phase!=ReloadInsertionPhase::Free){replacementFrame_.reset();return;}
 const auto& v=*tracking.target;
 const auto matches=[&](const MagazineReplacementFrame& f){return f.owner==v.owner&&f.item==v.item&&f.handClaim==v.handClaim&&
  f.gunClaim==v.gunClaim&&f.inputSequence==v.inputSequence&&f.cycle==v.nativeCycle&&f.observedNs==v.observedNs&&f.deadlineNs==v.deadlineNs;};
 // Same source sequence is immutable, even when native/renderer reads repeat.
 if(!replacementFrame_||!matches(*replacementFrame_)){
  replacementFrame_.reset();const auto& raw=s.raw;
  if(canCapture&&raw.valid&&raw.owner==s.nativeOwner&&s.originalHandEvidence&&SameInput(raw.inputEvidence,*s.originalHandEvidence)&&
   raw.inputEvidence.owner==v.owner&&raw.inputEvidence.sequence==v.inputSequence&&raw.inputEvidence.observedNs==v.observedNs&&
   raw.inputEvidence.deadlineNs>=v.deadlineNs&&Rigid(raw.weaponWorldMeters))
    replacementFrame_=MagazineReplacementFrame{v.owner,v.item,v.handClaim,v.gunClaim,v.inputSequence,v.nativeCycle,v.observedNs,v.deadlineNs,raw.weaponWorldMeters};
 }
 if(replacementFrame_)tracking.replacementFrame=replacementFrame_;
 else tracking.target.reset(); // Missing carry basis never becomes a gun-relative replacement.
}
void Bc2MagazinePhysicalReload::ObserveRetirement(const MagazinePhysicalSample& s)noexcept {
 if(retiring_&&api_.retire){
  if(retirement_&&retirement_->deadlineNs<=s.input.nowNs)retirement_.reset();
  if(!retirement_)retirement_=api_.retire(api_.context,owners_.native,owners_.cycle);
  if(retirement_&&retirement_->verified&&retirement_->identity==owners_.native&&retirement_->cycle==owners_.cycle&&
   retirement_->event&&Fresh(retirement_->observedNs,retirement_->deadlineNs,s.input.nowNs))drained_=true;
 }
 // Callback drain is durable old-cycle retirement, not an ammo outcome. An
 // unrelated current item can become usable while old supply stays quarantined.
 const bool same=s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&s.asset==profile_->geometry->asset;
 blocksCurrent_=active_||(retiring_&&(!drained_||same));
}
void Bc2MagazinePhysicalReload::Record(unsigned kind,const HandInteractionSample& in,std::uint64_t geometry)noexcept {
 if(eventCount_==events_.size()){++eventDropped_;return;}
 const auto* pending=supply_.Pending()?&*supply_.Pending():nullptr;
 events_[eventCount_++]={kind,unsigned(last_.phase),unsigned(last_.reason),owners_.cycle,in.sequence,geometry,
  supply_.Held()?supply_.Held()->item.generation:pending?pending->item.generation:0,
  last_.transaction.request?last_.transaction.request->id:pending?pending->request:0,last_.seat?last_.seat->id:last_.originalSeat?last_.originalSeat->id:pending?pending->seat:0,
  in.observedNs,in.nowNs,last_.insertion.progress,kind==7?unsigned(cancelCause_):0,
  originalMagazine_?originalMagazine_->item.id:0,originalMagazine_?originalMagazine_->item.generation:0,
  originalMagazine_?originalMagazine_->rounds:0,lastReserve_?lastReserve_->loaded:-1,lastReserve_?lastReserve_->reserve:-1,kind==2?startOrigin_:ReloadStartOrigin{},kind==7?last_.claimFailure:std::nullopt,last_.nativeFailureCheck,kind==7?nativeBoundary_:std::nullopt,kind==7?last_.motionFailure:std::nullopt};
}
bool Bc2MagazinePhysicalReload::RollbackUnstarted(const HandInteractionSample& in,HandInteraction& hands)noexcept {
 if(!unseat_||supply_.Pending()||supply_.Held()||!interaction_.RejectUnstarted(in,hands,*unseat_))return false;
 startupUnknown_=active_=retiring_=blocksCurrent_=gateApplied_=drained_=false;pulseUntil_=0;
 unseat_.reset();startupPulse_.reset();lease_.reset();gate_.reset();ack_.reset();originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;++unstarted_;
 return true;
}
void Bc2MagazinePhysicalReload::Cancel(const HandInteractionSample& in,HandInteraction& hands,MagazineCancelCause cause)noexcept {
 if(!enabled_||retiring_)return;
 if(startupUnknown_){pulseUntil_=0;blocksCurrent_=true;return;} // No global native cancellation before ownership is known.
 // Cancelling active work hands the reload back to stock: forget the chamber.
 if(active_){cancelCause_=cause;api_.cancel(api_.context);active_=false;retiring_=true;blocksCurrent_=true;++cancelled_;chamber_.Reset();rack_.reset();}
 if(last_.phase!=DetachableMagazinePhase::Attached&&last_.phase!=DetachableMagazinePhase::Cancelled)last_=interaction_.Cancel(in,hands);
 else if(last_.phase==DetachableMagazinePhase::Attached)interaction_.ResetAttached();
 if(retiring_)Record(7,in);
 supply_.Cancel(in,hands);pulseUntil_=0;lease_.reset();gate_.reset();ack_.reset();replacementFrame_.reset();removalFrame_.reset();pendingSeat_.reset();
}
void Bc2MagazinePhysicalReload::Feedback(MagazinePhysicalResult& out,FeedbackKind kind,const HandInteractionSample& input,unsigned hand)noexcept {
 // Same freshness/identity rules as Bc2PhysicalReload::Feedback; hand 0 is the loading hand.
 if(out.feedbackCount==out.feedback.size()||nextFeedback_==std::numeric_limits<std::uint64_t>::max()||
    !input.focused||!input.tracked[0]||!input.tracked[1]||input.observedNs<=0||
    input.observedNs>std::numeric_limits<std::int64_t>::max()-100000000){++feedbackRejected_;return;}
 const FeedbackEvent event{nextFeedback_+1,input.sequence,input.owner.space,input.observedNs,
  std::min(input.deadlineNs,input.observedNs+100000000),kind,hand<2?hand:0u};
 if(!ValidFeedback(event,Now(input.nowNs))){++feedbackRejected_;return;}
 ++nextFeedback_;++feedbackSent_;out.feedback[out.feedbackCount++]=event;
}
void Bc2MagazinePhysicalReload::ObserveChamber(const MagazinePhysicalSample& s,const Bc2AmmoReserveLease& reserve,HandInteraction& hands)noexcept {
 const auto* charging=profile_?profile_->chargingHandle:nullptr;
 const bool rackInput=rackEnabled_&&charging&&ValidChargingHandleProfile(*charging);
 const MagazineChamberKey key{s.input.owner,s.weapon};
 if(!firingKey_||!(*firingKey_==key)){
  // Engine data first, the catalog row for what it cannot express.
  firingKey_=key;action_=ResolveMagazineAction(api_.firing?api_.firing(api_.context):std::nullopt,
   profile_&&profile_->geometry?FindMagazineAction(profile_->geometry->mesh):nullptr);
 }
 chamber_.Bind(key,action_.kind,rackInput);
 chamber_.Observe(reserve.loaded,reserve.sequence);
 rackTravel_=-1;rackOwnsLeft_=false;rackTrace_.reset();
 if(!rackInput){rack_.reset();return;}
 if(!rack_||rack_->Profile().id!=charging->id||rack_->Profile().revision!=charging->revision)rack_.emplace(*charging);
 // The rack is VR metadata only and takes no claim: it runs while no magazine
 // work owns the loading hand. Live wiring must still map the handle pose.
 const bool idle=!active_&&!retiring_&&!startupUnknown_&&!supply_.Held()&&!supply_.Pending()&&!last_.removalClaim;
 const auto left=hands.Current(InteractionHand::Left);
 const bool busy=left&&(left->token.kind==HandClaimKind::AmmoObject||left->token.kind==HandClaimKind::Mechanism);
 const bool grabbed=rack_->Phase()==interaction::ChargingHandlePhase::Grabbed;
 // Every drop of a grabbed stroke carries its reason in the rack trace.
 const auto abort=[&](interaction::ChargingHandleReason why){
  const auto r=rack_->Abort(why);
  if(grabbed){MagazinePhysicalResult::RackTrace t;t.grip=s.gripPressed;t.phase=unsigned(r.phase);t.reason=unsigned(r.reason);rackTrace_=t;}};
 if(!idle||busy||!chamber_.NeedsRack()){
  if(grabbed)abort(!chamber_.NeedsRack()?interaction::ChargingHandleReason::NotOwed:interaction::ChargingHandleReason::HandBusy);
  else rack_->Reset();return;}
 rackTravel_=0;
 if(!s.raw.valid||s.raw.owner!=s.nativeOwner||!profile_->geometry||s.raw.rigFingerprint!=profile_->geometry->rigFingerprint||
    !Rigid(s.raw.rawLeftWristWorldMeters)||!Rigid(s.raw.weaponWorldMeters)){
  // A grabbed stroke rides a brief source gap (keeps the hand and its travel);
  // only a gap past the tracked-sample limit drops it.
  if(grabbed&&(!rackSourceGapNs_||s.input.nowNs-rackSourceGapNs_<=150000000)){
   if(!rackSourceGapNs_)rackSourceGapNs_=s.input.nowNs;rackOwnsLeft_=true;rackTravel_=rackHeldTravel_;++rackSourceGaps_;return;}
  if(grabbed)abort(interaction::ChargingHandleReason::SourceLost);else rack_->Reset();
  rackSourceGapNs_=0;return;}
 rackSourceGapNs_=0;
 ChargingHandleSample sample;sample.sequence=s.raw.inputEvidence.sequence;sample.observedNs=s.raw.inputEvidence.observedNs;
 // The raw wrist sits ~0.1 m from the grasp; use the point a held
 // magazine's origin would occupy (the profile's grasp) as the palm.
 const auto palmFromWrist=InverseRigid(profile_->geometry->interaction.insertion.itemFromHand);
 const auto weaponFromWrist=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));
 sample.weaponFromHand=palmFromWrist?Multiply(*palmFromWrist,weaponFromWrist):weaponFromWrist;
 sample.grip=s.gripPressed;sample.tracked=s.input.focused&&s.input.tracked[0];
 const auto racking=rack_->Update(sample);rackTravel_=racking.phase==interaction::ChargingHandlePhase::Grabbed?racking.travel:0.f;rackHeldTravel_=rackTravel_;
 {const auto& h=sample.weaponFromHand.values[3];double d2=0;for(unsigned i=0;i<3;++i){const double v=double(h[i])-charging->handleMeters[i];d2+=v*v;}
  rackOwnsLeft_=racking.phase==interaction::ChargingHandlePhase::Grabbed||d2<=double(charging->grabRadiusMeters)*charging->grabRadiusMeters;}
 {MagazinePhysicalResult::RackTrace t;t.grip=s.gripPressed;t.phase=unsigned(racking.phase);t.reason=unsigned(racking.reason);t.travel=racking.travel;
  const auto& h=sample.weaponFromHand.values[3];double along=0,lateral2=0;
  for(unsigned i=0;i<3;++i)along+=(double(h[i])-charging->handleMeters[i])*charging->pullDirection[i];
  for(unsigned i=0;i<3;++i){const double d=double(h[i])-charging->handleMeters[i]-along*charging->pullDirection[i];lateral2+=d*d;}
  t.along=float(along);t.lateral=float(std::sqrt(lateral2));t.distance=float(std::sqrt(along*along+lateral2));
  const auto& w=s.raw.weaponWorldMeters.values;const auto& p=charging->handleMeters;
  for(unsigned j=0;j<3;++j)t.handleWorld[j]=p[0]*w[0][j]+p[1]*w[1][j]+p[2]*w[2][j]+w[3][j];
  const auto palm=palmFromWrist?Multiply(*palmFromWrist,s.raw.rawLeftWristWorldMeters):s.raw.rawLeftWristWorldMeters;
  for(unsigned j=0;j<3;++j)t.palmWorld[j]=palm.values[3][j];
  rackTrace_=t;}
 if(racking.racked&&chamber_.Rack(reserve.loaded)){++racks_;Record(13,s.input);rackTravel_=1;}
}
MagazinePhysicalResult Bc2MagazinePhysicalReload::Tick(const MagazinePhysicalSample& supplied,HandInteraction& hands,
 std::uint64_t& sharedIntent)noexcept {
 if(!enabled_)return {};
 const unsigned racksBefore=racks_;
 // Decided from the previous Tick's state, exactly as the adapter decided
 // its HandInteraction latch before calling us.
 const bool latched=interaction_.HoldsLoadingHandThroughRelease();
 branch_=0;traceGate_.reset();
 // C3 (KF2 ReleaseSlideLock / AS2 slide release): with the bolt locked open
 // after an empty reload and a loaded magazine seated, the eject button
 // releases the bolt (chambers a round) instead of ejecting the magazine.
 const bool boltRelease=rackEnabled_&&supplied.ejectPressed&&!active_&&!retiring_&&last_.phase==DetachableMagazinePhase::Attached&&
  chamber_.NeedsRack()&&lastReserve_&&lastReserve_->loaded>0;
 auto sample=supplied;if(boltRelease)sample.ejectPressed=false;
 auto out=TickCore(sample,hands,sharedIntent,latched);
 if(boltRelease&&lastReserve_&&chamber_.Rack(lastReserve_->loaded)){++racks_;++boltReleases_;Record(13,supplied.input);out.boltReleased=true;}
 // Grip trace: every gather with the loading hand squeezed, which Tick branch
 // ran and what removal readiness saw (report: grip_trace, newest 256).
 if(supplied.gripPressed){auto& r=gripTrace_[gripTraceNext_++%gripTrace_.size()];++gripTraceTotal_;
  r={supplied.input.sequence,branch_,traceGate_?traceGate_->loaded:-1,traceGate_?traceGate_->capacity:-1,traceGate_?traceGate_->reserve:-1,
   traceGate_&&traceGate_->reloadInputReady,traceGate_&&traceGate_->allThreeIdle,active_,unsigned(last_.phase),unsigned(last_.reason),interaction_.ArmedNeutral()};}
 const int loaded=lastReserve_?lastReserve_->loaded:-1;
 // The coherent three-copy read misses between copies' callbacks around
 // every shot. That is a missing sample, not a new identity: the view keeps
 // the chamber's last observed count (Bind/Reset still clear it). Fire
 // blocking stays on fresh counts only.
 out.chamber=chamber_.View(loaded>=0?loaded:chamber_.LastLoaded());out.blockFire=!out.blocksWeaponActions&&chamber_.BlocksFire(loaded);
 out.latchedLoadingHand=latched;if(latched)++latchedTicks_;
 const bool settling=last_.insertion.settling;
 if(settling&&!settling_){++settles_;Record(11,supplied.input);out.settleStarted=true;}
 out.racked=racks_!=racksBefore;out.rackTravel=rackTravel_;out.rackOwnsLeft=rackOwnsLeft_;out.rackTrace=rackTrace_;
 // Successful return: both hands brace a physical rack; a button bolt
 // release clicks in the gun hand. Keep the existing kind and admission.
 const unsigned rackHands=ReloadRackFeedbackHands(out.racked,out.boltReleased);
 for(unsigned hand=0;hand<2;++hand)if(rackHands&(1u<<hand))
  Feedback(out,FeedbackKind::ReloadRack,supplied.input,hand);
 settling_=settling;return out;
}
MagazinePhysicalResult Bc2MagazinePhysicalReload::TickCore(const MagazinePhysicalSample& supplied,HandInteraction& hands,
 std::uint64_t& sharedIntent,bool latched)noexcept {
 MagazinePhysicalResult out;if(!enabled_)return out;
 nativeBoundary_.reset();
 auto s=supplied;s.input.nowNs=Now(s.input.nowNs);const auto cancel=[&](MagazineCancelCause cause=MagazineCancelCause::External){Cancel(s.input,hands,cause);ObserveRetirement(s);out.tracking.target.reset();out.blocksWeaponActions=BlocksEquipment();};
 branch_=20;if(!Api()||s.input.nowNs<lastNow_){cancel(MagazineCancelCause::ApiOrClock);return out;}lastNow_=s.input.nowNs;branch_=21;
 if(startupUnknown_){
  ++startInspections_;startupResult_=api_.inspectStart(api_.context,owners_.native,owners_.cycle,startupPulse_);
  if(startupResult_==MagazineCycleStartResult::NotStarted){
   if(!RollbackUnstarted(s.input,hands)){out.blocksWeaponActions=true;return out;}
   out.blocksWeaponActions=false;return out; // New neutral packet must rearm.
  }
  if(startupResult_==MagazineCycleStartResult::RegisteredCancelled){startupUnknown_=false;active_=true;cancel();return out;}
  out.blocksWeaponActions=true;return out; // Unknown/unsupported outcome cannot become a receipt.
 }
 ObserveRetirement(s);
 if(active_&&(s.nativeOwner!=owners_.native.owner||s.input.owner!=owners_.physical||s.asset!=profile_->geometry->asset))cancel(MagazineCancelCause::OwnerChanged);
 out.blocksWeaponActions=BlocksEquipment();
 if(active_&&gateApplied_&&observationGapDeadline_&&Now(s.input.nowNs)>=observationGapDeadline_){
  ++observationExpired_;cancel(MagazineCancelCause::NativeObservationExpired);return out;}
 const auto reserveObservation=api_.reserveObserved?api_.reserveObserved(api_.context):ReloadReserveObservation{ReloadObservationResult::Available,api_.reserve(api_.context)};
 auto reserve=reserveObservation.lease;s.input.nowNs=Now(s.input.nowNs);
 // Idle and attached (or a grab not yet started) with a still-fresh earlier
 // read: a deferred read (another native update in flight, most gathers live)
 // reuses that read within its own deadline, so the consumer keeps seeing
 // every packet. Skipping them left DetachableMagazine and the adapter
 // unfed for 100 ms+ and both treated the next packet as a tracking gap,
 // erasing the hand's neutral (live runs 081624/081801: begin 8 s late/never).
 const bool idleRetained=reserveObservation.result==ReloadObservationResult::Deferred&&!active_&&!retiring_&&!startupUnknown_&&
  (last_.phase==DetachableMagazinePhase::Attached||last_.phase==DetachableMagazinePhase::PreparingRemoval)&&
  retainedReserve_&&retainedReserve_->identity.owner==s.nativeOwner&&Fresh(retainedReserve_->observedNs,retainedReserve_->deadlineNs,s.input.nowNs);
 if(idleRetained){reserve=retainedReserve_;++idleRetainedReads_;branch_=22;}
 if(!idleRetained&&reserveObservation.result==ReloadObservationResult::Deferred){branch_=23;
  const auto currentGun=hands.Current(InteractionHand::Right);
  const bool currentSafe=active_&&s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&
   s.weapon==owners_.weapon&&s.family.binding==owners_.family.binding&&
   MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
   profile_&&profile_->geometry&&s.asset==profile_->geometry->asset&&s.meshes&&s.family.binding.profile&&
   MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs)&&
   !s.cancel&&s.input.sequence>=lastSequence_&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&
   !s.input.released[1]&&Fresh(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&
   s.trackingEpoch==s.nativeOwner.space&&currentGun&&currentGun->token.owner==s.input.owner&&
   currentGun->token.item==s.weapon&&currentGun->token.kind==HandClaimKind::GunHold&&
   currentGun->inputSequence==s.input.sequence&&currentGun->deadlineNs>s.input.nowNs;
  if(currentSafe&&ReloadKeepAliveDeferredWithinOriginalDeadline(ReloadKeepAliveResult::Deferred,s.input.nowNs,acceptedControlDeadline_)){
   // Same supply keep-alive as the held-read deferral below (98102f3): this
   // path also skipped AmmoSupply, and the carried replacement dropped after
   // 100 ms of observed deferrals (run 085146: pouch_start x14, no seat).
   if(supply_.Held()&&retainedReserve_&&Fresh(retainedReserve_->observedNs,retainedReserve_->deadlineNs,s.input.nowNs))
    if(const auto m=BindMagazineOwners(s.input.owner,s.weapon,*retainedReserve_,owners_.cycle,s.input.nowNs,s.family))
     if(const auto src=MagazineSupply(*m,*retainedReserve_,s.trackingEpoch,s.input.nowNs,supply_.Pending()?supply_.Pending()->units:0u)){
      AmmoSupplySample waiting{s.input,*src,0,s.trackingEpoch,s.bodyFromHand,s.gripPressed||latched,++sharedIntent};
      if(!supply_.WaitHeld(waiting,hands).held)++deferredSupplyDrops_;}
   out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());
   out.reloadHeld=s.input.nowNs<pulseUntil_; // never drop the native start pulse on a missing read
   return out;}
  if(active_&&!currentSafe){cancel(s.cancel?MagazineCancelCause::ExplicitInput:MagazineCancelCause::InputOrClaim);return out;}
  if(active_)cancel(MagazineCancelCause::ReserveUnavailable);
  else if(!retiring_){
   // Idle: this packet was seen, so the next available one is not a tracking
   // gap for the adapter (ResetAttached would erase the neutral; live runs
   // 081624/081801 begin 8 s late or never).
   ++idleReadDeferrals_;branch_=2;lastInputDeadline_=std::max(lastInputDeadline_,s.input.deadlineNs);}
  return out;
 }
 if(reserveObservation.result==ReloadObservationResult::Rejected&&api_.reserveObserved){branch_=5;if(!active_&&!retiring_)lastInputDeadline_=std::max(lastInputDeadline_,s.input.deadlineNs);
  if(active_)cancel(MagazineCancelCause::ReserveUnavailable);return out;
 }
 lastReserve_=reserve; // Read-only diagnostic snapshot; never used as a fresh lease.
 if(!reserve){
  ++reserveReadMisses_;
  // Three native branches transfer on separate callbacks. A coherent pooled
  // count read can be unavailable BETWEEN these callbacks without owner loss.
  // A submitted seat or an already acknowledged all-three native hold may
  // wait only within ORIGINAL source/input/held-lease deadlines. This grants
  // no new contact, acquisition, seat, completion or presentation lifetime.
  const auto gun=hands.Current(InteractionHand::Right);
  const bool submitted=submittedLease_&&supply_.Pending()&&last_.phase==DetachableMagazinePhase::AwaitingSeat;
  const bool held=gateApplied_&&lease_&&lease_->nativeBindingVerified&&lease_->allThreeHeld&&lease_->identity==owners_.native&&
   lease_->cycle==owners_.cycle&&Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)&&
   last_.phase>=DetachableMagazinePhase::Pulling&&last_.phase<=DetachableMagazinePhase::Guided;
  const bool same=active_&&(submitted||held)&&
   s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&s.weapon==owners_.weapon&&
   s.family.binding==owners_.family.binding&&MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
   s.asset==profile_->geometry->asset&&s.meshes&&s.family.binding.profile&&MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs)&&
   !s.cancel&&s.input.sequence>=lastSequence_&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
   Fresh(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&s.trackingEpoch==s.nativeOwner.space&&
   gun&&gun->token.owner==s.input.owner&&gun->token.item==s.weapon&&gun->token.kind==HandClaimKind::GunHold&&
   gun->inputSequence==s.input.sequence&&gun->deadlineNs>s.input.nowNs;
  const bool currentSafe=active_&&
   s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&s.weapon==owners_.weapon&&
   s.family.binding==owners_.family.binding&&MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs)&&
   s.asset==profile_->geometry->asset&&s.meshes&&s.family.binding.profile&&MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs)&&
   !s.cancel&&s.input.sequence>=lastSequence_&&s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
   Fresh(s.input.observedNs,s.input.deadlineNs,s.input.nowNs)&&s.trackingEpoch==s.nativeOwner.space&&
   gun&&gun->token.owner==s.input.owner&&gun->token.item==s.weapon&&gun->token.kind==HandClaimKind::GunHold&&
   gun->inputSequence==s.input.sequence&&gun->deadlineNs>s.input.nowNs;
  if(same&&retainedReserve_&&Fresh(retainedReserve_->observedNs,retainedReserve_->deadlineNs,s.input.nowNs)&&lastInputDeadline_>s.input.nowNs){
   const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
  if(keep==ReloadKeepAliveResult::Deferred){out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}
  if(keep==ReloadKeepAliveResult::Rejected){
    cancel(MagazineCancelCause::KeepAlive);return out;}
   // Keep the carried replacement's supply on this packet too: AmmoSupply
   // drops a held object when a fresh packet arrives after the last one it saw
   // expired, and deferred reads skip it for 100 ms+ (live run 075836: the
   // replacement dropped and was re-taken every 3-30 ticks). Same retained
   // source, same claim, no acquisition.
   if(supply_.Held())if(const auto m=BindMagazineOwners(s.input.owner,s.weapon,*retainedReserve_,owners_.cycle,s.input.nowNs,s.family))
    if(const auto src=MagazineSupply(*m,*retainedReserve_,s.trackingEpoch,s.input.nowNs,supply_.Pending()?supply_.Pending()->units:0u)){
     AmmoSupplySample waiting{s.input,*src,0,s.trackingEpoch,s.bodyFromHand,s.gripPressed||latched,++sharedIntent};
     if(!supply_.WaitHeld(waiting,hands).held)++deferredSupplyDrops_;}
   branch_=3;if(submitted)++transferReadDeferrals_;else ++heldReadDeferrals_;out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*retainedReserve_,owners_.cycle,last_.prop};
   // Retain only the ORIGINAL published target; target/hand evidence keeps its
   // own original expiry even though current safety was checked above.
   PublishReplacementFrame(out.tracking,s);
   if(!MagazineTargetFresh(out.tracking,Now(s.input.nowNs)))out.tracking.target.reset();
   out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;
  }
  // Started, native hold not yet observed: a missing read is not a failure
  // inside the start's own control deadline (live run 083402: the start was
  // cancelled the same gather, the pulse still reached the game and native
  // reloaded on its own, leaving the gun full before the physical reload).
  // The read may stay missing for many gathers (run 084155: cancelled 10
  // inputs after the start, native reloaded alone and the gun was full for
  // the next 8 s), so renew the native control with each fresh packet and
  // allow up to StartupReadGraceNs from the start for the hold to show.
  if(active_&&!gateApplied_&&!submittedLease_&&!s.cancel&&s.nativeOwner==owners_.native.owner&&s.input.owner==owners_.physical&&
     startNs_&&s.input.nowNs-startNs_<StartupReadGraceNs){
   const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
   if(keep!=ReloadKeepAliveResult::Rejected){
    ++startupReadDeferrals_;branch_=24;out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());
    out.reloadHeld=s.input.nowNs<pulseUntil_;return out;}}
  // Held, not yet submitted (Pulling..Guided), all current safety holding:
  // ride a short gap
  // from the last good read rather than abandon the reload mid-hand.
  if(currentSafe&&retainedReserve_&&
     !submittedLease_&&last_.phase>=DetachableMagazinePhase::Pulling&&last_.phase<=DetachableMagazinePhase::Guided&&
     s.input.nowNs>=retainedReserve_->observedNs&&s.input.nowNs-retainedReserve_->observedNs<HeldReadGapNs){
   const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
   if(keep!=ReloadKeepAliveResult::Rejected){++heldReadGapRides_;branch_=25;
    out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*retainedReserve_,owners_.cycle,last_.prop};PublishReplacementFrame(out.tracking,s);
    if(!MagazineTargetFresh(out.tracking,Now(s.input.nowNs)))out.tracking.target.reset();
    out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}}
  if(active_)reserveCancelWhy_|=(submitted?1u:0u)|(held?2u:0u)|(same?4u:0u)|(retainedReserve_?8u:0u)|
   (retainedReserve_&&Fresh(retainedReserve_->observedNs,retainedReserve_->deadlineNs,s.input.nowNs)?16u:0u)|(lastInputDeadline_>s.input.nowNs?32u:0u)|
   (gateApplied_?64u:0u)|(lease_?128u:0u)|(lease_&&lease_->allThreeHeld?256u:0u)|(lease_&&Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)?512u:0u);
  branch_=4;if(same)++transferReadExpirations_;
  // Idle and attached with nothing started: a deferred read (another native
  // update was in flight; most gathers in live runs 074830/075004) is a
  // missing sample, not a reason to forget the hand's neutral/grip history.
  // Cancelling here ran ResetAttached on ~80% of gathers, so a grab's
  // neutral->pressed edge was rarely seen on two available samples.
  if(!active_&&!retiring_&&!startupUnknown_&&(last_.phase==DetachableMagazinePhase::Attached||last_.phase==DetachableMagazinePhase::PreparingRemoval)){
   // This packet was seen: a later available one is not a tracking gap
   // (the gap check below would ResetAttached and erase the neutral).
   ++idleReadDeferrals_;branch_=2;lastInputDeadline_=std::max(lastInputDeadline_,s.input.deadlineNs);out.blocksWeaponActions=BlocksEquipment();out.ownsLeftHand=bool(last_.removalClaim);return out;}
  cancel(MagazineCancelCause::ReserveUnavailable);return out;
 }
 const auto map=BindMagazineOwners(s.input.owner,s.weapon,*reserve,active_||retiring_?owners_.cycle:0,s.input.nowNs,s.family);
 if(!map||map->native.owner!=s.nativeOwner||!s.meshes||(!s.family.binding.profile||!MagazineSelected(*s.meshes,s.nativeOwner,s.asset,*s.family.binding.profile,s.input.nowNs))){
 if(idleRetained&&!s.cancel){branch_=2;return out;}
 cancel(MagazineCancelCause::Mapping);return out;}
 retainedReserve_=reserve;branch_=11;
 out.tracking={s.family,true,s.nativeOwner,s.input,s.meshes,*reserve,active_||retiring_?owners_.cycle:0,{}};
 const auto gun=hands.Current(InteractionHand::Right);
 if(!MagazineTrackingFresh(out.tracking,s.input.nowNs)||!gun||gun->token.owner!=s.input.owner||gun->token.item!=s.weapon||
 gun->token.kind!=HandClaimKind::GunHold||gun->inputSequence!=s.input.sequence||gun->deadlineNs<=s.input.nowNs||
 s.trackingEpoch!=s.nativeOwner.space||(s.cancel&&!retiring_)){
  // A retained (older) read cannot prove current tracking freshness; on an idle
  // deferral that is a missing sample, never a cancel that erases the neutral.
  if(idleRetained&&!s.cancel){branch_=2;lastInputDeadline_=std::max(lastInputDeadline_,s.input.deadlineNs);return out;}
  cancel(s.cancel?MagazineCancelCause::ExplicitInput:MagazineCancelCause::InputOrClaim);return out;}
 if(active_&&*map!=owners_)cancel(MagazineCancelCause::Mapping);
 if(active_&&*map==owners_)owners_.family=map->family; // Renew only freshly proven mapping, not native request evidence.
 const auto publishBodyAmmo=[&]{
  if(!bodyAmmoEnabled_||retiring_||startupUnknown_||supply_.Held()||supply_.Pending()||last_.removalClaim||
   (out.tracking.target&&out.tracking.target->handTarget)||reserve->loaded<0||reserve->loaded>reserve->capacity||reserve->reserve<=0)return;
  if(active_){if(!lease_||!lease_->nativeBindingVerified||!lease_->allThreeHeld||lease_->identity!=reserve->identity||
    lease_->cycle!=owners_.cycle||lease_->loaded!=reserve->loaded||lease_->reserve!=reserve->reserve||lease_->capacity!=reserve->capacity||
    !Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs))return;}
  else if(!reserve->reloadInputReady&&!reserve->allThreeIdle)return;
  const auto source=MagazineBodySupply(*map,*reserve,s.trackingEpoch,s.input.nowNs);if(!source)return;
  AmmoSupplyVisualSample v;v.enabled=true;v.source=*source;v.input=s.input;v.gun=*gun;
  v.contact=pouch_.alternateContact.value_or(AmmoSupplyContact{pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters});v.frame=bodyAmmoFrame_;
  if(AmmoSupplyVisualFresh(v,Now(s.input.nowNs)))out.bodyAmmo=v;
 };
 // Retirement belongs to old identity; fresh source may be the new owner.
 if(!active_&&!retiring_){
  if(profile_!=map->family.binding.profile){
   // Switch measured data only after old native/supply ownership has retired.
   if(supply_.Held()||supply_.Pending()){cancel(MagazineCancelCause::Mapping);return out;}
   const auto& geometry=*map->family.binding.profile->geometry;
   auto config=geometry.interaction;config.attachedItem=geometry.attachedItem;
   if(!interaction_.Reconfigure(config)){cancel(MagazineCancelCause::Mapping);return out;}
   profile_=map->family.binding.profile;firingKey_.reset(); // Re-resolve the action for the new profile.
   history_={};historyNext_=0;last_={};retainedReserve_=reserve;
  }
  if((owners_.physical.actor&&owners_.physical!=map->physical)||(lastInputDeadline_&&s.input.observedNs>=lastInputDeadline_))interaction_.ResetAttached();
  owners_=*map;
 }
 ObserveChamber(s,*reserve,hands);
 // A pre-start cancel (e.g. during spawn settle, before the lease) has no
 // native cycle to retire, so the retirement path below never runs for it.
 if(!active_&&!retiring_&&last_.phase==DetachableMagazinePhase::Cancelled&&interaction_.ResetUnstarted()){last_={};++unstartedResets_;Record(14,s.input);}
 if(retiring_){
  // A retained original magazine is never a replacement supply reservation.
  // Stop proof and unchanged native counts are separate from callback drain.
  if(originalReturning_&&(s.input.owner!=owners_.physical||s.nativeOwner!=owners_.native.owner||s.weapon!=owners_.weapon||
    s.cancel||s.family.binding!=owners_.family.binding||!originalReserve_||reserve->identity!=originalReserve_->identity||reserve->loaded!=originalReserve_->loaded||
    reserve->reserve!=originalReserve_->reserve||reserve->capacity!=originalReserve_->capacity)){
   last_=interaction_.Cancel(s.input,hands);originalReturning_=false;++originalReturnRejected_;
  }
  ObserveRetirement(s);
  const auto& r=retirement_;
  // The drained request still belongs to owners_.native, but this attachment
  // observation belongs to the currently selected, independently verified map.
  // Requiring the old rig here strands retirement after a profile switch and
  // prevents the new weapon from ever starting another manual reload.
  const bool attached=s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==map->family.binding.profile->geometry->rigFingerprint&&s.raw.nativeMagazineAttached&&
   s.raw.inputEvidence.owner==s.input.owner&&s.raw.inputEvidence.deadlineNs>s.input.nowNs&&r&&s.raw.inputEvidence.observedNs>=r->observedNs;
  if(r&&!attached)++retireAttachedWaits_;
  if(r&&r->verified&&r->identity==owners_.native&&r->cycle==owners_.cycle&&Fresh(r->observedNs,r->deadlineNs,s.input.nowNs)&&
   reserve->observedNs>=r->observedNs&&attached){
   if(originalReturning_){
    // reserve reloadInputReady is produced only by coherent all-three idle
    // snapshots. Draining alone or a merely attached animated mag is insufficient.
    const OriginalMagazineReturnReceipt receipt{originalMagazine_.value_or(OriginalMagazine{}),owners_.cycle,
     last_.originalSeat?last_.originalSeat->id:0,r->event,reserve->observedNs,std::min(reserve->deadlineNs,r->deadlineNs),true};
    if(originalMagazine_&&originalReserve_&&last_.originalSeat&&reserve->reloadInputReady&&reserve->observedNs>=originalReturnNs_&&
       reserve->identity==originalReserve_->identity&&reserve->loaded==originalReserve_->loaded&&
       reserve->reserve==originalReserve_->reserve&&reserve->capacity==originalReserve_->capacity&&
       interaction_.CompleteOriginalReturn(s.input,hands,receipt)){
     originalReceipt_=receipt;chamber_.Seat(reserve->loaded); // The original magazine is back in.
     originalReturning_=retiring_=gateApplied_=blocksCurrent_=drained_=false;++originalReturns_;++reconciled_;
     Record(10,s.input);retirement_.reset();lease_.reset();submittedLease_.reset();unseat_.reset();gate_.reset();ack_.reset();
     originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;out.tracking.cycle=0;
    }
   }else{
   bool reconciled=!supply_.Pending();
   if(supply_.Pending())if(const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs,supply_.Pending()->units)){
    reconciled=supply_.Rebaseline(s.input,hands,{*supply_.Pending(),*source,r->event,r->observedNs,r->deadlineNs,true}).accepted;}
   if(reconciled&&interaction_.Rebaseline(s.input,hands,{owners_.physical,owners_.weapon,last_.nativeCycle,last_.cancelledRequest,
    r->event,r->observedNs,r->deadlineNs,true})){
    retiring_=false;gateApplied_=false;blocksCurrent_=false;drained_=false;++reconciled_;retirement_.reset();lease_.reset();submittedLease_.reset();unseat_.reset();gate_.reset();ack_.reset();
    Record(8,s.input);originalMagazine_.reset();originalReserve_.reset();last_={};history_={};historyNext_=0;out.tracking.cycle=0;
   }
   }
  }
  if(retirement_&&retirement_->deadlineNs<=s.input.nowNs)retirement_.reset();
  if(!s.cancel&&(originalReturning_||last_.phase==DetachableMagazinePhase::Complete)&&last_.prop){auto target=*last_.prop;target.gunClaim=gun->token;target.inputSequence=s.input.sequence;
   target.observedNs=s.input.observedNs;target.deadlineNs=std::min({s.input.deadlineNs,reserve->deadlineNs,gun->deadlineNs});
   out.tracking.target=target;if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();}
  out.interaction=last_;out.interaction.originalSeat.reset();out.blocksWeaponActions=BlocksEquipment();return out;
 }
 // Empty admission requires actual all-three scoped Step receipts. Missing
 // control evidence preserves native fallback; it never fabricates readiness.
 out.ordinaryReloadAllowed=!active_&&reserve->loaded==0&&!reserve->emptyReloadControlled;
 const bool fresh=s.input.sequence>lastSequence_;
 // Startup duplicates cannot acknowledge or progress, but still owe the
 // exact original unseat transaction's timeout and current input safety.
 if(!fresh&&active_&&!gateApplied_){
  DetachableMagazineSample waiting;waiting.input=s.input;waiting.weapon=s.weapon;waiting.trackingEpoch=s.trackingEpoch;
  waiting.gripPressed=s.gripPressed;waiting.nativeObservationDeferred=true;
  waiting.native={s.input.owner,s.weapon,owners_.cycle,reserve->observedNs,reserve->deadlineNs,true,false,false,{}};
  last_=interaction_.Update(waiting,hands);
  if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);out.interaction=last_;return out;}
 }
 if(!fresh){out.interaction=last_;out.interaction.transaction.request.reset();out.interaction.transaction.acknowledged=false;
  out.interaction.transaction.completed=false;out.interaction.seat.reset();out.interaction.originalSeat.reset();out.interaction.removalGrabbed=false;
  out.interaction.physicallyRemoved=false;out.interaction.insertion.captured=false;out.interaction.insertion.seat.reset();
  out.interaction.insertion.haptic=ReloadInsertionHaptic::None;
  out.tracking.target=last_.prop;out.blocksWeaponActions=BlocksEquipment();out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;
  PublishReplacementFrame(out.tracking,s);
  if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();publishBodyAmmo();return out;}
 lastSequence_=s.input.sequence;
 lastInputDeadline_=s.input.deadlineNs;
 if(sharedIntent>std::numeric_limits<std::uint64_t>::max()-2){cancel(MagazineCancelCause::CounterExhausted);return out;}
 MagazineNativeObservation native{s.input.owner,s.weapon,active_?owners_.cycle:0,reserve->observedNs,reserve->deadlineNs,true,false,false,{}};
 const auto describeLease=[&](const std::optional<ReloadMagazineLease>& l){NativeLeaseEvidence e;
  if(l)e={true,l->nativeBindingVerified,l->identity==owners_.native,l->cycle==owners_.cycle,l->allThreeHeld,l->sequence,l->cycle,l->observedNs,l->deadlineNs};return e;};
 nativeBoundary_.emplace();auto& boundary=*nativeBoundary_;boundary.supplied=native;boundary.prior=describeLease(lease_);
 boundary.gateApplied=gateApplied_;boundary.phase=unsigned(last_.phase);boundary.reserveObservedNs=reserve->observedNs;
 boundary.reserveDeadlineNs=reserve->deadlineNs;boundary.controlDeadlineNs=acceptedControlDeadline_;boundary.currentClaim=hands.Current(InteractionHand::Left);
 if(!active_&&(reserve->loaded<0||(reserve->loaded==0&&!reserve->emptyReloadControlled)||reserve->loaded>=reserve->capacity||reserve->reserve<=0||!reserve->reloadInputReady)){
  // Why removal is not permitted (report: removal_blocked): unknown count,
  // empty without the controlled empty path, full, no reserve, not ready
  // (copies not all idle in state 2, or not coherent).
  branch_=7;traceGate_=*reserve;
  ++removalBlocked_[reserve->loaded<0?0:(reserve->loaded==0&&!reserve->emptyReloadControlled)?1:reserve->loaded>=reserve->capacity?2:reserve->reserve<=0?3:reserve->allThreeIdle?4:5];
  DetachableMagazineSample unavailable;unavailable.input=s.input;unavailable.weapon=s.weapon;unavailable.trackingEpoch=s.trackingEpoch;
  unavailable.gripPressed=grabRemovalEnabled_&&s.gripPressed;unavailable.ejectPressed=s.ejectPressed;unavailable.native=native;unavailable.removalPermitted=false;
  last_=interaction_.Update(unavailable,hands);out.interaction=last_;publishBodyAmmo();return out;
 }
 if(active_){
  const auto priorControlDeadline=acceptedControlDeadline_;
  const auto keep=Keep({owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true},s.input.nowNs);
  if(keep==ReloadKeepAliveResult::Deferred){out.blocksWeaponActions=true;out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());return out;}
  if(keep==ReloadKeepAliveResult::Rejected){cancel(MagazineCancelCause::KeepAlive);return out;}
  const auto observation=api_.leaseObserved?api_.leaseObserved(api_.context,owners_.native,owners_.cycle):
   ReloadMagazineLeaseObservation{ReloadMagazineObservationResult::Ready,api_.lease(api_.context,owners_.native,owners_.cycle)};
  if(observation.result==ReloadMagazineObservationResult::Rejected){cancel(MagazineCancelCause::NativeObservationRejected);return out;}
  s.input.nowNs=Now(s.input.nowNs);
  if(observation.result==ReloadMagazineObservationResult::Deferred&&!submittedLease_){
   // A lost post-gate positive hold receipt gets one fixed freshness gap.
   // Startup has no prior hold receipt: its original unseat transaction
   // acknowledgement timeout remains the bound, checked below by interaction.
   // Keep may renew control, but cannot renew either original deadline.
   if(priorControlDeadline<=s.input.nowNs){cancel(MagazineCancelCause::KeepAlive);return out;}
   if(gateApplied_&&!observationGapDeadline_)observationGapDeadline_=s.input.nowNs+std::min<std::int64_t>(priorControlDeadline-s.input.nowNs,50000000);
   if((gateApplied_&&s.input.nowNs>=observationGapDeadline_)||acceptedControlDeadline_<=s.input.nowNs){
    ++observationExpired_;cancel(MagazineCancelCause::NativeObservationExpired);return out;}
   ++observationDeferred_;
   const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs);
   if(!source){branch_=13;if(idleRetained&&!s.cancel)return out;cancel(MagazineCancelCause::SourceUnavailable);return out;}
   const auto held=supply_.Held();
   // A latched settle keeps the replacement's claim; the recognizer below
   // still receives the physical grip.
   AmmoSupplySample waiting{s.input,*source,0,s.trackingEpoch,s.bodyFromHand,s.gripPressed||latched,++sharedIntent};
   const auto supplied=supply_.WaitHeld(waiting,hands);
   if(held&&s.gripPressed&&!s.input.released[0]&&!supplied.held){cancel(MagazineCancelCause::InputOrClaim);return out;}
   DetachableMagazineSample physical;physical.input=s.input;physical.weapon=s.weapon;physical.trackingEpoch=s.trackingEpoch;
   physical.gripPressed=s.gripPressed;physical.native=native;physical.native.cycle=owners_.cycle;
   physical.nativeObservationDeferred=true;physical.replacement=supplied.held;
   last_=interaction_.Update(physical,hands);
   if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);out.interaction=last_;return out;}
   out.interaction=last_;out.blocksWeaponActions=true;
   // A missing observation cannot interrupt an already authorized startup
   // Reload pulse. Its original deadline remains unchanged.
   out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;
   out.ownsLeftHand=bool(last_.removalClaim)||bool(supply_.Held());
   out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
   // Old presentation evidence is allowed to expire; never publish a new pose.
   out.tracking.target=last_.prop;PublishReplacementFrame(out.tracking,s);
   if(!MagazineTargetFresh(out.tracking,s.input.nowNs))out.tracking.target.reset();
   return out;
  }
  if(observation.result==ReloadMagazineObservationResult::Ready)observationGapDeadline_=0;
  const auto observedLease=observation.lease;boundary.returned=describeLease(observedLease);
  if(observedLease)lease_=observedLease;
  if(auto observed=api_.gate(api_.context,owners_.native,owners_.cycle))gate_=observed;
  if(auto observed=api_.ack(api_.context,owners_.native,owners_.cycle))ack_=observed;
  s.input.nowNs=Now(s.input.nowNs);
  if(lease_){const auto& l=*lease_;
   if(!l.nativeBindingVerified||l.identity!=owners_.native||l.cycle!=owners_.cycle||!l.sequence||!Fresh(l.observedNs,l.deadlineNs,s.input.nowNs))lease_.reset();
   else{native.observedNs=l.observedNs;native.deadlineNs=l.deadlineNs;native.allThreeHeld=l.allThreeHeld;}}
  if(gate_&&unseat_&&!gateApplied_)if(const auto g=MagazineGateObservation(owners_,*gate_,*unseat_,s.input.nowNs))native=*g;
  if(ack_&&submittedLease_&&supply_.Pending()){
   const auto receipt=MagazineSupplyReceipt(owners_,*supply_.Pending(),*submittedLease_,*ack_,*reserve,s.input.nowNs);
   if(receipt&&supply_.Resolve(s.input,hands,*receipt).consumed){native.acknowledgement=receipt->acknowledgement;
    native.acknowledgementVerified=true;++completed_;
    chamber_.Seat(reserve->loaded);} // Native receipt, not the geometric seat: the rounds have moved.
  }
 }
 boundary.supplied=native;boundary.accepted=describeLease(lease_);boundary.controlDeadlineNs=acceptedControlDeadline_;
 boundary.ownerMatches=native.owner==s.input.owner;boundary.weaponMatches=native.weapon==s.weapon;
 const unsigned units=supply_.Pending()?supply_.Pending()->units:
  (native.acknowledgementVerified&&native.acknowledgement.operation==ReloadOperation::SeatMagazine&&submittedLease_?
   unsigned(std::min(submittedLease_->capacity-submittedLease_->loaded,submittedLease_->reserve)):0);
 const auto source=MagazineSupply(*map,*reserve,s.trackingEpoch,s.input.nowNs,units);
 boundary.sourceChecked=true;boundary.sourceAvailable=bool(source);
 if(!source){branch_=13;if(idleRetained&&!s.cancel)return out;cancel(MagazineCancelCause::SourceUnavailable);return out;}
 AmmoSupplySample current{s.input,*source,s.geometrySequence,s.trackingEpoch,s.bodyFromHand,
  CanSupply(last_.phase)&&(s.gripPressed||latched),++sharedIntent};
 // A genuine native completion is resolved BEFORE a reduced pool is observed
 // by the carried-item policy. Unknown native outcomes stay quarantined.
 const auto suppliedAmmo=supply_.Update(current,hands);if(suppliedAmmo.acquired){++acquired_;Feedback(out,FeedbackKind::ReloadGrab,s.input);}
 else if(suppliedAmmo.hoverEntered&&CanSupply(last_.phase)&&!last_.removalClaim)Feedback(out,FeedbackKind::ReloadHover,s.input);
 history_[historyNext_]=Evidence{current};historyNext_=(historyNext_+1)%history_.size();
 const Evidence* original=nullptr;
 if(s.raw.valid&&s.raw.owner==s.nativeOwner&&s.raw.rigFingerprint==profile_->geometry->rigFingerprint&&s.originalHandEvidence&&
  SameInput(s.raw.inputEvidence,*s.originalHandEvidence)&&s.raw.inputEvidence.deadlineNs>s.input.nowNs){
  for(const auto& e:history_)if(e&&SameInput(e->supply.input,s.raw.inputEvidence)&&e->supply.trackingEpoch==s.trackingEpoch){original=&*e;break;}}
 DetachableMagazineSample physical;physical.input=s.input;physical.weapon=s.weapon;physical.trackingEpoch=s.trackingEpoch;
 physical.intent=++sharedIntent;physical.gripPressed=s.gripPressed;physical.ejectPressed=s.ejectPressed;physical.native=native;
 // An ignored receiver grip is neutral for removal admission. Otherwise it
 // disarms the legacy gesture and makes A require releasing support first.
 // Active insertion/carry still sees the real grip; supply uses it above too.
 if(!active_&&!grabRemovalEnabled_)physical.gripPressed=false;
 physical.removalPermitted=active_||s.ejectPressed||grabRemovalEnabled_;
 physical.replacement=supply_.Held();branch_=active_?8:9;traceGate_=*reserve;
 if(active_)physical.original=originalMagazine_;
 else if(nextCycle_<std::numeric_limits<std::uint64_t>::max())physical.original=OriginalMagazine{
  s.input.owner,s.weapon,{0x4243324f4d4147ull,nextCycle_+1},{profile_->geometry->interaction.insertion.id,profile_->geometry->interaction.insertion.revision},
  {reserve->identity.serverItem,reserve->identity.owner.equipGeneration},s.trackingEpoch,reserve->sequence,
  reserve->observedNs,reserve->deadlineNs,unsigned(reserve->loaded),unsigned(reserve->capacity)};
 if(original&&Rigid(s.raw.rawLeftWristWorldMeters)&&Rigid(s.raw.weaponWorldMeters)){
  physical.geometryInput=original->supply.input;physical.geometrySequence=original->supply.input.sequence;
  physical.weaponFromHandMeters=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));}
 boundary.currentClaim=hands.Current(InteractionHand::Left);
 last_=interaction_.Update(physical,hands);out.interaction=last_;
 if(last_.removalGrabbed){Record(1,s.input,physical.geometrySequence);Feedback(out,FeedbackKind::ReloadGrab,s.input);}
 // A replacement that was guided and then fell out (released early, lost contact).
 if(last_.insertion.cancelled&&last_.insertion.reason!=ReloadInsertionReason::Explicit&&last_.insertion.reason!=ReloadInsertionReason::None)
  Feedback(out,FeedbackKind::ReloadFailed,s.input);
 if(last_.physicallyRemoved){Record(3,s.input,physical.geometrySequence);chamber_.Eject(reserve->loaded);}
 if(last_.insertion.haptic==ReloadInsertionHaptic::Seated){Feedback(out,FeedbackKind::ReloadSeated,s.input);Feedback(out,FeedbackKind::ReloadGunSeat,s.input,1);Record(12,s.input,physical.geometrySequence);}
 if(last_.insertion.captured)Record(4,s.input,physical.geometrySequence);
 if(last_.seat)Record(5,s.input,physical.geometrySequence);
 if(last_.phase==DetachableMagazinePhase::Cancelled){cancel(MagazineCancelCause::InteractionRejected);return out;}
 if(last_.transaction.acknowledged&&native.acknowledgement.operation==ReloadOperation::UnseatMagazine)gateApplied_=true;
 if(last_.originalSeat){
  const auto& seat=*last_.originalSeat;
  if(!active_||!gateApplied_||!lease_||!lease_->nativeBindingVerified||!lease_->allThreeHeld||
     !Fresh(lease_->observedNs,lease_->deadlineNs,s.input.nowNs)||lease_->identity!=owners_.native||lease_->cycle!=owners_.cycle||
     !originalMagazine_||!originalReserve_||last_.original!=originalMagazine_||seat.identity.item!=originalMagazine_->item||
     reserve->identity!=originalReserve_->identity||reserve->loaded!=originalReserve_->loaded||reserve->reserve!=originalReserve_->reserve||
     reserve->capacity!=originalReserve_->capacity||supply_.Pending()||supply_.Held()){
   cancel(MagazineCancelCause::SeatSubmission);return out;}
  api_.cancel(api_.context);active_=false;retiring_=originalReturning_=blocksCurrent_=true;drained_=false;
  originalReturnNs_=s.input.nowNs;pulseUntil_=0;retirement_.reset();Record(9,s.input,physical.geometrySequence);
 }
 if(last_.transaction.request){const auto& r=*last_.transaction.request;
  if(r.operation==ReloadOperation::UnseatMagazine){
   const auto identity=api_.identity(api_.context);
   if(active_){cancel();return out;}
   if(!identity||*identity!=reserve->identity||nextCycle_==std::numeric_limits<std::uint64_t>::max()){
    // No native Start was called, so this exact local pre-gate intent can be
    // abandoned without inventing retirement for an unregistered cycle.
    unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   owners_=*map;owners_.cycle=++nextCycle_;originalMagazine_=last_.original;originalReserve_=*reserve;originalReceipt_.reset();drained_=false;retirement_.reset();unseat_=r;auto request=r;request.owner=MagazineNativeOwner(owners_);
   startOrigin_={s.actionFlagsKnown,s.gripPressed,s.ejectPressed,s.input.sequence,s.actionHeld,s.actionPressed};
   if(s.input.deadlineNs-s.input.observedNs<100000000ll){
    unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   const ReloadCycleControl control{owners_.native,owners_.cycle,s.input.sequence,s.input.observedNs,s.input.deadlineNs,true};
   const ReloadMagazineStartupPulse pulse{control,s.input.observedNs+100000000ll};
   if(pulse.endNs<=Now(s.input.nowNs)){unseat_=r;RollbackUnstarted(s.input,hands);out.tracking.target.reset();out.reloadHeld=false;return out;}
   startupPulse_=pulse;startupResult_=api_.start(api_.context,control,request,pulse);
   if(startupResult_!=MagazineCycleStartResult::Started){
    out.tracking.target.reset();out.reloadHeld=false;
    if(startupResult_==MagazineCycleStartResult::NotStarted){
     if(!RollbackUnstarted(s.input,hands)){startupUnknown_=true;blocksCurrent_=true;}
    }else if(startupResult_==MagazineCycleStartResult::RegisteredCancelled){active_=true;cancel();}
    else{startupUnknown_=true;blocksCurrent_=true;}
    out.blocksWeaponActions=BlocksEquipment();return out;
   }
   observationGapDeadline_=0;acceptedControlDeadline_=s.input.deadlineNs;active_=true;blocksCurrent_=true;++started_;startNs_=s.input.nowNs;Record(2,s.input,physical.geometrySequence);pulseUntil_=pulse.endNs;out.tracking.cycle=owners_.cycle;
  }else if(r.operation==ReloadOperation::SeatMagazine){
   if(!active_||!last_.seat){cancel(MagazineCancelCause::SeatSubmission);return out;}
   pendingSeat_=PendingSeat{r,*last_.seat,s.input.nowNs+SeatRetryNs};
  }
 }
 // Seat submission: this tick, or a later one while the lease or the original
 // hand evidence is briefly missing; cancel only when the window runs out.
 if(pendingSeat_){
  const auto& p=*pendingSeat_;bool done=false;
  if(!active_){pendingSeat_.reset();}
  else if(lease_&&original){
   const auto reservation=supply_.ReserveFrom(current,hands,original->supply,p.seat,p.request,owners_.cycle);
   const auto request=reservation?MagazineSeatRequest(owners_,*lease_,*reservation,p.request,s.input.nowNs):std::nullopt;
   if(request&&api_.submit(api_.context,*request)){submittedLease_=*lease_;++submitted_;done=true;
    if(p.deadlineNs-SeatRetryNs<s.input.nowNs)++seatRetrySubmits_;}
   else ++seatMissing_[2];}
  else{if(!lease_)++seatMissing_[0];if(!original)++seatMissing_[1];}
  if(done)pendingSeat_.reset();
  else if(pendingSeat_){++seatDeferrals_;if(s.input.nowNs>=pendingSeat_->deadlineNs){pendingSeat_.reset();cancel(MagazineCancelCause::SeatSubmission);return out;}}
 }
 if(last_.transaction.completed){Record(6,s.input);api_.cancel(api_.context);active_=false;retiring_=true;blocksCurrent_=true;pulseUntil_=0;}
 out.tracking.target=last_.prop;
 PublishReplacementFrame(out.tracking,s,original!=nullptr);
 if(!MagazineTargetFresh(out.tracking,s.input.nowNs)||(!active_&&!retiring_))out.tracking.target.reset();
 out.reloadHeld=active_&&s.input.nowNs<pulseUntil_;out.blocksWeaponActions=BlocksEquipment();
 out.ownsLeftHand=bool(last_.removalClaim)||supply_.Held().has_value();out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
 publishBodyAmmo();return out;
}
void Bc2MagazinePhysicalReload::Report(std::ostream& o)const {
 o<<"\"magazine_physical\":{\"active\":"<<active_<<",\"retiring\":"<<retiring_<<",\"drained\":"<<drained_<<",\"blocks_current\":"<<blocksCurrent_<<",\"cycle\":"<<owners_.cycle
  <<",\"phase\":"<<unsigned(last_.phase)<<",\"reason\":"<<unsigned(last_.reason)<<",\"transaction_reason\":"<<unsigned(last_.transaction.reason)<<",\"acquired\":"<<acquired_
  <<",\"started\":"<<started_<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_
  <<",\"original_returns\":"<<originalReturns_<<",\"original_returning\":"<<originalReturning_<<",\"original_return_rejected\":"<<originalReturnRejected_
  <<",\"startup_result\":"<<unsigned(startupResult_)<<",\"startup_unknown\":"<<startupUnknown_<<",\"unstarted\":"<<unstarted_<<",\"start_inspections\":"<<startInspections_
  <<",\"cancel_cause\":"<<unsigned(cancelCause_)<<",\"seat_deferrals\":"<<seatDeferrals_<<",\"seat_retry_submits\":"<<seatRetrySubmits_<<",\"seat_missing\":["<<seatMissing_[0]<<','<<seatMissing_[1]<<','<<seatMissing_[2]<<']'<<",\"held_read_deferrals\":"<<heldReadDeferrals_<<",\"reserve_read_misses\":"<<reserveReadMisses_<<",\"transfer_read_deferrals\":"<<transferReadDeferrals_<<",\"transfer_read_expirations\":"<<transferReadExpirations_<<",\"retire_attached_waits\":"<<retireAttachedWaits_
  <<",\"observation_deferred\":"<<observationDeferred_<<",\"observation_expired\":"<<observationExpired_
  <<",\"cancelled\":"<<cancelled_<<",\"reconciled\":"<<reconciled_<<",\"unstarted_resets\":"<<unstartedResets_<<",\"idle_read_deferrals\":"<<idleReadDeferrals_<<",\"idle_retained_reads\":"<<idleRetainedReads_<<",\"startup_read_deferrals\":"<<startupReadDeferrals_<<",\"held_read_gap_rides\":"<<heldReadGapRides_<<",\"reserve_cancel_why\":"<<reserveCancelWhy_<<",\"grip_trace_total\":"<<gripTraceTotal_<<",\"grip_trace\":[";
 {const auto n=std::min<std::uint64_t>(gripTraceTotal_,gripTrace_.size());for(std::uint64_t k=0;k<n;++k){
  const auto& r=gripTrace_[(gripTraceNext_-n+k)%gripTrace_.size()];if(k)o<<',';
  o<<'['<<r.input<<','<<r.branch<<','<<r.loaded<<','<<r.capacity<<','<<r.reserve<<','<<int(r.ready)<<','<<int(r.idle)<<','<<int(r.active)<<','<<r.phase<<','<<r.reason<<','<<int(r.neutral)<<']';}}
 o<<"]"<<",\"deferred_supply_drops\":"<<deferredSupplyDrops_<<",\"removal_blocked\":{\"unknown\":"<<removalBlocked_[0]<<",\"empty_uncontrolled\":"<<removalBlocked_[1]<<",\"full\":"<<removalBlocked_[2]<<",\"no_reserve\":"<<removalBlocked_[3]<<",\"idle_not_ready\":"<<removalBlocked_[4]<<",\"not_idle\":"<<removalBlocked_[5]<<"}"<<",\"idle_transaction_resets\":"<<interaction_.IdleTransactionResets()<<",\"pending\":"<<supply_.Pending().has_value()
  <<",\"insertion_feel\":"<<(profile_&&profile_->geometry?unsigned(profile_->geometry->interaction.insertion.feel):0u)
  <<",\"settles\":"<<settles_<<",\"latched_ticks\":"<<latchedTicks_<<",\"feedback_sent\":"<<feedbackSent_<<",\"feedback_rejected\":"<<feedbackRejected_
  <<",\"chamber\":{\"action_kind\":"<<unsigned(chamber_.ActionKind())<<",\"action_source\":"<<unsigned(action_.source)<<",\"rack_input\":"<<chamber_.RackInput()<<",\"racks\":"<<racks_<<",\"rack_source_gaps\":"<<rackSourceGaps_<<",\"bolt_releases\":"<<boltReleases_;
 {const auto v=Chamber();o<<",\"known\":"<<v.known<<",\"magazine_out\":"<<v.magazineOut<<",\"chamber_loaded\":"<<v.chamberLoaded
  <<",\"needs_rack\":"<<v.needsRack<<",\"display_ammo\":"<<v.displayAmmo<<'}';}
 o
  <<",\"event_dropped\":"<<eventDropped_<<",\"events\":[";
 for(unsigned n=0;n<eventCount_;++n){const auto& e=events_[n];if(n)o<<',';
  o<<"{\"kind\":"<<e.kind<<",\"phase\":"<<e.phase<<",\"reason\":"<<e.reason<<",\"cycle\":"<<e.cycle
   <<",\"cause\":"<<e.cause<<",\"input\":"<<e.input<<",\"geometry\":"<<e.geometry<<",\"item\":"<<e.item<<",\"request\":"<<e.request
   <<",\"seat\":"<<e.seat<<",\"observed_ns\":"<<e.observedNs<<",\"now_ns\":"<<e.nowNs<<",\"progress\":"<<e.progress
   <<",\"original_item\":"<<e.originalItem<<",\"original_generation\":"<<e.originalGeneration<<",\"original_rounds\":"<<e.originalRounds
   <<",\"native_loaded\":"<<e.nativeLoaded<<",\"native_reserve\":"<<e.nativeReserve<<",\"start_flags_known\":"<<(e.startOrigin.flagsKnown?"true":"false")
   <<",\"start_grip_pressed\":"<<(e.startOrigin.gripPressed?"true":"false")<<",\"start_eject_pressed\":"<<(e.startOrigin.ejectPressed?"true":"false")
   <<",\"start_input_sequence\":"<<e.startOrigin.inputSequence<<",\"start_held\":"<<e.startOrigin.held<<",\"start_pressed\":"<<e.startOrigin.pressed;
  if(e.motionFailure){const auto& f=*e.motionFailure;
   o<<",\"motion_failure\":{\"check\":"<<unsigned(f.check)<<",\"grip_pressed\":"<<f.gripPressed
    <<",\"released\":"<<f.released<<",\"pulled\":"<<f.pulled<<",\"geometry_input\":"<<f.geometrySequence
    <<",\"measured\":"<<f.measured<<",\"limit\":"<<f.limit<<'}';}
  if(e.claimFailure){const auto& f=*e.claimFailure;
   o<<",\"claim_failure\":{\"renew_reason\":"<<unsigned(f.reason)
    <<",\"expected_id\":"<<f.expected.token.id<<",\"expected_input\":"<<f.expected.inputSequence<<",\"expected_deadline_ns\":"<<f.expected.deadlineNs
    <<",\"current_present\":"<<(f.current?"true":"false")<<",\"current_id\":"<<(f.current?f.current->token.id:0)
    <<",\"current_input\":"<<(f.current?f.current->inputSequence:0)<<",\"current_deadline_ns\":"<<(f.current?f.current->deadlineNs:0)
    <<",\"geometry_input\":"<<f.geometrySequence<<",\"native_deadline_ns\":"<<f.nativeDeadlineNs<<'}';}
  if(e.nativeBoundary){const auto& b=*e.nativeBoundary;const auto& v=b.supplied;
   o<<",\"native_boundary\":{\"failure_check\":"<<unsigned(e.nativeFailureCheck)<<",\"consumer_phase_before\":"<<b.phase
    <<",\"observed_ns\":"<<v.observedNs<<",\"deadline_ns\":"<<v.deadlineNs<<",\"cycle\":"<<v.cycle
    <<",\"bindings_verified\":"<<v.bindingsVerified<<",\"all_three_held\":"<<v.allThreeHeld
    <<",\"owner_matches\":"<<b.ownerMatches<<",\"weapon_matches\":"<<b.weaponMatches
    <<",\"acknowledgement_verified\":"<<v.acknowledgementVerified<<",\"ack_request\":"<<v.acknowledgement.request
    <<",\"ack_operation\":"<<unsigned(v.acknowledgement.operation)<<",\"ack_status\":"<<unsigned(v.acknowledgement.status)
    <<",\"gate_applied\":"<<b.gateApplied<<",\"source_checked\":"<<b.sourceChecked<<",\"source_available\":"<<b.sourceAvailable
    <<",\"reserve_observed_ns\":"<<b.reserveObservedNs<<",\"reserve_deadline_ns\":"<<b.reserveDeadlineNs
    <<",\"control_deadline_ns\":"<<b.controlDeadlineNs<<",\"current_claim_present\":"<<bool(b.currentClaim)
    <<",\"current_claim_id\":"<<(b.currentClaim?b.currentClaim->token.id:0)<<",\"current_claim_input\":"<<(b.currentClaim?b.currentClaim->inputSequence:0)
    <<",\"current_claim_deadline_ns\":"<<(b.currentClaim?b.currentClaim->deadlineNs:0);
   const auto lease=[&](const char* name,const NativeLeaseEvidence& l){o<<",\""<<name<<"\":{\"present\":"<<l.present
    <<",\"verified\":"<<l.verified<<",\"identity_matches\":"<<l.identityMatches<<",\"cycle_matches\":"<<l.cycleMatches
    <<",\"all_three_held\":"<<l.held<<",\"sequence\":"<<l.sequence<<",\"cycle\":"<<l.cycle
    <<",\"observed_ns\":"<<l.observedNs<<",\"deadline_ns\":"<<l.deadlineNs<<'}';};
   lease("prior_lease",b.prior);lease("returned_lease",b.returned);lease("accepted_lease",b.accepted);o<<'}';}
  o<<'}';}
 o<<"]}";
}
} // namespace fvr::bc2

