#pragma once
// First-person weapon effect anchoring and weapon/effect/projectile basis
// telemetry (docs/WEAPON-EFFECTS-AND-LAUNCHER-BASIS-20261009.md).
//
// Portable core: no Windows, native memory or engine calls. The DLL glue in
// Bc2Gameplay.cpp supplies native transforms read from caller-owned buffers
// and performs the one verified native call (the engine's own effect
// transform setter) through the Set callback.
#include "fvr/interaction/TrackingMath.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <mutex>
#include <optional>
#include <ostream>
#include <string_view>
#include <vector>
namespace fvr::bc2 {

// FVR_BC2_EFFECT_ANCHOR=0 keeps native fire-and-forget spawns (observation
// only). FVR_BC2_EFFECT_ANCHOR_MS=N (0..3000) is how long a spawned 1P weapon
// effect follows the weapon root after its spawn.
// FVR_BC2_EFFECT_WRITTEN_ROOT=0 restores the older rule that both the FOV
// bypass and re-anchoring need a current published shot frame; by default a
// getter root written by the VR rig in this same update is enough (guns with no
// jntWpn_Flash shot frame otherwise kept the warped, world-fixed native spawn).
struct EffectAnchorConfig {bool enabled=true;std::int64_t windowNs=400000000;bool writtenRoot=true;};
inline EffectAnchorConfig ParseEffectAnchorConfig(const char* enabled,const char* windowMs)noexcept {
    EffectAnchorConfig config;
    if(enabled&&enabled[0]=='0'&&enabled[1]==0)config.enabled=false;
    if(windowMs&&*windowMs){
        unsigned ms=0;const auto end=windowMs+std::strlen(windowMs);
        const auto parsed=std::from_chars(windowMs,end,ms);
        if(parsed.ec==std::errc{}&&parsed.ptr==end&&ms<=3000)config.windowNs=std::int64_t(ms)*1000000;
    }
    return config;
}

// Native LinearTransform: four rows of x,y,z plus unspecified SIMD padding.
inline math::Matrix4 NativeAffine(const float* raw)noexcept {
    math::Matrix4 m{};
    for(unsigned r=0;r<4;++r){for(unsigned c=0;c<3;++c)m.values[r][c]=raw[r*4+c];m.values[r][3]=r==3?1.f:0.f;}
    return m;
}
inline bool FiniteAffine(const math::Matrix4& m)noexcept {
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)if(!std::isfinite(m.values[r][c]))return false;
    return true;
}

struct EffectAnchorOwner {
    std::uint32_t soldier=0,weak=0,weapon=0;
    bool operator==(const EffectAnchorOwner&)const=default;
};

