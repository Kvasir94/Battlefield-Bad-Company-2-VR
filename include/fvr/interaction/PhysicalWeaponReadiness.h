#pragma once
#include "PhysicalWeaponState.h"

namespace fvr::interaction {
enum class WeaponReadyCue { Unavailable, Settling, MagazineOut, Pump, Rack, Empty, Ready };
struct WeaponReadiness {
    WeaponReadyCue cue=WeaponReadyCue::Unavailable;
    bool physicalCanFire=false,projectionPending=false;
};
inline bool WeaponReadinessAllowsFireInput(const WeaponReadiness& r)noexcept {
    return r.physicalCanFire&&!r.projectionPending;
}
// A pump stroke clears a fired shell or chambers a newly loaded tube. Once
// ready (or completely empty after clearing), another stroke is not owed.
inline bool PhysicalPumpActionOwed(const PhysicalWeaponSnapshot& s)noexcept {
    return s.initialized&&s.config.family==PhysicalWeaponFamily::TubePump&&
        (s.pumpOwed||(s.needsRack&&s.feed>0));
}
// Presentation consumes owned state, never animation progress or native pooled
// counts. A chambered gun can fire with MAG OUT; settlement is independent of
// physical action debt. This function grants no command or native permission.
inline WeaponReadiness ReadWeaponReadiness(const PhysicalWeaponSnapshot& s,
    bool suspended=false,bool nativeBusy=false,bool commandPending=false)noexcept {
    if(!s.initialized||suspended)return {};
    WeaponReadiness r;r.physicalCanFire=s.canFire;r.projectionPending=nativeBusy||commandPending;
    if(r.projectionPending){r.cue=WeaponReadyCue::Settling;return r;}
    const bool magazine=s.config.family>=PhysicalWeaponFamily::ClosedBoltMagazine&&
        s.config.family<=PhysicalWeaponFamily::NoActionMagazine;
    r.cue=magazine&&!s.insertedMagazine?WeaponReadyCue::MagazineOut:
        s.pumpOwed?WeaponReadyCue::Pump:s.needsRack&&s.feed?WeaponReadyCue::Rack:
        s.canFire?WeaponReadyCue::Ready:WeaponReadyCue::Empty;
    return r;
}
inline const char* WeaponReadyCueName(WeaponReadyCue cue)noexcept {
    switch(cue){
    case WeaponReadyCue::Settling:return "SETTLING";
    case WeaponReadyCue::MagazineOut:return "MAG OUT";
    case WeaponReadyCue::Pump:return "PUMP";
    case WeaponReadyCue::Rack:return "RACK";
    case WeaponReadyCue::Empty:return "EMPTY";
    case WeaponReadyCue::Ready:return "READY";
    default:return "UNAVAILABLE";
    }
}
// One cue transition per item, not one per native firing copy or animation
// notification. Presentation can use it without owning ammunition or receipts.
class WeaponReadinessTransitions {
public:
    bool Update(const PhysicalWeaponIdentity& identity,WeaponReadyCue cue)noexcept {
        if(identity==identity_&&cue==cue_)return false;
        identity_=identity;cue_=cue;return true;
    }
private:
    PhysicalWeaponIdentity identity_{};
    WeaponReadyCue cue_=WeaponReadyCue::Unavailable;
};
}
