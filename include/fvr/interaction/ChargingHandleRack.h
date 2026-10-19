#pragma once
#include "MotionGuard.h"
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Charging-handle (or slide/bolt) rack recognizer for a magazine-fed gun.
//
// It feeds MagazineFeedState::Event::Rack (VR-side chamber metadata) and
// nothing else: no claim, no native command, no ammunition, no timer. The
// stroke and guard follow KF2 VR's RackMotion (VRReloadMotionGuard
// MotionGuardConfig::Rack(), 1500 UU/s, 2 UU slack): grab the handle on a
// fresh grip edge, pull it back along the weapon-local axis, and the rack
// completes when it reaches the rear stop (the bolt springs forward on its
// own). Releasing early, leaving the axis or a refused motion sample cancels
// the stroke; a new neutral grip is then needed.
//
// WeaponCycle is not reused: it is a native pump/bolt cycle that requires a
// held native lease per shot, while a charging handle here only clears the
// VR NeedsRack flag. Every per-weapon handle pose needs live mapping against
// the native rig before a profile is admitted, so adapters keep this off by
// default.
namespace fvr::interaction {
struct ChargingHandleProfile {
    std::uint64_t id=0,revision=0;
    // Grab point of the closed handle, weapon-local metres (translation only).
    std::array<float,3> handleMeters{};
    // Unit vector, weapon-local, from the closed handle toward its rear stop.
    std::array<float,3> pullDirection{0,0,-1};
    float strokeMeters=0,grabRadiusMeters=0,rearToleranceMeters=0,lateralReleaseMeters=0;
    MotionGuardConfig motion=MotionGuardConfig::Rack();
    bool pistolSlide=false; // Separate rearward ejection/forward feed and held-open grip.
};
[[nodiscard]] inline bool ValidChargingHandleProfile(const ChargingHandleProfile& p)noexcept {
    double n=0;for(float v:p.pullDirection){if(!std::isfinite(v))return false;n+=double(v)*v;}
    for(float v:p.handleMeters)if(!std::isfinite(v)||std::abs(v)>2.f)return false;
    const auto finitePositive=[](float v,float max){return std::isfinite(v)&&v>0&&v<=max;};
    return p.id&&p.revision&&std::abs(n-1)<=1e-4&&finitePositive(p.strokeMeters,.3f)&&finitePositive(p.grabRadiusMeters,.2f)&&
        finitePositive(p.rearToleranceMeters,p.strokeMeters*.5f)&&finitePositive(p.lateralReleaseMeters,.3f)&&
        MotionGuard::ValidConfig(p.motion);
}
struct ChargingHandleSample {
    std::uint64_t sequence=0;
    std::int64_t observedNs=0;
    // Grip/palm pose of the racking hand in weapon space (row-major, translation in values[3]).
    math::Matrix4 weaponFromHand{};
    bool grip=false,tracked=false;
};
enum class ChargingHandlePhase : std::uint8_t {Idle,Grabbed};
// Adapter reasons (7+): the hand became busy with magazine work, the rack is
// no longer owed, or the weapon/hand source was lost for too long.
enum class ChargingHandleReason : std::uint8_t {None,InvalidProfile,InvalidSample,TrackingLost,Released,OffAxis,MotionRejected,
    HandBusy,NotOwed,SourceLost};
struct ChargingHandleResult {
    ChargingHandlePhase phase=ChargingHandlePhase::Idle;
    ChargingHandleReason reason=ChargingHandleReason::None;
    bool grabbed=false,racked=false,cancelled=false;
    float travel=0; // 0..1 of the stroke
};
class ChargingHandleRack {
public:
    explicit ChargingHandleRack(ChargingHandleProfile p={})noexcept:profile_(p){}
    [[nodiscard]] bool Valid()const noexcept {return ValidChargingHandleProfile(profile_);}
    [[nodiscard]] const ChargingHandleProfile& Profile()const noexcept {return profile_;}
    void Reset()noexcept {phase_=ChargingHandlePhase::Idle;neutral_=false;guard_.Reset();sequence_=0;}
    [[nodiscard]] ChargingHandlePhase Phase()const noexcept {return phase_;}
    // Adapter cancellation with its reason (a grabbed stroke reports cancelled).
    ChargingHandleResult Abort(ChargingHandleReason why)noexcept {ChargingHandleResult r;r.phase=phase_;return Cancel(r,why);}
    ChargingHandleResult Update(const ChargingHandleSample& s)noexcept {
        ChargingHandleResult r;r.phase=phase_;
        if(!Valid())return Cancel(r,ChargingHandleReason::InvalidProfile);
        if(!s.sequence||s.observedNs<0)return Cancel(r,ChargingHandleReason::InvalidSample);
        if(s.sequence<=sequence_){r.travel=travel_;return r;} // Duplicates never advance a stroke.
        sequence_=s.sequence;
        if(!s.tracked)return Cancel(r,ChargingHandleReason::TrackingLost);
        const MotionGuard::Position hand{s.weaponFromHand.values[3][0],s.weaponFromHand.values[3][1],s.weaponFromHand.values[3][2]};
        if(!MotionGuard::ValidPosition(hand))return Cancel(r,ChargingHandleReason::InvalidSample);
        double along=0,lateral2=0;
        for(unsigned i=0;i<3;++i)along+=(double(hand[i])-profile_.handleMeters[i])*profile_.pullDirection[i];
        for(unsigned i=0;i<3;++i){const double d=double(hand[i])-profile_.handleMeters[i]-along*profile_.pullDirection[i];lateral2+=d*d;}
        if(phase_==ChargingHandlePhase::Idle){
            if(!s.grip){neutral_=true;return r;}
            const double distance=std::sqrt(along*along+lateral2);
            if(!neutral_||distance>profile_.grabRadiusMeters){neutral_=false;return r;} // Needs a fresh grip edge at the handle.
            if(!guard_.Begin(profile_.motion,hand,s.observedNs))return Cancel(r,ChargingHandleReason::MotionRejected);
            phase_=ChargingHandlePhase::Grabbed;grabAlong_=along;travel_=0;neutral_=false;
            r.phase=phase_;r.grabbed=true;return r;
        }
        if(!s.grip)return Cancel(r,ChargingHandleReason::Released);
        if(guard_.Check(hand,s.observedNs)!=MotionGuardVerdict::Accepted)return Cancel(r,ChargingHandleReason::MotionRejected);
        if(std::sqrt(lateral2)>profile_.lateralReleaseMeters)return Cancel(r,ChargingHandleReason::OffAxis);
        const double pulled=along-grabAlong_;
        travel_=float(std::clamp(pulled/profile_.strokeMeters,0.,1.));r.travel=travel_;
        if(pulled>=double(profile_.strokeMeters)-profile_.rearToleranceMeters){
            phase_=ChargingHandlePhase::Idle;guard_.Reset();travel_=0;
            r.phase=phase_;r.racked=true;r.travel=1;return r;
        }
        return r;
    }
private:
    ChargingHandleResult Cancel(ChargingHandleResult r,ChargingHandleReason why)noexcept {
        r.cancelled=phase_==ChargingHandlePhase::Grabbed;r.reason=why;
        phase_=ChargingHandlePhase::Idle;neutral_=false;guard_.Reset();travel_=0;r.phase=phase_;r.travel=0;return r;
    }
    ChargingHandleProfile profile_{};
    ChargingHandlePhase phase_=ChargingHandlePhase::Idle;
    MotionGuard guard_{};
    std::uint64_t sequence_=0;
    double grabAlong_=0;float travel_=0;bool neutral_=false;
};
} // namespace fvr::interaction