// Each 1P FireEffects entry is spawned once at world = local * root, where
// root is the weapon-getter output of that animation update (the VR-placed
// weapon root when RetargetWeapon wrote it). The engine discards the handle.
// This tracker keeps the handle and the exact native relation, local =
// spawn * inverse(root), and re-applies it to each later root inside the
// window. No offsets are authored here: the relation is the engine's own.
class WeaponEffectAnchor {
public:
    static constexpr std::size_t Capacity=16;
    enum class Track:std::uint8_t {Stored,InvalidOwner,InvalidHandle,InvalidRoot,InvalidSpawn};
    Track Add(const EffectAnchorOwner& owner,std::uint32_t manager,std::uint32_t handle,
              const math::Matrix4& root,const math::Matrix4& spawn,std::int64_t nowNs)noexcept {
        if(!owner.soldier||!owner.weapon)return Track::InvalidOwner;
        if(!manager||!handle)return Track::InvalidHandle;
        if(!FiniteAffine(root))return Track::InvalidRoot;
        const auto inverse=interaction::InverseAnimatedTransform(root);if(!inverse)return Track::InvalidRoot;
        if(!FiniteAffine(spawn)||!interaction::InverseAnimatedTransform(spawn))return Track::InvalidSpawn;
        if(!(owner==owner_)){Clear();owner_=owner;}
        Entry* slot=nullptr;
        for(auto& e:entries_)if(e.live&&e.manager==manager&&e.handle==handle){slot=&e;break;}
        if(!slot)for(auto& e:entries_)if(!e.live){slot=&e;break;}
        if(!slot){
            slot=&*std::min_element(entries_.begin(),entries_.end(),[](const Entry& a,const Entry& b){return a.spawnNs<b.spawnNs;});
            ++evicted_;
        }
        *slot=Entry{manager,handle,interaction::Multiply(spawn,*inverse),nowNs,true};
        ++stored_;return Track::Stored;
    }
    // Calls set(manager,handle,world) for each live entry of this owner whose
    // age is inside the window. Another owner clears everything first.
    template<class Set>unsigned Reanchor(const EffectAnchorOwner& owner,const math::Matrix4& root,
                                         std::int64_t nowNs,std::int64_t windowNs,Set&& set)noexcept {
        if(!(owner==owner_)){Clear();owner_=owner;return 0;}
        if(!FiniteAffine(root)||!interaction::InverseAnimatedTransform(root))return 0;
        unsigned applied=0;
        for(auto& e:entries_){
            if(!e.live)continue;
            if(nowNs<e.spawnNs||nowNs-e.spawnNs>=windowNs){e.live=false;++expired_;continue;}
            set(e.manager,e.handle,interaction::Multiply(e.local,root));++applied;
        }
        applied_+=applied;return applied;
    }
    void Clear()noexcept {for(auto& e:entries_)e.live=false;}
    unsigned Live()const noexcept {unsigned n=0;for(const auto& e:entries_)n+=e.live?1:0;return n;}
    std::uint64_t Stored()const noexcept {return stored_;}
    std::uint64_t Applied()const noexcept {return applied_;}
    std::uint64_t Expired()const noexcept {return expired_;}
    std::uint64_t Evicted()const noexcept {return evicted_;}
private:
    struct Entry {std::uint32_t manager=0,handle=0;math::Matrix4 local{};std::int64_t spawnNs=0;bool live=false;};
    std::array<Entry,Capacity> entries_{};EffectAnchorOwner owner_{};
    std::uint64_t stored_=0,applied_=0,expired_=0,evicted_=0;
};

// Angle between two bases' forward axes and the signed roll of their up axes
// about the first forward. Axes are local vectors transformed by each matrix's
// rotation rows (row-vector convention used by every rig/aim matrix here).
struct BasisDelta {bool valid=false;float forwardDegrees=0,rollDegrees=0;};
inline BasisDelta MeasureBasisDelta(const math::Matrix4& a,math::Vec3 aForward,math::Vec3 aUp,
                                    const math::Matrix4& b,math::Vec3 bForward,math::Vec3 bUp)noexcept {
    const auto rotate=[](math::Vec3 v,const math::Matrix4& m){
        return math::Vec3{v.x*m.values[0][0]+v.y*m.values[1][0]+v.z*m.values[2][0],
                          v.x*m.values[0][1]+v.y*m.values[1][1]+v.z*m.values[2][1],
                          v.x*m.values[0][2]+v.y*m.values[1][2]+v.z*m.values[2][2]};};
    const auto dot=[](math::Vec3 p,math::Vec3 q){return p.x*q.x+p.y*q.y+p.z*q.z;};
    const auto norm=[&](math::Vec3 v)->std::optional<math::Vec3>{const float l=std::sqrt(dot(v,v));
        if(!std::isfinite(l)||l<1e-4f)return {};return math::Vec3{v.x/l,v.y/l,v.z/l};};
    const auto f=norm(rotate(aForward,a)),u=norm(rotate(aUp,a)),g=norm(rotate(bForward,b)),w=norm(rotate(bUp,b));
    if(!f||!u||!g||!w)return {};
    constexpr float degrees=57.29577951308232f;
    BasisDelta out;out.valid=true;
    out.forwardDegrees=std::acos(std::clamp(dot(*f,*g),-1.f,1.f))*degrees;
    const auto project=[&](math::Vec3 v){const float d=dot(v,*f);return math::Vec3{v.x-d*f->x,v.y-d*f->y,v.z-d*f->z};};
    const auto pu=project(*u),pw=project(*w);
    const math::Vec3 cross{pw.y*pu.z-pw.z*pu.y,pw.z*pu.x-pw.x*pu.z,pw.x*pu.y-pw.y*pu.x};
    if(dot(pu,pu)<1e-8f||dot(pw,pw)<1e-8f){out.rollDegrees=0;return out;}
    out.rollDegrees=std::atan2(dot(cross,*f),dot(pw,pu))*degrees; // + rotates b's up onto a's up about a's forward
    return out;
}

