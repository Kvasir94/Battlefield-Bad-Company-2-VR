#include "Bc2NativeRecoilTransfer.h"
#include <algorithm>
#include <cstring>

namespace fvr::bc2 {
namespace {
bool SameKickOwner(const NativeRecoilContext& a,const NativeRecoilContext& b)noexcept {
    return a.owner==b.owner&&a.equipment==b.equipment&&a.rigEquipmentGeneration==b.rigEquipmentGeneration;
}
bool ValidKick(const NativeRecoilKickSample& sample)noexcept {
    return sample.sequence&&sample.shotQpc>0&&std::isfinite(sample.fromMeters)&&std::isfinite(sample.peakMeters)&&
        std::isfinite(sample.recoverySeconds)&&sample.fromMeters>=0&&sample.fromMeters<=sample.peakMeters&&
        sample.peakMeters>0&&sample.peakMeters<=.05f&&sample.recoverySeconds>=.15f&&sample.recoverySeconds<=.6f;
}
}
std::optional<float> NativeRecoilStrengthWeight(float degrees)noexcept {
    if(!std::isfinite(degrees)||degrees<=0)return {};
    // The original KF2 curve spans 50..1200 Unreal rotator units. Convert its
    // endpoints to degrees before comparing BC2's native authored increment.
    constexpr float low=50.f*360.f/65536.f,high=1200.f*360.f/65536.f;
    return std::clamp(std::log(std::clamp(degrees,low,high)/low)/std::log(24.f),0.f,1.f);
}
std::optional<float> EvaluateNativeRecoilKick(const NativeRecoilKickSample& sample,std::int64_t now,std::int64_t frequency)noexcept {
    if(!ValidKick(sample)||frequency<=0||now<sample.shotQpc)return {};
    const double elapsed=double(now-sample.shotQpc)/double(frequency);
    const auto meters=elapsed<.045?float(sample.fromMeters+(sample.peakMeters-sample.fromMeters)*elapsed/.045):
        float(sample.peakMeters*std::exp(-(elapsed-.045)*2.3/sample.recoverySeconds));
    return meters<.0002f?0.f:meters;
}
std::int64_t NativeRecoilKickDeadline(const NativeRecoilKickSample& sample,std::int64_t frequency)noexcept {
    if(!ValidKick(sample)||frequency<=0)return 0;
    const long double seconds=.045L+sample.recoverySeconds*std::log(static_cast<long double>(sample.peakMeters)/.0002f)/2.3L;
    const long double deadline=sample.shotQpc+std::ceil(seconds*frequency);
    return deadline>sample.shotQpc&&deadline<=INT64_MAX?std::int64_t(deadline):0;
}
bool NativeRecoilKickCurrent(const NativeRecoilKickSample& sample,unsigned soldier,unsigned weak,unsigned weapon,
    std::uint64_t ownerGeneration,std::uint64_t rigEquipmentGeneration,const WeaponEquipmentIdentity& equipment,
    std::uint64_t space,std::int64_t now,std::int64_t frequency)noexcept {
    const auto& c=sample.context;const auto& o=c.owner;const auto deadline=NativeRecoilKickDeadline(sample,frequency);
    return deadline&&now>=sample.shotQpc&&now<deadline&&o.player&&o.soldier&&o.weak&&o.weapon&&
        o.soldier==soldier&&o.weak==weak&&o.weapon==weapon&&o.actorGeneration&&o.actorGeneration==ownerGeneration&&
        o.equipGeneration&&o.space&&o.space==space&&c.rigEquipmentGeneration&&c.rigEquipmentGeneration==rigEquipmentGeneration&&
        c.equipment==equipment&&equipment.weapon==weapon;
}
std::optional<math::Matrix4> NativeRecoilKickDelta(const math::Matrix4& aim,float meters,float units)noexcept {
    if(!interaction::InverseRigid(aim)||!std::isfinite(meters)||meters<0||meters>.05f||!std::isfinite(units)||units<=0)return {};
    math::Matrix4 delta{};for(unsigned n=0;n<4;++n)delta.values[n][n]=1;
    // TrackedAimFrame uses canonical +Z along the bore. Pull its visual grip
    // towards the player after angular recoil, independent of model root axes.
    for(unsigned n=0;n<3;++n)delta.values[3][n]=-aim.values[2][n]*meters*units;
    return interaction::InverseRigid(delta)?std::optional<math::Matrix4>{delta}:std::nullopt;
}
bool NativeRecoilKickEnvelope::Shot(const NativeRecoilContext& context,std::uint64_t sequence,std::int64_t qpc,
    float degrees,bool braced,std::int64_t frequency)noexcept {
    const auto weight=NativeRecoilStrengthWeight(degrees);const auto& o=context.owner;
    if(!weight||!sequence||qpc<=0||frequency<=0||!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||
        !o.equipGeneration||!o.space||!context.rigEquipmentGeneration||context.equipment.weapon!=o.weapon)return false;
    if(sample_&&!SameKickOwner(sample_->context,context))sample_.reset();
    if(sample_&&(sequence<=sample_->sequence||qpc<sample_->shotQpc))return false;
    const auto current=sample_?EvaluateNativeRecoilKick(*sample_,qpc,frequency):std::optional<float>{0.f};if(!current)return false;
    const float impulse=(.008f+.032f*(*weight))*(braced?.5f:1.f);
    sample_=NativeRecoilKickSample{context,sequence,qpc,*current,std::min(*current+impulse,.05f),.15f+.45f*(*weight)};
    return true;
}
bool NativeRecoilPoseCurrent(const NativeRecoilPoseSample& sample,unsigned soldier,unsigned weak,unsigned weapon,
    std::uint64_t ownerGeneration,std::uint64_t rigEquipmentGeneration,const WeaponEquipmentIdentity& equipment,
    std::uint64_t space,std::int64_t nowQpc)noexcept {
    const auto& context=sample.native.context;const auto& owner=context.owner;const auto& copy=sample.native.copy;
    return sample.serial&&sample.tickQpc>0&&sample.deadlineQpc>=sample.tickQpc&&
        sample.deadlineQpc<=context.deadlineQpc&&nowQpc>=sample.tickQpc&&nowQpc<=sample.deadlineQpc&&
        owner.player&&owner.soldier&&owner.weak&&owner.weapon&&owner.soldier==soldier&&owner.weak==weak&&owner.weapon==weapon&&
        owner.actorGeneration&&owner.actorGeneration==ownerGeneration&&owner.equipGeneration&&
        owner.space&&owner.space==space&&context.input&&context.rigEquipmentGeneration&&
        context.rigEquipmentGeneration==rigEquipmentGeneration&&context.equipment==equipment&&
        equipment.weapon==weapon&&copy.cameraEnabled&&copy.firing&&copy.sway&&
        (copy.branch==NativeRecoilBranch::FiringA||copy.branch==NativeRecoilBranch::FiringB);
}
std::optional<math::Matrix4> NativeRecoilRotation(const std::array<float,16>& camera)noexcept {
    math::Matrix4 rotation{};rotation.values[3][3]=1;
    // Camera-local heading is opposite the tracked bore: native consumer
    // 0x89c7b4 subtracts pi before rebuilding its view. First convert native
    // coordinates with D=diag(1,1,-1), then change camera-local to aim-local
    // with H=diag(-1,1,-1): H*(D*M*D)*H = X*M*X, X=diag(-1,1,1).
    // D*M*D alone reverses the native negative-pitch muzzle rise into a dive.
    for(unsigned row=0;row<3;++row)for(unsigned column=0;column<3;++column)
        rotation.values[row][column]=camera[4*row+column]*((row==0)!=(column==0)?-1.f:1.f);
    // Existing rigidity tolerance admits native float roundoff. Check handedness
    // separately: an orthonormal reflection is not a native rotation.
    if(!interaction::InverseRigid(rotation))return {};
    const auto& a=rotation.values;
    const float determinant=a[0][0]*(a[1][1]*a[2][2]-a[1][2]*a[2][1])-
        a[0][1]*(a[1][0]*a[2][2]-a[1][2]*a[2][0])+a[0][2]*(a[1][0]*a[2][1]-a[1][1]*a[2][0]);
    if(!std::isfinite(determinant)||std::abs(determinant-1.f)>.02f)return {};
    return rotation;
}
std::optional<math::Matrix4> NativeRecoilMuzzleUp(const math::Matrix4& r,bool* rectified)noexcept {
    if(rectified)*rectified=false;
    if(!interaction::InverseRigid(r))return {};
    const auto& f=r.values[2];
    if(!(f[1]<0))return r;
    // Keep the native heading of the bore; drop only its descent (and the
    // small roll that accompanies it). Rows: right=up x forward.
    const float length=std::hypot(f[0],f[2]);
    math::Matrix4 out{};out.values[3][3]=1;
    if(!std::isfinite(length)||length<1e-6f){for(unsigned n=0;n<3;++n)out.values[n][n]=1;}
    else{
        const float x=f[0]/length,z=f[2]/length;
        out.values[0]={z,0,-x,0};out.values[1]={0,1,0,0};out.values[2]={x,0,z,0};
    }
    if(rectified)*rectified=true;
    return out;
}
std::optional<math::Matrix4> MuzzleBoreFrame(const math::Matrix4& weapon,const math::Matrix4& muzzle,
    const math::Matrix4& up,float minimum)noexcept {
    if(!std::isfinite(minimum)||minimum<=0||!interaction::InverseAnimatedTransform(weapon)||
       !interaction::InverseAnimatedTransform(muzzle)||!interaction::InverseRigid(up))return {};
    math::Vec3 f{muzzle.values[3][0]-weapon.values[3][0],muzzle.values[3][1]-weapon.values[3][1],muzzle.values[3][2]-weapon.values[3][2]};
    const float length=std::hypot(f.x,f.y,f.z);
    if(!std::isfinite(length)||length<minimum)return {};
    f={f.x/length,f.y/length,f.z/length};
    math::Vec3 u{up.values[1][0],up.values[1][1],up.values[1][2]};
    const float along=u.x*f.x+u.y*f.y+u.z*f.z;
    u={u.x-along*f.x,u.y-along*f.y,u.z-along*f.z};
    const float upLength=std::hypot(u.x,u.y,u.z);
    // A barrel parallel to the reference up has no defined roll.
    if(!std::isfinite(upLength)||upLength<.2f)return {};
    u={u.x/upLength,u.y/upLength,u.z/upLength};
    const math::Vec3 r{u.y*f.z-u.z*f.y,u.z*f.x-u.x*f.z,u.x*f.y-u.y*f.x};
    math::Matrix4 out{};
    out.values[0]={r.x,r.y,r.z,0};out.values[1]={u.x,u.y,u.z,0};out.values[2]={f.x,f.y,f.z,0};out.values[3]={0,0,0,1};
    if(!interaction::InverseRigid(out))return {};
    return out;
}
std::optional<math::Matrix4> NativeRecoilGripDelta(const std::array<float,16>& camera,
    const math::Matrix4& aimBasis,const math::Matrix4& firingWrist,bool muzzleUpOnly,bool* rectified)noexcept {
    if(rectified)*rectified=false;
    auto rotation=NativeRecoilRotation(camera);
    if(rotation&&muzzleUpOnly)rotation=NativeRecoilMuzzleUp(*rotation,rectified);
    if(!rotation||!interaction::InverseAnimatedTransform(firingWrist))return {};
    auto basis=aimBasis;basis.values[3]={0,0,0,1};
    const auto inverse=interaction::InverseAnimatedTransform(basis);if(!inverse)return {};
    auto delta=interaction::Multiply(interaction::Multiply(*inverse,*rotation),basis);
    // p' = (p - grip) * R + grip. Preserve the exact resolved pivot rather than
    // revolving the weapon/wrist about the eye or the model's root attachment.
    for(unsigned column=0;column<3;++column){
        delta.values[3][column]=firingWrist.values[3][column];
        for(unsigned row=0;row<3;++row)delta.values[3][column]-=firingWrist.values[3][row]*delta.values[row][column];
    }
    if(!interaction::InverseAnimatedTransform(delta))return {};
    return delta;
}
bool DiscoverNativeRecoilObservation(std::span<const std::byte> bytes,const engine::PeImage& pe){
    if(pe.machine!=0x14c)return false;
    constexpr std::array<unsigned char,24> tick{0x83,0xec,0x2c,0x53,0x8b,0x5c,0x24,0x38,0x8b,0x03,0x8b,0x50,0x14,0x55,0x56,0x8b,0xe9,0x57,0x8b,0xcb,0xff,0xd2,0x84,0xc0};
    constexpr std::array<unsigned char,24> camera{0x55,0x8b,0xec,0x83,0xe4,0xf0,0x81,0xec,0x8c,0x00,0x00,0x00,0xd9,0x41,0x40,0x56,0x8b,0x75,0x08,0xd9,0x1e,0x0f,0x57,0xc0};
    constexpr std::array<unsigned char,28> resolver{0x56,0x8b,0xf1,0xf6,0x46,0xf4,0x01,0x74,0x08,0x8b,0x8e,0x2c,0x01,0x00,0x00,0xeb,0x06,0x8b,0x8e,0x28,0x01,0x00,0x00,0xe8,0x04,0xc7,0xe3,0xff};
    const auto exact=[&](unsigned rva,const auto& signature){
        unsigned matches=0;bool expected=false;
        for(const auto& s:pe.sections)if((s.flags&0x20000000)&&s.rawOffset<=bytes.size()&&s.rawSize<=bytes.size()-s.rawOffset&&signature.size()<=s.rawSize)
            for(std::size_t n=0;n+signature.size()<=s.rawSize;++n)
                if(!std::memcmp(bytes.data()+s.rawOffset+n,signature.data(),signature.size())){++matches;expected|=std::uint64_t(s.rva)+n==rva;}
        return expected&&matches==1;
    };
    return exact(NativeRecoilBinding::tickRva,tick)&&exact(NativeRecoilBinding::cameraRva,camera)&&
        exact(NativeRecoilBinding::resolverRva,resolver);
}
} // namespace fvr::bc2
