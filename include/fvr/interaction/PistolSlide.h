#pragma once
#include "ChargingHandleRack.h"

namespace fvr::interaction {
// Raw hand recognition only. The adapter commits rearward ejection and forward
// feeding separately in PhysicalWeaponState; this class never grants ammo.
struct PistolSlideResult {
    bool grabbed=false,held=false,open=false,close=false,cancelled=false;
    float travel=0;
};
class PistolSlide {
public:
    explicit PistolSlide(ChargingHandleProfile profile):profile_(profile){}
    bool Held()const noexcept{return held_;}
    void Reset()noexcept{held_=neutral_=rear_=false;sequence_=0;travel_=0;guard_.Reset();}
    PistolSlideResult Cancel()noexcept {
        PistolSlideResult r;r.cancelled=held_;r.close=held_&&rear_;Reset();return r;
    }
    // A latched slide is acquired where it actually rests, at the rear. A
    // fresh grip followed by release is the physical slingshot gesture.
    PistolSlideResult Update(const ChargingHandleSample& s,bool lockedOpen)noexcept {
        PistolSlideResult r;r.held=held_;r.travel=held_?travel_:(lockedOpen?1.f:0.f);
        if(!ValidChargingHandleProfile(profile_)||!s.sequence||s.observedNs<=0||!s.tracked)return Cancel();
        if(s.sequence<=sequence_)return r;
        sequence_=s.sequence;
        MotionGuard::Position hand{s.weaponFromHand.values[3][0],s.weaponFromHand.values[3][1],s.weaponFromHand.values[3][2]};
        if(!MotionGuard::ValidPosition(hand))return Cancel();
        double along=0,lateral=0;
        for(unsigned i=0;i<3;++i)along+=(hand[i]-profile_.handleMeters[i])*profile_.pullDirection[i];
        for(unsigned i=0;i<3;++i){const double d=hand[i]-profile_.handleMeters[i]-along*profile_.pullDirection[i];lateral+=d*d;}
        if(!held_){
            if(!s.grip){neutral_=true;return r;}
            const double at=lockedOpen?profile_.strokeMeters:0;
            const bool edge=neutral_;neutral_=false;
            if(!edge||std::sqrt((along-at)*(along-at)+lateral)>profile_.grabRadiusMeters)return r;
            if(!guard_.Begin(profile_.motion,hand,s.observedNs))return Cancel();
            held_=true;rear_=lockedOpen;grabAlong_=along;startTravel_=lockedOpen?1.f:0.f;travel_=startTravel_;
            r.grabbed=r.held=true;r.open=lockedOpen;r.travel=travel_;return r;
        }
        if(!s.grip){r.close=rear_;r.held=false;r.travel=0;held_=rear_=false;neutral_=true;travel_=0;guard_.Reset();return r;}
        if(guard_.Check(hand,s.observedNs)!=MotionGuardVerdict::Accepted||std::sqrt(lateral)>profile_.lateralReleaseMeters)return Cancel();
        travel_=float(std::clamp(startTravel_+(along-grabAlong_)/profile_.strokeMeters,0.,1.));
        if(!rear_&&travel_>=1.f-profile_.rearToleranceMeters/profile_.strokeMeters){rear_=true;r.open=true;}
        // Returning the hand to battery while still gripping also closes it.
        // Keep the claim until release; it must never become a support grip.
        if(rear_&&travel_<=profile_.rearToleranceMeters/profile_.strokeMeters){r.close=true;rear_=false;}
        r.held=true;r.travel=travel_;return r;
    }
private:
    ChargingHandleProfile profile_{};MotionGuard guard_{};
    std::uint64_t sequence_=0;double grabAlong_=0;
    float travel_=0,startTravel_=0;bool held_=false,neutral_=false,rear_=false;
};
}
