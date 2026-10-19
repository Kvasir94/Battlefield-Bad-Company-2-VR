#pragma once
#include "Bc2PlayerInventory.h"
#include "fvr/interaction/PhysicalWeaponState.h"
#include "fvr/interaction/PhysicalWeaponReadiness.h"
#include <optional>
#include <ostream>

namespace fvr::bc2::physicalWeaponRuntime {
using interaction::PhysicalWeaponCommandKind;
struct View {
    interaction::PhysicalWeaponIdentity identity{};
    interaction::PhysicalWeaponSnapshot state{};
    std::array<interaction::PhysicalMagazine,interaction::PhysicalWeaponState::MaxMagazines> magazines{};
    PlayerInventorySlot native{};
    std::uint64_t revision=0;
    bool suspended=false;
    interaction::WeaponReadiness readiness{};
};
// Emitted only after the original accepted-shot callback returns and
// authoritative server reconciliation succeeds. Original identity/time survive
// presentation reads; client copies cannot create a second shot.
struct AcceptedShotReceipt {
    std::uint64_t sequence=0;std::int64_t qpc=0;
    std::uint32_t input=0,loadedBefore=0,loadedAfter=0,reserveAfter=0;
};
struct CameraShotObservation {
    interaction::PhysicalWeaponIdentity identity{};
    PlayerInventoryAnchor actor{};
    std::uint32_t clientWeapon=0,serverFiring=0;
    AcceptedShotReceipt accepted{};
};
// Cached authority only: no engine reads/calls; contention or missing/currently
// suspended selection returns unavailable. This is not render-owner validation.
std::optional<CameraShotObservation> ReadCameraShotObservation()noexcept;
std::optional<CameraShotObservation> ReadAcceptedShotObservation()noexcept;
void EnableCameraShotDiagnostic(bool enabled)noexcept;
enum class CommandStatus { Pending, Applied, Rejected };
struct Receipt {
    std::uint64_t id=0;
    CommandStatus status=CommandStatus::Pending;
    std::uint64_t magazine=0;
    std::uint64_t droppedMagazine=0;
};
// Each producer retains its command ID until Take returns a terminal result.
// Unobserved results pin their slot; later commands cannot erase the outcome
// of a gesture interrupted by tracking loss or an equipment change.
class CommandReceiptStore {
public:
    static constexpr std::size_t Capacity=256;
    bool Reserve(std::uint64_t id)noexcept {
        if(!id)return false;
        for(const auto& receipt:receipts_)if(receipt.id==id)return false;
        for(auto& receipt:receipts_)if(!receipt.id){receipt={id,CommandStatus::Pending};return true;}
        return false;
    }
    bool Complete(std::uint64_t id,CommandStatus status,std::uint64_t magazine=0,std::uint64_t dropped=0)noexcept {
        if(status==CommandStatus::Pending)return false;
        for(auto& receipt:receipts_)if(receipt.id==id&&id){
            if(receipt.status!=CommandStatus::Pending)return false;
            receipt={id,status,magazine,dropped};return true;
        }
        return false;
    }
    std::optional<Receipt> Take(std::uint64_t id)noexcept {
        for(auto& receipt:receipts_)if(receipt.id==id&&id){
            const auto result=receipt;
            if(result.status!=CommandStatus::Pending)receipt={};
            return result;
        }
        return {};
    }
private:
    std::array<Receipt,Capacity> receipts_{};
};
enum class NativeReloadPhase { Queued, Ready, Running, Completed, Cancelled, Rejected };
struct NativeReloadStatus {
    std::uint64_t token=0;
    NativeReloadPhase phase=NativeReloadPhase::Rejected;
    bool busy=false,inputNeeded=false;
};
struct NativeReloadTransfer {
    interaction::PhysicalWeaponIdentity identity{};
    std::uint64_t token=0;
    std::uint32_t firing=0,loaded=0,reserve=0;
};
enum class WeaponAdmission { Unavailable, Unmanaged, Initializing, Managed };
// One current owned observation for input and equipment admission. Missing a
// View is not permission to fall back to native reload. No native calls occur
// while this value is consumed; selectors must refresh it at their boundary.
struct ActionAdmission {
    WeaponAdmission weapon=WeaponAdmission::Unavailable;
    std::uint64_t incarnation=0;
    // Verified native firing layout permits ordinary input passthrough only;
    // it does not admit a physical family or call a reload function.
    bool nativeReloadInput=false,suspended=false,pendingCommand=false,nativeBusy=false;
    NativeReloadPhase nativePhase=NativeReloadPhase::Rejected;
    bool BlocksSelection()const noexcept {
        // Quarantine this weapon's ammunition and fire permission, not the
        // player's selection. Outstanding transactions still settle first.
        return weapon==WeaponAdmission::Unavailable||weapon==WeaponAdmission::Initializing||
            pendingCommand||nativeBusy;
    }
};
ActionAdmission ReadActionAdmission(std::uint32_t clientWeapon,std::uint32_t expectedData=0)noexcept;
inline bool SuspendedWeaponAgrees(const interaction::PhysicalWeaponSnapshot& committed,
    std::uint32_t loaded,std::uint32_t reserve,bool nativeIdle,bool nativeBusy)noexcept {
    return committed.initialized&&nativeIdle&&!nativeBusy&&
        loaded==committed.loaded&&reserve==committed.looseReserve;
}
inline bool NativeReloadSettlementAgrees(std::uint32_t loaded,std::uint32_t reserve,
    std::uint32_t recordedLoaded,std::uint32_t recordedReserve,bool nativeIdle,bool transferOpen)noexcept {
    return nativeIdle&&!transferOpen&&loaded==recordedLoaded&&reserve==recordedReserve;
}
inline bool NormalTransferCountsAdmitted(std::uint32_t loaded,std::uint32_t reserve,
    std::uint32_t capacity,std::int32_t reloadType)noexcept {
    return capacity&&capacity<=interaction::PhysicalWeaponState::MaxCapacity&&loaded<=capacity&&
        reserve<=interaction::PhysicalWeaponState::MaxReserve&&
        (reloadType==1||(reloadType==0&&loaded<capacity));
}
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t,const ReloadStateMemory&);
void Start()noexcept;
bool Stop()noexcept;
bool Enabled()noexcept;
bool Installed()noexcept;
// Actor lifetime is independent of selected weapon, tracking space and hand claims.
void PublishActor(const PlayerInventoryAnchor&)noexcept;
void ClearActor()noexcept;
void InventoryUpdate(std::uint32_t inventory)noexcept;
std::optional<PlayerInventorySnapshot> Inventory()noexcept;
std::optional<View> Read(std::uint32_t clientWeapon)noexcept;
bool ConfigureWeapon(std::uint32_t clientWeapon,interaction::PhysicalWeaponFamily,bool locksOpenOnEmpty=false,
    std::optional<std::int32_t> authoredBaseCapacity={})noexcept;
