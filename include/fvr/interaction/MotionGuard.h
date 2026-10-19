#pragma once
#include <array>
#include <cmath>
#include <cstdint>

// Tracking-continuity guard for a hand-driven stroke (insertion, rack, pull).
//
// Port of the KF2 VR mod's script/KF2VR/Classes/VRReloadMotionGuard.uc
// (commit 43452e24). It rejects invalid, backward, implausibly fast and
// gapped (> 150 ms) samples and then requires a fresh Begin. It only judges
// continuity: callers keep stroke progress, haptics and ammunition.
//
// Units: KF2 works in UU (= cm) and seconds; this port uses metres and
// nanoseconds. KF2 defaults: SpeedLimit 400 UU/s, PositionSlack 2 UU,
// MaxGap 0.15 s. Its reload insertion uses the insert guide's wider
// discontinuity envelope (12 UU or 1000 UU/s, 0.15 s); see
// MotionGuardConfig::Insertion().
namespace fvr::interaction {

// The project-wide tracked-sample gap. ActionPolicy, AmmoSupply,
// TemporaryHeldFire and GroundPickup use the same 150 ms literal.
inline constexpr std::int64_t TrackedSampleGapNs=150000000;

struct MotionGuardConfig {
    float speedLimitMetersPerSecond=4.f; // KF2 400 UU/s
    float positionSlackMeters=.02f;      // KF2 2 UU
    std::int64_t maxGapNs=TrackedSampleGapNs;
    // KF2 VRReloadInsertGuide continuity: FMax(12, 1000*DT) UU over <=0.15 s.
    // Expressed as a slack-plus-speed bound, which admits the same slams.
    static constexpr MotionGuardConfig Insertion()noexcept {return {10.f,.12f,TrackedSampleGapNs};}
    // KF2 VRInteractiveReload RackMotion/LeverMotion/LockMotion: 1500 UU/s.
    static constexpr MotionGuardConfig Rack()noexcept {return {15.f,.02f,TrackedSampleGapNs};}
};
enum class MotionGuardVerdict : std::uint8_t {Accepted,NotStarted,InvalidSample,Backward,Gap,Implausible};

class MotionGuard {
public:
    using Position=std::array<float,3>;
    // Bounded as well as finite so the distance cannot overflow (KF2: 1e8 UU).
    static bool ValidPosition(const Position& p)noexcept {
        for(float v:p)if(!std::isfinite(v)||std::abs(v)>=1e6f)return false;
        return true;
    }
    static bool ValidTime(std::int64_t ns)noexcept {return ns>=0;}
    static bool ValidConfig(const MotionGuardConfig& c)noexcept {
        return std::isfinite(c.speedLimitMetersPerSecond)&&c.speedLimitMetersPerSecond>0&&c.speedLimitMetersPerSecond<1e6f&&
            std::isfinite(c.positionSlackMeters)&&c.positionSlackMeters>=0&&c.positionSlackMeters<1e6f&&
            c.maxGapNs>0&&c.maxGapNs<=TrackedSampleGapNs;
    }
    void Reset()noexcept {ready_=false;last_={};lastNs_=0;}
    [[nodiscard]] bool Ready()const noexcept {return ready_;}
    bool Begin(const MotionGuardConfig& c,const Position& p,std::int64_t ns)noexcept {
        Reset();
        if(!ValidPosition(p)||!ValidTime(ns)||!ValidConfig(c))return false;
        config_=c;last_=p;lastNs_=ns;ready_=true;return true;
    }
    // Any rejection resets: the caller must Begin again from a fresh sample.
    MotionGuardVerdict Check(const Position& p,std::int64_t ns)noexcept {
        if(!ready_)return MotionGuardVerdict::NotStarted;
        auto reject=[&](MotionGuardVerdict v){Reset();return v;};
        if(!ValidPosition(p)||!ValidTime(ns))return reject(MotionGuardVerdict::InvalidSample);
        if(ns<lastNs_)return reject(MotionGuardVerdict::Backward);
        const std::int64_t dt=ns-lastNs_;
        if(dt>config_.maxGapNs)return reject(MotionGuardVerdict::Gap);
        double d2=0;for(unsigned i=0;i<3;++i){const double d=double(p[i])-last_[i];d2+=d*d;}
        const double distance=std::sqrt(d2);
        // A repeated timestamp may only repeat its position.
        if(dt==0&&distance!=0)return reject(MotionGuardVerdict::Implausible);
        if(!(distance<=double(config_.positionSlackMeters)+double(config_.speedLimitMetersPerSecond)*double(dt)*1e-9))
            return reject(MotionGuardVerdict::Implausible);
        last_=p;lastNs_=ns;return MotionGuardVerdict::Accepted;
    }
private:
    MotionGuardConfig config_{};Position last_{};std::int64_t lastNs_=0;bool ready_=false;
};
} // namespace fvr::interaction
