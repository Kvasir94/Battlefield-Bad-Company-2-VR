#pragma once
// Explicit private-profile audition of one native cue. No default asset IDs,
// asset-name lookup, sampler writes, or new native calls.
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace fvr::bc2 {
using ReloadAudioGuid=std::array<std::uint8_t,16>;
struct ReloadAudioProfile {
    ReloadAudioGuid weapon{},tree{},animation{},sound{},map{};
    bool operator==(const ReloadAudioProfile&)const=default;
};
// Five 32-digit memory-order GUIDs separated by ':'. The local profile generator
// owns canonical UUID -> native GUID conversion. Empty/zero/partial profiles fail.
inline std::optional<ReloadAudioProfile> ParseReloadAudioProfile(std::string_view s)noexcept {
    if(s.size()!=164)return {};
    ReloadAudioProfile p;std::array<ReloadAudioGuid*,5> ids{&p.weapon,&p.tree,&p.animation,&p.sound,&p.map};
    const auto hex=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;};
    for(unsigned i=0;i<ids.size();++i){bool nonzero=false;
        if(i&&s[i*33-1]!=':')return {};
        for(unsigned j=0;j<16;++j){const int a=hex(s[i*33+2*j]),b=hex(s[i*33+2*j+1]);if(a<0||b<0)return {};
            (*ids[i])[j]=std::uint8_t(a*16+b);nonzero|=(*ids[i])[j]!=0;}
        if(!nonzero)return {};
        for(unsigned j=0;j<i;++j)if(*ids[i]==*ids[j])return {};
    }return p;
}
struct ReloadAudioProfiles {
    static constexpr std::size_t Capacity=64;
    std::array<ReloadAudioProfile,Capacity> entries{};
    std::size_t count=0;
};
// Explicit private profiles separated by ';'. One exact weapon identity can
// have only one cue in an audition; conflicting rows reject the entire list.
inline std::optional<ReloadAudioProfiles> ParseReloadAudioProfiles(std::string_view s)noexcept {
    if(s.empty()||s.size()>ReloadAudioProfiles::Capacity*165-1)return {};
    ReloadAudioProfiles out;
    for(;;){const auto end=s.find(';');const auto p=ParseReloadAudioProfile(s.substr(0,end));
        if(!p||out.count==out.entries.size())return {};
        for(std::size_t i=0;i<out.count;++i)if(out.entries[i].weapon==p->weapon)return {};
        out.entries[out.count++]=*p;
        if(end==std::string_view::npos)return out;
        s.remove_prefix(end+1);
    }
}
struct ReloadAudioOwner {
    std::uint32_t player=0,soldier=0,weak=0,weapon=0,data=0;
    bool Valid()const noexcept{return player&&soldier&&weak&&weapon&&data;}
    bool operator==(const ReloadAudioOwner&)const=default;
};
struct ReloadAudioMemory {
    void* context=nullptr;
    bool (*read)(void*,std::uint32_t,void*,std::size_t)=nullptr;
    bool (*type)(void*,std::uint32_t,const char*)=nullptr;
};
struct ReloadAudioProof {
    std::uint32_t data=0,tree=0,result=0,animation=0,sound=0,map=0,mapData=0;
    ReloadAudioGuid guid{};
    bool operator==(const ReloadAudioProof&)const=default;
};
// No addresses or asset identities in diagnostics. A failed exact binding must
// identify the rejected contract without weakening it for the next audition.
enum class ReloadAudioResolveFailure : unsigned {
    None,Disabled,Getter,Owner,WeaponData,WeaponType,WeaponGuid,ProfileList,ProfileMissing,
    ProfileDuplicate,WeaponStates,TreePointer,TreeType,TreeGuid,ResultArray,ResultPointer,
    ResultType,AnimationArray,AnimationRead,AnimationType,AnimationCount,MapPointer,MapData,
    MapType,MapGuid,CategoryArray,PairArray,PairLimit,PairRead,AnimationObject,SoundType,
    SoundGuid,PairCount,Unreadable,Incoherent
};
inline const char* ReloadAudioResolveFailureName(ReloadAudioResolveFailure failure)noexcept {
    constexpr std::array names{"none","disabled","getter","owner","weapon_data","weapon_type","weapon_guid","profile_list","profile_missing",
        "profile_duplicate","weapon_states","tree_pointer","tree_type","tree_guid","result_array","result_pointer",
        "result_type","animation_array","animation_read","animation_type","animation_count","map_pointer","map_data",
        "map_type","map_guid","category_array","pair_array","pair_limit","pair_read","animation_object","sound_type",
        "sound_guid","pair_count","unreadable","incoherent"};
    const auto index=unsigned(failure);return index<names.size()?names[index]:"unknown";
}
struct ReloadAudioResolution {ReloadAudioResolveFailure failure=ReloadAudioResolveFailure::None;unsigned reads=0;bool unreadable=false;};
namespace reload_audio_detail {
struct Reader {
    const ReloadAudioMemory& m;ReloadAudioResolution* diagnostic=nullptr;unsigned reads=0;bool failed=false;
    bool Check(bool ok,ReloadAudioResolveFailure reason)noexcept {
        if(!ok&&diagnostic&&diagnostic->failure==ReloadAudioResolveFailure::None)diagnostic->failure=reason;
        return ok;
    }
    bool Read(std::uint64_t at,void* p,std::size_t n)noexcept {
        const bool ok=++reads<=4096&&at>=0x10000&&n&&n<=4096&&at+n<=UINT32_MAX&&m.read&&m.read(m.context,std::uint32_t(at),p,n);
        failed|=!ok;if(diagnostic){++diagnostic->reads;diagnostic->unreadable|=!ok;}return ok;
    }
    template<class T>bool Get(std::uint64_t at,T& out)noexcept{return Read(at,&out,sizeof(out));}
    bool Type(std::uint32_t at,const char* name)noexcept{return at&&m.type&&m.type(m.context,at,name);}
    bool Guid(std::uint32_t at,const ReloadAudioGuid& expected)noexcept {
        std::uint32_t ref=0;ReloadAudioGuid id{};
        return Get(std::uint64_t(at)+4,ref)&&Read(ref,id.data(),id.size())&&id==expected;
    }
    // Class arrays use count/items at +0xc/+0x10; struct arrays use begin/end
    // at +8/+0xc. These are the existing 795745 reflected array contracts.
    bool Classes(std::uint64_t at,unsigned limit,std::uint32_t& items,unsigned& count)noexcept {
        std::array<std::uint32_t,5> h{};
        if(!Get(at,h)||!h[0]||!h[1]||!h[3]||h[3]>limit||h[2]<h[3]||h[2]>4096||h[4]<0x10000||h[4]%4)return false;
        items=h[4];count=h[3];return std::uint64_t(items)+count*4<=UINT32_MAX;
    }
    bool Structs(std::uint64_t at,unsigned stride,unsigned limit,std::uint32_t& items,unsigned& count)noexcept {
        std::array<std::uint32_t,4> h{};
        if(!Get(at,h)||!h[0]||!h[1]||h[2]<0x10000||h[2]%4||h[3]<=h[2]||(h[3]-h[2])%stride)return false;
        items=h[2];count=(h[3]-h[2])/stride;return count<=limit;
    }
};
inline std::optional<ReloadAudioProof> Resolve(Reader& r,const ReloadAudioOwner& o,
    const ReloadAudioProfile& profile,std::uint32_t mapSlot)noexcept {
    ReloadAudioProof p;p.data=o.data;
    std::uint32_t data=0,states=0,results=0,animations=0,categories=0;unsigned count=0;
    using F=ReloadAudioResolveFailure;
    if(!r.Check(o.Valid(),F::Owner)||!r.Check(r.Get(std::uint64_t(o.weapon)+4,data)&&data==o.data,F::WeaponData)||
       !r.Check(r.Type(data,"SoldierWeaponData"),F::WeaponType)||!r.Check(r.Guid(data,profile.weapon),F::WeaponGuid)||
       !r.Check(r.Structs(std::uint64_t(data)+0x88,0xc8,1,states,count)&&count==1,F::WeaponStates)||
       !r.Check(r.Get(std::uint64_t(states)+0xb8,p.tree),F::TreePointer)||!r.Check(r.Type(p.tree,"SpecificAnimTreeData"),F::TreeType)||!r.Check(r.Guid(p.tree,profile.tree),F::TreeGuid)||
       !r.Check(r.Classes(std::uint64_t(p.tree)+0x20,1,results,count)&&count==1,F::ResultArray)||
       !r.Check(r.Get(results,p.result),F::ResultPointer)||!r.Check(r.Type(p.result,"ResultAnimTreeData"),F::ResultType)||
       !r.Check(r.Classes(std::uint64_t(p.result)+0x74,128,animations,count),F::AnimationArray))return {};
    unsigned matches=0;
    for(unsigned i=0;i<count;++i){std::uint32_t a=0;if(!r.Check(r.Get(std::uint64_t(animations)+4*i,a),F::AnimationRead))return {};
        if(a&&r.Guid(a,profile.animation)){if(!r.Check(r.Type(a,"AnimationAsset"),F::AnimationType))return {};p.animation=a;++matches;}}
    if(!r.Check(matches==1,F::AnimationCount)||!r.Check(r.Get(mapSlot,p.map),F::MapPointer)||!r.Check(r.Get(std::uint64_t(p.map)+0x44,p.mapData),F::MapData)||
       !r.Check(r.Type(p.mapData,"AnimationSoundMapData"),F::MapType)||!r.Check(r.Guid(p.mapData,profile.map),F::MapGuid)||
       !r.Check(r.Structs(std::uint64_t(p.mapData)+0x18,0x1c,8,categories,count),F::CategoryArray))return {};
    matches=0;unsigned total=0;
    for(unsigned i=0;i<count;++i){std::uint32_t pairs=0;unsigned n=0;
        if(!r.Check(r.Structs(std::uint64_t(categories)+i*0x1c+0xc,8,512,pairs,n),F::PairArray)||!r.Check(total+n<=512,F::PairLimit))return {};total+=n;
        for(unsigned j=0;j<n;++j){std::array<std::uint32_t,2> pair{};if(!r.Check(r.Get(std::uint64_t(pairs)+8*j,pair),F::PairRead))return {};
            if(!pair[1]||!r.Guid(pair[1],profile.animation))continue;
            // A duplicate GUID represented by another animation object also
            // rejects: the map constructor would otherwise choose a hash entry.
            if(!r.Check(pair[1]==p.animation,F::AnimationObject)||!r.Check(r.Type(pair[0],"SoundTemplate"),F::SoundType)||!r.Check(r.Guid(pair[0],profile.sound),F::SoundGuid))return {};
            p.sound=pair[0];++matches;}
    }
    if(!r.Check(matches==1,F::PairCount)||!r.Check(!r.failed,F::Unreadable))return {};p.guid=profile.animation;return p;
}
}
// Repeated bounded reads establish coherence, not atomicity. The adapter must
// independently resolve the same live player/weak/weapon before and after this
// read, and hold the existing native listener dispatch lock for playback.
inline std::optional<ReloadAudioProof> ResolveReloadAudioCue(const ReloadAudioMemory& m,
    const ReloadAudioOwner& owner,const ReloadAudioProfile& profile,std::uint32_t mapSlot,ReloadAudioResolution* diagnostic=nullptr)noexcept {
    if(diagnostic)*diagnostic={};reload_audio_detail::Reader reader{m,diagnostic};const auto a=reload_audio_detail::Resolve(reader,owner,profile,mapSlot);
    const auto b=a?reload_audio_detail::Resolve(reader,owner,profile,mapSlot):std::nullopt;
    if(a&&b&&!reader.Check(*a==*b,ReloadAudioResolveFailure::Incoherent))return {};
    return a&&b?a:std::nullopt;
}
inline std::optional<ReloadAudioProof> ResolveReloadAudioCue(const ReloadAudioMemory& m,
    const ReloadAudioOwner& owner,const ReloadAudioProfiles& profiles,std::uint32_t mapSlot,ReloadAudioResolution* diagnostic=nullptr)noexcept {
    using F=ReloadAudioResolveFailure;
    if(diagnostic)*diagnostic={};reload_audio_detail::Reader reader{m,diagnostic};std::uint32_t data=0,ref=0;ReloadAudioGuid id{};
    if(!reader.Check(owner.Valid(),F::Owner)||!reader.Check(profiles.count&&profiles.count<=profiles.entries.size(),F::ProfileList)||
       !reader.Check(reader.Get(std::uint64_t(owner.weapon)+4,data)&&data==owner.data,F::WeaponData)||!reader.Check(reader.Type(data,"SoldierWeaponData"),F::WeaponType)||
       !reader.Check(reader.Get(std::uint64_t(data)+4,ref)&&reader.Read(ref,id.data(),id.size()),F::WeaponGuid))return {};
    const ReloadAudioProfile* selected=nullptr;
    for(std::size_t i=0;i<profiles.count;++i)if(profiles.entries[i].weapon==id){
        if(!reader.Check(!selected,F::ProfileDuplicate))return {};selected=&profiles.entries[i];}
    // The full resolver rereads weapon identity and the entire binding twice.
    // No name/family fallback and no sibling cue inheritance.
    if(!reader.Check(selected!=nullptr,F::ProfileMissing))return {};
    return ResolveReloadAudioCue(m,owner,*selected,mapSlot,diagnostic);
}
class ReloadAudioCue {
public:
    enum class Reject {None,Disabled,NotPhysical,Button,AlreadyAttempted,Owner,Sequence,Clock,Expired,Window,Proof,Data};
    enum class DispatchReject {None,Owner,Proof,Clock,Expired,Listener,View};
    Reject LastRequestReject()const noexcept{std::lock_guard lock(m_);return lastRequestReject_;}
    DispatchReject LastDispatchReject()const noexcept{std::lock_guard lock(m_);return lastDispatchReject_;}
    // One explicit audition per process. Cancels and expired edges are consumed;
    // repeated input cannot turn a diagnostic into continuous cue playback.
    bool Request(bool enabled,bool physicalRack,bool button,const ReloadAudioOwner& owner,
        const ReloadAudioProof& proof,std::uint64_t sequence,std::int64_t now,std::int64_t deadline)noexcept {
        std::lock_guard lock(m_);
        const auto reject=[this](Reject reason){lastRequestReject_=reason;return false;};
        if(!enabled)return reject(Reject::Disabled);
        if(!physicalRack)return reject(Reject::NotPhysical);
        if(button)return reject(Reject::Button);
        if(attempted_)return reject(Reject::AlreadyAttempted);
        if(!owner.Valid())return reject(Reject::Owner);
        if(!sequence)return reject(Reject::Sequence);
        if(now<=0)return reject(Reject::Clock);
        if(deadline<=now)return reject(Reject::Expired);
        if(deadline-now>250000000)return reject(Reject::Window);
        if(!proof.map||!proof.mapData||!proof.tree||!proof.result||!proof.sound||!proof.animation)return reject(Reject::Proof);
        if(proof.data!=owner.data)return reject(Reject::Data);
        lastRequestReject_=Reject::None;
        attempted_=pending_=true;owner_=owner;proof_=proof;deadline_=deadline;return true;
    }
    void Cancel()noexcept{std::lock_guard lock(m_);pending_=false;stopDue_=playing_;}
    bool NeedsWork()const noexcept{std::lock_guard lock(m_);return pending_||playing_;}
    bool Due(std::int64_t now)noexcept{std::lock_guard lock(m_);
        if(pending_&&now>=deadline_){pending_=false;lastDispatchReject_=DispatchReject::Expired;}
        return pending_||(playing_&&(stopDue_||now>=stopAt_));}
    ReloadAudioOwner Owner()const noexcept{std::lock_guard lock(m_);return owner_;}
    struct Work {bool play=false,stop=false;ReloadAudioGuid guid{};};
    Work Poll(const ReloadAudioOwner& owner,const std::optional<ReloadAudioProof>& current,
        std::uintptr_t listener,bool playedView,std::int64_t now)noexcept {
        std::lock_guard lock(m_);Work w;
        if(!pending_&&!playing_)return w;
        if(owner!=owner_||!current||*current!=proof_||now<=0){
            lastDispatchReject_=owner!=owner_?DispatchReject::Owner:(!current||*current!=proof_)?DispatchReject::Proof:DispatchReject::Clock;
            pending_=playing_=stopDue_=false;return w;}
        if(pending_&&now>=deadline_){pending_=false;lastDispatchReject_=DispatchReject::Expired;return w;}
        if(!listener||!playedView){lastDispatchReject_=!listener?DispatchReject::Listener:DispatchReject::View;return w;}
        if(pending_){lastDispatchReject_=DispatchReject::None;pending_=false;playing_=true;listener_=listener;stopAt_=now+4000000000ll;w.play=true;w.guid=proof_.guid;return w;}
        if(playing_&&listener==listener_&&(stopDue_||now>=stopAt_)){
            playing_=stopDue_=false;w.stop=true;w.guid=proof_.guid;}
        return w;
    }
private:
    mutable std::mutex m_;
    Reject lastRequestReject_=Reject::None;
    DispatchReject lastDispatchReject_=DispatchReject::None;
    bool attempted_=false,pending_=false,playing_=false,stopDue_=false;
    ReloadAudioOwner owner_{};ReloadAudioProof proof_{};
    std::uintptr_t listener_=0;std::int64_t deadline_=0,stopAt_=0;
};
}
