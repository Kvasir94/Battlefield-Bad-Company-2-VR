#pragma once
#include "Bc2EquipmentIdentity.h"
#include "Bc2ReloadState.h"
#include "fvr/interaction/TrackingMath.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace fvr::bc2 {
#if defined(_M_IX86)
// Verified x86 contracts, shared by the runtime detours and authored ABI check.
using NativeSwayTickFn=void(__thiscall*)(void*,float,void*);
using NativeSwayCameraFn=void(__thiscall*)(void*,void*);
#endif
// Native padded columns stay native here. Presentation conversion is explicit
// below and never grants firing-angle, spread or HMD authority.
struct NativeRecoilContext {
    ReloadStateOwner owner{};
    WeaponEquipmentIdentity equipment{};
    std::uint64_t input=0,rigEquipmentGeneration=0;
    std::int64_t deadlineQpc=0;
    bool operator==(const NativeRecoilContext&)const=default;
};
enum class NativeRecoilBranch:std::uint8_t {Unknown,FiringA,FiringB,Alternate};
struct NativeRecoilCopy {
    NativeRecoilBranch branch=NativeRecoilBranch::Unknown;
    unsigned firing=0,sway=0;
    bool cameraEnabled=false;
    bool operator==(const NativeRecoilCopy&)const=default;
};
struct NativeRecoilBinding {
    static constexpr unsigned tickRva=0x6ec560-0x400000;
    static constexpr unsigned cameraRva=0x6d5f90-0x400000;
    static constexpr unsigned resolverRva=0x892240-0x400000;
    static constexpr unsigned swayVtableRva=0x1425af8-0x400000;
};
bool DiscoverNativeRecoilObservation(std::span<const std::byte>,const engine::PeImage&);

struct NativeRecoilMemory {
    void* context=nullptr;
    bool (*read)(void*,unsigned,void*,std::size_t)=nullptr;
    // Must verify the exact current local on-foot selected weapon/equipment.
    bool (*current)(void*,const NativeRecoilContext&)=nullptr;
};
enum class NativeRecoilReadStatus:std::uint8_t {
    Observed,InvalidContext,OwnerMismatch,ReadFailure,UnsupportedAlternate,
    InvalidFiring,InvalidSway,ChangedDuringRead,InvalidState
};
struct NativeRecoilSample {
    NativeRecoilContext context{};
    NativeRecoilCopy copy{};
    std::array<float,16> camera{};
    std::array<float,2> rawRadians{};
    std::optional<float> authoredMeanDegrees;
};
// Immutable latest completed native tick. Deadlines retain the original tick
// lifetime; copying into another tracked input must never renew it. The native
// camera response is absolute, so each fresh rig solve applies it once to its
// un-recoiled baseline, including repeated native ticks/render publications.
struct NativeRecoilPoseSample {
    NativeRecoilSample native{};
    std::uint64_t serial=0;
    std::int64_t tickQpc=0,deadlineQpc=0;
};
bool NativeRecoilPoseCurrent(const NativeRecoilPoseSample&,unsigned soldier,unsigned weak,unsigned weapon,
    std::uint64_t ownerGeneration,std::uint64_t rigEquipmentGeneration,const WeaponEquipmentIdentity&,
    std::uint64_t space,std::int64_t nowQpc)noexcept;
// Native column storage is already transposed for adapter row vectors. Convert
// coordinates with D=diag(1,1,-1), then the opposite camera/aim headings with
// H=diag(-1,1,-1): H*(D*M*D)*H. No extra transpose, translation or SIMD padding.
std::optional<math::Matrix4> NativeRecoilRotation(const std::array<float,16>&)noexcept;
// Aim-local rotation (+Z bore, +Y up) with any muzzle-DOWN component removed.
// Native hip recoil is a damped spring (0x6cc760) chasing the signed pitch
// target; on recovery it overshoots past zero, and wide authored heading
// ranges can also produce a positive (downward) raw pitch. The held VR gun
// keeps the native yaw/heading and the upward kick, never a dive below the
// un-recoiled bore. Returns the input unchanged when the bore is not below
// the baseline, otherwise the pure heading rotation of the same bore.
std::optional<math::Matrix4> NativeRecoilMuzzleUp(const math::Matrix4& aimLocalRotation,bool* rectified=nullptr)noexcept;
// Transfer the absolute camera-local response through the tracked aim basis
// about the resolved firing wrist. This delta only feeds the private palette.
// muzzleUpOnly applies NativeRecoilMuzzleUp to the converted response.
std::optional<math::Matrix4> NativeRecoilGripDelta(const std::array<float,16>& camera,
    const math::Matrix4& aimBasis,const math::Matrix4& firingWrist,bool muzzleUpOnly=false,bool* rectified=nullptr)noexcept;
