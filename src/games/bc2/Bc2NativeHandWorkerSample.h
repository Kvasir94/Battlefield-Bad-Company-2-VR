#pragma once
#include "Bc2RigWorkerBinding.h"
#include "Bc2ReloadDrawGeometry.h"
#include <atomic>
#include <cmath>
#include <mutex>
#include <ostream>
namespace fvr::bc2 {
// Independent read-only diagnostic. A sample is NEVER a producer or draw key.
// Missing copied lease context remains unknown; nativeFrame is never relabelled.
class Bc2NativeHandWorkerSample {
public:
    static constexpr unsigned Slots=32;
    enum class LeaseRelation:unsigned {Unavailable,ContextMismatch,SameFrame,LeaseOlder,LeaseNewer};
    enum class LeaseViewClass:unsigned {Unavailable,ContextMismatch,Eye0,Eye1};
    struct Record {
        std::uint32_t nativeFrame=0,leaseFrame=0;
        LeaseRelation leaseRelation=LeaseRelation::Unavailable;
        LeaseViewClass leaseViewClass=LeaseViewClass::Unavailable;
        bool leaseFresh=false,afterExact=false,paired=false;
        unsigned packCalls=0,calls147=0,validatedPacks=0;
        std::uint64_t workerIdentityHash=0,packedHash=0,inputGeneration=0,equipmentGeneration=0;
        std::int64_t beginNs=0,endNs=0;
    };
    void Enable(bool value)noexcept{enabled_.store(value,std::memory_order_release);}
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    class Scope {
    public:
        // Every callback, including a rejected nested callback, masks its parent.
        explicit Scope(Bc2NativeHandWorkerSample& recorder)noexcept:recorder_(recorder),previous_(current_){current_=this;}
        ~Scope(){if(started_&&!finished_)++recorder_.abandoned_;if(current_==this)current_=previous_;}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        void Begin(const RigWorkerIdentity& identity,RigWorkerStatus status,std::int64_t now,
            const std::optional<RigWorkerViewLease>& lease={})noexcept {
            if(started_||!recorder_.Enabled()||status!=RigWorkerStatus::NoExactView||now<=0)return;
            const auto& o=identity.owner;
            if(!o.actor||!o.weak||!o.weapon||!o.ownerGeneration||!o.space||!identity.world||!identity.request||!identity.view)return;
            started_=true;owner_=o;record_.nativeFrame=identity.nativeFrame;record_.beginNs=now;
            // Defined scalar tuple, no struct padding or native bytes. Allows a
            // local join to the worker report without storing object pointers.
            const std::array<std::uint64_t,10> tuple{o.actor,o.weak,o.weapon,o.ownerGeneration,o.space,
                identity.world,identity.request,identity.view,identity.nativeFrame,identity.call.workspace};
            record_.workerIdentityHash=ReloadDrawHash(std::as_bytes(std::span(tuple)));
            if(lease){const auto& a=lease->eyes[0];const auto& b=lease->eyes[1];
                record_.leaseFrame=a.nativeFrame;
                const bool context=a.world==identity.world&&b.world==a.world&&a.request==identity.request&&b.request==a.request&&
                    a.view&&b.view&&a.view!=b.view&&a.eye==0&&b.eye==1&&a.nativeFrame==b.nativeFrame&&
                    (identity.view==a.view||identity.view==b.view);
                // Copied view identity only, independent of frame/freshness.
                // Eye classification is diagnostic and grants no eye authority.
                record_.leaseViewClass=!context?LeaseViewClass::ContextMismatch:
                    identity.view==a.view?LeaseViewClass::Eye0:LeaseViewClass::Eye1;
                record_.leaseRelation=!context?LeaseRelation::ContextMismatch:a.nativeFrame==identity.nativeFrame?LeaseRelation::SameFrame:
                    a.nativeFrame<identity.nativeFrame?LeaseRelation::LeaseOlder:LeaseRelation::LeaseNewer;
                record_.leaseFresh=lease->observedNs>0&&lease->observedNs<=now&&now<lease->deadlineNs&&
                    lease->deadlineNs-lease->observedNs<=250000000ll;
            }
            ++recorder_.started_;
        }
        bool NeedsRevalidation()const noexcept{return started_&&!finished_;}
        void Complete(bool exact,std::int64_t end)noexcept {
            if(!started_||finished_)return;finished_=true;record_.endNs=end;
            record_.afterExact=exact&&recorder_.Enabled()&&current_==this&&end>=record_.beginNs&&
                end>=lastPackNs_&&end-record_.beginNs<=200000000ll;
            record_.paired=record_.afterExact&&!bad_&&!first_&&pairs_==1&&Source(source_,end);
            if(!record_.afterExact)++recorder_.postRejected_;
            else if(record_.paired)++recorder_.pairs_;
            else if(!record_.packCalls)++recorder_.noPack_;
            else ++recorder_.unpaired_;
            recorder_.Save(record_);
        }
        static Scope* Current()noexcept {auto* s=current_;return s&&s->started_&&!s->finished_&&s->recorder_.Enabled()?s:nullptr;}
        // Counts invocations only; unrelated/native-unmodified pack calls are not
        // inferred to belong to the selected rig and cannot complete a pair.
        void PackCalled(unsigned bones)noexcept{++record_.packCalls;if(bones==147)++record_.calls147;}
        void Observe(std::uint64_t serial,bool first,const ReloadPackedSource& source,
            std::uint64_t destination,std::int64_t now,std::span<const std::byte> input,
            std::span<const std::byte> packed,bool originalSourceUnchanged)noexcept {
            if(bad_||finished_)return;
            const auto reject=[&]{bad_=true;++recorder_.invalidPack_;};
            if(!originalSourceUnchanged||!serial||!destination||source.owner!=owner_||now<record_.beginNs||now<lastPackNs_||
                !Source(source,now)||!SamePacked(input,packed)){reject();return;}
            lastPackNs_=now;++record_.validatedPacks;const auto hash=ReloadDrawHash(packed);
            if(first){if(first_||pairs_){reject();return;}first_=true;serial_=serial;source_=source;destination_=destination;
                record_.packedHash=hash;record_.inputGeneration=source.inputGeneration;record_.equipmentGeneration=source.physicalEquipmentGeneration;
            }else{if(!first_||serial_!=serial||source_!=source||record_.packedHash!=hash||destination_==destination){reject();return;}
                first_=false;pairs_=1;}
        }
    private:
        static bool Source(const ReloadPackedSource& s,std::int64_t now)noexcept {
            return s.rigPose&&s.rigFingerprint==0xa7f219a1426216abull&&s.inputGeneration&&s.physicalEquipmentGeneration&&
                s.handsNamed&&s.boneCount==147&&s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&
                s.deadlineNs-s.observedNs<=250000000ll;
        }
        static bool SamePacked(std::span<const std::byte> input,std::span<const std::byte> packed)noexcept {
            if(input.size()!=147*64||packed.size()!=147*48)return false;
            for(unsigned b=0;b<147;++b)for(unsigned c=0;c<3;++c)for(unsigned r=0;r<4;++r){
                const auto* value=input.data()+b*64+r*16+c*4;float f=0;std::memcpy(&f,value,4);
                if(!std::isfinite(f)||std::memcmp(value,packed.data()+b*48+c*16+r*4,4))return false;
            }return true;
        }
        Bc2NativeHandWorkerSample& recorder_;Scope* previous_=nullptr;
        ReloadProducerOwner owner_{};ReloadPackedSource source_{};Record record_{};
        std::uint64_t serial_=0,destination_=0;std::int64_t lastPackNs_=0;
        bool started_=false,finished_=false,bad_=false,first_=false;unsigned pairs_=0;
    };
    struct Counters{std::uint64_t started=0,pairs=0,noPack=0,unpaired=0,postRejected=0,invalidPack=0,busy=0,evicted=0,abandoned=0;};
    Counters Statistics()const noexcept{return{started_.load(),pairs_.load(),noPack_.load(),unpaired_.load(),postRejected_.load(),invalidPack_.load(),busy_.load(),evicted_.load(),abandoned_.load()};}
    void Report(std::ostream& out){const auto c=Statistics();std::unique_lock lock(mutex_,std::try_to_lock);
        out<<"{\"enabled\":"<<(Enabled()?"true":"false")<<",\"started\":"<<c.started<<",\"pairs\":"<<c.pairs<<",\"no_pack\":"<<c.noPack
           <<",\"unpaired\":"<<c.unpaired<<",\"post_rejected\":"<<c.postRejected<<",\"invalid_pack\":"<<c.invalidPack<<",\"busy\":"<<c.busy
           <<",\"evicted\":"<<c.evicted<<",\"abandoned\":"<<c.abandoned<<",\"snapshot_complete\":"<<(lock?"true":"false")
           <<",\"lease_view_class_names\":[\"unavailable\",\"context_mismatch\",\"eye0\",\"eye1\"]"
           <<",\"lease_relation_names\":[\"unavailable\",\"context_mismatch\",\"same_frame\",\"lease_older\",\"lease_newer\"],\"records\":[";
        if(lock)for(unsigned n=0;n<size_;++n){if(n)out<<',';const auto& r=records_[(next_+Slots-size_+n)%Slots];
            out<<"{\"native_frame\":"<<r.nativeFrame<<",\"lease_frame\":"<<r.leaseFrame<<",\"lease_relation\":"<<unsigned(r.leaseRelation)
               <<",\"lease_view_class\":"<<unsigned(r.leaseViewClass)
               <<",\"worker_identity_hash\":"<<r.workerIdentityHash
               <<",\"lease_fresh\":"<<(r.leaseFresh?"true":"false")<<",\"after_exact\":"<<(r.afterExact?"true":"false")
               <<",\"paired\":"<<(r.paired?"true":"false")<<",\"pack_calls\":"<<r.packCalls<<",\"calls_147\":"<<r.calls147
               <<",\"validated_packs\":"<<r.validatedPacks<<",\"packed_hash\":"<<r.packedHash<<",\"input_generation\":"<<r.inputGeneration
               <<",\"equipment_generation\":"<<r.equipmentGeneration<<",\"begin_ns\":"<<r.beginNs<<",\"end_ns\":"<<r.endNs<<'}';}
        out<<"],\"producer_admitted\":false,\"native_state_written\":false}";
    }
private:
    void Save(const Record& r)noexcept{std::unique_lock lock(mutex_,std::try_to_lock);if(!lock){++busy_;return;}
        if(size_==Slots)++evicted_;else ++size_;records_[next_]=r;next_=(next_+1)%Slots;}
    inline static thread_local Scope* current_=nullptr;
    std::atomic<bool> enabled_=false;
    std::atomic<std::uint64_t> started_=0,pairs_=0,noPack_=0,unpaired_=0,postRejected_=0,invalidPack_=0,busy_=0,evicted_=0,abandoned_=0;
    std::array<Record,Slots> records_{};unsigned next_=0,size_=0;std::mutex mutex_;
};
}
