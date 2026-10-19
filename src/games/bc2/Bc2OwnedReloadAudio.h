#pragma once
#include "Bc2ReloadAudioCue.h"
#include <algorithm>
#include <cstring>

namespace fvr::bc2 {
enum class OwnedReloadAudioRole { Xm8Reload, SpasPump };
struct OwnedReloadAudioIdentity {
    ReloadAudioOwner owner{};
    std::uint64_t equipmentGeneration=0;
    bool operator==(const OwnedReloadAudioIdentity&)const=default;
};
struct OwnedReloadAudioProof {
    OwnedReloadAudioIdentity identity{};
    OwnedReloadAudioRole role{};
    ReloadAudioProof binding{};
    // SPAS uses a separate native shell-loading clip in addition to its pump.
    ReloadAudioProof shellBinding{};
    std::uint32_t mesh=0,persistence=0;
    bool operator==(const OwnedReloadAudioProof&)const=default;
    bool Covers(const ReloadAudioGuid& guid)const noexcept {
        return (binding.animation&&binding.guid==guid)||(role==OwnedReloadAudioRole::SpasPump&&shellBinding.animation&&shellBinding.guid==guid);
    }
};
enum class OwnedReloadAudioFailure {None,Owner,Weapon,Scope,Mesh,Tree,Animations,AnimationIdentity,Map,MapPair,Incoherent};
inline const char* OwnedReloadAudioFailureName(OwnedReloadAudioFailure value)noexcept {
    constexpr const char* names[]{"none","owner","weapon","scope","mesh","tree","animations","animation_identity","map","map_pair","incoherent"};
    const auto i=unsigned(value);return i<std::size(names)?names[i]:"unknown";
}
struct OwnedReloadAudioResolution {OwnedReloadAudioFailure failure=OwnedReloadAudioFailure::None;unsigned reads=0;bool unreadable=false;};
namespace owned_reload_audio_detail {
using Reader=reload_audio_detail::Reader;
// Existing reflected Name (+0xc), WeaponAssetName (+0x40), and mesh Name
// contracts. No AnimationAsset/SoundTemplate name offset is assumed.
inline bool Text(Reader& r,std::uint32_t object,unsigned offset,std::string_view expected)noexcept {
    std::uint32_t pointer=0;std::array<char,256> text{};
    return expected.size()<text.size()&&r.Get(std::uint64_t(object)+offset,pointer)&&
        r.Read(pointer,text.data(),expected.size()+1)&&
        !std::memcmp(text.data(),expected.data(),expected.size())&&!text[expected.size()];
}
inline bool Guid(Reader& r,std::uint32_t asset,ReloadAudioGuid& guid)noexcept {
    std::uint32_t pointer=0;
    return r.Get(std::uint64_t(asset)+4,pointer)&&r.Read(pointer,guid.data(),guid.size())&&
        std::any_of(guid.begin(),guid.end(),[](auto b){return b!=0;});
}
inline std::optional<OwnedReloadAudioProof> ReadAnimation(Reader& r,const OwnedReloadAudioIdentity& identity,
    OwnedReloadAudioResolution& diagnostic,bool includeShell)noexcept {
    OwnedReloadAudioProof out;out.identity=identity;auto& p=out.binding;const auto& o=identity.owner;
    std::uint32_t data=0,states=0,meshes=0,results=0,animations=0;unsigned count=0;
    diagnostic.failure=OwnedReloadAudioFailure::Weapon;
    if(!o.Valid()||!identity.equipmentGeneration||!r.Type(o.soldier,"ClientSoldierEntity")||
       !r.Get(std::uint64_t(o.weapon)+4,data)||data!=o.data||!r.Type(data,"SoldierWeaponData"))return {};
    p.data=data;
    diagnostic.failure=OwnedReloadAudioFailure::Scope;
    const bool xm8=Text(r,data,0xc,"XM8_sp_s")&&Text(r,data,0x40,"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped");
    const bool spas=!xm8&&Text(r,data,0xc,"SPAS12_sp")&&Text(r,data,0x40,"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12");
    if(!xm8&&!spas)return {};
    const auto meshName=xm8?"Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh":"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh";
    out.role=xm8?OwnedReloadAudioRole::Xm8Reload:OwnedReloadAudioRole::SpasPump;
    diagnostic.failure=OwnedReloadAudioFailure::Mesh;
    if(!r.Get(std::uint64_t(data)+0x64,out.persistence)||!out.persistence||
       !r.Structs(std::uint64_t(data)+0x88,0xc8,1,states,count)||count!=1||
       !r.Classes(std::uint64_t(states)+0x80,8,meshes,count))return {};
    unsigned found=0;
    for(unsigned i=0;i<count;++i){std::uint32_t mesh=0;
        if(!r.Get(std::uint64_t(meshes)+4*i,mesh)||!r.Type(mesh,"SkinnedMeshAsset"))return {};
        if(Text(r,mesh,0xc,meshName)){out.mesh=mesh;++found;}}
    if(found!=1)return {};
    diagnostic.failure=OwnedReloadAudioFailure::Tree;
    if(!r.Get(std::uint64_t(states)+0xb8,p.tree)||!r.Type(p.tree,"SpecificAnimTreeData")||
       !r.Classes(std::uint64_t(p.tree)+0x20,1,results,count)||count!=1||
       !r.Get(results,p.result)||!r.Type(p.result,"ResultAnimTreeData"))return {};
    diagnostic.failure=OwnedReloadAudioFailure::Animations;
    if(!r.Classes(std::uint64_t(p.result)+0x74,57,animations,count)||count!=57||
       !r.Get(std::uint64_t(animations)+4*(xm8?25:30),p.animation)||!r.Type(p.animation,"AnimationAsset")||
       !Guid(r,p.animation,p.guid))return {};
    // A GUID callback cannot distinguish different objects with the same GUID.
    // Repeated slots referencing the same object are harmless for suppression.
    if(spas&&includeShell){auto& shell=out.shellBinding;shell.data=p.data;shell.tree=p.tree;shell.result=p.result;
        if(!r.Get(std::uint64_t(animations)+4*25,shell.animation)||!r.Type(shell.animation,"AnimationAsset")||!Guid(r,shell.animation,shell.guid))return {};}
    diagnostic.failure=OwnedReloadAudioFailure::AnimationIdentity;
    found=0;unsigned shellFound=0;
    for(unsigned i=0;i<count;++i){std::uint32_t animation=0;ReloadAudioGuid id{};
        if(!r.Get(std::uint64_t(animations)+4*i,animation))return {};
        if(!animation)continue;
        if(!r.Type(animation,"AnimationAsset")||!Guid(r,animation,id))return {};
        if(id==p.guid){if(animation!=p.animation)return {};++found;}
        if(out.shellBinding.animation&&id==out.shellBinding.guid){if(animation!=out.shellBinding.animation)return {};++shellFound;}}
    if((includeShell?found==0:found!=1)||(out.shellBinding.animation&&(!shellFound||out.shellBinding.guid==p.guid))||r.failed)return {};
    diagnostic.failure=OwnedReloadAudioFailure::None;return out;
}
inline bool ReadMap(Reader& r,ReloadAudioProof& p,std::uint32_t slot,OwnedReloadAudioResolution& diagnostic)noexcept {
    std::uint32_t categories=0;unsigned count=0;
    diagnostic.failure=OwnedReloadAudioFailure::Map;
    if(!r.Get(slot,p.map)||!r.Get(std::uint64_t(p.map)+0x44,p.mapData)||
       !r.Type(p.mapData,"AnimationSoundMapData")||!r.Structs(std::uint64_t(p.mapData)+0x18,0x1c,8,categories,count))return {};
    diagnostic.failure=OwnedReloadAudioFailure::MapPair;
    unsigned found=0,total=0;
    for(unsigned i=0;i<count;++i){std::uint32_t pairs=0;unsigned n=0;
        if(!r.Structs(std::uint64_t(categories)+i*0x1c+0xc,8,512,pairs,n)||total+n>512)return {};total+=n;
        for(unsigned j=0;j<n;++j){std::array<std::uint32_t,2> pair{};ReloadAudioGuid id{};
            if(!r.Get(std::uint64_t(pairs)+8*j,pair))return {};
            if(!pair[1])continue;
            if(!r.Type(pair[1],"AnimationAsset")||!Guid(r,pair[1],id))return {};
            if(pair[1]!=p.animation&&id!=p.guid)continue;
            if(pair[1]!=p.animation||id!=p.guid||!r.Type(pair[0],"SoundTemplate"))return {};
            p.sound=pair[0];++found;}}
    if(found!=1||r.failed)return false;diagnostic.failure=OwnedReloadAudioFailure::None;return true;
}
}
// captureOwner must freshly re-resolve the existing complete selected-owner
// chain (including inventory selection and equipment), not return a cached
// token. The caller runs this outside the native sound callback and invalidates
// its cached result on owner/equipment/space/focus changes. This grants no audio
// readiness or physical-action authority by itself. No game calls or writes.
template<class CaptureOwner>
inline std::optional<OwnedReloadAudioProof> ResolveOwnedReloadAudio(const ReloadAudioMemory& memory,
    const OwnedReloadAudioIdentity& expected,std::uint32_t mapSlot,CaptureOwner&& captureOwner,
    OwnedReloadAudioResolution* diagnostic=nullptr)noexcept {
    OwnedReloadAudioResolution local;auto& d=diagnostic?*diagnostic:local;d={};d.failure=OwnedReloadAudioFailure::Owner;
    const auto before=captureOwner();if(!before||*before!=expected)return {};
    reload_audio_detail::Reader reader{memory};
    auto read=[&](){auto p=owned_reload_audio_detail::ReadAnimation(reader,expected,d,false);
        if(p&&!owned_reload_audio_detail::ReadMap(reader,p->binding,mapSlot,d))p.reset();return p;};
    const auto a=read();const auto b=a?read():std::nullopt;
    const auto after=captureOwner();
    d.reads=reader.reads;d.unreadable=reader.failed;
    if(!a||!b)return {};
    if(*a!=*b||!after||*after!=expected||reader.failed){d.failure=OwnedReloadAudioFailure::Incoherent;return {};}
    return a;
}
// Split PCM playback does not call the engine's sound map. Its suppression
// identity is the selected weapon's actual animation GUID, proved independently
// of unrelated map entries. The owned weapon/configuration/mesh and role indices
// are the same exact source joins used by the sampled native reload animations.
template<class CaptureOwner>
inline std::optional<OwnedReloadAudioProof> ResolveOwnedReloadAnimation(const ReloadAudioMemory& memory,
    const OwnedReloadAudioIdentity& expected,CaptureOwner&& captureOwner,OwnedReloadAudioResolution* diagnostic=nullptr)noexcept {
    OwnedReloadAudioResolution local;auto& d=diagnostic?*diagnostic:local;d={};d.failure=OwnedReloadAudioFailure::Owner;
    const auto before=captureOwner();if(!before||*before!=expected)return {};
    reload_audio_detail::Reader reader{memory};
    const auto a=owned_reload_audio_detail::ReadAnimation(reader,expected,d,true);
    const auto b=a?owned_reload_audio_detail::ReadAnimation(reader,expected,d,true):std::nullopt;
    const auto after=captureOwner();d.reads=reader.reads;d.unreadable=reader.failed;
    if(!a||!b)return {};
    if(*a!=*b||!after||*after!=expected||reader.failed){d.failure=OwnedReloadAudioFailure::Incoherent;return {};}
    return a;
}
}
