#pragma once
// Optional floating-hands presentation (docs/INTERACTION-POLISH-20261008.md,
// reports/floating-hands-intake/NATIVE-DRAW-CONTRACT.md, private).
//
// FVR_HANDS_MODE=floating|ik (default ik). Floating mode keeps every hand
// interaction exactly as in IK mode: the same tracked wrist targets, the
// same hand arbiter and hysteresis, the same immutable authored contacts in
// held-object space (applied before any procedural IK or recoil), no dual
// wield or melee. Floating wrists and their held item are not arm-reach limited.
// The native arm
// skin palette entries (upper arm, forearm and their twist/attachment bones,
// never the hand subtree) are collapsed onto the wrist joint, so sleeves and
// arm geometry vanish into the cuff and the native gloves float on the
// tracked wrists. This reuses the already verified private palette/Pack path
// (the same 12-scalar collapse that hides the magazine). It does NOT draw the
// capped standard-floating-hands-v1 mesh: that substitution needs the native
// glove draw/owner/palette/layout proof the contract lists as outstanding, so
// it is neither attempted nor claimed here. Untracked arms stay byte-for-byte
// native, as in IK mode.
#include "Bc2Rig.h"
#include "fvr/interaction/ArmIk.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace fvr::bc2 {
enum class HandsPresentationMode:unsigned {Ik,Floating};
// Keep the evaluated finger/attachment animation relative to each wrist.
// Floating hands have no shoulder reach constraint; IK keeps the existing
// anatomical solve. Cosmetic support/mechanism targets use this same path,
// while interaction contacts are captured earlier from raw tracked wrists.
inline std::optional<interaction::ArmPose> SolvePresentedArms(HandsPresentationMode mode,
    std::span<const std::int32_t> parents,std::span<const math::Matrix4> native,
    interaction::ArmJoints left,interaction::ArmJoints right,
    const interaction::ArmTarget& leftTarget,const interaction::ArmTarget& rightTarget){
    if(mode!=HandsPresentationMode::Floating)
        return interaction::SolveTrackedArms(parents,native,left,right,leftTarget,rightTarget);
    if(!interaction::ValidateArms(parents,left,right))return {};
    interaction::ArmPose result;
    for(unsigned side=0;side<2;++side){
        const auto& target=side?rightTarget:leftTarget;if(!target.enabled)continue;
        const auto writes=interaction::RetargetRigSubtree(parents,native,side?right.wrist:left.wrist,target.wrist);
        if(!writes)return {};
        result.writes.insert(result.writes.end(),writes->begin(),writes->end());
    }
    return result;
}
inline std::optional<math::Matrix4> PresentedWeaponFrame(HandsPresentationMode mode,
    const math::Matrix4& trackedWeapon,const math::Matrix4& trackedWrist,
    std::span<const interaction::BoneWrite> writes,std::uint32_t wrist){
    if(!interaction::InverseAnimatedTransform(trackedWeapon))return {};
    if(mode==HandsPresentationMode::Floating)return trackedWeapon;
    const auto inverse=interaction::InverseAnimatedTransform(trackedWrist);if(!inverse)return {};
    for(const auto& write:writes)if(write.index==wrist)
        return interaction::Multiply(interaction::Multiply(trackedWeapon,*inverse),write.transform);
    return trackedWeapon;
}
struct HandsModeSettings {HandsPresentationMode mode=HandsPresentationMode::Ik;bool invalid=false;};
template<class Getter>
HandsModeSettings ParseHandsModeSettings(Getter&& get)noexcept {
    HandsModeSettings s;const std::string_view v=get("FVR_HANDS_MODE");
    if(v.empty()||v=="ik"||v=="IK")return s;
    if(v=="floating"||v=="FLOATING"){s.mode=HandsPresentationMode::Floating;return s;}
    s.invalid=true;return s; // unknown value: the safe default
}
inline const char* HandsModeName(HandsPresentationMode m)noexcept{return m==HandsPresentationMode::Floating?"floating":"ik";}
namespace floating_hands_detail {
inline bool Below(std::span<const std::int32_t> parents,std::uint32_t bone,std::uint32_t root)noexcept {
    for(auto at=std::int32_t(bone);at>=0&&std::size_t(at)<parents.size();at=parents[std::size_t(at)])if(std::uint32_t(at)==root)return true;
    return false;
}
}
// Bones whose skin collapses for one arm: the shoulder (upper arm) subtree
// without the wrist subtree. Empty when the topology is not a valid chain.
inline std::vector<std::uint32_t> ArmCollapseBones(std::span<const std::int32_t> parents,const interaction::ArmJoints& arm){
    using floating_hands_detail::Below;std::vector<std::uint32_t> out;
    if(arm.shoulder>=parents.size()||arm.elbow>=parents.size()||arm.wrist>=parents.size()||
       !Below(parents,arm.elbow,arm.shoulder)||!Below(parents,arm.wrist,arm.elbow)||arm.wrist==arm.elbow)return out;
    for(std::uint32_t n=0;n<parents.size();++n)if(Below(parents,n,arm.shoulder)&&!Below(parents,n,arm.wrist))out.push_back(n);
    return out;
}
// Collapse the given arms of an already built plan onto their final wrist
// joints. `writes` are the final canonical world writes the plan came from;
// a side without a wrist write is left unchanged. Native SIMD padding (the
// fourth float of each row) is preserved, as BuildRigPosePlan does.
inline std::optional<RigPosePlan> CollapseArmsToWrists(const RigSnapshot& rig,RigPosePlan plan,
    std::span<const interaction::BoneWrite> writes,bool left,bool right){
    const auto& id=rig.identity;
    if(!id.count||rig.parents.size()!=id.count||rig.nativeEvaluated.size()!=id.count)return {};
    unsigned collapsedSides=0;
    for(const auto side:{0u,1u}){
        if(!(side==0?left:right))continue;
        const auto& arm=side==0?rig.left:rig.right;
        const auto wrist=std::find_if(writes.begin(),writes.end(),[&](const auto& w){return w.index==arm.wrist;});
        if(wrist==writes.end())continue;
        const auto& p=wrist->transform.values[3];
        if(!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))return {};
        const auto bones=ArmCollapseBones(rig.parents,arm);if(bones.empty())return {};
        for(const auto bone:bones){
            if(std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),bone)!=rig.nativeHiddenLeaves.end())continue;
            auto edit=std::find_if(plan.edits.begin(),plan.edits.end(),[&](const auto& e){return e.index==bone;});
            if(edit==plan.edits.end()){
                plan.edits.push_back({bone,id.evaluatedMatrices+bone*64,rig.nativeEvaluated[bone],rig.nativeEvaluated[bone]});
                edit=plan.edits.end()-1;
            }
            // Zero basis + wrist translation: every vertex weighted to this bone
            // lands on the wrist joint (canonical -> native: Z negated).
            const float row3[3]{p[0],p[1],-p[2]};
            for(unsigned row=0;row<3;++row)std::memset(edit->after.data()+row*16,0,12);
            std::memcpy(edit->after.data()+48,row3,12);
        }
        ++collapsedSides;
    }
    if(!collapsedSides)return {};
    return plan;
}
} // namespace fvr::bc2
