#pragma once
// Floor for a dropped magazine (presentation only). The play-space estimate
// (the hand's tracked height above a floor-relative space) maps the player's
// real floor into the game, which sits about 30 cm above the game ground in
// SP_02 (run 211951: the mag rested at y 174.02, the depth floor under it
// 173.71). The depth ground probe (Bc2GroundHeight.h, FVR_BC2_GROUND_PROBE=1)
// measures the ground ahead of the player; a fresh sample within reach is the
// floor, and otherwise the play-space estimate is corrected by the last
// observed ground offset. Canonical metres (the body-prop space: native Z negated).
#include <cmath>
#include <optional>
#include <string_view>

namespace fvr::bc2 {
enum class DroppedFloorSource : unsigned {PlaySpace,Fixed,Ground,Offset};
struct DroppedFloorGround {float y=0,x=0,z=0;};
struct DroppedFloorChoice {float y=0;DroppedFloorSource source=DroppedFloorSource::Fixed;};
class DroppedFloor {
public:
    // The probe looks 0.75-4.5 m ahead; farther than this from the hand is
    // another piece of ground.
    static constexpr float ReachMeters=5.f;
    // Each gameplay step: a fresh ground sample with a current play-space
    // floor refreshes the offset between them.
    void Observe(std::optional<float> playFloor,const std::optional<DroppedFloorGround>& ground)noexcept {
        if(!playFloor||!ground||!std::isfinite(*playFloor)||!std::isfinite(ground->y))return;
        const float offset=ground->y-*playFloor;
        if(std::fabs(offset)>2.f){++rejected_;return;} // not the floor the player stands on
        offset_=offset;++observed_;
    }
    DroppedFloorChoice Choose(float handX,float handZ,std::optional<float> playFloor,float fallback,
        const std::optional<DroppedFloorGround>& ground)noexcept {
        if(ground&&std::isfinite(ground->y)){const float dx=ground->x-handX,dz=ground->z-handZ;
            if(dx*dx+dz*dz<=ReachMeters*ReachMeters){++ground_;return {ground->y,DroppedFloorSource::Ground};}}
        if(playFloor&&offset_){++offset_used_;return {*playFloor+*offset_,DroppedFloorSource::Offset};}
        if(playFloor){++play_;return {*playFloor,DroppedFloorSource::PlaySpace};}
        ++fixed_;return {fallback,DroppedFloorSource::Fixed};
    }
    // The current floor as the visibility check should compare it.
    std::optional<float> Corrected(std::optional<float> playFloor)const noexcept {
        if(!playFloor)return {};return offset_?*playFloor+*offset_:*playFloor;}
    std::optional<float> Offset()const noexcept{return offset_;}
    struct Stats {unsigned observed=0,rejected=0,ground=0,offset=0,playSpace=0,fixed=0;};
    Stats Counts()const noexcept{return {observed_,rejected_,ground_,offset_used_,play_,fixed_};}
private:
    std::optional<float> offset_;
    unsigned observed_=0,rejected_=0,ground_=0,offset_used_=0,play_=0,fixed_=0;
};
inline const char* DroppedFloorLabel(bool eject,DroppedFloorSource s)noexcept {
    switch(s){
    case DroppedFloorSource::Ground:return eject?"eject_ground":"drop_ground";
    case DroppedFloorSource::Offset:return eject?"eject_offset":"drop_offset";
    case DroppedFloorSource::PlaySpace:return eject?"eject":"drop";
    default:return eject?"eject_fixed_floor":"drop_fixed_floor";
    }
}
} // namespace fvr::bc2
