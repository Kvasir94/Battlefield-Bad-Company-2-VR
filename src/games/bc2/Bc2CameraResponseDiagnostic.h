#pragma once
#include "Bc2PhysicalWeaponRuntime.h"
#include <array>
#include <mutex>
#include <vector>
namespace fvr::bc2 {
// Read-only render-camera observations. A rotation difference is a camera
// residual, never an automatically attributed recoil signal.
struct CameraResponseSample {
    std::uint32_t world=0,request=0,frame=0;
    std::uint64_t tracking=0,space=0;
    std::int64_t beforeQpc=0,afterQpc=0;
    bool adjusted=false,rawValid=false,adjustedValid=false;
    std::array<float,9> rawRotation{},adjustedRotation{};
    std::optional<physicalWeaponRuntime::CameraShotObservation> before,after;
};
class CameraResponseDiagnostic {
public:
    static constexpr unsigned Capacity=4096;
    void Enable(bool value){enabled_=value;if(value)records_.resize(Capacity);}
    bool Enabled()const noexcept{return enabled_;}
    void Push(const CameraResponseSample& sample){
        if(!enabled_)return;std::lock_guard lock(mutex_);
        if(count_){const auto& last=records_[(next_+Capacity-1)%Capacity];
            if(last.world==sample.world&&last.request==sample.request&&last.frame==sample.frame){++duplicates_;return;}}
        records_[next_]=sample;next_=(next_+1)%Capacity;
        if(count_<Capacity)++count_;else ++overwrites_;
    }
    void Write(std::ostream& out,std::int64_t frequency){
        const auto previousPrecision=out.precision(9);
        std::vector<CameraResponseSample> records;std::uint64_t overwritten=0,duplicates=0;
        {std::lock_guard lock(mutex_);overwritten=overwrites_;duplicates=duplicates_;
            records.reserve(count_);const auto start=(next_+Capacity-count_)%Capacity;
            for(unsigned i=0;i<count_;++i)records.push_back(records_[(start+i)%Capacity]);}
        out<<"{\"requested\":"<<(enabled_?"true":"false")<<",\"capacity\":"<<Capacity
           <<",\"qpc_frequency\":"<<frequency<<",\"overwrites\":"<<overwritten<<",\"duplicates_skipped\":"<<duplicates
           <<",\"recoil_attribution\":false,\"native_recovery_boundary_verified\":false,\"render_owner_verified\":false,\"records\":[";
        const auto boolean=[&](bool v){out<<(v?"true":"false");};
        const auto rotation=[&](const std::array<float,9>& value,bool valid){if(!valid){out<<"null";return;}out<<'[';for(unsigned i=0;i<9;++i){if(i)out<<',';out<<value[i];}out<<']';};
        const auto owner=[&](const std::optional<physicalWeaponRuntime::CameraShotObservation>& value){
            if(!value){out<<"null";return;}const auto& v=*value;const auto& r=v.accepted;
            out<<"{\"actor\":"<<v.identity.actor<<",\"actor_generation\":"<<v.identity.actorGeneration
               <<",\"weapon\":"<<v.identity.weapon<<",\"weapon_generation\":"<<v.identity.weaponGeneration
               <<",\"soldier\":"<<v.actor.soldier<<",\"weak\":"<<v.actor.weak<<",\"client_weapon\":"<<v.clientWeapon
               <<",\"server_firing\":"<<v.serverFiring<<",\"accepted_server_shot\":";
            if(!r.sequence)out<<"null";
            else out<<"{\"sequence\":"<<r.sequence<<",\"qpc\":"<<r.qpc<<",\"input\":"<<r.input
                    <<",\"loaded_before\":"<<r.loadedBefore<<",\"loaded_after\":"<<r.loadedAfter<<",\"reserve_after\":"<<r.reserveAfter<<'}';
            out<<'}';
        };
        bool comma=false;for(const auto& r:records){if(comma)out<<',';comma=true;
            out<<"{\"world\":"<<r.world<<",\"request\":"<<r.request<<",\"frame\":"<<r.frame
               <<",\"tracking\":"<<r.tracking<<",\"space\":"<<r.space<<",\"before_qpc\":"<<r.beforeQpc<<",\"after_qpc\":"<<r.afterQpc
               <<",\"adjustment_succeeded\":";boolean(r.adjusted);
            out<<",\"cached_identity_stable\":";boolean(r.before&&r.after&&r.before->identity==r.after->identity&&r.before->actor==r.after->actor&&r.before->clientWeapon==r.after->clientWeapon&&r.before->serverFiring==r.after->serverFiring);
            out<<",\"raw_rotation\":";rotation(r.rawRotation,r.rawValid);out<<",\"adjusted_rotation\":";rotation(r.adjustedRotation,r.adjustedValid);
            out<<",\"cached_owned_before\":";owner(r.before);out<<",\"cached_owned_after\":";owner(r.after);out<<'}';
        }out<<"]}";out.precision(previousPrecision);
    }
private:
    bool enabled_=false;std::mutex mutex_;std::vector<CameraResponseSample> records_;
    unsigned count_=0,next_=0;std::uint64_t overwrites_=0,duplicates_=0;
};
}
