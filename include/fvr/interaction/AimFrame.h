#pragma once
#include "fvr/interaction/ActionPolicy.h"
#include "fvr/math/StereoMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace fvr::interaction {
// Engine-independent controller direction in the upright tracking reference.
// Positive yaw turns right; positive pitch points up. No current HMD pose enters.
struct AimAngles {float yaw=0,pitch=0;};
inline std::optional<AimAngles> ControllerAim(const math::Pose& reference,const math::Pose& aim,float previousYaw=0) noexcept {
    const auto p=math::MakeRelativePose(reference,aim);if(!p||!std::isfinite(previousYaw))return {};
    const auto& q=p->orientation;
    const float x=-2*(q.x*q.z+q.w*q.y),y=2*(q.w*q.x-q.y*q.z),z=1-2*(q.x*q.x+q.y*q.y);
    return AimAngles{std::hypot(x,z)>.01f?std::atan2(x,z):previousYaw,std::asin(std::clamp(y,-1.f,1.f))};
}
// Translate head-relative locomotion into a different native input heading.
inline void RotateMovement(float radians,float& forward,float& strafe) noexcept {
    const float f=forward,s=strafe,c=std::cos(radians),sn=std::sin(radians);
    forward=f*c-s*sn;strafe=f*sn+s*c;
}
// Use the current native heading even when controller aiming is unavailable.
// A cached controller yaw is not the native basis after a turn/item change.
// Native sprint is forward-only and travels along the native aim yaw. While the
// sprint button is held without fire/zoom (which cancel it natively), the HMD
// heading must be that yaw so HMD-forward input is native-forward input.
inline bool SprintOwnsHeading(std::uint32_t held) noexcept {
    return (held&Sprint)&&!(held&(Fire|AlternateFire));
}
inline float SprintNativeYaw(float bodyYaw,float headYaw) noexcept { return bodyYaw+headYaw; }
inline void MovementToNativeHeading(float bodyYaw,float nativeYaw,float& forward,float& strafe) noexcept {
    RotateMovement(bodyYaw-nativeYaw,forward,strafe);
}
}
