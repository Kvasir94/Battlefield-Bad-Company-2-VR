#include "Bc2InteractionZones.h"
#include "Bc2MagazineGeometryProfile.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr;
using namespace fvr::bc2;
using K=interaction::InteractionZoneKind;
int main(){
    // Settings: defaults on, every switch, scale range.
    std::map<std::string,std::string> env;
    const auto get=[&](std::string_view n)->std::string_view{const auto i=env.find(std::string(n));return i==env.end()?std::string_view{}:std::string_view(i->second);};
    auto s=ParseInteractionZoneSettings(get);CHECK(s.zones&&s.magnet&&s.hoverCues&&s.supportKeepOnFlick&&s.scale==1);
    CHECK(!s.magazineGrabRemove);
    for(const auto value:{"", "0", "true", "10"}){env={{"FVR_BC2_MAGAZINE_GRAB_REMOVE",value}};CHECK(!ParseInteractionZoneSettings(get).magazineGrabRemove);}
    env={{"FVR_BC2_MAGAZINE_GRAB_REMOVE","1"}};CHECK(ParseInteractionZoneSettings(get).magazineGrabRemove);
    env={{"FVR_INTERACT_ZONES","0"},{"FVR_INTERACT_MAGNET","0"},{"FVR_INTERACT_HOVER_CUES","0"},{"FVR_BC2_SUPPORT_KEEP_ON_FLICK","0"},{"FVR_INTERACT_ZONE_SCALE","1.25"}};
    s=ParseInteractionZoneSettings(get);CHECK(!s.zones&&!s.magnet&&!s.hoverCues&&!s.supportKeepOnFlick&&s.scale==1.25f);
    env={{"FVR_INTERACT_ZONE_SCALE","3"}};CHECK(ParseInteractionZoneSettings(get).scale==1);
    // Kill switch: legacy support ranges, no magnetism, profiles unwidened.
    const auto legacy=ResolveInteractionZones({false});
    const auto support=SupportGripConfigFor(legacy[K::Support]);
    CHECK(support.captureMeters==.16f&&support.releaseMeters==.28f&&support.squeezeOn==.7f&&support.squeezeOff==.35f);
    for(const auto& z:legacy.zones)CHECK(z.magnetInNs==0&&z.magnetOutNs==0&&!z.hoverCue);
    const auto& xm8=Xm8MagazineGeometry().interaction.insertion;
    const auto unchanged=ZonedInsertion(xm8,legacy[K::MagazineInsert]);
    CHECK(unchanged.captureDistanceMeters==xm8.captureDistanceMeters&&unchanged.releaseDistanceMeters==xm8.releaseDistanceMeters&&
        unchanged.captureAngleRadians==xm8.captureAngleRadians&&unchanged.releaseAngleRadians==xm8.releaseAngleRadians);
    CHECK(ZonedRadius(.09f,legacy[K::MagazineGrab])==.09f);
    // Defaults widen every range but never narrow a more generous profile,
    // and the widened magwell rail is still a valid insertion profile.
    const auto zones=ResolveInteractionZones({});
    const auto wide=ZonedInsertion(xm8,zones[K::MagazineInsert]);
    CHECK(interaction::ValidateReloadInsertionProfile(wide)&&wide.captureDistanceMeters==.08f&&wide.releaseDistanceMeters==.16f&&
        wide.captureAngleRadians>1.13f&&wide.releaseAngleRadians>1.48f&&wide.releaseAngleRadians<1.5708f);
    CHECK(ZonedRadius(.09f,zones[K::MagazineGrab])==.12f&&ZonedRadius(.15f,zones[K::MagazineGrab],true)==.18f&&ZonedRadius(.3f,zones[K::MagazineGrab])==.3f);
    const auto anchors=ZonedBodyAnchors({},zones[K::Holster]);
    CHECK(interaction::ValidBodyAnchors(anchors)&&anchors.chest.radius==.2f&&anchors.shoulders[0].radius==.2f&&anchors.shoulders[1].radius==.2f);
    interaction::ChargingHandleProfile handle;handle.id=handle.revision=1;handle.strokeMeters=.06f;handle.grabRadiusMeters=.05f;
    handle.rearToleranceMeters=.01f;handle.lateralReleaseMeters=.05f;CHECK(interaction::ValidChargingHandleProfile(handle));
    const auto h=ZonedHandle(handle,zones[K::ChargingHandle]);CHECK(interaction::ValidChargingHandleProfile(h)&&h.grabRadiusMeters==.1f&&h.lateralReleaseMeters==.12f);
    // Scale and magnet switches.
    const auto scaled=ResolveInteractionZones({true,true,true,true,1.5f});CHECK(scaled[K::Support].captureMeters==.3f&&interaction::ValidInteractionZones(scaled));
    const auto snap=ResolveInteractionZones({true,false});CHECK(snap[K::Support].magnetInNs==0&&snap[K::Support].captureMeters==.2f);
    std::puts("Bc2InteractionZones: env settings, legacy kill switch, profile widening (never narrowing), valid widened magwell rail, anchors and handle passed.");
    return 0;
}
