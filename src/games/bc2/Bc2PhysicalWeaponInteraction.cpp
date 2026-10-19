#include "Bc2PhysicalWeaponInteraction.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdlib>
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
#include FVR_BC2_NATIVE_RACK_PHASE_HEADER
#endif

namespace fvr::bc2 {
namespace {
using namespace interaction;
using namespace reload_insertion_detail;
using K=PhysicalWeaponCommandKind;
using L=PhysicalMagazineLocation;
std::span<const NativeActionPoseProfile> NativeRackPhases(const MagazineEquipmentProfile& p)noexcept {
#ifdef FVR_BC2_NATIVE_RACK_PHASE_HEADER
 const char* setting=std::getenv("FVR_BC2_NATIVE_XM8_RACK");if(setting&&setting[0]=='0')return {};
 const auto& phases=generated::xm8_rack_proposal::NativeActionPoses;
 if(phases.size()==4&&p.geometry&&phases[0].assetName==p.geometry->asset&&phases[0].meshPath==p.geometry->mesh&&
    phases[0].rigFingerprint==p.geometry->rigFingerprint)return phases;
#else
 (void)p;
#endif
 return {};
}
bool AuthorityCurrent(const PhysicalWeaponIdentity& identity,std::uint64_t revision,std::uint32_t clientWeapon)noexcept {
 const auto view=physicalWeaponRuntime::Read(clientWeapon);
 return view&&!view->suspended&&view->identity==identity&&view->revision==revision;
}
HandInteractionKey MagazineKey(const PhysicalWeaponIdentity& identity,std::uint64_t magazine)noexcept {
 return {0x504859534d414700ull^identity.weapon,magazine};
}
bool RawFresh(const MagazinePhysicalSample& s,const MagazineEquipmentProfile& p)noexcept {
 const auto& raw=s.raw;const auto& in=raw.inputEvidence;
 return raw.valid&&raw.owner==s.nativeOwner&&raw.rigFingerprint==p.geometry->rigFingerprint&&
  in.owner==s.input.owner&&in.sequence&&in.sequence<=s.input.sequence&&in.focused&&in.tracked[0]&&in.tracked[1]&&
  in.observedNs>0&&in.observedNs<=s.input.nowNs&&in.deadlineNs>s.input.nowNs&&
  in.deadlineNs-in.observedNs<=100000000&&Rigid(raw.rawLeftWristWorldMeters)&&Rigid(raw.weaponWorldMeters);
}
PhysicalWeaponFamily Family(const MagazineEquipmentProfile& p)noexcept {
 switch(p.actionKind){case MagazineActionKind::OpenBolt:return PhysicalWeaponFamily::OpenBoltMagazine;
 case MagazineActionKind::NoAction:return PhysicalWeaponFamily::NoActionMagazine;
 case MagazineActionKind::ClosedBolt:return PhysicalWeaponFamily::ClosedBoltMagazine;}
 return PhysicalWeaponFamily::Unsupported;
}
}
Bc2PhysicalWeaponInteraction::Bc2PhysicalWeaponInteraction(bool enabled,AmmoSupplyConfig pouch)noexcept
 :enabled_(enabled),pouch_(pouch){}
bool Bc2PhysicalWeaponInteraction::LatchesLoadingHand()const noexcept {
 return enabled_&&((insertion_&&(insertion_->Settling()||insertion_->FinishesOnRelease()))||
  (pending_&&pendingKind_==K::SeatMagazine));
}
bool Bc2PhysicalWeaponInteraction::Pouch(const MagazinePhysicalSample& s,bool releasing)const noexcept {
 if(!Rigid(s.bodyFromHand))return false;
 const auto& zone=zones_[InteractionZoneKind::Pouch];
 const auto inside=[&](const std::array<float,3>& center,float radius){
  if(!(radius>0))return false;radius=ZonedRadius(radius,zone,releasing);
  double d=0;for(unsigned i=0;i<3;++i){const double v=s.bodyFromHand.values[3][i]-center[i];d+=v*v;}
  return d<=double(radius)*radius;};
 return inside(pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters)||
  (pouch_.alternateContact&&inside(pouch_.alternateContact->centerMeters,pouch_.alternateContact->radiusMeters));
}
// Magazine/handle/pouch discoverability: an empty, open left hand entering a
// zone gets one light tick (KF2 VR / AS2 hover cue, ReloadHover 0.1/12 ms).
// Distances are measured on the RAW tracked wrist; the cue never grabs.
void Bc2PhysicalWeaponInteraction::HoverCues(const MagazinePhysicalSample& s,const math::Matrix4& hand,bool raw,bool handFree,
 MagazinePhysicalResult& out)noexcept {
 if(!handFree||!raw||s.gripPressed){magazineZone_.Reset();handleZone_.Reset();if(!handFree||s.gripPressed)pouchZone_.Reset();return;}
 const auto view=physicalWeaponRuntime::Read(s.nativeOwner.weapon);if(!view)return;
 const auto& geometry=*profile_->geometry;
 const auto cue=[&](ZoneHysteresis& zone,InteractionZoneKind kind,float distance,unsigned& count){
  const auto& z=zones_[kind];const bool was=zone.Inside();
  if(zone.Update(distance,{ZonedRadius(geometry.interaction.grabRadiusMeters,z),ZonedRadius(geometry.interaction.grabRadiusMeters,z,true)+.001f})&&!was&&z.hoverCue){
   ++count;Feedback(out,FeedbackKind::ReloadHover,s.input);}};
 if(grabRemovalEnabled_&&view->state.insertedMagazine){
  const auto grasp=Multiply(geometry.interaction.insertion.itemFromHand,geometry.attachedItem);
  cue(magazineZone_,InteractionZoneKind::MagazineGrab,Distance(hand,grasp),hoverCues_.magazineHover);
 }else magazineZone_.Reset();
 if((slide_||(rack_&&view->state.needsRack))&&profile_->chargingHandle){
  const auto item=slide_?hand:Multiply(*InverseRigid(geometry.interaction.insertion.itemFromHand),hand);
  auto h=profile_->chargingHandle->handleMeters;
  if(slide_&&view->state.actionOpen)for(unsigned i=0;i<3;++i)h[i]+=profile_->chargingHandle->pullDirection[i]*profile_->chargingHandle->strokeMeters;
  const float d=float(std::sqrt(double(item.values[3][0]-h[0])*(item.values[3][0]-h[0])+double(item.values[3][1]-h[1])*(item.values[3][1]-h[1])+
   double(item.values[3][2]-h[2])*(item.values[3][2]-h[2])));
  const auto& z=zones_[InteractionZoneKind::ChargingHandle];const bool was=handleZone_.Inside();
  const float capture=ZonedRadius(profile_->chargingHandle->grabRadiusMeters,z),release=std::max(capture+.001f,ZonedRadius(profile_->chargingHandle->grabRadiusMeters,z,true));
  if(handleZone_.Update(d,{capture,release})&&!was&&z.hoverCue){++hoverCues_.handleHover;Feedback(out,FeedbackKind::ReloadHover,s.input);}
 }else handleZone_.Reset();
 if(!view->state.insertedMagazine||view->state.feed<view->state.config.feedCapacity){
  const bool was=pouchZone_.Inside();const bool in=Pouch(s,was);
  if(in&&!was&&zones_[InteractionZoneKind::Pouch].hoverCue){++hoverCues_.pouchHover;Feedback(out,FeedbackKind::ReloadHover,s.input);}
  pouchZone_.Update(in?0.f:1.f,{.5f,.5f+.001f});
 }else pouchZone_.Reset();
}
void Bc2PhysicalWeaponInteraction::Feedback(MagazinePhysicalResult& out,FeedbackKind kind,
 const HandInteractionSample& in,unsigned hand)noexcept {
 if(out.feedbackCount>=out.feedback.size()||nextFeedback_==std::numeric_limits<std::uint64_t>::max())return;
 FeedbackEvent e{nextFeedback_+1,in.sequence,in.owner.space,in.observedNs,
  std::min(in.deadlineNs,in.observedNs+100000000),kind,hand};
 if(ValidFeedback(e,in.nowNs)){++nextFeedback_;out.feedback[out.feedbackCount++]=e;}
}
void Bc2PhysicalWeaponInteraction::Release(const HandInteractionSample& in,HandInteraction& hands)noexcept {
 if(claim_)hands.Release(in,claim_->token);claim_.reset();slideClaim_=rackClaim_=false;
}
bool Bc2PhysicalWeaponInteraction::Claim(const MagazinePhysicalSample& s,HandInteraction& hands,
 std::uint64_t& intent,HandClaimKind kind,HandInteractionKey item,const HandInteractionSample& evidence,bool slide,const HandClaim* replaced)noexcept {
 const auto gun=hands.Current(InteractionHand::Right);
 if(!gun||gun->token.kind!=HandClaimKind::GunHold||gun->token.item!=s.weapon||
  gun->token.owner!=s.input.owner||intent==std::numeric_limits<std::uint64_t>::max())return false;
 const auto key=slide&&profile_->chargingHandle?HandInteractionKey{profile_->chargingHandle->id,profile_->chargingHandle->revision}:
  kind==HandClaimKind::Mechanism?profile_->geometry->interaction.removalContact:
  HandInteractionKey{profile_->geometry->interaction.insertion.id,profile_->geometry->interaction.insertion.revision};
 HandClaimRequest request{s.input.owner,InteractionHand::Left,kind,item,
  {key,evidence.sequence,evidence.deadlineNs,true},++intent,kind==HandClaimKind::Mechanism?gun->token.id:0};
 const auto* previous=claim_?&*claim_:replaced;
 const auto result=previous?hands.TransferFrom(s.input,evidence,previous->token,request):hands.AcquireFrom(s.input,evidence,request);
 if(!result.accepted||!result.claim)return false;claim_=result.claim;return true;
}
bool Bc2PhysicalWeaponInteraction::Queue(K kind,std::uint64_t magazine,int rounds,ActionOrigin origin)noexcept {
 if(pending_||cleanup_)return false;
 if(kind==K::RetainMagazine||kind==K::DropMagazine){
  CleanupMagazine(identity_,clientWeapon_,kind,magazine,false);return pending_!=0;
 }
 const auto id=physicalWeaponRuntime::Submit(identity_,kind,magazine,rounds);
 if(!id)return false;
 pending_=id;pendingIdentity_=identity_;pendingClientWeapon_=clientWeapon_;pendingKind_=kind;pendingMagazine_=magazine;++submitted_;
 pendingActionOrigin_=origin;
 if(kind==K::SeatMagazine)++seatsSubmitted_;return true;
}
void Bc2PhysicalWeaponInteraction::CleanupMagazine(const PhysicalWeaponIdentity& owner,std::uint32_t client,
 K kind,std::uint64_t magazine,bool abandoned)noexcept {
 if(!cleanup_)cleanup_=Cleanup{owner,client,kind,magazine,abandoned};
 RetryCleanup();
}
void Bc2PhysicalWeaponInteraction::RetryCleanup()noexcept {
 if(!cleanup_||pending_)return;
 const auto work=*cleanup_;
 auto client=work.clientWeapon;
 auto view=physicalWeaponRuntime::Read(client);
 if(!view||view->identity!=work.identity){
  // Missing publication may be transient. Only a fresh inventory proving actor
  // replacement or item removal can retire an obligation without a receipt.
  const auto inventory=physicalWeaponRuntime::Inventory();
  if(!inventory)return;
  const auto end=inventory->slots.begin()+std::min<std::size_t>(inventory->count,inventory->slots.size());
  const auto slot=std::find_if(inventory->slots.begin(),end,
   [&](const auto& item){return item.owned&&item.item==work.identity.weapon;});
  if(inventory->anchor.actorGeneration!=work.identity.actorGeneration||
     inventory->serverSoldier!=work.identity.actor||slot==end){cleanup_.reset();return;}
  // Client wrappers can change while the server item survives. Follow the fresh
  // mapping, but never transfer cleanup to its replacement weapon incarnation.
  client=slot->clientWeapon;view=physicalWeaponRuntime::Read(client);
  if(!view)return;
  if(view->identity!=work.identity){cleanup_.reset();return;}
  cleanup_->clientWeapon=client;
 }
 // Runtime permits only metadata cleanup on a suspended exact incarnation.
 // Its ammunition remains quarantined; finishing this hand obligation must
 // not block every later weapon from receiving physical commands.
 const auto magazine=std::find_if(view->magazines.begin(),view->magazines.end(),
  [&](const auto& item){return item.id==work.magazine&&item.location==L::Held;});
 if(magazine==view->magazines.end()){cleanup_.reset();return;}
 const auto id=physicalWeaponRuntime::Submit(work.identity,work.kind,work.magazine);
 if(!id)return;
 pending_=id;pendingIdentity_=work.identity;pendingClientWeapon_=client;
 pendingKind_=work.kind;pendingMagazine_=work.magazine;cancelPending_=work.abandoned;++submitted_;
 pendingActionOrigin_=ActionOrigin::None;
}
void Bc2PhysicalWeaponInteraction::Cancel(const HandInteractionSample& in,HandInteraction& hands,MagazineCancelCause)noexcept {
 Release(in,hands);pulling_=false;neutral_=false;if(insertion_)insertion_->Reset();if(rack_)rack_->Reset();
 if(slide_)slide_->Reset();slideCommands_.clear();
 StopNativeRack(in,hands);
 if(cleanup_)cleanup_->abandoned=true;
 if(pending_)cancelPending_=true;
 else if(heldMagazine_) {
  // An interrupted reload keeps its physical magazine, even across a switch.
  // Cleanup is an authority command; no optimistic disappearance or refund.
  CleanupMagazine(identity_,clientWeapon_,K::RetainMagazine,heldMagazine_,true);
 }
 else RetryCleanup();
 heldMagazine_=0;insertResult_={};history_={};++cancelled_;last_={};
 // A latched short B never survives a cancel; the post-insert window does,
 // because the seated magazine and its chamber state are unchanged.
 if(shortLatchUntil_){shortLatchUntil_=0;++shortCounters_.latchDropped;}
}
void Bc2PhysicalWeaponInteraction::OpenAfterInsert(std::int64_t now,std::uint64_t magazine)noexcept {
 if(!shortRackAfterInsert_||now<=0||!magazine)return;
 afterInsertUntil_=now+ShortReloadAfterInsertNs;afterInsertMagazine_=magazine;
 afterInsertSpent_=std::numeric_limits<std::uint64_t>::max(); // captured from the first post-seat view
}
bool Bc2PhysicalWeaponInteraction::AfterInsert(const PhysicalWeaponSnapshot& state,std::int64_t now)noexcept {
 if(!afterInsertUntil_)return false;
 if(afterInsertSpent_==std::numeric_limits<std::uint64_t>::max())afterInsertSpent_=state.spent;
 if(now>=afterInsertUntil_||state.insertedMagazine!=afterInsertMagazine_||state.spent!=afterInsertSpent_){afterInsertUntil_=0;return false;}
 return true;
}
void Bc2PhysicalWeaponInteraction::Drain(const MagazinePhysicalSample& s,HandInteraction& hands,
 std::uint64_t& intent,MagazinePhysicalResult& out)noexcept {
 if(!pending_){RetryCleanup();return;}
 const auto receipt=physicalWeaponRuntime::Result(pending_);
 if(!receipt||receipt->status==physicalWeaponRuntime::CommandStatus::Pending)return;
 const auto kind=pendingKind_;const auto owner=pendingIdentity_;const auto client=pendingClientWeapon_;
 const auto actionOrigin=pendingActionOrigin_;
 const auto magazine=receipt->magazine?receipt->magazine:pendingMagazine_;
 const bool abandoned=cancelPending_||owner!=identity_||handOwner_!=s.input.owner;
 pending_=0;cancelPending_=false;pendingActionOrigin_=ActionOrigin::None;
 if(receipt->status!=physicalWeaponRuntime::CommandStatus::Applied){
  if(kind==K::DrawMagazine)Release(s.input,hands);
  if(kind==K::OpenAction||kind==K::CloseAction){
   slideCommands_.clear();if(slide_)slide_->Reset();if(slideClaim_)Release(s.input,hands);
  }
  if(cleanup_){cleanup_->abandoned|=abandoned;RetryCleanup();}
  else if(abandoned&&kind==K::SeatMagazine)CleanupMagazine(owner,client,K::RetainMagazine,magazine,true);
  pulling_=false;if(insertion_)insertion_->Reset();Feedback(out,FeedbackKind::ReloadFailed,s.input);return;
 }
 ++completed_;
 if(receipt->droppedMagazine&&owner==identity_&&handOwner_==s.input.owner){
  ++unseats_;out.physicalDrop=true;out.physicalDropFromWell=true;out.physicalUnseat=!abandoned;
 }
 const auto readyNow=[&]{
  if(abandoned)return false;
  const auto current=physicalWeaponRuntime::Read(client);
  return current&&current->identity==owner&&current->readiness.cue==WeaponReadyCue::Ready;
 };
 if(kind==K::UnseatMagazine){++unseats_;out.physicalUnseat=!abandoned;}
 if(kind==K::DrawMagazine&&!magazine){Release(s.input,hands);return;}
 if(kind==K::DrawMagazine||kind==K::AcquireMagazine||kind==K::RecoverMagazine||kind==K::UnseatMagazine){
  if(abandoned||ejectDrop_||!s.gripPressed){
   const auto cleanup=(ejectDrop_&&!abandoned)?K::DropMagazine:K::RetainMagazine;
   dropFromWell_=ejectDrop_&&!abandoned;
   CleanupMagazine(owner,client,cleanup,magazine,abandoned);
   ejectDrop_=false;heldMagazine_=0;Release(s.input,hands);return;
  }
  heldMagazine_=magazine;pulling_=false;insertResult_={};if(insertion_)insertion_->Reset();
  heldOriginal_=kind==K::UnseatMagazine;
  insertionArmed_=!heldOriginal_;insertionObserved_=false;
  // Acquisition starts only after the native write and readback receipt. The
  // hand request uses the real packet, independent of any native lease.
  if(!Claim(s,hands,intent,HandClaimKind::AmmoObject,MagazineKey(identity_,magazine),s.input)){
   CleanupMagazine(owner,client,K::RetainMagazine,magazine,true);heldMagazine_=0;Release(s.input,hands);return;
  }
  if(kind!=K::UnseatMagazine)++acquired_;Feedback(out,FeedbackKind::ReloadGrab,s.input);
 }else if(kind==K::SeatMagazine){
  ++seatsCompleted_;out.physicalSeat=!abandoned;
  if(owner==identity_)OpenAfterInsert(s.input.nowNs,magazine);
  heldMagazine_=0;Release(s.input,hands);insertResult_={};if(insertion_)insertion_->Reset();
  Feedback(out,FeedbackKind::ReloadSeated,s.input);
  Feedback(out,readyNow()?FeedbackKind::ReloadReady:FeedbackKind::ReloadGunSeat,s.input,1);out.interaction.physicallyRemoved=false;
 }else if(kind==K::RetainMagazine||kind==K::DropMagazine){
  cleanup_.reset();
  // An applied drop IS on the floor in the ledger, abandoned or not (a cancel
  // between submit and receipt used to leave it invisible). Only the eject
  // origin depends on the gesture surviving.
  if(kind==K::DropMagazine){out.physicalDrop=true;out.physicalDropFromWell=dropFromWell_&&!abandoned;}dropFromWell_=false;
  heldMagazine_=0;Release(s.input,hands);insertResult_={};if(insertion_)insertion_->Reset();
 }else if(kind==K::ForgetDroppedMagazine&&!abandoned&&s.gripPressed){Queue(K::AcquireMagazine);}
 else if(kind==K::RefillMagazine&&!abandoned&&s.gripPressed){Queue(K::RecoverMagazine,magazine);}
 else if((kind==K::CycleAction||kind==K::CloseAction)&&!abandoned&&
         (actionOrigin==ActionOrigin::Button||actionOrigin==ActionOrigin::Hand)){
  out.racked=true;out.boltReleased=actionOrigin==ActionOrigin::Button;Feedback(out,FeedbackKind::ReloadRack,s.input,1);
  if(actionOrigin==ActionOrigin::Hand)Feedback(out,FeedbackKind::ReloadRack,s.input,0);
  if(readyNow())Feedback(out,FeedbackKind::ReloadReady,s.input,1);
  // Insert, rack by hand, then B: still part of the same reload.
  if(afterInsertUntil_&&s.input.nowNs<afterInsertUntil_)afterInsertUntil_=s.input.nowNs+ShortReloadAfterInsertNs;}
}
MagazinePhysicalResult Bc2PhysicalWeaponInteraction::Tick(const MagazinePhysicalSample& s,HandInteraction& hands,
 std::uint64_t& intent)noexcept {
 MagazinePhysicalResult out;out.latchedLoadingHand=LatchesLoadingHand();
 if(!enabled_||!physicalWeaponRuntime::Enabled())return out;
 const auto* profile=s.family.binding.profile;
 const bool valid=s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
  s.input.sequence&&s.input.observedNs>0&&s.input.observedNs<=s.input.nowNs&&s.input.deadlineNs>s.input.nowNs&&
  profile&&profile->Ready()&&profile->geometry->asset==s.asset&&s.meshes&&MagazineFamilyFresh(s.family,s.nativeOwner,s.input.owner,s.weapon,s.input.nowNs);
 if(!valid||s.cancel){Cancel(s.input,hands);Drain(s,hands,intent,out);return out;}
 const bool pistol=profile->chargingHandle&&profile->chargingHandle->pistolSlide;
 const auto family=Family(*profile);
 const auto authoredBase=family==PhysicalWeaponFamily::ClosedBoltMagazine?
    std::optional<std::int32_t>{profile->native->configuration.values.baseCapacity}:std::nullopt;
 if(!physicalWeaponRuntime::ConfigureWeapon(s.nativeOwner.weapon,family,pistol&&profile->locksOpenOnEmpty,authoredBase))return out;
 auto view=physicalWeaponRuntime::Read(s.nativeOwner.weapon);
 if(!view||view->suspended){Cancel(s.input,hands);Drain(s,hands,intent,out);return out;}
 if(identity_!=view->identity||handOwner_!=s.input.owner||profile_!=profile){
  Cancel(s.input,hands);
  if(identity_!=view->identity)afterInsertUntil_=0;
  identity_=view->identity;clientWeapon_=s.nativeOwner.weapon;handOwner_=s.input.owner;profile_=profile;sequence_=0;
  auto insertion=profile_->geometry->interaction.insertion;
  insertion.feel=ReloadInsertionFeel::Slider;
  insertion_.emplace(ZonedInsertion(insertion,zones_[InteractionZoneKind::MagazineInsert]));
  if(profile_->chargingHandle&&ValidChargingHandleProfile(*profile_->chargingHandle))
   rack_.emplace(ZonedHandle(*profile_->chargingHandle,zones_[InteractionZoneKind::ChargingHandle]));else rack_.reset();
  if(pistol&&rack_)slide_.emplace(rack_->Profile());else slide_.reset();
  nativeRack_.reset();const auto nativePhases=NativeRackPhases(*profile_);
  if(!nativePhases.empty()){nativeRack_.emplace(NativeRackConfig{nativePhases});if(!nativeRack_->Valid())nativeRack_.reset();}
  magazineZone_.Reset();handleZone_.Reset();pouchZone_.Reset();
 }
 Drain(s,hands,intent,out);
 view=physicalWeaponRuntime::Read(s.nativeOwner.weapon);
 if(!view||view->suspended||view->identity!=identity_)return out;
 state_=view->state;
 const auto gun=hands.Current(InteractionHand::Right);
 if(!gun||gun->token.kind!=HandClaimKind::GunHold||gun->token.owner!=s.input.owner||gun->token.item!=s.weapon){Cancel(s.input,hands);return out;}
 const bool fresh=s.input.sequence>sequence_;if(fresh)sequence_=s.input.sequence;
 const bool pressed=fresh&&s.gripPressed&&neutral_;
 if(fresh)neutral_=!s.gripPressed;
 const bool raw=RawFresh(s,*profile_);
 math::Matrix4 hand=Identity();
 if(raw)hand=Multiply(s.raw.rawLeftWristWorldMeters,*InverseRigid(s.raw.weaponWorldMeters));
 RefreshNativeRackOwnership(s,hands,intent);
 if(claim_){
  const auto current=hands.Current(InteractionHand::Left);
  if(!current||current->token!=claim_->token){
   claim_.reset();pulling_=false;
   if(rackClaim_){rackClaim_=false;if(rack_)rack_->Reset();
    if(nativeRack_&&s.gripPressed)StopNativeRack(s.input,hands);}
   if(slideClaim_){slideClaim_=false;if(slide_)slide_->Reset();slideCommands_.clear();}
   if(heldMagazine_&&!pending_)Queue(Pouch(s,true)?K::RetainMagazine:K::DropMagazine,heldMagazine_);
  }else {
   // An admitted native mechanism grip renews from current real ownership;
   // missing renderer geometry may pause progress but cannot release the grip.
   const auto& evidence=nativeRack_&&rackClaim_?s.input:raw?s.raw.inputEvidence:s.input;
   const HandContactProof contact{claim_->token.contact,evidence.sequence,evidence.deadlineNs,true};
   const auto renewed=hands.RenewFrom(s.input,evidence,claim_->token,contact);
   if(renewed.accepted&&renewed.claim)claim_=renewed.claim;
   else if(rackClaim_){Release(s.input,hands);if(rack_)rack_->Reset();
    if(nativeRack_&&s.gripPressed)StopNativeRack(s.input,hands);}
  }
 }
 if(!pending_&&pulling_){
  if(!s.gripPressed||!claim_){pulling_=false;Release(s.input,hands);}
  else if(raw){
   const auto& c=profile_->geometry->interaction;
   if(Distance(hand,pullPrevious_)>c.maxPullStepMeters||Angle(hand,pullPrevious_)>c.insertion.maxStepRadians){
    pulling_=false;Release(s.input,hands);Feedback(out,FeedbackKind::ReloadFailed,s.input);
   }else {
    const auto inv=InverseRigid(c.insertion.weaponFromEntry);
    const auto start=Multiply(pullStart_,*inv),at=Multiply(hand,*inv);
    double pulled=0,lateral=0;
    for(unsigned i=0;i<3;++i)pulled+=(start.values[3][i]-at.values[3][i])*c.insertion.travelDirection[i];
    pullProgress_=float(std::clamp(pulled/c.pullMeters,0.,1.));
    for(unsigned i=0;i<3;++i){const double d=at.values[3][i]-start.values[3][i]+pulled*c.insertion.travelDirection[i];lateral+=d*d;}
    if(pulled<-.02||std::sqrt(lateral)>ZonedRadius(c.pullReleaseMeters,zones_[InteractionZoneKind::MagazineGrab],true)){pulling_=false;Release(s.input,hands);}
    else if(pulled>=c.pullMeters)Queue(K::UnseatMagazine,view->state.insertedMagazine);
    pullPrevious_=hand;
   }
  }
 }
 // Short B (Bc2ShortReloadAction.h). Button rack is the deliberate action for
 // a lock release or a weapon with no independently mapped handle. After our
 // own insertion it completes the reload and never ejects; a press while the
 // seat is settling or its receipt is in flight waits for that receipt.
 bool shortConsumed=false;
 {
  const auto now=s.input.nowNs;
  if(shortLatchUntil_&&now>=shortLatchUntil_){shortLatchUntil_=0;++shortCounters_.latchDropped;}
  ShortReloadInput b;
  b.pressed=s.ejectPressed;b.latched=shortLatchUntil_!=0;
  b.seatInFlight=LatchesLoadingHand()||(heldMagazine_&&insertResult_.phase==ReloadInsertionPhase::Guided);
  b.busy=pending_!=0||pulling_||heldMagazine_!=0||slideClaim_||rackClaim_||!slideCommands_.empty();
  b.inserted=view->state.insertedMagazine!=0;b.needsRack=view->state.needsRack;b.loaded=view->state.loaded;
  b.afterInsert=AfterInsert(view->state,now);
  const auto action=DecideShortReload(b,shortRackAfterInsert_);
  switch(action){
  case ShortReloadAction::Latch:
   if(!shortLatchUntil_){shortLatchUntil_=now+ShortReloadLatchNs;++shortCounters_.latched;}
   shortConsumed=true;break;
  case ShortReloadAction::Rack:
   if(Queue(slide_&&view->state.actionOpen?K::CloseAction:K::CycleAction,0,0,ActionOrigin::Button))++shortCounters_.racks;
   shortConsumed=true;break;
  case ShortReloadAction::Eject:
   if(Queue(K::UnseatMagazine,view->state.insertedMagazine)){ejectDrop_=true;++shortCounters_.ejects;}
   shortConsumed=true;break;
  case ShortReloadAction::Confirm:
   ++shortCounters_.confirms;
   Feedback(out,view->readiness.cue==WeaponReadyCue::Ready?FeedbackKind::ReloadReady:FeedbackKind::ReloadHover,s.input,1);
   shortConsumed=true;break;
  case ShortReloadAction::None:break;
  }
  if(b.latched&&action!=ShortReloadAction::Latch){
   shortLatchUntil_=0;
   if(action==ShortReloadAction::None)++shortCounters_.latchDropped;else ++shortCounters_.latchResolved;
  }
 }
 if(!pending_&&!pulling_&&!heldMagazine_&&!shortConsumed&&!slideClaim_&&!rackClaim_&&slideCommands_.empty()){
  if(pressed&&Pouch(s)&&!hands.Current(InteractionHand::Left)&&
     (!view->state.insertedMagazine||view->state.feed<view->state.config.feedCapacity)){
   // KF2: the pouch squeeze starts the exchange, even with the old magazine
   // still installed. Reserve the hand while the server commits the draw;
   // the resulting real magazine replaces this temporary supply claim.
   if(Claim(s,hands,intent,HandClaimKind::AmmoObject,
      {profile_->geometry->interaction.insertion.id,s.input.sequence},s.input)){
    if(!Queue(K::DrawMagazine))Release(s.input,hands);
   }
  }else if(grabRemovalEnabled_&&pressed&&view->state.insertedMagazine&&raw&&!hands.Current(InteractionHand::Left)){
   const auto& geometry=*profile_->geometry;
   const auto grasp=Multiply(geometry.interaction.insertion.itemFromHand,geometry.attachedItem);
   if(Distance(hand,grasp)<=ZonedRadius(geometry.interaction.grabRadiusMeters,zones_[InteractionZoneKind::MagazineGrab])&&
    Claim(s,hands,intent,HandClaimKind::Mechanism,s.weapon,s.raw.inputEvidence)){
    pulling_=true;pullProgress_=0;pullStart_=pullPrevious_=hand;Feedback(out,FeedbackKind::ReloadGrab,s.input);
   }
  }
 }
 if(heldMagazine_&&claim_&&raw){
  const auto& p=profile_->geometry->interaction.insertion;
  heldItem_=Multiply(*InverseRigid(p.itemFromHand),hand);
  bool recorded=false;
  for(const auto& e:history_)if(e&&e->input.sequence==s.input.sequence){recorded=true;break;}
  if(!recorded){history_[historyNext_]=Evidence{s.input,*claim_,*gun};historyNext_=(historyNext_+1)%history_.size();}
  const Evidence* original=nullptr;
  for(const auto& e:history_)if(e&&e->input.owner==s.raw.inputEvidence.owner&&
   e->input.sequence==s.raw.inputEvidence.sequence&&e->input.observedNs==s.raw.inputEvidence.observedNs&&
   e->input.deadlineNs==s.raw.inputEvidence.deadlineNs&&e->item.token==claim_->token&&e->gun.token==gun->token){original=&*e;break;}
  if(!pending_&&original){
   if(!insertionArmed_){
    // Withdrawal must not immediately recapture the magazine in its own
    // socket. Arm reinsertion on a deliberate 10 mm inward reversal.
    const auto rawItem=Multiply(Multiply(p.itemFromInsertion,heldItem_),*InverseRigid(p.weaponFromEntry));
    const float along=float(TravelCoordinate(rawItem,p));
    if(!insertionObserved_){insertionMinimum_=along;insertionObserved_=true;}
    insertionMinimum_=std::min(insertionMinimum_,along);
    insertionArmed_=along-insertionMinimum_>=.01f;
   }
   const auto& evidence=s.raw.inputEvidence;
   ReloadInsertionSample insertion;
   insertion.identity={s.input.owner,s.weapon,MagazineKey(identity_,heldMagazine_),s.trackingEpoch};
   insertion.profile={p.id,p.revision};insertion.itemClaim=original->item;insertion.weaponClaim=original->gun;
   insertion.sequence=insertion.geometrySequence=evidence.sequence;
   insertion.observedNs=evidence.observedNs;insertion.deadlineNs=std::min({evidence.deadlineNs,original->item.deadlineNs,original->gun.deadlineNs});insertion.nowNs=s.input.nowNs;
   insertion.focused=s.input.focused;insertion.itemTracked=s.input.tracked[0];insertion.weaponTracked=s.input.tracked[1];
   insertion.held=s.gripPressed;insertion.eligible=!view->state.insertedMagazine&&insertionArmed_;insertion.weaponFromHand=hand;
   const bool settling=insertion_->Settling();insertResult_=insertion_->Update(insertion);
   out.settleStarted=!settling&&insertResult_.settling;
   if(insertResult_.seat)Queue(K::SeatMagazine,heldMagazine_);
   else if(!s.gripPressed&&!insertResult_.settling){Queue(Pouch(s,true)?K::RetainMagazine:K::DropMagazine,heldMagazine_);}
  }
 }
 HoverCues(s,hand,raw,!pending_&&!heldMagazine_&&!pulling_&&!hands.Current(InteractionHand::Left),out);
 if(slide_){
  const auto left=hands.Current(InteractionHand::Left);
  const bool ownPending=pending_&&(pendingKind_==K::OpenAction||pendingKind_==K::CloseAction);
  const bool settlingOther=(pending_&&!ownPending)||(view->readiness.projectionPending&&!ownPending);
  if(settlingOther)slideCommands_.clear();
  const bool eligible=!heldMagazine_&&!pulling_&&raw&&!settlingOther&&
   (slide_->Held()||(!pending_&&slideCommands_.empty()))&&(!left||slideClaim_||left->token.kind==HandClaimKind::WeaponSupport);
  PistolSlideResult r;
  if(eligible){
   ChargingHandleSample sample;
   sample.sequence=s.raw.inputEvidence.sequence;sample.observedNs=s.raw.inputEvidence.observedNs;
   sample.weaponFromHand=hand;
   sample.grip=s.gripPressed;sample.tracked=true;
   r=slide_->Update(sample,view->state.actionLocked);
   if(r.grabbed){
    // Support must yield to a deliberate mechanism contact; do not let its
    // magnetic hand placement become the source of the contact evidence.
    const auto* support=left&&left->token.kind==HandClaimKind::WeaponSupport?&*left:nullptr;
    slideClaim_=Claim(s,hands,intent,HandClaimKind::Mechanism,s.weapon,s.raw.inputEvidence,true,support);
    if(!slideClaim_){slide_->Reset();r={};}
   }
  }else if(slide_->Held()){r=slide_->Cancel();if(settlingOther){slideCommands_.clear();r.open=r.close=false;}}
  // Preserve crossing order while native receipts are in flight. Never turn
  // Open/Close/Open into one atomic rack or feed before the preceding ejection.
  if(slideCommands_.size()<8){
   if(r.open&&slideClaim_)slideCommands_.push_back({K::OpenAction,ActionOrigin::Hand});
   if(r.close)slideCommands_.push_back({K::CloseAction,ActionOrigin::Hand});
  }else{slideCommands_.clear();slide_->Reset();r.held=false;}
  if(!r.held&&slideClaim_)Release(s.input,hands);
  // A switch/tracking interruption releases the physical hold. Once the
  // owned item is current again, finish its spring return, never lose ammo.
  if(view->state.actionOpen&&!view->state.actionLocked&&!slide_->Held()&&!pending_&&slideCommands_.empty())slideCommands_.push_back({K::CloseAction,ActionOrigin::Recovery});
  if(!pending_&&!settlingOther&&!slideCommands_.empty()&&Queue(slideCommands_.front().kind,0,0,slideCommands_.front().origin))slideCommands_.pop_front();
  out.rackOwnsLeft=slideClaim_;
  out.rackPoseOwned=slideClaim_||view->state.actionOpen||ownPending||!slideCommands_.empty();
  out.rackTravel=r.held?r.travel:(view->state.actionOpen?1.f:0.f);
 }else if(nativeRack_){
  TickNativeRack(s,hands,intent,hand,raw,out);
 }else if(rack_){
  const auto left=hands.Current(InteractionHand::Left);
  // An open-bolt weapon can be cocked deliberately even when already ready.
  // CycleAction preserves its ammunition and never forces a post-empty rack.
  const bool owed=view->state.needsRack||view->state.config.family==PhysicalWeaponFamily::OpenBoltMagazine;
  const bool eligible=!pending_&&!heldMagazine_&&!pulling_&&raw&&owed&&
   (!left||rackClaim_||left->token.kind==HandClaimKind::WeaponSupport);
  if(eligible){
   ChargingHandleSample rackSample;
   rackSample.sequence=s.raw.inputEvidence.sequence;rackSample.observedNs=s.raw.inputEvidence.observedNs;
   rackSample.weaponFromHand=Multiply(*InverseRigid(profile_->geometry->interaction.insertion.itemFromHand),hand);
   rackSample.grip=s.gripPressed;rackSample.tracked=true;
   auto r=rack_->Update(rackSample);
   if(r.grabbed){
    const auto* support=left&&left->token.kind==HandClaimKind::WeaponSupport?&*left:nullptr;
    rackClaim_=Claim(s,hands,intent,HandClaimKind::Mechanism,s.weapon,s.raw.inputEvidence,true,support);
    if(!rackClaim_){rack_->Reset();r={};}
   }
   out.rackTravel=r.travel;out.rackOwnsLeft=rackClaim_&&r.phase==ChargingHandlePhase::Grabbed;
   out.rackPoseOwned=out.rackOwnsLeft;
   if(r.racked&&rackClaim_)Queue(K::CycleAction,0,0,ActionOrigin::Hand);
   if(r.phase!=ChargingHandlePhase::Grabbed&&rackClaim_)Release(s.input,hands);
  }else{
   // Busy hands, stale raw contact and readiness changes end this gesture;
   // it cannot resume from an old grab after another interaction yields.
   rack_->Reset();if(rackClaim_)Release(s.input,hands);
  }
 }
 // This frame uses only the last committed authority view. A queued seat is
 // still a held magazine until its write/readback receipt arrives.
 out.chamber={true,!view->state.insertedMagazine,view->state.chamber!=0,view->state.needsRack,
  int(view->state.loaded),int(view->state.loaded)};
 out.blockFire=!view->state.canFire||pending_!=0||pulling_||slideClaim_||out.rackOwnsLeft||
  nativeRackResult_.ownsPart||nativeRackClosing_||nativeRackCycleDue_||!slideCommands_.empty();
 // Switching cancels hand intent while preserving this weapon's ledger.
 out.blocksWeaponActions=false;
 out.ownsLeftHand=claim_.has_value();out.acquired=acquired_;out.submitted=submitted_;out.completed=completed_;
 out.interaction.insertion=insertResult_;out.interaction.removalClaim=claim_;
 out.interaction.phase=pending_&&pendingKind_==K::SeatMagazine?DetachableMagazinePhase::AwaitingSeat:
  pulling_?DetachableMagazinePhase::Pulling:heldMagazine_?
   (insertResult_.phase==ReloadInsertionPhase::Guided?DetachableMagazinePhase::Guided:
    heldOriginal_?DetachableMagazinePhase::RemovedHeld:DetachableMagazinePhase::ReplacementHeld):
  view->state.insertedMagazine?DetachableMagazinePhase::Attached:DetachableMagazinePhase::WellEmpty;
 out.tracking.enabled=true;out.tracking.physicalAuthority=true;out.tracking.authorityWeapon=view->identity;
 out.tracking.authorityRevision=view->revision;out.tracking.authorityCurrent=AuthorityCurrent;
 out.tracking.authorityClientWeapon=s.nativeOwner.weapon;
 out.tracking.family=s.family;out.tracking.owner=s.nativeOwner;out.tracking.inputEvidence=s.input;out.tracking.selected=s.meshes;
 out.tracking.cycle=identity_.weaponGeneration;
 MagazinePropTarget target;
 target.owner=s.input.owner;target.weapon=s.weapon;target.profile={profile_->geometry->interaction.insertion.id,profile_->geometry->interaction.insertion.revision};
 target.gunClaim=gun->token;target.trackingEpoch=s.trackingEpoch;target.nativeCycle=out.tracking.cycle;
 target.inputSequence=s.input.sequence;target.observedNs=s.input.observedNs;target.deadlineNs=s.input.deadlineNs;
 target.role=view->state.insertedMagazine?MagazinePropRole::Attached:MagazinePropRole::Hidden;
 target.weaponFromItemMeters=profile_->geometry->attachedItem;
 if((heldMagazine_||pulling_)&&claim_&&raw){
  target.role=pulling_?MagazinePropRole::Removed:MagazinePropRole::Replacement;
  target.handTarget=true;target.handClaim=claim_->token;target.item=claim_->token.item;
  target.inputSequence=s.raw.inputEvidence.sequence;target.observedNs=s.raw.inputEvidence.observedNs;
  target.deadlineNs=std::min(s.raw.inputEvidence.deadlineNs,claim_->deadlineNs);
  target.weaponFromItemMeters=pulling_?Multiply(Multiply(profile_->geometry->attachedItem,*InverseRigid(pullStart_)),hand):
   insertResult_.guidedItem.value_or(heldItem_);
  target.weaponFromHandMeters=insertResult_.guidedHand.value_or(hand);
  MagazineCarryFrame carry{target.owner,target.item,target.handClaim,target.gunClaim,target.inputSequence,
   target.nativeCycle,target.observedNs,target.deadlineNs,s.raw.weaponWorldMeters};
  if(pulling_)out.tracking.removalFrame=carry;else out.tracking.replacementFrame=carry;
 }
 out.tracking.target=target;
 out.tracking.actionConstrained=pulling_||(heldMagazine_&&insertResult_.guidedItem.has_value());
 out.tracking.actionProgress=pulling_?pullProgress_:insertResult_.insertFraction;
 out.tracking.actionAlignment=pulling_?1.f:insertResult_.alignment;
 out.tracking.actionRawWristInWeaponMeters=pulling_?std::nullopt:insertResult_.rawHand;
 if(bodyAmmo_){
  std::uint32_t available=view->state.looseReserve,best=0;
  for(const auto& magazine:view->magazines)if(magazine.location==L::Retained){
   available+=magazine.rounds;best=std::max(best,magazine.rounds);}
  const auto units=std::min(view->state.config.feedCapacity,view->state.looseReserve+best);
  AmmoSupplyVisualSample visual;
  visual.enabled=true;visual.occupied=heldMagazine_||pulling_||pending_;visual.input=s.input;visual.gun=*gun;
  visual.contact={pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters};visual.frame=frame_;
  visual.source.identity={s.input.owner,s.weapon,target.profile,
   {view->native.ammoAddress,identity_.weaponGeneration},s.trackingEpoch};
  visual.source.family=ReloadInsertionFamily::Magazine;visual.source.reserveUnits=available;visual.source.objectUnits=units;
  visual.source.sequence=s.input.sequence;visual.source.observedNs=s.input.observedNs;visual.source.deadlineNs=s.input.deadlineNs;
  visual.source.verified=true;
  if(AmmoSupplyVisualFresh(visual,s.input.nowNs)){out.bodyAmmo=visual;out.tracking.bodySupply=visual.source;
   out.tracking.bodyMaterial=BodyAmmoMaterialFor(view->identity,s.nativeOwner.weapon,visual.source.identity.pool,view->state,view->magazines);}
 }
 last_=out;return out;
}
void Bc2PhysicalWeaponInteraction::StopNativeRack(const HandInteractionSample& input,HandInteraction& hands)noexcept {
 if(nativeRack_)nativeRack_->Reset();nativeRackResult_={};nativeRackAction_=0;nativeRackGun_={};
 nativeRackOwner_={};nativeRackTrackingEpoch_=0;nativeRackGunDeadlineNs_=0;
 nativeRackCycleDue_=nativeRackForward_=nativeRackArmed_=nativeRackClosing_=false;
 if(rackClaim_)Release(input,hands);
}
void Bc2PhysicalWeaponInteraction::RefreshNativeRackOwnership(const MagazinePhysicalSample& s,HandInteraction& hands,
 std::uint64_t& intent)noexcept {
 if(!nativeRack_||!nativeRackAction_)return;
 const auto gun=hands.Current(InteractionHand::Right);
 const bool same=gun&&gun->token.kind==HandClaimKind::GunHold&&gun->token.owner==nativeRackGun_.owner&&
  gun->token.item==nativeRackGun_.item&&gun->token.contact==nativeRackGun_.contact&&
  s.input.owner==nativeRackGun_.owner&&s.nativeOwner==nativeRackOwner_&&s.trackingEpoch==nativeRackTrackingEpoch_;
 const bool current=s.input.focused&&s.input.tracked[0]&&s.input.tracked[1]&&!s.input.released[1]&&
  s.input.observedNs>0&&s.input.observedNs<=s.input.nowNs&&s.input.deadlineNs>s.input.nowNs;
 if(!same||!current){StopNativeRack(s.input,hands);return;}
 const bool replacedGun=gun->token!=nativeRackGun_;
 bool rebound=replacedGun;
 // Only an expired lease can be rebound. A deliberate transfer/release while
 // the old lease is current ends the action even if the same gun comes back.
 if(replacedGun&&s.input.nowNs<nativeRackGunDeadlineNs_){StopNativeRack(s.input,hands);return;}
 if(rackClaim_&&s.gripPressed){
  const auto left=hands.Current(InteractionHand::Left);
  const bool owned=claim_&&left&&left->token==claim_->token;
  if(s.input.released[0]||(!owned&&left)||
     (!owned&&claim_&&s.input.nowNs<claim_->deadlineNs&&!replacedGun)){
   StopNativeRack(s.input,hands);return;
  }
  if(!owned||replacedGun){
   rebound=true;
   // This is continuation of an admitted sampled action, not a new contact.
   // The actual current squeeze/ownership can renew its claim, but cannot
   // stamp the old wrist geometry or change the original action token.
   claim_.reset();
   if(!Claim(s,hands,intent,HandClaimKind::Mechanism,s.weapon,s.input,true)){
    StopNativeRack(s.input,hands);return;
   }
  }
 }else if(rackClaim_)Release(s.input,hands);
 nativeRackGun_=gun->token;nativeRackGunDeadlineNs_=gun->deadlineNs;
 if(rebound&&nativeRackResult_.ownsHand&&s.gripPressed){
  // Expired ownership also bounds an unobserved geometry interval. Pause the
  // existing curve before the next real wrist sample so a long gap rebases
  // motion continuity without guessing a path through the missing frames.
  nativeRack_->Heartbeat({identity_.weapon,identity_.actorGeneration,nativeRackAction_},
   true,true,true,s.input.nowNs);
 }
}
void Bc2PhysicalWeaponInteraction::TickNativeRack(const MagazinePhysicalSample& s,HandInteraction& hands,std::uint64_t& intent,
 const math::Matrix4& hand,bool raw,MagazinePhysicalResult& out)noexcept {
 if(!nativeRack_||!profile_)return;
 const auto gun=hands.Current(InteractionHand::Right);
 const auto view=physicalWeaponRuntime::Read(s.nativeOwner.weapon);
 const auto stop=[&]{StopNativeRack(s.input,hands);};
 if(!gun||!view||view->identity!=identity_||view->suspended||
    (nativeRackAction_&&nativeRackGun_!=gun->token)){stop();return;}
 if(nativeRackClosing_){
  if(Queue(K::CycleAction,0,0,ActionOrigin::Hand)){nativeRackClosing_=nativeRackCycleDue_=false;nativeRackAction_=0;}
  return;
 }
 const bool active=nativeRackResult_.phase!=NativeRackPhase::Idle;
 if(!raw&&!active){if(rackClaim_)stop();return;}
 if(!active&&!rackClaim_){
  const bool captureEdge=s.gripPressed&&nativeRackArmed_;
  nativeRackArmed_=!s.gripPressed;
  const auto left=hands.Current(InteractionHand::Left);
  const bool eligible=!pending_&&!cleanup_&&!heldMagazine_&&!pulling_&&!slideClaim_&&slideCommands_.empty()&&
    (!left||left->token.kind==HandClaimKind::WeaponSupport);
  if(!eligible)return;
  if(s.gripPressed){
   if(!captureEdge)return;
   // The helper requires neutral first; claiming is independently bounded by
   // this exact native contact, never by a magazine-to-palm approximation.
   const auto phases=NativeRackPhases(*profile_);if(phases.size()!=4)return;
   const auto& entry=phases[1].frames.front().wristInWeapon;
   if(Distance(hand,entry)>.06f||Angle(hand,entry)>.8f)return;
   const auto* previous=left&&left->token.kind==HandClaimKind::WeaponSupport?&*left:nullptr;
   // Geometry proves the bounded contact; the current actual squeeze proves
   // acquisition. A prior renderer packet may still say the hand was open.
   if(!Claim(s,hands,intent,HandClaimKind::Mechanism,s.weapon,s.input,true,previous))return;
   rackClaim_=true;nativeRackAction_=claim_->token.id;nativeRackGun_=gun->token;
   nativeRackOwner_=s.nativeOwner;nativeRackTrackingEpoch_=s.trackingEpoch;nativeRackGunDeadlineNs_=gun->deadlineNs;
  }
 }
 NativeRackSample sample;
 sample.identity={identity_.weapon,identity_.actorGeneration,nativeRackAction_?nativeRackAction_:gun->token.id};
 sample.sequence=s.raw.inputEvidence.sequence;sample.observedNs=s.raw.inputEvidence.observedNs;sample.nowNs=s.input.nowNs;
 sample.wristInWeapon=hand;sample.focused=s.input.focused;sample.tracked=true;sample.grip=s.gripPressed;
 const bool wasHeld=nativeRackResult_.ownsHand;
 auto result=raw?nativeRack_->Update(sample):nativeRack_->Heartbeat(sample.identity,s.input.focused,
  s.input.tracked[0]&&s.input.tracked[1],s.gripPressed,s.input.nowNs);
 if(raw&&!result.cancelled&&(result.grabbed||((result.ownsHand||wasHeld)&&
    (result.phase!=nativeRackResult_.phase||result.progress!=nativeRackResult_.progress))))nativeRackProgressEvidence_=s.raw.inputEvidence;
 // Claim acquisition may precede the next original raw-geometry packet. A
 // duplicate packet cannot advance/cancel that real, renewed mechanism claim.
 if(result.phase==NativeRackPhase::Idle&&!result.cancelled&&!result.closed&&rackClaim_&&s.gripPressed&&!wasHeld){
  out.rackOwnsLeft=true;out.rackPoseOwned=false;return;
 }
 nativeRackResult_=result;
 if(result.cancelled){stop();return;}
 if(result.rearStop){nativeRackCycleDue_=true;out.nativeRackRearStop=true;}
 if(!result.ownsHand&&rackClaim_)Release(s.input,hands);
 if(!nativeRackForward_&&((result.phase==NativeRackPhase::Rear&&result.progress<.99999f)||
    (result.phase==NativeRackPhase::Return&&result.progress>0))){
  nativeRackForward_=true;out.nativeRackForwardStarted=true;
 }
 if(result.closed){
  out.nativeRackClosed=true;
  if(nativeRackCycleDue_){
   // Only full original return after a real rear stop may mutate ammunition.
   // Submission/receipt still belongs to this exact physical weapon identity.
   if(Queue(K::CycleAction,0,0,ActionOrigin::Hand))nativeRackCycleDue_=false;
   else nativeRackClosing_=true;
  }
 }
 if(!result.ownsPart){
  if(nativeRackCycleDue_&&!result.closed){stop();return;}
  if(!nativeRackClosing_)nativeRackAction_=0;nativeRackForward_=false;return;
 }
 out.rackOwnsLeft=result.ownsHand&&rackClaim_;out.rackPoseOwned=true;out.rackTravel=result.progress;
 const auto evaluated=EvaluateNativeActionPose(*result.pose,result.progress,false);if(!evaluated){stop();return;}
 auto presentation=std::make_shared<NativeRackPresentation>();
 presentation->assetName=result.pose->assetName;presentation->meshPath=result.pose->meshPath;
 presentation->clipSha256=result.pose->clipSha256;presentation->skeletonSha256=result.pose->skeletonSha256;
 for(auto bone:result.pose->partBones)presentation->partBones.emplace_back(bone);
 presentation->rigFingerprint=result.pose->rigFingerprint;presentation->actionToken=nativeRackAction_;
 presentation->trackingEpoch=s.trackingEpoch;presentation->pose=*evaluated;presentation->phase=result.phase;
 presentation->ownsHand=out.rackOwnsLeft;presentation->owner=s.nativeOwner;presentation->weapon=s.weapon;
 presentation->authorityWeapon=view->identity;presentation->authorityRevision=view->revision;
 presentation->authorityClientWeapon=s.nativeOwner.weapon;presentation->authorityCurrent=AuthorityCurrent;
 presentation->inputEvidence=s.input;presentation->progressEvidence=nativeRackProgressEvidence_;presentation->gunClaim=gun->token;
 presentation->gunDeadlineNs=std::min(gun->deadlineNs,presentation->inputEvidence.deadlineNs);
 if(presentation->ownsHand&&claim_){presentation->handClaim=claim_->token;
  presentation->handDeadlineNs=std::min(claim_->deadlineNs,presentation->inputEvidence.deadlineNs);}
 if(NativeRackPresentationFresh(*presentation,s.input.nowNs))out.nativeRack=std::move(presentation);
 else {out.rackPoseOwned=false;} // Presentation rejection never destroys a genuine current hand claim.
}
MagazinePhysicalProbeState Bc2PhysicalWeaponInteraction::ProbeState(std::int64_t)const noexcept {
 MagazinePhysicalProbeState p;p.active=heldMagazine_||pulling_||pending_||cleanup_.has_value();p.pending=pending_!=0;p.ownsHand=claim_.has_value();
 p.blocksEquipment=BlocksEquipment();p.acquired=acquired_;p.started=unseats_;p.submitted=seatsSubmitted_;p.completed=seatsCompleted_;p.physicalState=state_;
 p.phase=last_.interaction.phase;p.reason=last_.interaction.reason;p.cycle=identity_.weaponGeneration;return p;
}
void Bc2PhysicalWeaponInteraction::Report(std::ostream& out)const {
 out<<"\"physical_weapon_interaction\":{\"acquired\":"<<acquired_<<",\"submitted\":"<<submitted_<<",\"completed\":"<<completed_
  <<",\"cancelled\":"<<cancelled_<<",\"pending\":"<<pending_<<",\"held_magazine\":"<<heldMagazine_
  <<",\"short_b\":{\"rack_after_insert\":"<<(shortRackAfterInsert_?"true":"false")<<",\"racks\":"<<shortCounters_.racks
  <<",\"ejects\":"<<shortCounters_.ejects<<",\"confirms\":"<<shortCounters_.confirms<<",\"latched\":"<<shortCounters_.latched
  <<",\"latch_resolved\":"<<shortCounters_.latchResolved<<",\"latch_dropped\":"<<shortCounters_.latchDropped<<"}}";
}
} // namespace fvr::bc2
