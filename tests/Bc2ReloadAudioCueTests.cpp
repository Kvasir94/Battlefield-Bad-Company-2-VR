#include "../src/games/bc2/Bc2ReloadAudioCue.h"
#include "Test.h"
#include <cstring>
#include <map>
#include <string>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr std::uint32_t Weapon=0x10000,Data=0x11000,States=0x12000,Tree=0x13000,Results=0x14000,
    Result=0x15000,Animations=0x16000,Animation=0x17000,Sound=0x18000,Slot=0x19000,Map=0x1a000,
    MapData=0x1b000,Categories=0x1c000,Pairs=0x1d000;
constexpr std::int64_t Now=10000000000ll;
ReloadAudioOwner Owner(){return {0x40000,0x41000,0x42000,Weapon,Data};}
struct Memory {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(0x30000);
    std::map<std::uint32_t,std::string> types;
    ReloadAudioProfile profile;
    bool changeOnSecondPass=false;unsigned slotReads=0;
    template<class T>void Put(std::uint32_t at,const T& value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
    void Guid(std::uint32_t at,std::uint32_t ref,ReloadAudioGuid& id,unsigned tag){id[0]=std::uint8_t(tag);Put(at+4,ref);Put(ref,id);}
    void Classes(std::uint32_t at,std::uint32_t items,unsigned count){Put(at,std::array<std::uint32_t,5>{0x1f000,0x1f100,count,count,items});}
    void Structs(std::uint32_t at,std::uint32_t items,unsigned stride,unsigned count){Put(at,std::array<std::uint32_t,4>{0x1f000,0x1f100,items,items+stride*count});}
    Memory(){
        types={{Data,"SoldierWeaponData"},{Tree,"SpecificAnimTreeData"},{Result,"ResultAnimTreeData"},
            {Animation,"AnimationAsset"},{Sound,"SoundTemplate"},{MapData,"AnimationSoundMapData"}};
        Put(Weapon+4,Data);Guid(Data,0x21000,profile.weapon,1);Guid(Tree,0x21020,profile.tree,2);
        Guid(Animation,0x21040,profile.animation,3);Guid(Sound,0x21060,profile.sound,4);Guid(MapData,0x21080,profile.map,5);
        Structs(Data+0x88,States,0xc8,1);Put(States+0xb8,Tree);Classes(Tree+0x20,Results,1);Put(Results,Result);
        Classes(Result+0x74,Animations,1);Put(Animations,Animation);Put(Slot,Map);Put(Map+0x44,MapData);
        Structs(MapData+0x18,Categories,0x1c,1);Structs(Categories+0xc,Pairs,8,1);Put(Pairs,std::array<std::uint32_t,2>{Sound,Animation});
    }
    ReloadAudioMemory Api(){return {this,[](void* context,std::uint32_t at,void* dst,std::size_t n){
        auto& m=*static_cast<Memory*>(context);if(std::uint64_t(at)+n>m.bytes.size())return false;
        if(at==Slot&&++m.slotReads==2&&m.changeOnSecondPass)m.bytes[0x21060]^=1;
        std::memcpy(dst,m.bytes.data()+at,n);return true;
    },[](void* context,std::uint32_t at,const char* name){const auto& t=static_cast<Memory*>(context)->types;
        const auto i=t.find(at);return i!=t.end()&&i->second==name;}};}
    std::optional<ReloadAudioProof> Resolve(ReloadAudioResolution* diagnostic=nullptr){return ResolveReloadAudioCue(Api(),Owner(),profile,Slot,diagnostic);}
};
std::string Text(const ReloadAudioProfile& p){std::string s;const char* hex="0123456789abcdef";
    for(const auto* id:{&p.weapon,&p.tree,&p.animation,&p.sound,&p.map}){if(!s.empty())s+=':';
        for(auto b:*id){s+=hex[b>>4];s+=hex[b&15];}}return s;}
int ProfilesAndLoadedBinding(){
    Memory m;const auto text=Text(m.profile);CHECK(ParseReloadAudioProfile(text)==m.profile);
    CHECK(!ParseReloadAudioProfile({}));CHECK(!ParseReloadAudioProfile(text.substr(1)));
    auto bad=text;bad[32]=';';CHECK(!ParseReloadAudioProfile(bad));bad=text;bad[0]='x';CHECK(!ParseReloadAudioProfile(bad));
    bad=text;bad.replace(0,32,32,'0');CHECK(!ParseReloadAudioProfile(bad));bad=text;bad.replace(33,32,text.substr(0,32));CHECK(!ParseReloadAudioProfile(bad));
    const auto p=m.Resolve();CHECK(p&&p->animation==Animation&&p->sound==Sound&&p->guid==m.profile.animation);
    using F=ReloadAudioResolveFailure;
    constexpr std::array expected{F::WeaponGuid,F::TreeGuid,F::AnimationCount,F::SoundGuid,F::MapGuid,
        F::AnimationCount,F::PairCount,F::SoundType,F::SoundGuid};
    for(unsigned fault=0;fault<9;++fault){Memory x;
        if(fault==0)x.profile.weapon[0]^=1;
        if(fault==1)x.profile.tree[0]^=1;
        if(fault==2)x.profile.animation[0]^=1;
        if(fault==3)x.profile.sound[0]^=1;
        if(fault==4)x.profile.map[0]^=1;
        if(fault==5){x.Classes(Result+0x74,Animations,2);x.Put(Animations+4,Animation);}
        if(fault==6){x.Structs(Categories+0xc,Pairs,8,2);x.Put(Pairs+8,std::array<std::uint32_t,2>{Sound,Animation});}
        if(fault==7)x.types[Sound]="UnrelatedAsset";
        if(fault==8)x.changeOnSecondPass=true;
        ReloadAudioResolution diagnostic;CHECK(!x.Resolve(&diagnostic));
        CHECK(diagnostic.failure==expected[fault]&&diagnostic.reads>0&&!diagnostic.unreadable);
    }
    Memory unreadable;unreadable.Classes(Result+0x74,Animations,2);unreadable.Put(Animations+4,std::uint32_t(0x40000));
    ReloadAudioResolution diagnostic;CHECK(!unreadable.Resolve(&diagnostic));CHECK(diagnostic.failure==F::Unreadable&&diagnostic.unreadable);
    Memory multipleStates;multipleStates.Structs(Data+0x88,States,0xc8,2);CHECK(!multipleStates.Resolve());
    return 0;
}
int OnePhysicalEdgeAndExpiry(){
    Memory m;const auto p=m.Resolve();CHECK(p);ReloadAudioCue cue;
    CHECK(!cue.Request(false,true,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(!cue.Request(true,false,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(!cue.Request(true,true,true,Owner(),*p,1,Now,Now+100000000));
    CHECK(!cue.Request(true,true,false,Owner(),*p,1,Now,Now));
    CHECK(!cue.Request(true,true,false,Owner(),*p,1,Now,Now+250000001));
    CHECK(cue.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(!cue.Request(true,true,false,Owner(),*p,1,Now+1,Now+100000000));
    CHECK(!cue.Poll(Owner(),p,0x50000,false,Now+1).play);
    CHECK(cue.LastDispatchReject()==ReloadAudioCue::DispatchReject::View);
    const auto w=cue.Poll(Owner(),p,0x50000,true,Now+2);CHECK(w.play&&!w.stop&&w.guid==p->guid);
    CHECK(!cue.Poll(Owner(),p,0x50000,true,Now+3).play);
    CHECK(!cue.Poll(Owner(),p,0x50004,true,Now+4000000002ll).stop);
    CHECK(cue.Poll(Owner(),p,0x50000,true,Now+4000000002ll).stop);
    CHECK(!cue.NeedsWork());CHECK(!cue.Request(true,true,false,Owner(),*p,2,Now+5000000000ll,Now+5100000000ll));
    ReloadAudioCue stale;CHECK(stale.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(!stale.Poll(Owner(),p,0x50000,true,Now+100000000).play&&!stale.NeedsWork());
    ReloadAudioCue noListener;CHECK(noListener.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(noListener.Due(Now+1));CHECK(!noListener.Due(Now+100000000)&&!noListener.NeedsWork());
    CHECK(noListener.LastDispatchReject()==ReloadAudioCue::DispatchReject::Expired);
    CHECK(!noListener.Request(true,true,false,Owner(),*p,2,Now+100000001,Now+200000000));
    return 0;
}
int ExactProfileList(){
    Memory m;auto sibling=m.profile;sibling.weapon[0]=99;
    const auto text=Text(sibling)+';'+Text(m.profile);const auto profiles=ParseReloadAudioProfiles(text);
    CHECK(profiles&&profiles->count==2);
    const auto proof=ResolveReloadAudioCue(m.Api(),Owner(),*profiles,Slot);CHECK(proof&&proof==m.Resolve());
    CHECK(!ParseReloadAudioProfiles({}));CHECK(!ParseReloadAudioProfiles(text+';'));
    CHECK(!ParseReloadAudioProfiles(';'+text));CHECK(!ParseReloadAudioProfiles(Text(m.profile)+';'+Text(m.profile)));
    auto conflict=m.profile;conflict.sound[0]=77;
    CHECK(!ParseReloadAudioProfiles(Text(m.profile)+';'+Text(conflict)));
    const auto unknown=ParseReloadAudioProfiles(Text(sibling));CHECK(unknown);
    ReloadAudioResolution diagnostic;CHECK(!ResolveReloadAudioCue(m.Api(),Owner(),*unknown,Slot,&diagnostic));
    CHECK(diagnostic.failure==ReloadAudioResolveFailure::ProfileMissing&&!diagnostic.unreadable);
    auto duplicate=*profiles;duplicate.entries[0]=duplicate.entries[1];
    CHECK(!ResolveReloadAudioCue(m.Api(),Owner(),duplicate,Slot));
    std::string full;
    for(unsigned i=0;i<ReloadAudioProfiles::Capacity;++i){auto row=m.profile;row.weapon[0]=std::uint8_t(128+i);
        if(!full.empty())full+=';';full+=Text(row);}
    const auto maximum=ParseReloadAudioProfiles(full);CHECK(maximum&&maximum->count==ReloadAudioProfiles::Capacity);
    CHECK(!ParseReloadAudioProfiles(full+';'+Text(m.profile)));
    return 0;
}
int CancellationAndOwnership(){
    Memory m;const auto p=m.Resolve();CHECK(p);
    for(unsigned fault=0;fault<3;++fault){ReloadAudioCue cue;CHECK(cue.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));
        auto owner=Owner();auto proof=p;
        if(fault==0)owner.weak+=4;
        if(fault==1)proof->map+=4;
        if(fault==2)proof.reset();
        CHECK(!cue.Poll(owner,proof,0x50000,true,Now+1).play&&!cue.NeedsWork());
        CHECK(cue.LastDispatchReject()==(fault==0?ReloadAudioCue::DispatchReject::Owner:ReloadAudioCue::DispatchReject::Proof));
    }
    ReloadAudioCue pending;CHECK(pending.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));pending.Cancel();
    const auto cancelled=pending.Poll(Owner(),p,0x50000,true,Now+1);CHECK(!cancelled.play&&!cancelled.stop&&!pending.NeedsWork());
    CHECK(!pending.Request(true,true,false,Owner(),*p,2,Now+2,Now+100000000));
    ReloadAudioCue playing;CHECK(playing.Request(true,true,false,Owner(),*p,1,Now,Now+100000000));
    CHECK(playing.Poll(Owner(),p,0x50000,true,Now+1).play);playing.Cancel();
    CHECK(!playing.Poll(Owner(),p,0x50004,true,Now+2).stop);
    const auto stopped=playing.Poll(Owner(),p,0x50000,true,Now+3);CHECK(stopped.stop&&!stopped.play);
    CHECK(!playing.Poll(Owner(),p,0x50000,true,Now+4).stop);
    return 0;
}
}
int main(){if(ProfilesAndLoadedBinding()||ExactProfileList()||OnePhysicalEdgeAndExpiry()||CancellationAndOwnership())return 1;
    std::puts("Bc2ReloadAudioCue: exact binding and bounded audition policy passed; native layout and audible cue remain runtime-unverified.");return 0;}
