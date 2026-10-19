#pragma once
// BC2 binding of the interaction zone table (include/fvr/interaction/InteractionZones.h).
// Profiles keep their measured/authored geometry; a zone only WIDENS a grab
// range (capture/release radii and cones), never narrows a profile, so a
// weapon whose profile is already more generous keeps it.
//
// Environment (read once per process by Bc2Gameplay):
//   FVR_INTERACT_ZONES=0          legacy hard-coded ranges, no magnetism (kill switch)
//   FVR_INTERACT_ZONE_SCALE=x     0.5..2 uniform scale of every capture/release distance
//   FVR_INTERACT_MAGNET=0         instant snap and release (no blend), ranges kept
//   FVR_INTERACT_HOVER_CUES=0     no haptic tick on entering magazine/handle zones
//   FVR_BC2_SUPPORT_KEEP_ON_FLICK=0  a weapon flick cancels the support grip again
//   FVR_BC2_MAGAZINE_GRAB_REMOVE=1   optional receiver-grab removal (default uses A)
//   FVR_BC2_SUPPORT_ALL_WEAPONS=0    support grip only for profile-verified/authored guns again
//   FVR_BC2_SUPPORT_CURRENT_EVIDENCE=0  support claim waits for the lagging renderer packet again
#include "fvr/interaction/BodyAnchors.h"
#include "fvr/interaction/ChargingHandleRack.h"
#include "fvr/interaction/InteractionZones.h"
#include "fvr/interaction/ReloadInsertion.h"
#include "fvr/interaction/SupportGrip.h"
#include <algorithm>
#include <charconv>
#include <string_view>

namespace fvr::bc2 {
struct InteractionZoneSettings {
    bool zones=true,magnet=true,hoverCues=true,supportKeepOnFlick=true;
    float scale=1;
    bool magazineGrabRemove=false;
    // Every gun with a published shot frame offers the front support grip
    // (native left-wrist contact sampled from the original 1P pose).
    bool supportAllWeapons=true;
    // Support ownership uses the current tracked packet (the squeeze edge)
    // rather than the renderer's older packet, which predates the squeeze.
    bool supportCurrentEvidence=true;
};
template<class Getter>
InteractionZoneSettings ParseInteractionZoneSettings(Getter&& get)noexcept {
    InteractionZoneSettings s;
    const auto off=[&](std::string_view name){const auto v=get(name);return !v.empty()&&v[0]=='0';};
    s.zones=!off("FVR_INTERACT_ZONES");s.magnet=!off("FVR_INTERACT_MAGNET");s.hoverCues=!off("FVR_INTERACT_HOVER_CUES");
    s.supportKeepOnFlick=!off("FVR_BC2_SUPPORT_KEEP_ON_FLICK");
    s.magazineGrabRemove=get("FVR_BC2_MAGAZINE_GRAB_REMOVE")=="1";
    s.supportAllWeapons=!off("FVR_BC2_SUPPORT_ALL_WEAPONS");
    s.supportCurrentEvidence=!off("FVR_BC2_SUPPORT_CURRENT_EVIDENCE");
    if(const auto v=get("FVR_INTERACT_ZONE_SCALE");!v.empty()){float f=0;
        const auto r=std::from_chars(v.data(),v.data()+v.size(),f);
        if(r.ec==std::errc{}&&f>=.5f&&f<=2.f)s.scale=f;}
    return s;
}
// The ranges before this table existed: SupportGrip .16/.28 m with squeeze
// .7/.35, no magnetism, and every profile radius used as is (tiny zones never widen).
constexpr interaction::InteractionZoneTable LegacyInteractionZones()noexcept {
    auto t=interaction::DefaultInteractionZones();
    for(auto& z:t.zones){z.captureMeters=.001f;z.releaseMeters=.002f;z.captureRadians=z.releaseRadians=0;z.magnetInNs=z.magnetOutNs=0;z.hoverCue=false;}
    t[interaction::InteractionZoneKind::Support]={.16f,.28f,0,0,.7f,.35f,0,0,false};
    return t;
}
constexpr interaction::InteractionZoneTable ResolveInteractionZones(const InteractionZoneSettings& s)noexcept {
    if(!s.zones)return LegacyInteractionZones();
    auto t=interaction::ScaleInteractionZones(interaction::DefaultInteractionZones(),s.scale);
    for(auto& z:t.zones){if(!s.magnet)z.magnetInNs=z.magnetOutNs=0;if(!s.hoverCues)z.hoverCue=false;}
    return t;
}
inline interaction::SupportGripConfig SupportGripConfigFor(const interaction::InteractionZone& z)noexcept {
    return {z.captureMeters,z.releaseMeters,z.gripOn,z.gripOff};
}
inline interaction::BodyAnchorConfig ZonedBodyAnchors(interaction::BodyAnchorConfig c,const interaction::InteractionZone& z)noexcept {
    const auto widen=[&](interaction::BodyAnchor& a){a.radius=std::clamp(std::max(a.radius,z.captureMeters),.03f,.35f);};
    widen(c.chest);widen(c.shoulders[0]);widen(c.shoulders[1]);return c;
}
// Magwell rail: widen capture/release distance and cones, keeping the rail's
// own validity rules (release beyond capture, cones below 90 degrees).
inline interaction::ReloadInsertionProfile ZonedInsertion(interaction::ReloadInsertionProfile p,const interaction::InteractionZone& z)noexcept {
    auto q=p;
    q.captureDistanceMeters=std::max(p.captureDistanceMeters,z.captureMeters);
    q.releaseDistanceMeters=std::max({p.releaseDistanceMeters,z.releaseMeters,q.captureDistanceMeters+.01f});
    if(z.captureRadians>0){q.captureAngleRadians=std::max(p.captureAngleRadians,z.captureRadians);
        q.releaseAngleRadians=std::min(1.55f,std::max({p.releaseAngleRadians,z.releaseRadians,q.captureAngleRadians+.05f}));}
    return interaction::ValidateReloadInsertionProfile(q)?q:p;
}
inline interaction::ChargingHandleProfile ZonedHandle(interaction::ChargingHandleProfile p,const interaction::InteractionZone& z)noexcept {
    auto q=p;q.grabRadiusMeters=std::min(.2f,std::max(p.grabRadiusMeters,z.captureMeters));
    q.lateralReleaseMeters=std::min(.3f,std::max(p.lateralReleaseMeters,z.releaseMeters));
    return interaction::ValidChargingHandleProfile(q)?q:p;
}
inline float ZonedRadius(float profileRadius,const interaction::InteractionZone& z,bool releasing=false)noexcept {
    return std::max(profileRadius,releasing?z.releaseMeters:z.captureMeters);
}
} // namespace fvr::bc2
