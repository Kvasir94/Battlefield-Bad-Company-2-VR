#pragma once
#include "fvr/interaction/AmmoSupply.h"

namespace fvr::interaction {
struct DetachableMagazineConfig {
    ReloadInsertionProfile insertion{};
    HandInteractionKey removalContact{};
    InteractionHand hand=InteractionHand::Left;
    float grabRadiusMeters=.09f,pullMeters=.06f,pullReleaseMeters=.15f,maxPullStepMeters=.05f;
    std::int64_t ackTimeoutNs=4000000000,transactionTimeoutNs=30000000000;
    // An input guide may follow the original wrist rather than the magazine
    // bone. In that case the adapter supplies the actual seated item transform;
    // deriving attachment from the guide's terminal would move the magazine.
    std::optional<math::Matrix4> attachedItem;
};
// Adapter evidence. Unseat Applied means that native reload is safely gated,
// NOT that loaded rounds were removed from native inventory. Seat Applied must
// come from the exact native transfer/owner observer. Geometry cannot set it.
struct MagazineNativeObservation {
    HandInteractionOwner owner{};HandInteractionKey weapon{};
    std::uint64_t cycle=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool bindingsVerified=false,allThreeHeld=false,acknowledgementVerified=false;
    ManualReloadAck acknowledgement{};
};
// Immutable fact captured before this exact removal. The original magazine is
// not replacement ammunition and is never admitted into the reserve provider.
struct OriginalMagazine {
    HandInteractionOwner owner{};HandInteractionKey weapon{},item{},profile{},pool{};
    std::uint64_t trackingEpoch=0,sourceSequence=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned rounds=0,capacity=0;
    bool operator==(const OriginalMagazine&)const=default;
};
struct OriginalMagazineReturnReceipt {
    OriginalMagazine original{};std::uint64_t cycle=0,seat=0,retirement=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Adapter proves native work drained, all copies idle with the SAME counts,
    // and native attachment restored. This is not an ammunition transfer ack.
    bool verified=false;
};
struct DetachableMagazineSample {
    HandInteractionSample input{};
    HandInteractionKey weapon{};
    std::uint64_t trackingEpoch=0,geometrySequence=0,intent=0;
    // Renderer N-1 evidence may be consumed only if this exact packet was
    // previously retained here and by HandInteraction. Empty means current.
    std::optional<HandInteractionSample> geometryInput{};
    bool gripPressed=false,ejectPressed=false,cancel=false,removalPermitted=true;
    // Explicit adapter observation gap; never fabricates native hold evidence.
    bool nativeObservationDeferred=false;
    math::Matrix4 weaponFromHandMeters{}; // RAW wrist, coherent with input.
    MagazineNativeObservation native{};
    // Produced/renewed by the EXISTING AmmoSupply. Removed magazines are never
    // represented here: they cannot replenish reserve or become replacement ammo.
    std::optional<AmmoSupplyObject> replacement{};
    std::optional<OriginalMagazine> original{};
};
enum class DetachableMagazinePhase:std::uint8_t {
    Attached,PreparingRemoval,Pulling,RemovedHeld,WellEmpty,ReplacementHeld,Guided,AwaitingSeat,Complete,Cancelled,AwaitingOriginalReturn
};
enum class DetachableMagazineReason:std::uint8_t {
    None,InvalidConfig,InvalidInput,StaleInput,IdentityChanged,TrackingLost,
    UnverifiedNative,NativeCycleChanged,GunClaimLost,NeedNeutral,OutsideMagazine,
    HandUnavailable,ClaimLost,PoseJump,PullAbandoned,ReplacementRejected,
    NativeRejected,Explicit,NeedsReconciliation,CounterExhausted
};
enum class MagazinePropRole:std::uint8_t {Attached,Removed,Replacement,Hidden};
struct MagazinePropTarget {
    MagazinePropRole role=MagazinePropRole::Attached;
    HandInteractionOwner owner{};HandInteractionKey weapon{},profile{},item{};
    HandClaimToken handClaim{},gunClaim{};
    std::uint64_t trackingEpoch=0,inputSequence=0,nativeCycle=0;
    std::int64_t observedNs=0,deadlineNs=0;
    math::Matrix4 weaponFromItemMeters{},weaponFromHandMeters{};
    bool handTarget=false;
    std::optional<OriginalMagazine> originalMagazine{};
};
// Bounded evidence captured only when renewal rejects the owned removal claim.
// It does not alter ownership, input/native deadlines, or cancellation policy.
struct MagazineClaimFailure {
    HandInteractionReason reason=HandInteractionReason::None;
    HandClaim expected{};std::optional<HandClaim> current;
    std::uint64_t geometrySequence=0;
    std::int64_t nativeDeadlineNs=0;
};
enum class MagazineNativeFailureCheck:std::uint8_t {None,Observation,UnseatAcknowledgement,HeldCycle};
enum class MagazineMotionFailureCheck:std::uint8_t {GripReleased,TranslationStep,RotationStep,OffAxis,ReversePull};
struct MagazineMotionFailure {
    MagazineMotionFailureCheck check{};
    bool gripPressed=false,released=false,pulled=false;
    std::uint64_t geometrySequence=0;
    double measured=0,limit=0;
};
struct DetachableMagazineResult {
    DetachableMagazinePhase phase=DetachableMagazinePhase::Attached;
    DetachableMagazineReason reason=DetachableMagazineReason::None;
    ManualReloadResult transaction{};
    ReloadInsertionResult insertion{};
    std::optional<MagazinePropTarget> prop{};
    std::optional<ReloadInsertionSeat> seat{};
    std::optional<HandClaim> removalClaim{};
    bool removalGrabbed=false,physicallyRemoved=false,cancelNativeCycle=false;
    std::uint64_t nativeCycle=0,cancelledRequest=0;
    std::optional<ReloadInsertionSeat> originalSeat{};
    std::optional<OriginalMagazine> original{};
    std::optional<MagazineClaimFailure> claimFailure{};
    MagazineNativeFailureCheck nativeFailureCheck=MagazineNativeFailureCheck::None;
    std::optional<MagazineMotionFailure> motionFailure{};
};
struct MagazineRebaseline {
    HandInteractionOwner retiredOwner{};HandInteractionKey retiredWeapon{};
    std::uint64_t retiredCycle=0,cancelledRequest=0,retirement=0;
    std::int64_t observedNs=0,deadlineNs=0;
    // Native adapter proves outstanding old work drained AND current attached
    // magazine state re-established. Elapsed time/owner loss is not this proof.
    bool verified=false;
};
// Reusable interaction coordinator. Owns only its Mechanism claim during old
// magazine removal; shares AmmoSupply, ReloadInsertion and ManualReload. No
// native addresses, commands, ammunition writes, new reserve, or implicit ack.
// Adapter updates shared hands and the opposite GunHold before calling Update.
class DetachableMagazine {
public:
    explicit DetachableMagazine(DetachableMagazineConfig)noexcept;
    DetachableMagazine(const DetachableMagazine&)=delete;
    DetachableMagazine& operator=(const DetachableMagazine&)=delete;
    bool ValidConfig()const noexcept;
    DetachableMagazineResult Update(const DetachableMagazineSample&,HandInteraction&)noexcept;
    DetachableMagazineResult Cancel(const HandInteractionSample&,HandInteraction&)noexcept;
    bool Rebaseline(const HandInteractionSample&,HandInteraction&,const MagazineRebaseline&)noexcept;
    // No transaction exists while idle Attached. A focus/equip/gap boundary may
    // forget neutral/contact history here without retiring any native work.
    bool ResetAttached()noexcept;
    // A cancel that never reached the game (no gate, no native cycle, no
    // pending request, magazine never removed; the caller proves no native
    // cycle was started) has nothing to retire, so no retirement receipt will ever arrive for Rebaseline. Clear
    // it to a fresh Attached state that needs a new neutral packet, as KF2 VR
    // re-arms on neutral. Refused for anything the game might still own.
    bool ResetUnstarted()noexcept;
    // Idle ManualReload resets ignored while Attached (diagnostic count).
    std::uint64_t IdleTransactionResets()const noexcept{return idleTransactionResets_;}
    bool ArmedNeutral()const noexcept{return neutral_;} // diagnostic
    // A packet gap while idle Attached with nothing in flight (the adapter's
    // native read was deferred, live BC2) up to 500 ms is not a cancel; a held
    // grip across it still needs the armed neutral from before.
    bool IdleGapTolerated(std::int64_t observedNs)const noexcept{
        return cached_.phase==DetachableMagazinePhase::Attached&&!pending_&&!removal_&&!gateAccepted_&&!cycle_&&!pulled_&&
            observedNs-deadline_<500000000;}
    // Changes measured data only while idle, retaining all event watermarks.
    bool Reconfigure(DetachableMagazineConfig)noexcept;
    // Adapter has proven that this exact pre-gate request never registered any
    // native work. This only abandons local intent; it acknowledges nothing.
    bool RejectUnstarted(const HandInteractionSample&,HandInteraction&,const ManualReloadRequest&)noexcept;
    bool CompleteOriginalReturn(const HandInteractionSample&,HandInteraction&,const OriginalMagazineReturnReceipt&)noexcept;
    // A Slider insertion (replacement or original) is settling, or a release on
    // the next fresh sample would start its settle. While true the adapter keeps
    // the loading hand's claims alive (HandInteraction released=false, the
    // AmmoSupply grip held) but still passes the PHYSICAL grip as
    // DetachableMagazineSample::gripPressed so the recognizer sees the release.
    [[nodiscard]] bool HoldsLoadingHandThroughRelease()const noexcept {
        return insertion_.Settling()||insertion_.FinishesOnRelease()||
            (originalInsertion_&&(originalInsertion_->Settling()||originalInsertion_->FinishesOnRelease()));
    }
private:
    DetachableMagazineResult Snapshot()const noexcept;
    DetachableMagazineResult Reject(const HandInteractionSample&,HandInteraction&,DetachableMagazineReason)noexcept;
    MagazinePropTarget Target(const DetachableMagazineSample&,const HandClaim&,MagazinePropRole,
        const math::Matrix4&,const math::Matrix4&,std::optional<HandClaim> claim={})const noexcept;
    DetachableMagazineConfig config_{};ReloadInsertion insertion_;ManualReload transaction_;
    DetachableMagazineResult cached_{};
    HandInteractionOwner owner_{};HandInteractionKey weapon_{};
    std::optional<HandClaim> removal_{};std::optional<ManualReloadRequest> pending_{};
    math::Matrix4 grabRaw_{},previousRaw_{},itemFromGrip_{};
    std::optional<OriginalMagazine> original_{};
    std::optional<ReloadInsertion> originalInsertion_{};
    std::optional<ReloadInsertionSeat> originalSeat_{};std::int64_t originalSeatNs_=0;
    std::uint64_t epoch_=0,sequence_=0,nextGesture_=0,cycle_=0,lastRetirement_=0,cancelledRequest_=0,idleTransactionResets_=0;
    unsigned heldUnpermitted_=0;
    struct GeometryEvidence {HandInteractionSample input{};HandClaim gun{};std::optional<AmmoSupplyObject> replacement{};std::optional<HandClaim> removal{};};
    std::array<std::optional<GeometryEvidence>,32> history_{};std::size_t historyNext_=0;
    std::uint64_t lastGeometry_=0;
    std::int64_t observed_=0,deadline_=0,lastNow_=0,cancelledNs_=0;
    bool seen_=false,neutral_=false,ejectMode_=false,pulled_=false,gateAccepted_=false;
};
} // namespace fvr::interaction
