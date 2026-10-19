#pragma once
#include "fvr/interaction/AimFrame.h"
#include "fvr/interaction/TrackingMath.h"
namespace fvr::bc2 {
// Orientation-only relations. Callers separately bind native model axes and
// item/wrist attachment to exact current equipment and its original lifetime.
inline std::optional<math::Matrix4> GripBoreRelation(const math::Matrix4& boreInModel,
    const math::Matrix4& weaponInWrist,const math::Matrix4& wristToGrip)noexcept {
    if(!interaction::InverseRigid(boreInModel)||!interaction::InverseRigid(weaponInWrist)||
       !interaction::InverseRigid(wristToGrip))return {};
    auto relation=interaction::Multiply(interaction::Multiply(boreInModel,weaponInWrist),wristToGrip);
    relation.values[3]={0,0,0,1};return relation;
}
inline std::optional<math::Matrix4> CurrentGripBore(const math::Pose& reference,const math::Pose& grip,
    const math::Matrix4& boreInGrip)noexcept {
    if(!interaction::InverseRigid(boreInGrip))return {};
    const auto relative=math::MakeRelativePose(reference,grip);if(!relative)return {};
    const auto view=math::MakeLhViewFromOpenXRPose(*relative);if(!view)return {};
    const auto current=interaction::InverseRigid(*view);if(!current)return {};
    auto bore=interaction::Multiply(boreInGrip,*current);bore.values[3]={0,0,0,1};return bore;
}
inline std::optional<interaction::AimAngles> GripBoreAngles(const math::Matrix4& bore,float previousYaw=0)noexcept {
    if(!interaction::InverseRigid(bore)||!std::isfinite(previousYaw))return {};
    const auto& f=bore.values[2];
    return interaction::AimAngles{std::hypot(f[0],f[2])>.01f?std::atan2(f[0],f[2]):previousYaw,
        std::asin(std::clamp(f[1],-1.f,1.f))};
}
} // namespace fvr::bc2
