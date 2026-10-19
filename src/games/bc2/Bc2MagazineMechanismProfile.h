#pragma once
#include "Bc2Rig.h"
#include "fvr/interaction/ChargingHandleRack.h"
#include <algorithm>
#include <span>
#include <string_view>

namespace fvr::bc2 {
struct MagazineMechanismPart {
    std::string_view bone{};
    math::Matrix4 closedInWeaponMeters{};
};
// Exact authored moving parts and a VR rack contact. Stroke/contact tolerances
// describe the VR gesture; they never infer native animation or action timing.
// The existing physical authority owns chamber completion via CycleAction.
struct MagazineMechanismProfile {
    std::string_view asset{},mesh{};
    std::uint64_t rigFingerprint=0;
    interaction::ChargingHandleProfile handle{};
    std::span<const MagazineMechanismPart> parts{};
    std::string_view sourceDigest{};
};
inline bool ValidMagazineMechanism(const MagazineMechanismProfile& p)noexcept {
    if(p.asset.empty()||p.mesh.empty()||!p.rigFingerprint||p.handle.pistolSlide||
       !interaction::ValidChargingHandleProfile(p.handle)||p.parts.empty()||p.parts.size()>32||
       p.sourceDigest.size()!=64||!std::all_of(p.sourceDigest.begin(),p.sourceDigest.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))return false;
    for(std::size_t n=0;n<p.parts.size();++n){
        const auto& part=p.parts[n];if(part.bone.empty()||!interaction::InverseRigid(part.closedInWeaponMeters))return false;
        for(std::size_t k=0;k<n;++k)if(part.bone==p.parts[k].bone)return false;
    }
    return true;
}
inline const MagazineMechanismProfile* SelectMagazineMechanism(std::span<const MagazineMechanismProfile> profiles,
    std::string_view asset,std::string_view mesh,std::uint64_t rig)noexcept {
    if(profiles.size()>256||asset.empty()||mesh.empty()||!rig)return nullptr;
    const MagazineMechanismProfile* selected=nullptr;
    for(const auto& p:profiles)if(p.asset==asset&&p.mesh==mesh&&p.rigFingerprint==rig){
        if(selected||!ValidMagazineMechanism(p))return nullptr;selected=&p;
    }
    if(selected)for(const auto& p:profiles)if(&p!=selected&&p.handle.id==selected->handle.id)return nullptr;
    return selected;
}
// Palette-only absolute closed poses, never native source animation writes.
// Every part must be uniquely named and remain below this exact weapon root.
inline std::optional<std::vector<interaction::BoneWrite>> MagazineMechanismWrites(const MagazineMechanismProfile& profile,
    const RigSnapshot& rig,const math::Matrix4& placed,float travel,float units){
    if(!ValidMagazineMechanism(profile)||!interaction::InverseAnimatedTransform(placed)||!std::isfinite(travel)||
       !std::isfinite(units)||units<=0||rig.names.size()!=rig.parents.size()||rig.weaponBone>=rig.parents.size())return {};
    std::vector<interaction::BoneWrite> writes;
    for(const auto& part:profile.parts){
        const auto at=std::find(rig.names.begin(),rig.names.end(),part.bone);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),part.bone)!=rig.names.end())return {};
        const auto bone=unsigned(at-rig.names.begin());if(bone==rig.weaponBone)return {};
        bool below=false;
        std::size_t depth=0;
        for(auto parent=rig.parents[bone];parent>=0;parent=rig.parents[unsigned(parent)]){
            if(++depth>rig.parents.size())return {};
            if(unsigned(parent)>=rig.parents.size())return {};
            if(unsigned(parent)==rig.weaponBone){below=true;break;}
        }
        if(!below||std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),bone)!=rig.nativeHiddenLeaves.end())return {};
        auto local=part.closedInWeaponMeters;
        for(unsigned n=0;n<3;++n)local.values[3][n]=(local.values[3][n]+std::clamp(travel,0.f,1.f)*profile.handle.strokeMeters*profile.handle.pullDirection[n])*units;
        writes.push_back({bone,interaction::Multiply(local,placed)});
    }
    return writes;
}
}
#ifdef FVR_BC2_MAGAZINE_MECHANISM_HEADER
#include FVR_BC2_MAGAZINE_MECHANISM_HEADER
#endif
namespace fvr::bc2 {
inline const MagazineMechanismProfile* FindMagazineMechanism(std::string_view asset,std::string_view mesh,std::uint64_t rig)noexcept {
#ifdef FVR_BC2_MAGAZINE_MECHANISM_HEADER
    return SelectMagazineMechanism(generated::MagazineMechanisms,asset,mesh,rig);
#else
    (void)asset;(void)mesh;(void)rig;return nullptr;
#endif
}
}
