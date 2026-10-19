#include "Bc2Xm8BoltPresentation.h"
#include "Bc2SightContact.h"
#include "Bc2WeaponVisibility.h"
#include <algorithm>
#include <cmath>

namespace fvr::bc2 {
std::optional<Xm8BoltRigBinding> BindXm8BoltRig(const RigSnapshot& rig){return BindXm8BoltRigForFingerprint(rig,Xm8BoltRigFingerprint);}
std::optional<Xm8BoltRigBinding> BindXm8BoltRigForFingerprint(const RigSnapshot& rig,std::uint64_t expected){
    const auto n=rig.names.size();
    if(!n||n>1024||rig.parents.size()!=n||rig.inverseBind.size()!=n)return {};
    const auto named=[&](std::string_view name)->std::optional<std::uint32_t>{
        const auto at=std::find(rig.names.begin(),rig.names.end(),name);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),name)!=rig.names.end())return {};
        return std::uint32_t(at-rig.names.begin());};
    const auto weapon=named(Xm8BoltWeaponBone),carrier=named(Xm8BoltCarrierBone),handle=named(Xm8BoltHandleBone),card=named(Xm8BoltCardBone);
    if(!weapon||!carrier||!handle||!card||rig.weaponBone!=*weapon||
       rig.parents[*carrier]!=std::int32_t(*weapon)||rig.parents[*handle]!=std::int32_t(*carrier)||rig.parents[*card]!=std::int32_t(*weapon)||
       std::find(rig.parents.begin(),rig.parents.end(),std::int32_t(*card))!=rig.parents.end()||
       std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),*card)!=rig.nativeHiddenLeaves.end())return {};
    const auto fingerprint=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind);
    if(!expected||fingerprint!=expected)return {};
    return Xm8BoltRigBinding{fingerprint,*weapon,*carrier,*handle,*card};
}
std::optional<math::Matrix4> Xm8BoltCarrierFromWeapon(const Xm8BoltAsset& a,float travel,float units)noexcept {
    if(!std::isfinite(travel)||travel<0||travel>a.stroke||!std::isfinite(units)||units<=0||units>1000)return {};
    math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;
    for(unsigned k=0;k<3;++k)m.values[3][k]=(a.carrierRest[k]+a.axis[k]*travel)*units;
    return m;
}
std::optional<float> Xm8BoltObservedTravel(const Xm8BoltAsset& a,const math::Matrix4& local,float units)noexcept {
    if(!std::isfinite(units)||units<=0)return {};
    for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)if(!(std::abs(local.values[r][c]-(r==c?1.f:0.f))<.01f))return {};
    std::array<double,3> d{};for(unsigned k=0;k<3;++k)d[k]=double(local.values[3][k])/units-a.carrierRest[k];
    const double along=d[0]*a.axis[0]+d[1]*a.axis[1]+d[2]*a.axis[2];
    double lateral2=0;for(unsigned k=0;k<3;++k){const double x=d[k]-along*a.axis[k];lateral2+=x*x;}
    if(!std::isfinite(along)||lateral2>.005*.005)return {};
    return float(along);
}
std::optional<math::Matrix4> Xm8BoltPartWorld(const math::Matrix4& carrier,float units)noexcept {
    if(!std::isfinite(units)||units<=0||units>1000)return {};
    auto world=carrier;for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)world.values[r][c]*=units;
    for(unsigned r=0;r<4;++r)world.values[r][3]=r==3?1.f:0.f;
    if(!graphics::RigidPropWorldValid(world))return {};
    return world;
}
Xm8BoltCardReason DecideXm8BoltCard(const Xm8BoltCardInputs& i)noexcept {
    using R=Xm8BoltCardReason;
    if(!i.enabled)return R::Disabled;if(!i.assetLoaded)return R::NoAsset;if(!i.xm8)return R::OtherWeapon;
    if(!i.rigBound)return R::NoRig;if(!i.gunRetargeted)return R::GunNative;if(!i.rendererHealthy)return R::RendererUnproven;
    return R::Collapse;
}
const char* Xm8BoltCardReasonName(Xm8BoltCardReason r)noexcept {
    static constexpr const char* names[]{"collapse","disabled","no_asset","other_weapon","no_rig","gun_native","renderer_unproven"};
    return unsigned(r)<unsigned(Xm8BoltCardReason::Count)?names[unsigned(r)]:"unknown";
}
std::optional<std::vector<std::array<std::byte,64>>> CollapseXm8BoltCard(
    std::span<const std::array<std::byte,64>> posed,const Xm8BoltRigBinding& binding){
    if(binding.card>=posed.size())return {};
    const std::array<std::uint32_t,1> index{binding.card};
    return weapon_visibility_detail::CollapseWeightedPalette(posed,index);
}
} // namespace fvr::bc2
