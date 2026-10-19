#pragma once
#include "Bc2NativeOperationCapability.h"
#include "Bc2TestRangeFields.h"
#include "fvr/runtime/TestRange.h"
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
namespace fvr::bc2 {
// In-game test range executor (docs/BC2_TEST_RANGE.md). It sequences the
// operations a runtime::RangeSpec needs, against read-only native observations,
// and stays inert unless Begin() accepted a spec. Every native effect goes
// through one RangeHooks entry, and each entry is used only when it is bound
// AND admitted by an exact reviewed capability record for this executable and
// source build. Default builds have no records: all operations are unavailable
// and the executor can only wait for a level/loadout that already matches.
// Nothing here writes ammo, reload or consumer state by itself.
enum class RangeOperation:std::uint8_t {
    WriteSettings,  // route B: settings registry write at the main menu (Client.Level and the switches)
    LoadLevel,      // fire the menu command newSP (UI command 132 -> 0x7600f0), which loads Client.Level
    SkipCinematic,  // end/skip the observed cutscene or movie
    PrepareLoadout, // reversible kit Weapons[] rewrite after level load, before spawn (loaded assets only)
    ClearInventory, // remove carried weapons not in the keep list
    GiveWeapon,     // post-spawn runtime give or pickup spawn, one asset
    SelectWeapon,   // select a carried asset
    SetAmmo,        // loaded/reserve of one carried asset
    Teleport,       // place the player soldier
    Count
};
std::string_view RangeOperationName(RangeOperation)noexcept;
inline constexpr std::string_view RangeOperationAlgorithm="bc2-test-range-ops-v1";
struct RangeOperationCapability {
    NativeOperationBinding binding;
    RangeOperation operation=RangeOperation::Count;
    std::string_view algorithm,sourceSha256,executableSha256,readbackReceiptSha256,restoreReceiptSha256;
    bool reviewed=false,readbackVerified=false,restoreVerified=false;
};
// Exactly one matching reviewed record; reversible patches also need a restore receipt.
bool RangeOperationAdmitted(const NativeOperationBinding&,RangeOperation,
    std::span<const RangeOperationCapability>,std::string_view build=NativeOperationBuild)noexcept;
// Dispatch outcome reported by a hook. Accepted means the native call was made;
// the executor still waits for an observation proving the effect. Unknown is
// never retried: the run fails and asks for reconciliation.
enum class RangeDispatch:std::uint8_t {NotStarted,Accepted,Unknown};
struct RangeAmmoRequest {std::string_view asset;bool full=false;int loaded=-1,reserve=-1;};
// Filled by later engine mapping. nullptr = unmapped. Hooks run on the game
// thread inside the existing gather boundary, one per Tick at most.
struct RangeHooks {
    void* context=nullptr;
    RangeDispatch (*writeSettings)(void*,std::span<const runtime::RangeSetting>)=nullptr;
    RangeDispatch (*loadLevel)(void*,std::string_view level)=nullptr;
    RangeDispatch (*skipCinematic)(void*)=nullptr;
    RangeDispatch (*prepareLoadout)(void*,std::span<const std::string> weapons,std::span<const int> slots)=nullptr;
    bool (*restoreLoadout)(void*)=nullptr; // must restore every byte prepareLoadout changed
    RangeDispatch (*clearInventory)(void*,std::span<const std::string> keep)=nullptr;
    RangeDispatch (*giveWeapon)(void*,std::string_view asset)=nullptr;
    RangeDispatch (*selectWeapon)(void*,std::string_view asset)=nullptr;
    RangeDispatch (*setAmmo)(void*,const RangeAmmoRequest&)=nullptr;
    RangeDispatch (*teleport)(void*,const runtime::RangeTeleport&)=nullptr;
};
bool RangeHookBound(const RangeHooks&,RangeOperation)noexcept;
// Current mapping on build 795745: nothing is bound (docs/BC2_TEST_RANGE.md §5).
RangeHooks UnmappedRangeHooks()noexcept;
// Read-only native facts, published by the existing owner/inventory readers.
// Values that cannot be read stay at their "unknown" defaults.
struct RangeWeaponObservation {std::string asset;int loaded=-1,reserve=-1,capacity=-1,reserveCapacity=-1;};
struct RangeObservation {
    std::uint64_t sequence=0;std::int64_t observedNs=0;
    bool frontEnd=false,levelLoaded=false,cinematic=false,playerAlive=false;
    std::int32_t menuId=-1;         // MenuIdEnum when readable (MiePlaying=4); informational, adapters derive the flags
    std::string level;              // "" = not observable
    std::uint64_t owner=0;          // player/soldier/actor-generation identity, 0 = none
    std::vector<RangeWeaponObservation> weapons;std::string selected;
    bool hasPosition=false;float x=0,y=0,z=0;
};
// Route A receipt from the early boot stage (add-override before the setting
// groups register). Only `applied` with a read-back is accepted.
struct RangeBootReceipt {bool applied=false;unsigned written=0,deferred=0;};
enum class RangeStep:std::uint8_t {None,WriteSettings,PrepareLoadout,AwaitLevel,LoadLevel,AwaitPlayer,SkipCinematic,
    ClearInventory,GiveWeapon,SelectWeapon,SetAmmo,Teleport,Ready,Restore};
enum class RangePhase:std::uint8_t {Inactive,Running,Ready,Failed,Finished};
enum class RangeReason:std::uint8_t {None,InvalidSpec,Unavailable,NotStarted,UnknownDispatch,ReadbackTimeout,ReadyTimeout,
    RunTimeout,WrongLevel,Unobservable,OwnerLost,Stopped,RestoreFailed,LevelAlreadyLoaded,PlayerAlreadySpawned,Boot};
enum class RangeResult:std::uint8_t {Dispatch,Ok,Wait,Skip,Fail,Ready,Finished};
std::string_view RangeStepName(RangeStep)noexcept;
std::string_view RangeReasonName(RangeReason)noexcept;
std::string_view RangeResultName(RangeResult)noexcept;
struct RangeEvent {std::uint64_t serial=0;std::int64_t nowNs=0;RangeStep step=RangeStep::None;RangeResult result=RangeResult::Wait;
    RangeReason reason=RangeReason::None;std::string asset;};
// One gun-event-log v1 line (docs/GUN-EVENT-LOG.md): src "fvr", phase "range".
std::string FormatRangeEvent(const RangeEvent&,std::uint64_t tick,std::int64_t qpcUs);
inline constexpr std::int64_t RangeFreshNs=200000000,RangeReadbackNs=5000000000,RangeLevelReadbackNs=120000000000;
inline constexpr std::size_t RangeEventLimit=256;
class TestRange {
public:
    // Validates the spec and, for routes that need one, the level operation.
    // Refusal leaves the executor Failed without calling any hook.
    bool Begin(const runtime::RangeSpec&,const NativeOperationBinding&,std::span<const RangeOperationCapability>,
        const RangeHooks&,std::int64_t nowNs,std::string_view build=NativeOperationBuild,const RangeBootReceipt* boot=nullptr);
    // At most one hook call per tick. Stale or out-of-order observations only
    // advance timeouts. Inactive: returns immediately.
    void Tick(const RangeObservation&,std::int64_t nowNs);
    // Explicit stop (tape end, runner deadline). Restores reversible patches.
    void Stop(std::int64_t nowNs);
    RangePhase Phase()const noexcept{return phase_;}
    RangeReason Reason()const noexcept{return reason_;}
    RangeStep Step()const noexcept{return step_;}
    bool Active()const noexcept{return phase_==RangePhase::Running||phase_==RangePhase::Ready;}
    bool Available(RangeOperation op)const noexcept{return op<RangeOperation::Count&&available_[std::size_t(op)];}
    std::uint64_t Owner()const noexcept{return owner_;}
    std::vector<RangeEvent> TakeEvents();
    std::uint64_t DroppedEvents()const noexcept{return dropped_;}
private:
    void Emit(std::int64_t now,RangeStep,RangeResult,RangeReason=RangeReason::None,std::string_view asset={});
    void Fail(std::int64_t now,RangeStep,RangeReason,std::string_view asset={});
    void Finish(std::int64_t now,RangeReason);
    bool Restore(std::int64_t now);
    bool Dispatch(std::int64_t now,RangeStep,RangeOperation,RangeDispatch,std::int64_t readbackNs,std::string_view asset={});
    bool Verified(const RangeObservation&)const;
    void Advance(const RangeObservation&,std::int64_t now);
    bool Carries(const RangeObservation&,std::string_view asset)const;
    const RangeWeaponObservation* Find(const RangeObservation&,std::string_view asset)const;
    bool AmmoTarget(std::string_view asset,RangeAmmoRequest&)const;
    bool AmmoMatches(const RangeWeaponObservation&,const RangeAmmoRequest&)const;
    runtime::RangeSpec spec_{};RangeHooks hooks_{};std::vector<runtime::RangeSetting> settings_;
    std::array<bool,std::size_t(RangeOperation::Count)> available_{};
    RangePhase phase_=RangePhase::Inactive;RangeReason reason_=RangeReason::None;RangeStep step_=RangeStep::None;
    std::int64_t beginNs_=0,deadlineNs_=0;std::uint64_t sequence_=0,dispatchSequence_=0,owner_=0,serial_=0,dropped_=0;
    bool awaiting_=false,loadoutPatched_=false,loadoutRestored_=false,levelRequested_=false,
        cinematicSkipped_=false,cinematicWaitLogged_=false;
    std::string pendingAsset_;RangeAmmoRequest pendingAmmo_{};std::string pendingAmmoAsset_;
    std::vector<RangeEvent> events_;
};
}
#ifdef FVR_BC2_TEST_RANGE_CAPABILITIES_HEADER
#include FVR_BC2_TEST_RANGE_CAPABILITIES_HEADER
#else
namespace fvr::bc2 {inline constexpr std::array<RangeOperationCapability,0> RangeOperationCapabilities{};}
#endif
