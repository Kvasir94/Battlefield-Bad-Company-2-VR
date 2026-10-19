#pragma once
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/graphics/InteractionZoneProp.h"
#include "fvr/interaction/ReloadHints.h"
#include "fvr/interaction/HandInteraction.h"
#include <algorithm>
namespace fvr::bc2 {
inline constexpr std::uint32_t ReloadGuidanceAmber=255u|166u<<8|26u<<16|255u<<24;
inline unsigned ReloadGuidanceOpacity(float alpha)noexcept {
    return std::isfinite(alpha)?unsigned(std::clamp(alpha/interaction::ReloadHints::PeakAlpha*150.f+.5f,0.f,150.f)):0;
}
// centerWorld is already in canonical native world units. Scale only the
// primitive's unit-radius geometry; never translate a second time or renew the
// source input/pose lease. The cue owns no input, item or ammunition.
inline std::optional<graphics::BodyPropInstance> ReloadGuidanceZone(const math::Matrix4& centerWorld,
    float radiusMeters,float units,float alpha,const interaction::HandInteractionOwner& owner,
    std::int64_t observed,std::int64_t deadline,std::int64_t now,unsigned ordinal)noexcept {
    const auto opacity=ReloadGuidanceOpacity(alpha);
    if(!opacity||!std::isfinite(radiusMeters)||radiusMeters<=0||radiusMeters>.2f||!std::isfinite(units)||units<=0)return {};
    graphics::BodyPropInstance out;out.geometry=graphics::InteractionZonePropKey;out.world=centerWorld;
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)out.world.values[r][c]*=radiusMeters*units;
    out.actorGeneration=owner.actorGeneration;out.spaceGeneration=owner.space;
    out.equipmentGeneration=graphics::BodyPropWireEpoch(graphics::BodyPropSourceKind::Guidance,owner.equipGeneration,ordinal);
    out.observedNs=observed;out.deadlineNs=deadline;out.tintRgba=ReloadGuidanceAmber;out.opacity=opacity;
    return graphics::BodyPropFresh(out,now)?std::optional{out}:std::nullopt;
}
} // namespace fvr::bc2
