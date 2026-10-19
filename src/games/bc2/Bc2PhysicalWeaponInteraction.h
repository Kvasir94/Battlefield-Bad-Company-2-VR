#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2PhysicalWeaponRuntime.h"
#include "Bc2InteractionZones.h"
#include "Bc2ShortReloadAction.h"
#include "fvr/interaction/PistolSlide.h"
#include <deque>

namespace fvr::bc2 {
// Physical gestures submit finite-inventory commands; native reload states,
// animation timers and three-copy hold leases do not participate. This object
// owns transient hand gestures only. The runtime owns ammunition across swaps.
class Bc2PhysicalWeaponInteraction {
public:
 explicit Bc2PhysicalWeaponInteraction(bool enabled=true,
  interaction::AmmoSupplyConfig pouch=Bc2MagazinePhysicalReload::DefaultPouch())noexcept;
 MagazinePhysicalResult Tick(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
 void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,
  MagazineCancelCause=MagazineCancelCause::External)noexcept;
 bool LatchesLoadingHand()const noexcept;
 bool BlocksEquipment()const noexcept{return pending_!=0||cleanup_.has_value()||pulling_||slideClaim_||rackClaim_||!slideCommands_.empty();}
 MagazinePhysicalProbeState ProbeState(std::int64_t)const noexcept;
 void Report(std::ostream&)const;
 void EnableBodyAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept {bodyAmmo_=enabled;frame_=frame;}
 void EnableChamberRack(bool=true)noexcept {} // Authority mechanics always apply.
 // Interaction zone table (Bc2InteractionZones.h): widens magazine grab, pull
 // breakaway, magwell capture, pouch and charging-handle ranges; hover cues.
 // Takes effect on the next weapon/profile binding.
 void ConfigureZones(const interaction::InteractionZoneTable& zones)noexcept {zones_=zones;}
 // A ejects by default. Receiver-grab removal is a separate opt-in; pouch,
 // insertion and action/slide gestures retain their physical grip input.
 void EnableGrabRemoval(bool enabled=true)noexcept {grabRemovalEnabled_=enabled;}
 // Short B after our own insertion completes the reload (rack or confirm),
 // never ejects (Bc2ShortReloadAction.h). false restores the aca6ffd rule.
 void EnableShortRackAfterInsert(bool enabled=true)noexcept {shortRackAfterInsert_=enabled;if(!enabled){afterInsertUntil_=0;shortLatchUntil_=0;}}
 struct ShortReloadCounters {unsigned racks=0,ejects=0,confirms=0,latched=0,latchResolved=0,latchDropped=0;};
 ShortReloadCounters ShortReload()const noexcept{return shortCounters_;}
 struct ZoneCounters {unsigned magazineHover=0,handleHover=0,pouchHover=0;};
 ZoneCounters HoverCues()const noexcept{return hoverCues_;}
 std::array<std::optional<interaction::AmmoSupplyContact>,2> PouchContacts()const noexcept {
  return {interaction::AmmoSupplyContact{pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters},pouch_.alternateContact};}
 interaction::SupplyAnchorFrame PouchFrame()const noexcept{return frame_;}
 MagazineChamberView Chamber()const noexcept{return last_.chamber;}
private:
 enum class ActionOrigin : std::uint8_t {None,Button,Hand,Recovery};
 bool Queue(interaction::PhysicalWeaponCommandKind,std::uint64_t magazine=0,int rounds=0,ActionOrigin=ActionOrigin::None)noexcept;
 void CleanupMagazine(const interaction::PhysicalWeaponIdentity&,std::uint32_t clientWeapon,
  interaction::PhysicalWeaponCommandKind,std::uint64_t magazine,bool abandoned)noexcept;
 void RetryCleanup()noexcept;
 void Drain(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&,MagazinePhysicalResult&)noexcept;
 bool Claim(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&,
  interaction::HandClaimKind,interaction::HandInteractionKey,const interaction::HandInteractionSample&,bool slide=false,
  const interaction::HandClaim* replaced=nullptr)noexcept;
 void Release(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
 bool Pouch(const MagazinePhysicalSample&,bool releasing=false)const noexcept;
 void HoverCues(const MagazinePhysicalSample&,const math::Matrix4& hand,bool raw,bool handFree,MagazinePhysicalResult&)noexcept;
 interaction::InteractionZoneTable zones_=LegacyInteractionZones();
 interaction::ZoneHysteresis magazineZone_,handleZone_,pouchZone_;ZoneCounters hoverCues_{};
 void Feedback(MagazinePhysicalResult&,interaction::FeedbackKind,const interaction::HandInteractionSample&,unsigned hand=0)noexcept;
 bool enabled_=true,bodyAmmo_=false,neutral_=false,pulling_=false,cancelPending_=false,ejectDrop_=false,dropFromWell_=false,heldOriginal_=false;
 bool grabRemovalEnabled_=false;
 interaction::SupplyAnchorFrame frame_=interaction::SupplyAnchorFrame::HeadYaw;
 interaction::AmmoSupplyConfig pouch_{};
 interaction::PhysicalWeaponIdentity identity_{},pendingIdentity_{};
 // A released hand does not release the authority obligation. Keep this exact
 // incarnation until its cleanup succeeds or fresh ownership proves it gone.
 struct Cleanup {
  interaction::PhysicalWeaponIdentity identity{};
  std::uint32_t clientWeapon=0;
  interaction::PhysicalWeaponCommandKind kind=interaction::PhysicalWeaponCommandKind::RetainMagazine;
  std::uint64_t magazine=0;
  bool abandoned=false;
 };
 std::optional<Cleanup> cleanup_;
 std::uint32_t clientWeapon_=0,pendingClientWeapon_=0;
 interaction::HandInteractionOwner handOwner_{};
 const MagazineEquipmentProfile* profile_=nullptr;
 std::optional<interaction::HandClaim> claim_;
 std::optional<interaction::ReloadInsertion> insertion_;
 std::optional<interaction::ChargingHandleRack> rack_;
 std::optional<NativeRack> nativeRack_;
 NativeRackResult nativeRackResult_{};
 interaction::HandInteractionSample nativeRackProgressEvidence_{};
 interaction::HandClaimToken nativeRackGun_{};
 ReloadStateOwner nativeRackOwner_{};
 std::uint64_t nativeRackTrackingEpoch_=0;
 std::int64_t nativeRackGunDeadlineNs_=0;
 std::uint64_t nativeRackAction_=0;
 bool nativeRackCycleDue_=false,nativeRackForward_=false,nativeRackArmed_=false,nativeRackClosing_=false;
 void StopNativeRack(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
 void RefreshNativeRackOwnership(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&)noexcept;
 void TickNativeRack(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&,
  const math::Matrix4&,bool raw,MagazinePhysicalResult&)noexcept;
 std::optional<interaction::PistolSlide> slide_;
 bool slideClaim_=false,rackClaim_=false;
 struct SlideCommand {interaction::PhysicalWeaponCommandKind kind;ActionOrigin origin;};
 std::deque<SlideCommand> slideCommands_;
 interaction::ReloadInsertionResult insertResult_{};
 math::Matrix4 pullStart_{},pullPrevious_{},heldItem_{};
 float insertionMinimum_=0,pullProgress_=0;bool insertionArmed_=false,insertionObserved_=false;
 struct Evidence {interaction::HandInteractionSample input;interaction::HandClaim item,gun;};
 std::array<std::optional<Evidence>,32> history_{};std::size_t historyNext_=0;
 std::uint64_t heldMagazine_=0,pending_=0,pendingMagazine_=0,sequence_=0,nextFeedback_=0;
 interaction::PhysicalWeaponCommandKind pendingKind_=interaction::PhysicalWeaponCommandKind::AcquireMagazine;
 // Completion feedback/audio uses the admitted gesture, even after the hand
 // releases while the authority receipt is in flight.
 ActionOrigin pendingActionOrigin_=ActionOrigin::None;
 unsigned acquired_=0,submitted_=0,completed_=0,cancelled_=0;
 unsigned seatsSubmitted_=0,seatsCompleted_=0,unseats_=0;
 std::optional<interaction::PhysicalWeaponSnapshot> state_;
 MagazinePhysicalResult last_{};
 // Post-insert window: opened by our applied SeatMagazine, refreshed by an
 // applied rack while open; a shot, unseat or identity change closes it.
 bool shortRackAfterInsert_=true;
 std::int64_t afterInsertUntil_=0,shortLatchUntil_=0;
 std::uint64_t afterInsertMagazine_=0,afterInsertSpent_=0;
 ShortReloadCounters shortCounters_{};
 void OpenAfterInsert(std::int64_t now,std::uint64_t magazine)noexcept;
 bool AfterInsert(const interaction::PhysicalWeaponSnapshot&,std::int64_t now)noexcept;
};

// Select one authority for an entire session. Keeping the legacy consumer
// here makes the environment kill switch a complete fallback, including
// diagnostics and presentation, without running both reload paths together.
class Bc2MagazineFrontend {
public:
 Bc2MagazineFrontend(bool enabled=true,MagazinePhysicalApi api={},
  interaction::AmmoSupplyConfig pouch=Bc2MagazinePhysicalReload::DefaultPouch())noexcept
  :legacy_(enabled,api,pouch),physical_(enabled,pouch){}
 MagazinePhysicalResult Tick(const MagazinePhysicalSample& s,interaction::HandInteraction& h,std::uint64_t& i)noexcept {
  return physicalWeaponRuntime::Enabled()?physical_.Tick(s,h,i):legacy_.Tick(s,h,i);}
 void Cancel(const interaction::HandInteractionSample& s,interaction::HandInteraction& h,MagazineCancelCause c=MagazineCancelCause::External)noexcept {
  if(physicalWeaponRuntime::Enabled())physical_.Cancel(s,h,c);else legacy_.Cancel(s,h,c);}
 bool BlocksEquipment()const noexcept{return physicalWeaponRuntime::Enabled()?physical_.BlocksEquipment():legacy_.BlocksEquipment();}
 bool LatchesLoadingHand()const noexcept{return physicalWeaponRuntime::Enabled()?physical_.LatchesLoadingHand():legacy_.LatchesLoadingHand();}
 MagazinePhysicalProbeState ProbeState(std::int64_t now)const noexcept{return physicalWeaponRuntime::Enabled()?physical_.ProbeState(now):legacy_.ProbeState(now);}
 MagazineChamberView Chamber()const noexcept{return physicalWeaponRuntime::Enabled()?physical_.Chamber():legacy_.Chamber();}
 void EnableBodyAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept {
  legacy_.EnableBodyAmmo(enabled,frame);physical_.EnableBodyAmmo(enabled,frame);}
 void EnableChamberRack(bool enabled=true)noexcept{legacy_.EnableChamberRack(enabled);}
 void ConfigureZones(const interaction::InteractionZoneTable& zones)noexcept{physical_.ConfigureZones(zones);}
 void EnableGrabRemoval(bool enabled=true)noexcept{legacy_.EnableGrabRemoval(enabled);physical_.EnableGrabRemoval(enabled);}
 void EnableShortRackAfterInsert(bool enabled=true)noexcept{physical_.EnableShortRackAfterInsert(enabled);}
 Bc2PhysicalWeaponInteraction::ShortReloadCounters ShortReload()const noexcept{return physical_.ShortReload();}
 Bc2PhysicalWeaponInteraction::ZoneCounters HoverCues()const noexcept{return physical_.HoverCues();}
 std::array<std::optional<interaction::AmmoSupplyContact>,2> PouchContacts()const noexcept {
  return physicalWeaponRuntime::Enabled()?physical_.PouchContacts():legacy_.PouchContacts();}
 interaction::SupplyAnchorFrame PouchFrame()const noexcept{return physicalWeaponRuntime::Enabled()?physical_.PouchFrame():legacy_.PouchFrame();}
 void Report(std::ostream& out)const {if(physicalWeaponRuntime::Installed())physical_.Report(out);else legacy_.Report(out);}
private:
 Bc2MagazinePhysicalReload legacy_;
 Bc2PhysicalWeaponInteraction physical_;
};
} // namespace fvr::bc2
