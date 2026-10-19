#pragma once
#include "fvr/interaction/ActionPolicy.h"
#include "fvr/interaction/ControllerInput.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <optional>
namespace fvr::bc2 {
// EntryInputActionEnum, NOT the device Concept enum. Verified by native
// metadata, float/bit setters and the local player gather/consume path.
enum class EntryAction : unsigned {Throttle=0,Strafe=1,Yaw=4,Pitch=5,SwitchPrimaryWeapon=7,Fire=8,
    Zoom=14,Jump=15,ChangeVehicle=16,ChangePose=26,Interact=27,Reload=29,Sprint=31,Menu=32,GrenadeLauncher=33,DynamicGadget2=36,ThrowGrenade=38};
inline constexpr std::size_t InputBytes=0xa0;
// Diagnostic-only, after the original Gather on its verified current owner.
// Neutralizes all 49 entry actions without using an XR input lease. Scalar
// actions 0..10 occupy +8..+30; action flags occupy +98/+9c. Header, gaps and
// bits outside the verified action range survive. Native rebuilds the next tick.
bool SuppressNativeHandCaptureInput(std::span<std::byte>,bool captureRequested) noexcept;
// Same verified native cache neutralization, without suppressing synthetic IPC
// controls. Call before the reader; retain a later normal synthetic commit only
// while its original lease is fresh. No native values are restored on failure.
bool SuppressNativePhysicalInput(std::span<std::byte>,bool blockRequested) noexcept;
bool CompleteNativePhysicalInput(std::span<std::byte>,bool blockRequested,bool syntheticCommitted,bool syntheticFresh) noexcept;
// Keep synthetic tracking identity and poses; admit no gameplay components.
void SuppressNativeHandCaptureControls(interaction::InputFrame&,bool captureRequested) noexcept;
// Native gather has already run. Stage a command in its current input buffer.
// Commit transfers this tick's fields to BC2: later native consumers need them.
// Next original gather rebuilds all 49 actions, including on stale XR input.
// Before commit, rollback preserves native restrictions and unrelated buttons.
// No native pointer survives commit or rollback.
class InputOverride {
    struct FloatEdit {std::size_t offset;std::uint32_t before,written;};
    std::span<std::byte> cache_{};
    std::array<FloatEdit,4> edits_{};unsigned editCount_=0;
    std::uint32_t before_[2]{},written_[2]{},masks_[2]{};
    bool active_=false;
public:
    InputOverride()=default;
    InputOverride(const InputOverride&)=delete;InputOverride& operator=(const InputOverride&)=delete;
    ~InputOverride(){Restore();}
    // Optional grenade command is used only by the bounded death diagnostic.
    // The optional alias is admitted only by a fresh native on-foot router
    // proving Interact27 and ChangeVehicle16 both resolve to concept36.
    bool Apply(std::span<std::byte>,const interaction::ActionOutput&,std::optional<bool> diagnosticGrenade={},std::optional<bool> diagnosticCycle={},std::optional<EntryAction> verifiedWeaponMode={},bool verifiedContextUseVehicleAlias=false) noexcept;
    // Vehicle exit owns only ChangeVehicle; native driving/aim/fire survives.
    bool ApplyVehicleExit(std::span<std::byte>,const interaction::ActionOutput&) noexcept;
    bool Restore() noexcept;
    void Commit()noexcept{active_=false;cache_={};} // Command consumed by the native tick.
    bool Active()const noexcept{return active_;}
};
}

namespace fvr::bc2 {
// On-foot native input rejects axes below about 0.1. Bounded roomscale drive
// crosses that threshold; the feedback policy stops at its physical lean radius.
float RoomscaleNativeAxis(float metresPerSecond) noexcept;
}
