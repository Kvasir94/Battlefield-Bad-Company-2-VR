#pragma once
#include "Bc2Xm8BoltAsset.h"
#include "fvr/interaction/WeaponCycle.h"
#include <ostream>

// XM8 charging-handle cycle: presentation plus gating ONLY.
//
// The XM8 is registered as a WeaponCycleFamily::Pump profile with the asset
// lane's contract (axis canonical {0,0,+1}, stroke from the asset, grab point at
// the jntWpn_17 rest frame). The left hand enters through the shared arbiter as
// a Mechanism claim dependent on the right GunHold, never while the magazine
// lane owns the hand or owes its own chamber rack. Fire is suppressed while the
// handle is held. No native completion hook is identified for the XM8
// (fltAutomaticFire, BoltActionTime 0), so a finished stroke is a VR-side
// "presentation complete" event: WeaponCycle::Complete is never called, no
// ammunition, timer or native state is touched, and the lease below is a
// VR-only presentation lease, not a native held-cycle capability.
namespace fvr::bc2 {
inline constexpr interaction::HandInteractionKey Xm8BoltContactKey{0x584d38424f4c54ull,1}; // "XM8BOLT"
interaction::WeaponCycleProfile Xm8BoltCycleProfile(const Xm8BoltAsset&)noexcept;
// Grasp point of the left hand in canonical weapon-local metres: the raw
// anatomical wrist relative to the placed weapon, moved by the same palm offset
// the magazine lane's existing rack uses (when supplied).
std::optional<math::Matrix4> Xm8BoltWeaponFromHand(const math::Matrix4& rawLeftWristWorldMeters,
    const math::Matrix4& weaponWorldMeters,const math::Matrix4* palmFromWrist)noexcept;

struct Xm8BoltCycleSample {
    interaction::HandInteractionSample input{};                    // Current arbiter packet (GunHold renewed).
    std::optional<interaction::HandInteractionSample> evidence;    // Original packet of the raw contact geometry.
    interaction::HandInteractionKey weapon{};                       // Shared gun item.
    std::optional<math::Matrix4> weaponFromHand;                    // From Xm8BoltWeaponFromHand.
    bool xm8=false,gripPressed=false;
    bool magazineOwnsLeft=false; // Magazine lane holds/claims the hand, owes its rack, or blocks weapon actions.
    bool cancel=false;
};
struct Xm8BoltCycleResult {
    interaction::WeaponCyclePhase phase=interaction::WeaponCyclePhase::Idle;
    interaction::WeaponCycleFailure failure=interaction::WeaponCycleFailure::None;
    float travel=0;             // Metres along the asset axis while held; 0 otherwise.
    bool blocksFire=false,ownsLeft=false;
    bool grabbed=false,presentationComplete=false,cancelled=false;
};
class Bc2Xm8BoltCycle {
public:
    explicit Bc2Xm8BoltCycle(const Xm8BoltAsset& asset)noexcept:profile_(Xm8BoltCycleProfile(asset)){}
    bool Valid()const noexcept {return interaction::weapon_cycle_detail::Profile(profile_);}
    const interaction::WeaponCycleProfile& Profile()const noexcept {return profile_;}
    Xm8BoltCycleResult Tick(const Xm8BoltCycleSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
    void Cancel(const interaction::HandInteractionSample& current,interaction::HandInteraction&)noexcept;
    void Report(std::ostream&)const;
    struct Counters {std::uint64_t ticks=0,hovers=0,grabs=0,claimRejected=0,completions=0,cancels=0,claimLost=0,yielded=0,fireBlockedTicks=0;};
    const Counters& Stats()const noexcept {return counters_;}
private:
    void Retire(const interaction::HandInteractionSample&,interaction::HandInteraction&,bool cancelled)noexcept;
    interaction::WeaponCycleLease Lease(const interaction::HandInteractionSample&,interaction::HandInteractionKey weapon)const noexcept;
    interaction::WeaponCycleProfile profile_{};
    interaction::WeaponCycle cycle_{};
    std::optional<interaction::HandClaim> claim_;
    std::uint64_t cycleId_=0;
    bool rearmed_=true;   // Grip released since the last stroke/abort (adapter neutral).
    bool active_=false;   // A cycle object is between Begin and retirement.
    float lastTravel_=0;
    interaction::WeaponCycleFailure lastFailure_=interaction::WeaponCycleFailure::None;
    Counters counters_{};
};
} // namespace fvr::bc2
