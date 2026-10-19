#pragma once
#include "Bc2NativeRackPresentation.h"
#include "Bc2SightContact.h"
namespace fvr::bc2 {
struct NativeRackPosePlan {
 std::vector<interaction::BoneWrite> writes;
 std::optional<math::Matrix4> wristTarget;
};
// The transport already owns one coherent original animation evaluation.
// Place its full part matrices, including handle rotation, without inferring
// another travel fraction or borrowing a sister weapon's hand.
inline std::optional<NativeRackPosePlan> BuildNativeRackPose(const NativeRackPresentation& p,
 const RigSnapshot& rig,const math::Matrix4& weapon,float units,std::int64_t now){
 if(!NativeRackPresentationFresh(p,now)||p.assetName!=Xm8MagazineAsset||p.meshPath!=Xm8MagazineMesh||
  p.rigFingerprint!=SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)||
  rig.identity.soldier!=p.owner.soldier||rig.identity.weak!=p.owner.weak||
  !std::isfinite(units)||units<=0)return {};
 std::vector<std::string_view> names;names.reserve(p.partBones.size());for(const auto& n:p.partBones)names.push_back(n);
 const auto frame=p.pose.View();const std::array frames{frame};
 const NativeActionPoseProfile profile{p.assetName,p.meshPath,p.clipSha256,p.skeletonSha256,
  p.rigFingerprint,NativeActionPoseKind::Rack,names,frames};
 if(!ValidNativeActionPose(profile)||!NativeActionPartIndex(profile,"jntWpn_17"))return {};
 const auto parts=NativeActionPartWrites(profile,frame,rig,weapon,units);if(!parts)return {};
 NativeRackPosePlan out;out.writes=*parts;
 for(const auto& w:out.writes)if(std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),w.index)!=rig.nativeHiddenLeaves.end())return {};
 if(p.ownsHand){
  const auto wrist=NativeActionWrist(frame,weapon,units);if(!wrist||rig.left.wrist>=rig.names.size()||rig.names[rig.left.wrist]!="LeftHand")return {};
  out.wristTarget=*wrist;out.writes.push_back({rig.left.wrist,*wrist});
  if(!ApplyNativeActionFingers(frame,rig,out.writes,*wrist,units))return {};
 }
 return out;
}
inline ReloadPackedPaletteChoice SelectNativeRackPackedPalette(const NativeRackPresentation& old,
 const NativeRackPresentation* current,std::int64_t now,std::span<const std::array<std::byte,64>> posed,
 std::span<const std::array<std::byte,64>> base,bool coherent)noexcept {
 if(!posed.empty()&&posed.size()<=1024&&current&&coherent&&NativeRackPresentationRetained(old,*current,now))return {posed,false};
 if(!posed.empty()&&posed.size()<=1024&&base.size()==posed.size())return {base,true};
 return {{},true};
}
}
