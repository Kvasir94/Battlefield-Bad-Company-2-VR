#pragma once
#include "Bc2Rig.h"
#include "fvr/interaction/AuthoredContacts.h"
#include "fvr/interaction/InteractionZones.h"
#include <limits>
namespace fvr::bc2 {
enum class NativeActionPoseKind:std::uint8_t {Pump,Rack,Reload,ReloadInsert,ReloadRemove,ReloadCarry,ReloadSettle};
enum class NativeActionPoseSource:std::uint8_t {AuthoredClip,EffectiveNativeCapture};
// Exact evaluated native samples, canonical row vectors and metres. Native
// trajectories live in a PRIVATE generated header, never in public source.
struct NativeActionPoseFrame {
    float timeSeconds=0,progress=0;bool returning=false;
    math::Matrix4 wristInWeapon{};
    std::array<math::Matrix4,15> fingersInWrist{}; // Thumb,Index,Middle,Ring,Pinky x3
    std::span<const math::Matrix4> partsInWeapon;
};
struct NativeActionPoseProfile {
    std::string_view assetName,meshPath,clipSha256,skeletonSha256;
    std::uint64_t rigFingerprint=0;
    NativeActionPoseKind action=NativeActionPoseKind::Pump;
    std::span<const std::string_view> partBones;
    std::span<const NativeActionPoseFrame> frames;
    NativeActionPoseSource source=NativeActionPoseSource::AuthoredClip;
    std::string_view captureSha256; // Effective observed report identity, never a clip hash.
};
inline bool ValidNativeActionPose(const NativeActionPoseProfile& p)noexcept {
    const auto digest=[](std::string_view s){return s.size()==64&&std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});};
    const bool sourceValid=p.source==NativeActionPoseSource::AuthoredClip?
        digest(p.clipSha256)&&p.captureSha256.empty():p.source==NativeActionPoseSource::EffectiveNativeCapture&&
        p.clipSha256.empty()&&digest(p.captureSha256);
    if(p.assetName.empty()||p.meshPath.empty()||!p.rigFingerprint||!sourceValid||!digest(p.skeletonSha256)||
       p.frames.empty()||p.frames.size()>4096||p.partBones.size()>32)return false;
    for(std::size_t n=0;n<p.partBones.size();++n){
        if(p.partBones[n].empty())return false;
        for(std::size_t k=0;k<n;++k)if(p.partBones[k]==p.partBones[n])return false;
    }
    for(const auto& f:p.frames){
        if(!std::isfinite(f.timeSeconds)||f.timeSeconds<0||!std::isfinite(f.progress)||f.progress<0||f.progress>1||
           f.partsInWeapon.size()!=p.partBones.size()||!interaction::authored_contact_detail::Rigid(f.wristInWeapon,2.f))return false;
        for(const auto& m:f.fingersInWrist)if(!interaction::authored_contact_detail::Rigid(m,.5f))return false;
        for(const auto& m:f.partsInWeapon)if(!interaction::authored_contact_detail::Rigid(m,2.f))return false;
    }
    return true;
}
inline const NativeActionPoseProfile* FindNativeActionPose(std::span<const NativeActionPoseProfile> profiles,
    std::string_view asset,std::string_view mesh,std::uint64_t rig,NativeActionPoseKind action)noexcept {
    if(asset.empty()||mesh.empty()||!rig||profiles.size()>256)return nullptr;
    const NativeActionPoseProfile* found=nullptr;
    for(const auto& p:profiles)if(p.assetName==asset&&p.meshPath==mesh&&p.rigFingerprint==rig&&p.action==action){
        if(found||!ValidNativeActionPose(p))return nullptr;found=&p;
    }
    return found;
}
inline const NativeActionPoseFrame* SampleNativeActionPose(const NativeActionPoseProfile& profile,float progress,bool returning)noexcept {
    if(!std::isfinite(progress)||progress<0||progress>1)return nullptr;
    const NativeActionPoseFrame* best=nullptr;float error=std::numeric_limits<float>::infinity();
    for(const auto& frame:profile.frames)if(frame.returning==returning){
        const float distance=std::abs(frame.progress-progress);
        if(distance<error){best=&frame;error=distance;}
    }
    return best; // Diagnostic nearest-original lookup; presentation evaluates below.
}
// Own the evaluated parts: a returned frame view never points into a temporary
// interpolation buffer. As in KF2's VRReloadHandPose, positions lerp and rotations
// take the shortest quaternion arc. Every channel uses the SAME physical progress.
struct EvaluatedNativeActionPose {
    float timeSeconds=0,progress=0;bool returning=false;
    math::Matrix4 wristInWeapon{};
    std::array<math::Matrix4,15> fingersInWrist{};
    std::vector<math::Matrix4> partsInWeapon;
    NativeActionPoseFrame View()const noexcept{return {timeSeconds,progress,returning,wristInWeapon,fingersInWrist,partsInWeapon};}
};
inline std::optional<EvaluatedNativeActionPose> EvaluateNativeActionPose(
    const NativeActionPoseProfile& profile,float progress,bool returning) {
    if(!std::isfinite(progress)||progress<0||progress>1||profile.frames.empty()||profile.frames.size()>4096||profile.partBones.size()>32)return {};
    const NativeActionPoseFrame *lo=nullptr,*hi=nullptr;
    for(const auto& f:profile.frames)if(f.returning==returning){
        if(!std::isfinite(f.progress)||f.progress<0||f.progress>1)return {};
        if(f.progress<=progress&&(!lo||f.progress>lo->progress))lo=&f;
        if(f.progress>=progress&&(!hi||f.progress<hi->progress))hi=&f;
    }
    // Pump/rack catalogs mark the shared turnaround sample as outbound, and
    // mark only subsequent frames returning. Include that ORIGINAL peak as
    // the return's upper endpoint: otherwise a fully rearward controller can
    // lose the whole sampled hand/part pose above the first return sample.
    // This is interpolation across adjacent native frames, not extrapolation
    // or a pose borrowed from the outbound path at the requested progress.
    if(returning&&!hi&&(profile.action==NativeActionPoseKind::Pump||profile.action==NativeActionPoseKind::Rack)){
        const NativeActionPoseFrame* firstReturn=nullptr;
        for(const auto& f:profile.frames)if(f.returning&&(!firstReturn||f.timeSeconds<firstReturn->timeSeconds))firstReturn=&f;
        if(firstReturn)for(const auto& f:profile.frames)
            if(!f.returning&&f.progress==1.f&&f.timeSeconds<=firstReturn->timeSeconds&&
               (!hi||f.timeSeconds>hi->timeSeconds))hi=&f;
        if(hi&&progress==hi->progress)lo=hi;
    }
    // No endpoint extrapolation. A single carry sample is
    // deliberately static and is evaluated at its authored progress (zero).
    if(!lo||!hi||lo->partsInWeapon.size()!=profile.partBones.size()||hi->partsInWeapon.size()!=profile.partBones.size())return {};
    const float t=hi->progress>lo->progress?(progress-lo->progress)/(hi->progress-lo->progress):0.f;
    const auto blend=[&](const math::Matrix4& a,const math::Matrix4& b,float bound)->std::optional<math::Matrix4>{
        if(!interaction::authored_contact_detail::Rigid(a,bound)||!interaction::authored_contact_detail::Rigid(b,bound))return {};
        if(t==0)return a;if(t==1)return b;return interaction::BlendTransform(a,b,t);
    };
    EvaluatedNativeActionPose out;out.progress=progress;out.returning=returning;
    out.timeSeconds=lo->timeSeconds+(hi->timeSeconds-lo->timeSeconds)*t;
    const auto wrist=blend(lo->wristInWeapon,hi->wristInWeapon,2.f);if(!wrist)return {};out.wristInWeapon=*wrist;
    for(unsigned n=0;n<15;++n){const auto m=blend(lo->fingersInWrist[n],hi->fingersInWrist[n],.5f);if(!m)return {};out.fingersInWrist[n]=*m;}
    out.partsInWeapon.reserve(profile.partBones.size());
    for(std::size_t n=0;n<profile.partBones.size();++n){const auto m=blend(lo->partsInWeapon[n],hi->partsInWeapon[n],2.f);if(!m)return {};out.partsInWeapon.push_back(*m);}
    return out;
}
inline std::optional<std::size_t> NativeActionPartIndex(const NativeActionPoseProfile& profile,std::string_view name)noexcept {
    const auto at=std::find(profile.partBones.begin(),profile.partBones.end(),name);
    if(at==profile.partBones.end()||std::find(at+1,profile.partBones.end(),name)!=profile.partBones.end())return {};
    return std::size_t(at-profile.partBones.begin());
}
// Fit the authored trajectory to the live rig's seated ammo transform once,
// using its terminal sample, then apply that same correction to wrist and all
// parts. Finger transforms remain in their authored wrist frame.
inline bool AnchorNativeActionPose(EvaluatedNativeActionPose& evaluated,
    const NativeActionPoseProfile& profile,std::size_t part,float anchorProgress,const math::Matrix4& target) {
    const auto anchor=EvaluateNativeActionPose(profile,anchorProgress,false);
    if(!anchor||part>=anchor->partsInWeapon.size()||!interaction::authored_contact_detail::Rigid(target,2.f))return false;
    const auto inverse=interaction::InverseAnimatedTransform(anchor->partsInWeapon[part]);if(!inverse)return false;
    const auto correction=interaction::Multiply(*inverse,target);
    evaluated.wristInWeapon=interaction::Multiply(evaluated.wristInWeapon,correction);
    for(auto& p:evaluated.partsInWeapon)p=interaction::Multiply(p,correction);
    return true;
}
struct NativeActionHandPose {
    math::Matrix4 wrist{};
    std::array<math::Matrix4,15> fingersInWrist{};
};
// Cosmetic only: the raw tracked wrist remains the source of every gesture.
// Acquisition/release use absolute time, including repeated eye publications.
// A released hand decays its last correction in the TRACKED wrist's frame;
// moving the weapon after release cannot carry the hand away with it.
class NativeActionHandTransition {
public:
    void Reset()noexcept{*this={};}
    std::optional<NativeActionHandPose> Update(std::int64_t now,const NativeActionHandPose& free,
        const NativeActionHandPose* authored,std::uint64_t token,float units) {
        const auto inverse=interaction::InverseAnimatedTransform(free.wrist);
        if(now<=0||!inverse||!std::isfinite(units)||units<=0||(lastNs_&&now<lastNs_)){Reset();return {};}
        lastNs_=now;const bool held=authored&&token;
        if(held&&(token_!=token||!held_)){
            startNs_=now;start_=active_?last_:free;
            correction_=interaction::Multiply(start_.wrist,*inverse);
            const auto& a=start_.wrist.values[3];const auto& b=authored->wrist.values[3];
            const float distance=std::hypot(a[0]-b[0],a[1]-b[1],a[2]-b[2])/units;
            durationNs_=80000000+std::int64_t(std::min(distance,1.f)*100000000.f);
            token_=token;
        }else if(!held&&held_){
            startNs_=now;durationNs_=120000000;start_=last_;
            correction_=interaction::Multiply(start_.wrist,*inverse);token_=0;
        }
        held_=held;
        if(!held&&!active_)return {};
        const float alpha=interaction::SmoothStep(float(double(now-startNs_)/double(durationNs_)));
        if(!held&&alpha>=1){Reset();return {};}
        NativeActionHandPose out;
        math::Matrix4 identity{};for(unsigned n=0;n<4;++n)identity.values[n][n]=1;
        const auto decayed=interaction::BlendTransform(correction_,identity,alpha);
        if(!decayed){Reset();return {};}
        const auto initial=interaction::Multiply(*decayed,free.wrist);
        const auto wrist=held?interaction::BlendTransform(initial,authored->wrist,alpha):std::optional{initial};
        if(!wrist){Reset();return {};}out.wrist=*wrist;
        for(unsigned n=0;n<15;++n){
            const auto finger=interaction::BlendTransform(start_.fingersInWrist[n],held?authored->fingersInWrist[n]:free.fingersInWrist[n],alpha);
            if(!finger){Reset();return {};}out.fingersInWrist[n]=*finger;
        }
        last_=out;active_=true;return out;
    }
    bool Active()const noexcept{return active_;}
private:
    NativeActionHandPose start_{},last_{};math::Matrix4 correction_{};
    std::int64_t startNs_=0,lastNs_=0,durationNs_=80000000;
    std::uint64_t token_=0;bool held_=false,active_=false;
};
inline std::optional<math::Matrix4> NativeActionWrist(const NativeActionPoseFrame& frame,const math::Matrix4& weapon,float units)noexcept {
    if(!std::isfinite(units)||units<=0||!interaction::InverseAnimatedTransform(weapon)||
       !interaction::authored_contact_detail::Rigid(frame.wristInWeapon,2.f))return {};
    return interaction::Multiply(interaction::ScaleTranslation(frame.wristInWeapon,units),weapon);
}
// Same-receiver native rack grasp. Some guns have a VR charging handle but no
// shipped clip in which the hand racks it (the scoped XM8's only reload clip
// never moves jntWpn_17). A sister gun that shares the exact receiver rig can
// supply the ORIGINAL authored wrist/finger samples for that handle: the XM8
// LMG 1P_Reload rack window grasps the same jntWpn_17 at the same weapon-local
// rest (docs/SPAS-PUMP-BOLT-GRASPS-20261011.md). The donor's wrist is re-anchored
// on the handle it grasps, never interpolated, and its moving parts are NOT
// used (the recipient keeps its own handle travel). Table entries are explicit;
// nothing is inferred from a weapon class or a shared bone name alone.
struct NativeRackGraspDonor {
    std::string_view asset,donorAsset,donorMesh,handleBone;
    std::array<float,3> restHandleMeters{};float toleranceMeters=0;
};
inline const NativeRackGraspDonor* FindNativeRackGraspDonor(std::string_view asset)noexcept {
    // XM8_sp_s: US_rgl_XM8 HandsIkPose jntWpn_17 rest (0,0.0697,-0.4967) equals
    // US_rif_XM8_LMG HandsIkPose to <0.1 mm (and jntWpnwpnJnt_16, jntWpn_6).
    static constexpr std::array<NativeRackGraspDonor,1> donors{{
        {"XM8_sp_s","XM8 LMG","Objects/Weapons/Handheld/US_rif_XM8lmg/US_rif_XM8lmg_Mesh","jntWpn_17",{0.f,.069722f,-.496687f},.002f}}};
    for(const auto& d:donors)if(d.asset==asset)return &d;
    return nullptr;
}
// Index of the grasped handle in the donor profile, or -1 when the donor does
// not carry that part or its closed (progress 0, outbound) sample is not at the
// recipient's rest handle within tolerance.
inline int NativeRackGraspDonorPart(const NativeActionPoseProfile& p,const NativeRackGraspDonor& d)noexcept {
    if(p.action!=NativeActionPoseKind::Rack||p.assetName!=d.donorAsset||p.meshPath!=d.donorMesh||
       !std::isfinite(d.toleranceMeters)||d.toleranceMeters<=0||!ValidNativeActionPose(p))return -1;
    int part=-1;
    for(std::size_t n=0;n<p.partBones.size();++n)if(p.partBones[n]==d.handleBone){if(part>=0)return -1;part=int(n);}
    if(part<0)return -1;
    const NativeActionPoseFrame* closed=nullptr;
    for(const auto& f:p.frames)if(!f.returning&&(!closed||f.progress<closed->progress))closed=&f;
    if(!closed||closed->progress>.01f)return -1;
    const auto& t=closed->partsInWeapon[std::size_t(part)].values[3];
    const float dx=t[0]-d.restHandleMeters[0],dy=t[1]-d.restHandleMeters[1],dz=t[2]-d.restHandleMeters[2];
    return std::sqrt(dx*dx+dy*dy+dz*dz)<=d.toleranceMeters?part:-1;
}
// The sampled native wrist relative to the part it grasps, placed on that
// part's CURRENT world transform (canonical row vectors; units per metre).
inline std::optional<math::Matrix4> NativeActionWristOnPart(const NativeActionPoseFrame& frame,std::size_t part,
    const math::Matrix4& partWorld,float units)noexcept {
    if(part>=frame.partsInWeapon.size()||!std::isfinite(units)||units<=0||!interaction::InverseAnimatedTransform(partWorld)||
       !interaction::authored_contact_detail::Rigid(frame.wristInWeapon,2.f))return {};
    const auto inversePart=interaction::InverseAnimatedTransform(frame.partsInWeapon[part]);
    if(!inversePart)return {};
    const auto wristInPart=interaction::Multiply(frame.wristInWeapon,*inversePart);
    return interaction::Multiply(interaction::ScaleTranslation(wristInPart,units),partWorld);
}
inline bool ApplyNativeActionFingers(const NativeActionPoseFrame& frame,const RigSnapshot& rig,
    std::vector<interaction::BoneWrite>& writes,const math::Matrix4& wrist,float units){
    interaction::AuthoredContact contact;contact.fingerCount=15;contact.fingerInWrist=frame.fingersInWrist;
    constexpr std::array<const char*,5> names{"Thumb","Index","Middle","Ring","Pinky"};
    for(unsigned finger=0;finger<5;++finger)for(unsigned joint=0;joint<3;++joint){
        if(!interaction::authored_contact_detail::Rigid(frame.fingersInWrist[finger*3+joint],.5f))return false;
        contact.fingerNames[finger*3+joint]=std::string("LeftHand")+names[finger]+std::to_string(joint+1);
    }
    return interaction::ApplyAuthoredFingers(rig.parents,rig.names,writes,contact,wrist,units);
}
inline std::optional<std::vector<interaction::BoneWrite>> NativeActionPartWrites(const NativeActionPoseProfile& profile,
    const NativeActionPoseFrame& frame,const RigSnapshot& rig,const math::Matrix4& weapon,float units){
    if(frame.partsInWeapon.size()!=profile.partBones.size()||!std::isfinite(units)||units<=0||
       !interaction::InverseAnimatedTransform(weapon)||rig.names.size()!=rig.parents.size()||rig.weaponBone>=rig.parents.size())return {};
    std::vector<interaction::BoneWrite> out;
    for(std::size_t n=0;n<profile.partBones.size();++n){
        const auto at=std::find(rig.names.begin(),rig.names.end(),profile.partBones[n]);
        if(at==rig.names.end()||std::find(at+1,rig.names.end(),profile.partBones[n])!=rig.names.end())return {};
        const auto bone=unsigned(at-rig.names.begin());if(bone==rig.weaponBone)return {};
        bool below=false;std::size_t depth=0;
        for(auto parent=rig.parents[bone];parent!=-1;parent=rig.parents[parent]){
            if(parent<0||std::size_t(parent)>=rig.parents.size()||++depth>rig.parents.size())return {};
            if(unsigned(parent)==rig.weaponBone){below=true;break;}
        }
        if(!below||!interaction::authored_contact_detail::Rigid(frame.partsInWeapon[n],2.f))return {};
        out.push_back({bone,interaction::Multiply(interaction::ScaleTranslation(frame.partsInWeapon[n],units),weapon)});
    }
    return out;
}
} // namespace fvr::bc2
