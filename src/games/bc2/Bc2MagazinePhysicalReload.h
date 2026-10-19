#include "Bc2ReloadStartOrigin.h"
#include "Bc2ReloadObservation.h"
#include "Bc2MagazineLeaseObservation.h"
#include "Bc2ReloadKeepAlive.h"
#pragma once
#include "Bc2MagazinePresentation.h"
#include "Bc2NativeRackPresentation.h"
#include "Bc2ReloadRetirement.h"
#include "Bc2MagazineStart.h"
#include "Bc2MagazineChamber.h"
#include "Bc2FiringFunctionFields.h"
#include "fvr/interaction/Feedback.h"
#include "fvr/interaction/ActionPolicy.h"
#include "fvr/interaction/AmmoSupplyVisual.h"
#include <ostream>
namespace fvr::bc2 {
enum class MagazineCancelCause:unsigned {
 External=1,ApiOrClock,ReserveUnavailable,OwnerChanged,Mapping,InputOrClaim,
 ExplicitInput,KeepAlive,CounterExhausted,SourceUnavailable,InteractionRejected,InvalidStart,SeatSubmission,NativeObservationRejected,NativeObservationExpired
};
struct MagazinePhysicalApi {
 void* context=nullptr;
 std::optional<Bc2AmmoReserveLease> (*reserve)(void*)noexcept=nullptr;
 std::optional<ReloadHoldIdentity> (*identity)(void*)noexcept=nullptr;
 MagazineCycleStartResult (*start)(void*,const ReloadCycleControl&,const interaction::ManualReloadRequest&,const ReloadMagazineStartupPulse&)noexcept=nullptr;
 MagazineCycleStartResult (*inspectStart)(void*,const ReloadHoldIdentity&,std::uint64_t,const std::optional<ReloadMagazineStartupPulse>&)noexcept=nullptr;
 bool (*keep)(void*,const ReloadCycleControl&)noexcept=nullptr;
 std::optional<ReloadMagazineLease> (*lease)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 std::optional<ReloadMagazineGateAcknowledgement> (*gate)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 bool (*submit)(void*,const ReloadMagazineNativeRequest&)noexcept=nullptr;
 std::optional<ReloadMagazineAckEvidence> (*ack)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 void (*cancel)(void*)noexcept=nullptr;
 std::optional<ReloadCycleRetirement> (*retire)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 std::int64_t (*clock)(void*)noexcept=nullptr;
 ReloadKeepAliveResult (*keepObserved)(void*,const ReloadCycleControl&)noexcept=nullptr;
    ReloadReserveObservation (*reserveObserved)(void*)noexcept=nullptr;
 ReloadMagazineLeaseObservation (*leaseObserved)(void*,const ReloadHoldIdentity&,std::uint64_t)noexcept=nullptr;
 // Optional engine firing data for the current weapon (ReadFiringFunctionFields).
 // Read once per chamber identity. Null in the DLL until the offsets are verified live.
 std::optional<FiringFunctionFields> (*firing)(void*)noexcept=nullptr;
};
struct MagazinePhysicalSample {
 bool actionFlagsKnown=false;std::uint64_t actionHeld=0,actionPressed=0;
 MagazineFamilyEvidence family{};
 ReloadStateOwner nativeOwner{};interaction::HandInteractionSample input{};
 interaction::HandInteractionKey weapon{};std::uint64_t trackingEpoch=0,geometrySequence=0;
 math::Matrix4 bodyFromHand{};bool gripPressed=false,ejectPressed=false,cancel=false;
 std::string_view asset{};std::shared_ptr<const SelectedMeshesSnapshot> meshes;
 MagazineRawContact raw{};std::optional<interaction::HandInteractionSample> originalHandEvidence;
};
struct MagazinePhysicalResult {
 std::shared_ptr<const NativeRackPresentation> nativeRack;
 bool nativeRackRearStop=false,nativeRackForwardStarted=false,nativeRackClosed=false;
 MagazineTracking tracking{};interaction::DetachableMagazineResult interaction{};
 bool reloadHeld=false,blocksWeaponActions=false,ownsLeftHand=false;
 // Fresh idle empty-weapon evidence grants ordinary Reload input only. It is
 // not a native hold, physical insertion, or ammunition completion receipt.
 bool ordinaryReloadAllowed=false;
 unsigned acquired=0,submitted=0,completed=0;
 std::optional<interaction::AmmoSupplyVisualSample> bodyAmmo;
 // VR chamber: Fire is blocked only for a loaded, seated gun waiting on a
 // wired charging-handle rack. Applied only by ApplyMagazinePhysicalActions.
 bool blockFire=false;
 MagazineChamberView chamber{};
 // The loading hand was latched this frame (Slider settle). The adapter
 // decides the latch before Tick from LatchesLoadingHand().
 bool latchedLoadingHand=false;
 // Edges for the gun-event log: a slider settle began / a rack cleared NeedsRack this Tick.
 bool settleStarted=false,racked=false,boltReleased=false;
 // Committed physical-authority drop, distinct from cancellation or retain.
 bool physicalDrop=false,physicalDropFromWell=false;
 bool physicalUnseat=false,physicalSeat=false; // Applied edges for this current incarnation only.
 // C2 handle visual: 0..1 of the stroke while the hand holds the handle,
 // 0 while a rack is owed, -1 when no rack is owed (or the rack is off).
 float rackTravel=-1;
 bool rackPoseOwned=false;
 // While a rack is owed, the left hand at the handle (or racking) belongs
 // to the rack, not to the two-hand support grip.
 bool rackOwnsLeft=false;
 // Rack diagnostic for this gather while a rack is owed: the palm point
 // against the handle line, the recognizer's phase/reason, and both points
 // in world metres.
 struct RackTrace {bool grip=false;unsigned phase=0,reason=0;float distance=0,along=0,lateral=0,travel=0;
  std::array<float,3> handleWorld{},palmWorld{};};
 std::optional<RackTrace> rackTrace;
 // Drain like PhysicalReloadResult::feedback (Bc2Gameplay writeFeedback).
 std::array<interaction::FeedbackEvent,4> feedback{};
 unsigned feedbackCount=0;
};
// A physically submitted/attached magazine may free the support hand while
// its native ammo transaction still blocks Fire and equipment changes. This
// tests original current-frame evidence; it never advances or acknowledges it.
inline bool MagazineBlocksSupport(const MagazinePhysicalResult& r,std::int64_t now)noexcept {
 if(r.ownsLeftHand)return true;
 if(!r.blocksWeaponActions)return false;
 const auto phase=r.interaction.phase;
 const auto& target=r.tracking.target;
 return !((phase==interaction::DetachableMagazinePhase::AwaitingSeat||phase==interaction::DetachableMagazinePhase::Complete)&&
  r.tracking.cycle&&target&&target->role==interaction::MagazinePropRole::Attached&&
  !target->handTarget&&!target->handClaim.id&&MagazineTargetFresh(r.tracking,now));
}
inline void ApplyMagazinePhysicalActions(interaction::ActionOutput& out,const MagazineEquipmentProfile* profile,
 const MagazinePhysicalResult& magazine)noexcept {
 const bool ordinary=magazine.ordinaryReloadAllowed&&!magazine.blocksWeaponActions&&!magazine.reloadHeld;
 if(profile&&profile->Ready()&&!ordinary){out.held&=~interaction::Reload;out.pressed&=~interaction::Reload;}
 if(magazine.reloadHeld)out.held|=interaction::Reload;
 if(magazine.blocksWeaponActions){
  constexpr auto blocked=interaction::Fire|interaction::AlternateFire|interaction::Use|interaction::NextWeapon|interaction::PreviousWeapon;
  out.held&=~blocked;out.pressed&=~blocked;
 }
 if(magazine.blockFire){out.held&=~interaction::Fire;out.pressed&=~interaction::Fire;}
}
// Slider release-to-finish latch for the shared HandInteraction sample: while
// the consumer holds the loading hand, a physical grip release is not a
// HandInteraction release. Physical grip still goes to the consumer as
// MagazinePhysicalSample::gripPressed (from ReloadGripActive on the raw squeeze).
inline void LatchLoadingHand(interaction::HandInteractionSample& s,bool latched)noexcept {
 if(latched)s.released[0]=false;
}
inline void ApplyMagazinePhysicalActions(interaction::ActionOutput& out,std::string_view asset,
 const MagazinePhysicalResult& magazine)noexcept {
 ApplyMagazinePhysicalActions(out,FindMagazineEquipment(asset),magazine);
}
struct MagazinePhysicalProbeState {
 bool active=false,nativeHolding=false,retiring=false,pending=false,ownsHand=false,blocksEquipment=false;
 unsigned acquired=0,started=0,submitted=0,completed=0;
 interaction::DetachableMagazinePhase phase=interaction::DetachableMagazinePhase::Attached;
 interaction::DetachableMagazineReason reason=interaction::DetachableMagazineReason::None;
 std::uint64_t cycle=0;
 std::optional<Bc2AmmoReserveLease> reserve;
 unsigned originalReturns=0;bool originalReturning=false;
 std::optional<interaction::OriginalMagazine> original;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt;
 std::optional<interaction::PhysicalWeaponSnapshot> physicalState;
};
// Update shared HandInteraction + renew right GunHold first. Call before support
// grab arbitration so a real magazine contact can claim Mechanism; it never
// steals existing support/sight. Feed raw anatomical wrist from the prior
// renderer sample with ORIGINAL HandInteraction evidence; never guided wrist.
// Wire reloadHeld as the single native Reload pulse, suppress Fire/equipment
// while blocksWeaponActions. Native gate enabling is separately verified.
class Bc2MagazinePhysicalReload {
public:
 static interaction::AmmoSupplyConfig DefaultPouch()noexcept;
 explicit Bc2MagazinePhysicalReload(bool enabled=false,MagazinePhysicalApi api={},
     interaction::AmmoSupplyConfig pouch=DefaultPouch())noexcept;
 MagazinePhysicalResult Tick(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
 void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&,MagazineCancelCause=MagazineCancelCause::External)noexcept;
 bool BlocksEquipment()const noexcept{return active_||blocksCurrent_;}
 void Report(std::ostream&)const;
 void EnableBodyAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept {bodyAmmoEnabled_=enabled;bodyAmmoFrame_=frame;}
 // Render-only: the pouch contacts (body frame, metres) the hand takes from.
 std::array<std::optional<interaction::AmmoSupplyContact>,2> PouchContacts()const noexcept {
  return {interaction::AmmoSupplyContact{pouch_.pouchCenterMeters,pouch_.pouchRadiusMeters},pouch_.alternateContact};}
 interaction::SupplyAnchorFrame PouchFrame()const noexcept {return bodyAmmoFrame_;}
 // Charging-handle rack input. Off by default: it needs a live-mapped
 // MagazineEquipmentProfile::chargingHandle, and none is admitted. While off,
 // BC2's own empty-reload clip counts as the rack.
 void EnableChamberRack(bool enabled=true)noexcept {rackEnabled_=enabled;}
 // Button removal remains available independently of physical receiver grabs.
 void EnableGrabRemoval(bool enabled=true)noexcept {grabRemovalEnabled_=enabled;}
 // Decide before Tick, from the state the previous Tick left: true while a
 // Slider insertion is settling or would settle on release. The adapter then
 // keeps HandInteraction released[0] false (LatchLoadingHand); Tick passes
 // the AmmoSupply grip as held itself.
 bool LatchesLoadingHand()const noexcept {return enabled_&&interaction_.HoldsLoadingHandThroughRelease();}
 MagazineChamberView Chamber()const noexcept {return chamber_.View(lastReserve_?lastReserve_->loaded:chamber_.LastLoaded());}
 MagazinePhysicalProbeState ProbeState(std::int64_t now)const noexcept {
  return {active_,lease_&&lease_->allThreeHeld&&lease_->deadlineNs>now,retiring_,bool(supply_.Pending()),
   bool(last_.removalClaim)||bool(supply_.Held()),BlocksEquipment(),acquired_,started_,submitted_,completed_,last_.phase,last_.reason,
   owners_.cycle,lastReserve_,originalReturns_,originalReturning_,originalMagazine_,originalReceipt_};
 }
private:
 bool Api()const noexcept;
 bool RollbackUnstarted(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
 void ObserveRetirement(const MagazinePhysicalSample&)noexcept;
 std::int64_t Now(std::int64_t fallback)const noexcept;
 void PublishReplacementFrame(MagazineTracking&,const MagazinePhysicalSample&,bool canCapture=false)noexcept;
 MagazinePhysicalResult TickCore(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent,bool latched)noexcept;
 void ObserveChamber(const MagazinePhysicalSample&,const Bc2AmmoReserveLease&,interaction::HandInteraction&)noexcept;
 void Feedback(MagazinePhysicalResult&,interaction::FeedbackKind,const interaction::HandInteractionSample&,unsigned hand=0)noexcept;
 Bc2MagazineChamber chamber_{};bool rackEnabled_=false,settling_=false;float rackTravel_=-1,rackHeldTravel_=0;bool rackOwnsLeft_=false;std::int64_t rackSourceGapNs_=0;unsigned rackSourceGaps_=0,boltReleases_=0;std::optional<MagazinePhysicalResult::RackTrace> rackTrace_;
 std::optional<MagazineChamberKey> firingKey_;ResolvedMagazineAction action_{};
 std::optional<interaction::ChargingHandleRack> rack_;
 std::uint64_t nextFeedback_=0;unsigned feedbackSent_=0,feedbackRejected_=0,settles_=0,racks_=0,latchedTicks_=0;
 const MagazineEquipmentProfile* profile_=&Xm8MagazineEquipment();
 interaction::AmmoSupply supply_;interaction::DetachableMagazine interaction_{Xm8MagazineConfig()};
 interaction::AmmoSupplyConfig pouch_{};bool bodyAmmoEnabled_=false;
 interaction::SupplyAnchorFrame bodyAmmoFrame_=interaction::SupplyAnchorFrame::HeadYaw;
 std::optional<MagazineReplacementFrame> replacementFrame_;
 std::optional<MagazineCarryFrame> removalFrame_;
 ReloadKeepAliveResult Keep(const ReloadCycleControl&,std::int64_t)noexcept;
 std::int64_t acceptedControlDeadline_=0;
 std::int64_t observationGapDeadline_=0;unsigned observationDeferred_=0,observationExpired_=0;
 ReloadStartOrigin startOrigin_{};
 MagazinePhysicalApi api_{};bool enabled_=false,active_=false,retiring_=false,gateApplied_=false,drained_=false,blocksCurrent_=false;
 bool grabRemovalEnabled_=false;
 bool startupUnknown_=false;MagazineCycleStartResult startupResult_=MagazineCycleStartResult::NotStarted;
 unsigned unstarted_=0,startInspections_=0;
 Bc2MagazineOwnerMap owners_{};
 interaction::DetachableMagazineResult last_{};
 std::optional<interaction::ManualReloadRequest> unseat_;
 std::optional<ReloadMagazineLease> lease_,submittedLease_;
 // Seat submission waiting for a fresh reserve lease or this tick's original
 // hand evidence (F2000 rack run 155932: one missing tick cancelled a seated
 // reload and the native reload ran on). Retried up to SeatRetryNs.
 struct PendingSeat {interaction::ManualReloadRequest request{};interaction::ReloadInsertionSeat seat{};std::int64_t deadlineNs=0;};
 std::optional<PendingSeat> pendingSeat_;unsigned seatDeferrals_=0,seatRetrySubmits_=0,seatMissing_[3]{};
 static constexpr std::int64_t SeatRetryNs=300000000;
 std::optional<Bc2AmmoReserveLease> lastReserve_,retainedReserve_,originalReserve_;
 std::optional<interaction::OriginalMagazine> originalMagazine_;
 std::optional<interaction::OriginalMagazineReturnReceipt> originalReceipt_;
 bool originalReturning_=false;unsigned originalReturns_=0,originalReturnRejected_=0;
 std::int64_t originalReturnNs_=0;
 MagazineCancelCause cancelCause_=MagazineCancelCause::External;unsigned heldReadDeferrals_=0;
 unsigned reserveReadMisses_=0,transferReadDeferrals_=0,transferReadExpirations_=0,retireAttachedWaits_=0;
 std::optional<ReloadMagazineGateAcknowledgement> gate_;
 std::optional<ReloadMagazineStartupPulse> startupPulse_;
 std::optional<ReloadMagazineAckEvidence> ack_;
 std::optional<ReloadCycleRetirement> retirement_;
 struct Evidence{interaction::AmmoSupplySample supply{};};
 std::array<std::optional<Evidence>,32> history_{};std::size_t historyNext_=0;
 std::uint64_t nextCycle_=0,lastSequence_=0;
 std::int64_t pulseUntil_=0,lastNow_=0,lastInputDeadline_=0;
 unsigned acquired_=0,started_=0,submitted_=0,completed_=0,cancelled_=0,reconciled_=0,unstartedResets_=0;
 std::array<unsigned,6> removalBlocked_{};unsigned idleReadDeferrals_=0,deferredSupplyDrops_=0,idleRetainedReads_=0,startupReadDeferrals_=0;
 std::int64_t startNs_=0;static constexpr std::int64_t StartupReadGraceNs=750000000;
 // A held reload (pull to guided insertion, before submission) rides a reserve-read gap this long past the
 // last good read before cancelling (XM8 run 164845: cancel cause 3 after the
 // capture, gun left empty). Bits of why the held-read retention failed.
 static constexpr std::int64_t HeldReadGapNs=500000000;unsigned heldReadGapRides_=0,reserveCancelWhy_=0;
 // branch: 0 other early return, 2 idle deferral, 3 held deferral, 4 deferral cancel, 5 rejected read,
 // 7 removal not permitted, 8 active update, 9 idle permitted update, 11 mapped, 13 no supply source,
 // 20 entry, 21 clock ok, 22 idle retained read, 23 observed deferral, 24 startup missing read.
 struct GripRow {std::uint64_t input=0;unsigned branch=0;int loaded=-1,capacity=-1,reserve=-1;bool ready=false,idle=false,active=false;unsigned phase=0,reason=0;bool neutral=false;};
 std::array<GripRow,256> gripTrace_{};std::uint64_t gripTraceNext_=0,gripTraceTotal_=0;unsigned branch_=0;std::optional<Bc2AmmoReserveLease> traceGate_;
 struct NativeLeaseEvidence {
  bool present=false,verified=false,identityMatches=false,cycleMatches=false,held=false;
  std::uint64_t sequence=0,cycle=0;std::int64_t observedNs=0,deadlineNs=0;
 };
 struct NativeBoundaryEvidence {
  interaction::MagazineNativeObservation supplied{};
  NativeLeaseEvidence prior{},returned{},accepted{};
  std::optional<interaction::HandClaim> currentClaim;
  bool gateApplied=false,sourceChecked=false,sourceAvailable=false,ownerMatches=false,weaponMatches=false;
  unsigned phase=0;std::int64_t reserveObservedNs=0,reserveDeadlineNs=0,controlDeadlineNs=0;
 };
 std::optional<NativeBoundaryEvidence> nativeBoundary_;
 struct Event {unsigned kind=0,phase=0,reason=0;std::uint64_t cycle=0,input=0,geometry=0,item=0,request=0,seat=0;
  std::int64_t observedNs=0,nowNs=0;float progress=0;unsigned cause=0;
  std::uint64_t originalItem=0,originalGeneration=0;unsigned originalRounds=0;int nativeLoaded=-1,nativeReserve=-1;ReloadStartOrigin startOrigin{};std::optional<interaction::MagazineClaimFailure> claimFailure;
  interaction::MagazineNativeFailureCheck nativeFailureCheck=interaction::MagazineNativeFailureCheck::None;
  std::optional<NativeBoundaryEvidence> nativeBoundary;
  std::optional<interaction::MagazineMotionFailure> motionFailure;};
 std::array<Event,128> events_{};unsigned eventCount_=0,eventDropped_=0;
 void Record(unsigned,const interaction::HandInteractionSample&,std::uint64_t geometry=0)noexcept;
};
} // namespace fvr::bc2