// Rotation-only bore frame (+Z from the weapon root to its muzzle, +Y the
// reference up made orthogonal). The rig feeds the drawn jntWpn_Flash bone and
// the controller aim's up, so the visible barrel, not the XR aim pose or an
// unverified model axis, defines recoil pitch/kick axes. Rejects a muzzle
// closer than minimumDistance (Flash at root) or a barrel parallel to up.
std::optional<math::Matrix4> MuzzleBoreFrame(const math::Matrix4& weaponWorld,const math::Matrix4& muzzleWorld,
    const math::Matrix4& upReference,float minimumDistance)noexcept;
// KF2's cosmetic translation envelope, driven by confirmed native shots and
// authored native mean strength in DEGREES. It does not alter native rotation,
// spread or recovery. Its lifetime is independent of the camera tick snapshot.
struct NativeRecoilKickSample {
    NativeRecoilContext context{};
    std::uint64_t sequence=0;std::int64_t shotQpc=0;
    float fromMeters=0,peakMeters=0,recoverySeconds=0;
};
std::optional<float> NativeRecoilStrengthWeight(float authoredMeanDegrees)noexcept;
std::optional<float> EvaluateNativeRecoilKick(const NativeRecoilKickSample&,std::int64_t nowQpc,std::int64_t qpcFrequency)noexcept;
std::int64_t NativeRecoilKickDeadline(const NativeRecoilKickSample&,std::int64_t qpcFrequency)noexcept;
bool NativeRecoilKickCurrent(const NativeRecoilKickSample&,unsigned soldier,unsigned weak,unsigned weapon,
    std::uint64_t ownerGeneration,std::uint64_t rigEquipmentGeneration,const WeaponEquipmentIdentity&,
    std::uint64_t space,std::int64_t nowQpc,std::int64_t qpcFrequency)noexcept;