enum class WeaponOrientationSource:std::uint8_t {None,ProfileAxes,AuthoredAxes,NativeAttachment,ControllerGrip};
inline std::string_view WeaponOrientationSourceName(WeaponOrientationSource s)noexcept {
    switch(s){case WeaponOrientationSource::ProfileAxes:return "profile_axes";case WeaponOrientationSource::AuthoredAxes:return "authored_axes";
        case WeaponOrientationSource::NativeAttachment:return "native_attachment";case WeaponOrientationSource::ControllerGrip:return "controller_grip";default:return "none";}
}
enum class WeaponEffectRecordKind:std::uint8_t {Admission,Placement,Spawn,Shot};
namespace weapon_effect_flag {
// Admission
inline constexpr std::uint32_t CandidateProfile=1,NativeModeMap=2,VisualMeshes=4,VisualFamily=8,Admitted=16,SelectedSecondary=32;
// Placement
inline constexpr std::uint32_t AimValid=1,AttachmentPending=2,RightTracked=4,ProfileAdmitted=8;
// Spawn
inline constexpr std::uint32_t RootWritten=1,AnchorEnabled=2,Tracked=4;
// Shot
inline constexpr std::uint32_t ClientPath=1,OriginWritten=2,ShotFrame=4,EventMuzzle=8,SupportedMuzzle=16;
}
// One fixed-size row. Matrices are stored exactly as the producer names them:
//   admission: none
//   placement: m0 native weapon root, m1 placed weapon root, m2 aim frame,
//              m3 body/eye base (all canonical +Z-forward rig space)
//   spawn:     m0 effect root as read from the weapon-getter output, m1 spawn
//              transform passed to the engine (both native space)
//   shot:      m0 native final shot composition, m1 after optional origin write
//              (native space), m2 placed weapon root (canonical) when a same-pose
//              shot frame exists, m3 event muzzle (canonical) when matched
struct WeaponEffectRecord {
    WeaponEffectRecordKind kind=WeaponEffectRecordKind::Admission;
    WeaponOrientationSource source=WeaponOrientationSource::None;
    std::uint8_t matrices=0;
    std::uint64_t tick=0,generation=0;std::int64_t qpcNs=0;
    std::uint32_t weapon=0,flags=0,a=0,b=0;
    std::array<char,24> asset{};
    std::array<math::Matrix4,4> m{};
    BasisDelta delta{};
};
inline void SetRecordAsset(WeaponEffectRecord& r,std::string_view text)noexcept {
    std::size_t n=0;for(char c:text){if(n+1>=r.asset.size())break;if(static_cast<unsigned char>(c)>=32&&static_cast<unsigned char>(c)<127&&c!='"'&&c!='\\')r.asset[n++]=c;}
    for(;n<r.asset.size();++n)r.asset[n]=0;
}

