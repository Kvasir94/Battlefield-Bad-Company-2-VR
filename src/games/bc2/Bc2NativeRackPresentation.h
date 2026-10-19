#pragma once
#include "Bc2NativeRack.h"
#include "Bc2MagazinePresentation.h"
namespace fvr::bc2 {
// Immutable copy of one original animation evaluation and its original input
// authority. No view points into an interaction object's optional state.
struct NativeRackPresentation {
 std::string assetName,meshPath,clipSha256,skeletonSha256;
 std::vector<std::string> partBones;
 std::uint64_t rigFingerprint=0,actionToken=0,trackingEpoch=0;
 EvaluatedNativeActionPose pose;
 NativeRackPhase phase=NativeRackPhase::Idle;
 bool ownsHand=false;
 ReloadStateOwner owner{};
 interaction::PhysicalWeaponIdentity authorityWeapon{};
 std::uint64_t authorityRevision=0;
 std::uint32_t authorityClientWeapon=0;
 bool (*authorityCurrent)(const interaction::PhysicalWeaponIdentity&,std::uint64_t,std::uint32_t)noexcept=nullptr;
 // Current real ownership heartbeat and the unchanged original geometry
 // evidence that last advanced progress. Their leases are not interchangeable.
 interaction::HandInteractionSample inputEvidence{},progressEvidence{};
 interaction::HandInteractionKey weapon{};
 interaction::HandClaimToken gunClaim{},handClaim{};
 std::int64_t gunDeadlineNs=0,handDeadlineNs=0;
};
inline bool NativeRackPresentationFresh(const NativeRackPresentation& p,std::int64_t now)noexcept {
 const auto& in=p.inputEvidence;
 return p.actionToken&&p.rigFingerprint&&p.trackingEpoch&&p.phase!=NativeRackPhase::Idle&&
  p.authorityRevision&&p.authorityClientWeapon&&p.weapon.id&&p.owner.soldier&&p.owner.weapon&&
  p.progressEvidence.sequence&&p.progressEvidence.owner==in.owner&&p.progressEvidence.sequence<=in.sequence&&
  p.progressEvidence.observedNs>0&&p.progressEvidence.observedNs<=in.observedNs&&
  !p.assetName.empty()&&!p.meshPath.empty()&&!p.partBones.empty()&&p.partBones.size()==p.pose.partsInWeapon.size()&&
  in.sequence&&in.focused&&in.tracked[0]&&in.tracked[1]&&in.observedNs>0&&in.observedNs<=now&&in.deadlineNs>now&&
  in.deadlineNs-in.observedNs<=100000000&&p.gunClaim.id&&p.gunClaim.owner==in.owner&&p.gunDeadlineNs>now&&
  p.gunClaim.kind==interaction::HandClaimKind::GunHold&&p.gunClaim.hand==interaction::InteractionHand::Right&&
  p.gunClaim.item==p.weapon&&!in.released[1]&&p.gunDeadlineNs<=in.deadlineNs&&
  (!p.ownsHand||(p.handClaim.id&&p.handClaim.owner==in.owner&&p.handDeadlineNs>now&&
   p.handClaim.kind==interaction::HandClaimKind::Mechanism&&p.handClaim.hand==interaction::InteractionHand::Left&&
   p.handClaim.item==p.weapon&&p.handDeadlineNs<=in.deadlineNs&&p.handClaim.prerequisiteClaim==p.gunClaim.id))&&
  (p.ownsHand||!p.handClaim.id)&&
  p.authorityCurrent&&p.authorityCurrent(p.authorityWeapon,p.authorityRevision,p.authorityClientWeapon);
}
inline bool NativeRackPresentationRetained(const NativeRackPresentation& old,const NativeRackPresentation& current,std::int64_t now)noexcept {
 return NativeRackPresentationFresh(old,now)&&NativeRackPresentationFresh(current,now)&&
  old.actionToken==current.actionToken&&old.weapon==current.weapon&&old.owner==current.owner&&old.authorityWeapon==current.authorityWeapon&&
  old.authorityRevision==current.authorityRevision&&old.authorityClientWeapon==current.authorityClientWeapon&&
  old.rigFingerprint==current.rigFingerprint&&old.trackingEpoch==current.trackingEpoch&&old.assetName==current.assetName&&
  old.meshPath==current.meshPath&&old.clipSha256==current.clipSha256&&old.skeletonSha256==current.skeletonSha256&&
  old.partBones==current.partBones&&old.phase==current.phase&&old.ownsHand==current.ownsHand&&
  old.gunClaim==current.gunClaim&&old.handClaim==current.handClaim&&
  old.progressEvidence.owner==current.progressEvidence.owner&&old.progressEvidence.sequence<=current.progressEvidence.sequence&&
  old.inputEvidence.owner==current.inputEvidence.owner&&old.inputEvidence.sequence<=current.inputEvidence.sequence;
}
}