std::optional<math::Matrix4> NativeRecoilKickDelta(const math::Matrix4& aimBasis,float meters,float unitsPerMeter)noexcept;
class NativeRecoilKickEnvelope {
    std::optional<NativeRecoilKickSample> sample_;
public:
    void Reset()noexcept{sample_.reset();}
    std::optional<NativeRecoilKickSample> Sample()const noexcept{return sample_;}
    bool Shot(const NativeRecoilContext&,std::uint64_t sequence,std::int64_t shotQpc,
        float authoredMeanDegrees,bool braced,std::int64_t qpcFrequency)noexcept;
};
struct NativeRecoilReadResult {
    NativeRecoilReadStatus status=NativeRecoilReadStatus::InvalidContext;
    NativeRecoilCopy copy{};
    std::optional<NativeRecoilSample> sample;
};
// Caller first validates the three code bindings and existing reload binding.
// No native function calls, writes, integration or total-angle reads.
inline NativeRecoilReadResult ReadNativeRecoilObservation(const NativeRecoilMemory& m,
    const ReloadStateBinding& firingBinding,unsigned image,const NativeRecoilContext& context)noexcept {
    NativeRecoilReadResult out;
    const auto& o=context.owner;
    if(!m.read||!m.current||!o.player||!o.soldier||!o.weak||!o.weapon||!o.actorGeneration||
       !o.equipGeneration||!o.space||!context.input||!context.rigEquipmentGeneration||context.deadlineQpc<=0||
       context.equipment.weapon!=o.weapon||!firingBinding.firingVtableRva)return out;
    const auto read=[&](std::uint64_t at,auto& value){return at>=0x10000&&at+sizeof(value)<=UINT32_MAX&&
        m.read(m.context,unsigned(at),&value,sizeof(value));};
    if(!m.current(m.context,context)){out.status=NativeRecoilReadStatus::OwnerMismatch;return out;}
    const auto select=[&](NativeRecoilCopy& copy)->NativeRecoilReadStatus {
        std::uint8_t flags=0,camera=0;
        if(!read(std::uint64_t(o.soldier)+0x114,flags)||!read(std::uint64_t(o.soldier)+0x475,camera))
            return NativeRecoilReadStatus::ReadFailure;
        copy.cameraEnabled=bool(camera&0x10);
        if(!(flags&1)){copy.branch=NativeRecoilBranch::Alternate;return NativeRecoilReadStatus::UnsupportedAlternate;}
        copy.branch=(flags&0x10)?NativeRecoilBranch::FiringB:NativeRecoilBranch::FiringA;
        unsigned firingVtable=0,swayVtable=0,tick=0,getter=0;
        if(!read(std::uint64_t(o.weapon)+(copy.branch==NativeRecoilBranch::FiringA?0x3c:0x40),copy.firing)||
           !read(copy.firing,firingVtable))return NativeRecoilReadStatus::ReadFailure;
        if(std::uint64_t(image)+firingBinding.firingVtableRva!=firingVtable)return NativeRecoilReadStatus::InvalidFiring;
        if(!read(std::uint64_t(copy.firing)+0x14,copy.sway)||!read(copy.sway,swayVtable)||
           !read(std::uint64_t(swayVtable)+0x5c,tick)||!read(std::uint64_t(swayVtable)+0x68,getter))
            return NativeRecoilReadStatus::ReadFailure;
        if(std::uint64_t(image)+NativeRecoilBinding::swayVtableRva!=swayVtable||
           std::uint64_t(image)+NativeRecoilBinding::tickRva!=tick||
           std::uint64_t(image)+NativeRecoilBinding::cameraRva!=getter)return NativeRecoilReadStatus::InvalidSway;
        return NativeRecoilReadStatus::Observed;
    };
    out.status=select(out.copy);if(out.status!=NativeRecoilReadStatus::Observed)return out;
    NativeRecoilSample s;s.context=context;s.copy=out.copy;
    if(!read(std::uint64_t(s.copy.sway)+0x40,s.camera)||!read(std::uint64_t(s.copy.sway)+0x2c,s.rawRadians)){
        out.status=NativeRecoilReadStatus::ReadFailure;return out;
    }
    // Padding is not a native matrix component. Never serialize or classify it.
    for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component)
        if(!std::isfinite(s.camera[4*axis+component])){out.status=NativeRecoilReadStatus::InvalidState;return out;}
    for(float value:s.rawRadians)if(!std::isfinite(value)){out.status=NativeRecoilReadStatus::InvalidState;return out;}
    NativeRecoilCopy again;
    if(select(again)!=NativeRecoilReadStatus::Observed||again!=s.copy||!m.current(m.context,context)){
        out.status=NativeRecoilReadStatus::ChangedDuringRead;return out;
    }
    out.sample=s;return out;
}

