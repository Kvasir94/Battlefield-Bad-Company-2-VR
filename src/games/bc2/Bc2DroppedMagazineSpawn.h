#pragma once
// Released-magazine spawn decision (docs/INTERACTION-POLISH-20261008.md, "Dropped
// magazines"). Presentation only: no ammunition, claims or native state.
//
// Why released magazines vanished (session native-trace-20261009-021010-472):
// the physical authority applied seven DropMagazine commands (every ledger
// magazine ended in location Dropped) but dropped_magazines.dropped stayed 0.
// The presentation spawned a prop only when ALL of these held on the exact
// gather that observed the receipt: the receipt was not "abandoned", the probe
// phase was already WellEmpty, and the renderer's raw contact was valid on
// that gather. Raw contact is renderer-published N-1 evidence that drops out on
// the frame the hand claim is released (pack rows show magazine=0 there), and a
// missed gather was never retried. The weapon's magazine bone is hidden while
// the well is empty, so the magazine simply disappeared. This spawner keys the
// drop on the authority receipt alone, starts it from the last carried pose
// (retained 500 ms) and defers up to 300 ms for fresh contact, so a committed
// drop always produces a falling prop or a counted loss.
#include "fvr/interaction/DroppedProps.h"
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fvr::bc2 {
struct DroppedMagazineSettings {
    bool enabled=true;            // FVR_BC2_DROPPED_MAGS=0 kill switch
    float lifetimeSeconds=30;     // FVR_BC2_DROPPED_MAG_LIFETIME_S (1..600)
    unsigned maxLive=4;           // FVR_BC2_DROPPED_MAG_MAX (1..6)
    float visibleMeters=8;        // FVR_BC2_DROPPED_MAG_VISIBLE_M (1..50) horizontal draw radius
    float floorToleranceMeters=1; // a prop on another level (floor estimate this far off) is not drawn
};
// Getter returns the variable's text, or an empty view when unset.
template<class Getter>
DroppedMagazineSettings ParseDroppedMagazineSettings(Getter&& get)noexcept {
    DroppedMagazineSettings s;
    const auto number=[&](std::string_view name,float lo,float hi,float& out){
        const std::string_view v=get(name);if(v.empty())return;float f=0;
        const auto r=std::from_chars(v.data(),v.data()+v.size(),f);
        if(r.ec==std::errc{}&&std::isfinite(f)&&f>=lo&&f<=hi)out=f;
    };
    if(const auto v=get("FVR_BC2_DROPPED_MAGS");!v.empty()&&v[0]=='0')s.enabled=false;
    number("FVR_BC2_DROPPED_MAG_LIFETIME_S",1,600,s.lifetimeSeconds);
    float live=float(s.maxLive);number("FVR_BC2_DROPPED_MAG_MAX",1,float(interaction::DroppedProps::Slots),live);s.maxLive=unsigned(live);
    number("FVR_BC2_DROPPED_MAG_VISIBLE_M",1,50,s.visibleMeters);
    return s;
}
inline interaction::DroppedPropConfig DroppedMagazineConfig(const DroppedMagazineSettings& s)noexcept {
    auto c=interaction::MagazineDropConfig();
    c.lifetimeNs=std::int64_t(double(s.lifetimeSeconds)*1e9);c.maxLive=s.maxLive;return c;
}

struct DroppedMagazineLaunch {math::Matrix4 worldFromItem{};math::Vec3 velocity{},angularVelocity{};bool fromCarried=false,fromWell=false;};
// Hand/magazine motion history and the drop-receipt latch.
class DroppedMagazineSpawner {
public:
    static constexpr std::int64_t CarriedRetainNs=500000000,DeferNs=300000000;
    // Each gather with a valid raw wrist (world metres, row-vector basis).
    void ObserveWrist(const math::Matrix4& wrist,std::int64_t now)noexcept {
        history_[next_]={wrist,now};next_=(next_+1)%history_.size();if(count_<history_.size())++count_;
    }
    // The carried magazine's own world pose while it is in the hand.
    void ObserveCarried(const math::Matrix4& item,std::int64_t now)noexcept{carried_=item;carriedNs_=now;}
    std::optional<math::Matrix4> Carried(std::int64_t now)const noexcept {
        if(carried_&&now>=carriedNs_&&now-carriedNs_<=CarriedRetainNs)return carried_;return {};}
    // An authority/consumer drop receipt (exactly once per dropped magazine).
    void Commit(bool fromWell,std::int64_t now)noexcept{pending_=Pending{fromWell,now};++committed_;}
    bool PendingDrop()const noexcept{return pending_.has_value();}
    // Resolves a pending drop. wellPose: the seated pose on the current weapon
    // (button eject starts there). wrist: the current raw wrist, if valid.
    std::optional<DroppedMagazineLaunch> Resolve(std::int64_t now,const std::optional<math::Matrix4>& wrist,
        const std::optional<math::Matrix4>& wellPose)noexcept {
        if(!pending_)return {};
        DroppedMagazineLaunch out;const bool well=pending_->fromWell;
        if(well&&wellPose){out.worldFromItem=*wellPose;out.fromWell=true;}
        else if(const auto c=Carried(now)){out.worldFromItem=*c;out.fromCarried=true;}
        else if(!well&&wrist)out.worldFromItem=*wrist;
        else if(well&&wrist){out.worldFromItem=*wrist;out.fromWell=true;}
        else{
            if(now-pending_->atNs>DeferNs){pending_.reset();++lost_;}
            else ++deferredGathers_;
            return {};
        }
        if(!well)Velocities(now,out.velocity,out.angularVelocity);
        pending_.reset();carried_.reset();++spawned_;return out;
    }
    void Reset()noexcept{pending_.reset();carried_.reset();count_=0;next_=0;}
    struct Stats {std::uint64_t committed=0,spawned=0,lost=0,deferredGathers=0;};
    Stats Counts()const noexcept{return {committed_,spawned_,lost_,deferredGathers_};}
    // Wrist linear and angular velocity over the newest sample 30..200 ms older
    // than the latest one (gathers are ~5 ms apart; a 30 ms base rejects jitter).
    bool Velocities(std::int64_t now,math::Vec3& linear,math::Vec3& angular)const noexcept {
        if(!count_)return false;const auto& last=history_[(next_+history_.size()-1)%history_.size()];
        if(now-last.ns>CarriedRetainNs)return false;
        for(unsigned k=1;k<count_;++k){const auto& h=history_[(next_+history_.size()-1-k)%history_.size()];
            const auto dt=last.ns-h.ns;if(dt<30000000)continue;if(dt>200000000)break;
            const float s=float(double(dt)*1e-9);const auto& a=last.pose.values;const auto& b=h.pose.values;
            linear={(a[3][0]-b[3][0])/s,(a[3][1]-b[3][1])/s,(a[3][2]-b[3][2])/s};
            // Small-rotation angular velocity: w = 1/2 sum(old_i x new_i) / dt.
            math::Vec3 w{};for(unsigned r=0;r<3;++r){
                const float ox=b[r][0],oy=b[r][1],oz=b[r][2],nx=a[r][0],ny=a[r][1],nz=a[r][2];
                w.x+=oy*nz-oz*ny;w.y+=oz*nx-ox*nz;w.z+=ox*ny-oy*nx;}
            angular={w.x*.5f/s,w.y*.5f/s,w.z*.5f/s};
            return std::isfinite(linear.x+linear.y+linear.z+angular.x+angular.y+angular.z);
        }
        return false;
    }
private:
    struct Sample {math::Matrix4 pose{};std::int64_t ns=0;};
    struct Pending {bool fromWell=false;std::int64_t atNs=0;};
    std::array<Sample,24> history_{};unsigned next_=0,count_=0;
    std::optional<math::Matrix4> carried_;std::int64_t carriedNs_=0;
    std::optional<Pending> pending_;
    std::uint64_t committed_=0,spawned_=0,lost_=0,deferredGathers_=0;
};
} // namespace fvr::bc2