// Bounded store drained by the once-per-second session telemetry writer.
// Producers run inside native callbacks: they never wait (try-lock; a busy
// store counts the row as dropped). Rows exist only on selection, launcher
// admission changes, effect spawns and shots, so the per-second cost is a few
// fixed copies. Formatting happens on the telemetry thread.
class WeaponEffectTelemetry {
public:
    static constexpr std::size_t Capacity=128;
    bool Push(const WeaponEffectRecord& r)noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()){busy_.fetch_add(1,std::memory_order_relaxed);return false;}
        if(count_==Capacity){head_=(head_+1)%Capacity;--count_;overwritten_.fetch_add(1,std::memory_order_relaxed);}
        ring_[(head_+count_)%Capacity]=r;++count_;pushed_.fetch_add(1,std::memory_order_relaxed);return true;
    }
    struct Counters {
        std::atomic<std::uint64_t> spawnsSeen{0},spawnsTracked{0},spawnsNativeRoot{0},spawnsNativeRootCorrected{0},spawnsRejected{0},reanchorCalls{0},
            reanchorFrames{0},reanchorSkippedUnwritten{0},weaponOutputsWritten{0},weaponOutputsNative{0},fovCompensationBypassed{0},fovBypassWrittenRoot{0},reanchorWrittenRoot{0},
            launcherAdmittedFrames{0},launcherRejectedFrames{0},launcherAdmissionChanges{0},shots{0};
    };
    Counters& Count()noexcept {return counters_;}
    std::uint64_t Pushed()const noexcept {return pushed_.load(std::memory_order_relaxed);}
    std::uint64_t Busy()const noexcept {return busy_.load(std::memory_order_relaxed);}
    std::uint64_t Overwritten()const noexcept {return overwritten_.load(std::memory_order_relaxed);}
    // Removes at most maxRecords rows (oldest first) under the lock.
    std::vector<WeaponEffectRecord> Take(std::size_t maxRecords)noexcept {
        std::vector<WeaponEffectRecord> out;
        try{out.reserve(maxRecords);}catch(...){return out;}
        std::lock_guard lock(mutex_);
        while(count_&&out.size()<maxRecords){out.push_back(ring_[head_]);head_=(head_+1)%Capacity;--count_;}
        return out;
    }
    std::size_t Pending()noexcept {std::lock_guard lock(mutex_);return count_;}
    // One JSON object: counters plus up to maxRecords drained rows.
    void Write(std::ostream& out,const EffectAnchorConfig& config,bool installed,std::size_t maxRecords=32){
        const auto rows=Take(maxRecords);
        const auto c=[&](const std::atomic<std::uint64_t>& v){return v.load(std::memory_order_relaxed);};
        out<<"{\"anchor_installed\":"<<(installed?"true":"false")<<",\"anchor_enabled\":"<<(config.enabled?"true":"false")
           <<",\"anchor_window_ms\":"<<config.windowNs/1000000
           <<",\"spawns_seen\":"<<c(counters_.spawnsSeen)<<",\"spawns_tracked\":"<<c(counters_.spawnsTracked)
           <<",\"spawns_native_root\":"<<c(counters_.spawnsNativeRoot)<<",\"spawns_native_root_corrected\":"<<c(counters_.spawnsNativeRootCorrected)
           <<",\"spawns_rejected\":"<<c(counters_.spawnsRejected)
           <<",\"fov_compensation_bypassed\":"<<c(counters_.fovCompensationBypassed)<<",\"fov_bypass_written_root\":"<<c(counters_.fovBypassWrittenRoot)
           <<",\"written_root_fallback\":"<<(config.writtenRoot?"true":"false")<<",\"reanchor_written_root\":"<<c(counters_.reanchorWrittenRoot)
           <<",\"reanchor_calls\":"<<c(counters_.reanchorCalls)<<",\"reanchor_frames\":"<<c(counters_.reanchorFrames)
           <<",\"reanchor_skipped_unwritten\":"<<c(counters_.reanchorSkippedUnwritten)
           <<",\"weapon_outputs_written\":"<<c(counters_.weaponOutputsWritten)<<",\"weapon_outputs_native\":"<<c(counters_.weaponOutputsNative)
           <<",\"launcher_admitted_frames\":"<<c(counters_.launcherAdmittedFrames)<<",\"launcher_rejected_frames\":"<<c(counters_.launcherRejectedFrames)
           <<",\"launcher_admission_changes\":"<<c(counters_.launcherAdmissionChanges)<<",\"shots\":"<<c(counters_.shots)
           <<",\"rows_pushed\":"<<Pushed()<<",\"rows_busy\":"<<Busy()<<",\"rows_overwritten\":"<<Overwritten()<<",\"rows\":[";
        bool first=true;
        for(const auto& r:rows){if(!first)out<<',';first=false;WriteRecord(out,r);}
        out<<"]}";
    }
    static void WriteRecord(std::ostream& out,const WeaponEffectRecord& r){
        static constexpr const char* kinds[]={"admission","placement","spawn","shot"};
        const auto flags=std::ios::fmtflags(out.flags());const auto precision=out.precision();
        out<<"{\"kind\":\""<<kinds[unsigned(r.kind)&3]<<"\",\"tick\":"<<r.tick<<",\"qpc_ns\":"<<r.qpcNs
           <<",\"weapon\":\"0x"<<std::hex<<r.weapon<<std::dec<<"\",\"asset\":\""<<std::string_view(r.asset.data(),std::strlen(r.asset.data()))
           <<"\",\"flags\":"<<r.flags;
        switch(r.kind){
        case WeaponEffectRecordKind::Admission:
            out<<",\"candidate_profile\":"<<B(r.flags&weapon_effect_flag::CandidateProfile)<<",\"native_mode_map\":"<<B(r.flags&weapon_effect_flag::NativeModeMap)
               <<",\"visual_meshes\":"<<B(r.flags&weapon_effect_flag::VisualMeshes)<<",\"visual_family\":"<<B(r.flags&weapon_effect_flag::VisualFamily)
               <<",\"admitted\":"<<B(r.flags&weapon_effect_flag::Admitted)<<",\"selected_secondary\":"<<B(r.flags&weapon_effect_flag::SelectedSecondary)
               <<",\"rig_epoch\":"<<r.generation;break;
        case WeaponEffectRecordKind::Placement:
            out<<",\"source\":\""<<WeaponOrientationSourceName(r.source)<<"\",\"input_generation\":"<<r.generation
               <<",\"equipment_generation\":"<<r.a<<",\"aim_valid\":"<<B(r.flags&weapon_effect_flag::AimValid)
               <<",\"attachment_pending\":"<<B(r.flags&weapon_effect_flag::AttachmentPending)
               <<",\"profile_admitted\":"<<B(r.flags&weapon_effect_flag::ProfileAdmitted);break;
        case WeaponEffectRecordKind::Spawn:
            out<<",\"manager\":\"0x"<<std::hex<<r.a<<"\",\"handle\":\"0x"<<r.b<<std::dec<<"\",\"root_written\":"<<B(r.flags&weapon_effect_flag::RootWritten)
               <<",\"anchor_enabled\":"<<B(r.flags&weapon_effect_flag::AnchorEnabled)<<",\"tracked\":"<<B(r.flags&weapon_effect_flag::Tracked);break;
        case WeaponEffectRecordKind::Shot:
            out<<",\"client_path\":"<<B(r.flags&weapon_effect_flag::ClientPath)<<",\"origin_written\":"<<B(r.flags&weapon_effect_flag::OriginWritten)
               <<",\"shot_frame\":"<<B(r.flags&weapon_effect_flag::ShotFrame)<<",\"event_muzzle\":"<<B(r.flags&weapon_effect_flag::EventMuzzle)
               <<",\"supported_muzzle\":"<<B(r.flags&weapon_effect_flag::SupportedMuzzle)<<",\"shot_token\":"<<r.a<<",\"shot_generation\":"<<r.generation
               <<",\"source\":\""<<WeaponOrientationSourceName(r.source)<<'"';break;
        }
        if(r.delta.valid)out<<",\"forward_deg\":"<<std::setprecision(4)<<r.delta.forwardDegrees<<",\"roll_deg\":"<<r.delta.rollDegrees;
        static constexpr const char* names[4][4]={{"m0","m1","m2","m3"},{"native_weapon","placed_weapon","aim","body"},
            {"effect_root","spawn","m2","m3"},{"native_shot","shot_after","placed_weapon","event_muzzle"}};
        const unsigned set=unsigned(r.kind)==0?0:unsigned(r.kind);
        out<<std::setprecision(6);
        for(unsigned n=0;n<4;++n)if(r.matrices&(1u<<n)){
            out<<",\""<<names[set][n]<<"\":[";
            for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col){if(row||col)out<<',';out<<r.m[n].values[row][col];}
            out<<']';
        }
        out<<'}';out.flags(flags);out.precision(precision);
    }
private:
    static const char* B(std::uint32_t v)noexcept {return v?"true":"false";}
    std::mutex mutex_;std::array<WeaponEffectRecord,Capacity> ring_{};std::size_t head_=0,count_=0;
    std::atomic<std::uint64_t> pushed_{0},busy_{0},overwritten_{0};
    Counters counters_;
};
// The DLL's single store (function-local static in an inline function: one
// instance per module).
inline WeaponEffectTelemetry& WeaponEffectEvidence()noexcept {static WeaponEffectTelemetry store;return store;}
}
