#pragma once
#include "fvr/math/StereoMath.h"
#include "fvr/interaction/ActionPolicy.h"
#include <array>
namespace fvr::interaction {
// Runtime-neutral input. Poses use the same reference space/time as the eyes.
enum ControllerComponent : std::uint32_t {
    Stick=1, Trigger=2, Squeeze=4, Primary=8, Secondary=16, StickClick=32, MenuClick=64
};
inline constexpr std::uint32_t Components=127, Buttons=120;
enum ControllerTouch : std::uint32_t {ThumbTouch=1,IndexTouch=2};
inline constexpr std::uint32_t TouchComponents=3;
struct ControllerState {
    math::Pose grip{},aim{};
    bool gripTracked=false,aimTracked=false;
    std::uint32_t active=0,held=0;
    std::uint32_t touchActive=0,touched=0; // Optional sensor state; no gameplay buttons.
    float stickX=0,stickY=0,trigger=0,squeeze=0;
};
struct InputFrame {
    std::uint64_t generation=0,spaceGeneration=0;
    std::int64_t predictedNs=0;
    bool focused=false,headValid=false,floorRelative=false;
    float worldUnitsPerMeter=1;
    math::Pose referenceHead{},head{};
    std::array<ControllerState,2> hands{}; // Left, right. No native pointers.
};
bool ValidInput(const InputFrame&) noexcept;
struct InputOwner {
    std::uint64_t owner=0,generation=0;
    bool playing=false,bindingsVerified=false;
};
// Right stick click equipment cycling. Times are source-sample (predicted)
// nanoseconds; repeated packets never extend or re-emit a click.
struct StickCycleOptions {
    // false: the October 8 behavior, one Equipment on any release (no backward).
    bool backward=true;
    // A click held this long emits EquipmentBack once, at the threshold.
    std::int64_t backwardHoldNs=450000000;
    // A press starting this soon after the previous release is contact bounce.
    std::int64_t bounceNs=30000000;
};
// Emits semantic requests only; engine adapters own native dispatch and aiming.
class ControllerActions {
public:
    ActionOutput Update(const InputFrame&,const InputOwner&,std::int64_t nowNs,const TurnOptions& options={}) noexcept;
    void SetStickCycle(const StickCycleOptions& options)noexcept{cycle_=options;}
private:
    // Right stick click: Idle (armed), Pressed alone, Spent (left click joined,
    // backward emitted, sample gap, or recovery: needs a release first).
    enum class Click : std::uint8_t {Idle,Pressed,Spent};
    std::uint32_t StickClickEdge(const InputFrame&) noexcept;
    std::array<ActionPolicy,2> policies_;
    std::uint64_t ownerGeneration_=0,spaceGeneration_=0;
    float heading_=0;int weaponDirection_=0;
    StickCycleOptions cycle_{};
    Click click_=Click::Spent;
    std::uint64_t clickGeneration_=0;
    std::int64_t clickSinceNs_=0,clickLastNs_=0,clickReleaseNs_=0;
    bool triggerHeld_=false;
};
struct RecenterEvidence {
    std::uint64_t samples=0,buttonsActive=0,leftHeld=0,rightHeld=0,bothHeld=0;
    std::uint64_t bothHeldWithoutHandPose=0,starts=0,completed=0,interruptions=0,maxHoldNs=0;
};
// Explicit button gesture: hand pose visibility is not needed to recenter a
// valid HMD. Focus/head/button loss still requires neutral before another hold.
// One event after both stick clicks remain held for one second.
class RecenterGesture {
public:
    bool Update(const InputFrame&) noexcept;
    const RecenterEvidence& Evidence()const noexcept{return evidence_;}
private:
    RecenterEvidence evidence_{};
    std::uint64_t space_=0;std::int64_t since_=0,last_=0;bool armed_=false;
};
// Horizontal reference keeps roomscale up aligned with native gravity even if
// the user looks down while starting/recentering. Does not move a player collider.
std::optional<math::Pose> UprightReference(const math::Pose&) noexcept;
// Looking straight down cannot define a new yaw. Recenter position while
// retaining the previous valid upright yaw in that case.
std::optional<math::Pose> ExplicitRecenterReference(const math::Pose& head,const math::Pose& previous)noexcept;
}