enum class NativeRecoilSeam:std::uint8_t {
    PostTick,BeforePublication,CameraRead,BeforeVrBaseline,AfterVrBaseline,
    ClientShotBase,ServerShotBase
};
// Boundary payloads retain their own input/pose identity. The context attached
// to a native callback only labels the last publication; it does not prove
// which tracked input the native simulation consumed.
struct NativeRecoilBoundary {
    std::uint64_t input=0,rigGeneration=0;
    std::int64_t inputDeadlineQpc=0,rigDeadlineQpc=0;
    std::array<float,2> requestedAim{},aimerAim{},simulationAim{};
    bool anglesValid=false;
    unsigned shotToken=0,shotSway=0;bool shotTokenValid=false,shotSwayValid=false;
    // Composition output before the origin override. Spread is applied later
    // by native ClientShoot/ServerShoot: this is NOT the final projectile basis.
    std::array<float,16> nativeShotBase{},trackedWeapon{};
    bool shotBaseValid=false,trackedWeaponValid=false;
};
struct NativeRecoilObservation {
    NativeRecoilSeam seam=NativeRecoilSeam::BeforePublication;
    NativeRecoilReadStatus status=NativeRecoilReadStatus::InvalidContext;
    NativeRecoilContext context{};NativeRecoilCopy copy{};
    std::int64_t qpc=0,tickQpc=0;
    std::uint64_t serial=0,tickInputContext=0;
    unsigned thread=0;float dt=0;bool dtValid=false;
    bool matchedTick=false,cameraMatrixMatchesTick=false;
    bool shotUsesSelectedSway=false;
    NativeRecoilBoundary boundary{};
    std::optional<NativeRecoilSample> sample;
};
// Synchronization belongs to the caller. Every post-original native call gets
// its own serial; repeated calls in one input are NEVER suppressed. All matrices
// are absolute snapshots, never summed. Only original tick time is retained.
template<std::size_t Capacity=4096> class NativeRecoilObservationHistory {
    static_assert(Capacity>0);
    std::array<NativeRecoilObservation,Capacity> rows_{};
    std::size_t count_=0,next_=0;std::uint64_t total_=0,ticks_=0;
    std::optional<NativeRecoilObservation> latest_;
    static bool SameOwner(const NativeRecoilContext& a,const NativeRecoilContext& b)noexcept {
        return a.owner==b.owner&&a.equipment==b.equipment&&a.rigEquipmentGeneration==b.rigEquipmentGeneration;
    }
public:
    static constexpr bool transferImplemented=false,sameFrameAuthority=false;
    void Invalidate()noexcept{latest_.reset();}
    void Record(NativeRecoilObservation row,std::int64_t maximumAgeQpc)noexcept {
        row.shotUsesSelectedSway=row.boundary.shotSwayValid&&row.sample&&
            row.status==NativeRecoilReadStatus::Observed&&row.boundary.shotSway==row.copy.sway;
        if(row.seam==NativeRecoilSeam::PostTick){
            row.serial=++ticks_;row.tickQpc=row.qpc;row.tickInputContext=row.context.input;
            // Failure on a current selected native tick invalidates an old sample.
            if(row.sample&&row.status==NativeRecoilReadStatus::Observed){
                if(!latest_||row.qpc>=latest_->qpc)latest_=row;
            }else latest_.reset();
        }else if(row.status==NativeRecoilReadStatus::Observed&&row.sample&&latest_&&latest_->sample&&SameOwner(row.context,latest_->context)&&
                 row.copy==latest_->copy&&row.qpc>=latest_->qpc&&maximumAgeQpc>0&&row.qpc-latest_->qpc<=maximumAgeQpc){
            row.matchedTick=true;row.serial=latest_->serial;row.tickQpc=latest_->qpc;
            row.tickInputContext=latest_->context.input;
            row.cameraMatrixMatchesTick=true;
            for(unsigned axis=0;axis<4;++axis)for(unsigned component=0;component<3;++component)
                if(row.sample->camera[4*axis+component]!=latest_->sample->camera[4*axis+component])row.cameraMatrixMatchesTick=false;
        }else if(row.seam==NativeRecoilSeam::BeforePublication){
            // No later publication may resurrect a sample from another item/space.
            latest_.reset();
        }
        rows_[next_]=row;next_=(next_+1)%Capacity;if(count_<Capacity)++count_;++total_;
    }
    std::size_t Count()const noexcept{return count_;}
    std::uint64_t Total()const noexcept{return total_;}
    std::uint64_t Overwritten()const noexcept{return total_-count_;}
    std::uint64_t Ticks()const noexcept{return ticks_;}
    template<class F>void ForEach(F&& f)const {
        for(std::size_t n=0;n<count_;++n)f(rows_[(next_+Capacity-count_+n)%Capacity]);
    }
};
} // namespace fvr::bc2