// Queued commands capture an immutable weapon incarnation. Replays cannot
// operate on a replacement that happens to reuse its address or slot.
std::uint64_t Submit(const interaction::PhysicalWeaponIdentity&,PhysicalWeaponCommandKind,
    std::uint64_t magazine=0,int rounds=0)noexcept;
// Pending results can be polled. Reading a terminal result consumes it exactly
// once; keep the ID through cancellation until that result has been observed.
std::optional<Receipt> Result(std::uint64_t)noexcept;
// Gather queues after frontend cancellation/cleanup receipts. Only Ready
// admits a native Reload pulse; Running needs no continued button input.
bool RequestNativeReload(const interaction::PhysicalWeaponIdentity&,std::uint64_t token)noexcept;
std::optional<NativeReloadStatus> ReadNativeReload(const interaction::PhysicalWeaponIdentity&,std::uint64_t token=0)noexcept;
bool CancelNativeReload(const interaction::PhysicalWeaponIdentity&,std::uint64_t token)noexcept;
bool BlockNativeReload(std::uint32_t firing)noexcept;
std::optional<NativeReloadTransfer> BeginNativeReloadTransfer(std::uint32_t firing,std::uint32_t caller)noexcept;
void FinishNativeReloadTransfer(const NativeReloadTransfer&)noexcept;
void AfterUpdate(std::uint32_t firing)noexcept;
bool Managed(std::uint32_t firing)noexcept;
bool PermitFire(std::uint32_t firing)noexcept;
void BeforeUpdate(std::uint32_t firing)noexcept;
void AfterStep(std::uint32_t firing,std::uint32_t previousState)noexcept;
// Periodic writer: try-locks cached authority only, performs no native reads or
// calls, and formats outside the lock. Busy samples explicitly say unavailable.
void WriteTelemetry(std::ostream&);
void Report(std::ostream&);
}
