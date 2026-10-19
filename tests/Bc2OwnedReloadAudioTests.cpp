#include "../src/games/bc2/Bc2OwnedReloadAudio.h"
#include "Test.h"
#include <map>
#include <string>
#include <vector>
using namespace fvr::bc2;
namespace {
constexpr unsigned W=0x10000,D=0x11000,S=0x12000,T=0x13000,R=0x14000,A=0x15000,
    Clip=0x16000,Sound=0x17000,Slot=0x18000,Map=0x19000,MD=0x1a000,C=0x1b000,P=0x1c000,
    Meshes=0x1d000,Mesh=0x1e000,Shell=0x26000;
struct Fixture {
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x40000);
    std::map<unsigned,std::string> types;
    OwnedReloadAudioIdentity identity{{0x31000,0x32000,0x33000,W,D},7};
    unsigned captures=0,slotReads=0,clipReads=0;bool mutateOwner=false,mutateWeapon=false,mutateDuringRead=false,mutateClip=false;
    template<class V>void Put(unsigned at,const V& v){std::memcpy(bytes.data()+at,&v,sizeof(v));}
    void Text(unsigned object,unsigned offset,unsigned ptr,const char* text){Put(object+offset,ptr);std::memcpy(bytes.data()+ptr,text,std::strlen(text)+1);}
    void Classes(unsigned at,unsigned items,unsigned count){Put(at,std::array<unsigned,5>{1,2,count,count,items});}
    void Structs(unsigned at,unsigned items,unsigned stride,unsigned count){Put(at,std::array<unsigned,4>{1,2,items,items+stride*count});}
    explicit Fixture(bool spas=false){
        types={{identity.owner.soldier,"ClientSoldierEntity"},{D,"SoldierWeaponData"},{T,"SpecificAnimTreeData"},
            {R,"ResultAnimTreeData"},{Clip,"AnimationAsset"},{Sound,"SoundTemplate"},{MD,"AnimationSoundMapData"},{Mesh,"SkinnedMeshAsset"}};
        Put(W+4,D);Put(D+0x64,0x34500u);
        Text(D,0xc,0x20000,spas?"SPAS12_sp":"XM8_sp_s");
        Text(D,0x40,0x21000,spas?"Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12":"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped");
        Text(Mesh,0xc,0x22000,spas?"Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh":"Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh");
        Structs(D+0x88,S,0xc8,1);Classes(S+0x80,Meshes,1);Put(Meshes,Mesh);Put(S+0xb8,T);
        Classes(T+0x20,0x23000,1);Put(0x23000,R);Classes(R+0x74,A,57);Put(A+4*(spas?30:25),Clip);
        Put(Clip+4,0x24000u);Put(0x24000,ReloadAudioGuid{9,8,7,6,5,4,3,2,1,2,3,4,5,6,7,8});
        if(spas){types[Shell]="AnimationAsset";Put(A+4*25,Shell);Put(Shell+4,0x27000u);
            Put(0x27000,ReloadAudioGuid{8,8,7,6,5,4,3,2,1,2,3,4,5,6,7,8});}
        Put(Slot,Map);Put(Map+0x44,MD);Structs(MD+0x18,C,0x1c,1);Structs(C+0xc,P,8,1);Put(P,std::array<unsigned,2>{Sound,Clip});
    }
    ReloadAudioMemory Memory(){return {this,[](void* ctx,unsigned at,void* dst,std::size_t n){auto& f=*static_cast<Fixture*>(ctx);
        if(std::uint64_t(at)+n>f.bytes.size())return false;
        if(at==Slot&&++f.slotReads==1&&f.mutateDuringRead)f.bytes[0x24000]^=1;
        if(at==Clip+4&&++f.clipReads==3&&f.mutateClip)f.bytes[0x24001]^=1;
        std::memcpy(dst,f.bytes.data()+at,n);return true;},
        [](void* ctx,unsigned at,const char* type){const auto& map=static_cast<Fixture*>(ctx)->types;const auto i=map.find(at);return i!=map.end()&&i->second==type;}};}
    std::optional<OwnedReloadAudioProof> Resolve(){return ResolveOwnedReloadAudio(Memory(),identity,Slot,[this]()->std::optional<OwnedReloadAudioIdentity>{
        auto out=identity;if(++captures==2){if(mutateOwner)++out.equipmentGeneration;if(mutateWeapon)++out.owner.weapon;}return out;});}
    std::optional<OwnedReloadAudioProof> Animation(OwnedReloadAudioResolution* diagnostic=nullptr){
        return ResolveOwnedReloadAnimation(Memory(),identity,[this]()->std::optional<OwnedReloadAudioIdentity>{
            auto out=identity;if(++captures==2){if(mutateOwner)++out.equipmentGeneration;if(mutateWeapon)++out.owner.weapon;}return out;},diagnostic);}
};
int Checks(){
    for(bool spas:{false,true}){Fixture f(spas);auto p=f.Resolve();CHECK(p);CHECK(p->binding.guid[0]==9);CHECK(p->binding.animation==Clip&&p->binding.sound==Sound);
        CHECK(p->role==(spas?OwnedReloadAudioRole::SpasPump:OwnedReloadAudioRole::Xm8Reload));CHECK(f.captures==2);}
    // Exact native paths, types, layout and object equality are independent guards.
    for(unsigned fault=0;fault<15;++fault){Fixture f;
        switch(fault){
        case 0:f.bytes[0x21000]='x';break;
        case 1:f.bytes[0x22000]='x';break;
        case 2:f.types[T]="UnknownTree";break;
        case 3:f.types[Mesh]="Asset";break;
        case 4:f.Classes(R+0x74,A,56);break;
        case 5:f.Put(A+4*25,0u);f.Put(A+4*30,Clip);break;
        case 6:f.Put(A+4*30,Clip);break;
        case 7:f.Structs(C+0xc,P,8,2);f.Put(P+8,std::array<unsigned,2>{Sound,Clip});break;
        case 8:f.types[Sound]="Asset";break;
        case 9:f.mutateOwner=true;break;
        case 10:f.mutateWeapon=true;break;
        case 11:f.Put(Clip+4,0xfffffff8u);break;
        case 12:f.types[0x25000]="AnimationAsset";f.Put(0x25004,0x24000u);f.Put(P+4,0x25000u);break;
        case 13:f.Structs(C+0xc,P,8,513);break;
        case 14:f.mutateDuringRead=true;break;}
        CHECK(!f.Resolve());}
    // Production split PCM admission needs the current owned native animation,
    // independently of unrelated sound-map entries. It never reads the map.
    for(bool spas:{false,true}){Fixture f(spas);f.Put(Slot,0u);OwnedReloadAudioResolution d;
        const auto p=f.Animation(&d);CHECK(p);CHECK(d.failure==OwnedReloadAudioFailure::None);CHECK(d.reads>0&&!d.unreadable);
        CHECK(f.slotReads==0&&f.captures==2);CHECK(p->Covers(p->binding.guid));CHECK(!p->binding.map&&!p->binding.sound);
        auto unrelated=p->binding.guid;unrelated[0]^=64;CHECK(!p->Covers(unrelated));
        if(spas){CHECK(p->shellBinding.animation==Shell);CHECK(p->Covers(p->shellBinding.guid));}
        else CHECK(!p->shellBinding.animation);}
    for(unsigned fault=0;fault<8;++fault){Fixture f(true);OwnedReloadAudioResolution d;
        switch(fault){
        case 0:f.bytes[0x21000]='x';break;
        case 1:f.mutateOwner=true;break;
        case 2:f.mutateWeapon=true;break;
        case 3:f.mutateClip=true;break;
        case 4:f.Put(A+4*25,0u);break;
        case 5:f.Put(Shell+4,0x24000u);break;
        case 6:f.types[0x28000]="AnimationAsset";f.Put(0x28004,0x27000u);f.Put(A+4*29,0x28000u);break;
        case 7:f.Put(Clip+4,0xfffffff8u);break;}
        CHECK(!f.Animation(&d));CHECK(d.failure!=OwnedReloadAudioFailure::None);
        if(fault==0)CHECK(d.failure==OwnedReloadAudioFailure::Scope);
        if(fault>=1&&fault<=3)CHECK(d.failure==OwnedReloadAudioFailure::Incoherent);
        if(fault==7)CHECK(d.unreadable);}
    {Fixture f(true);f.Put(A+4*29,Shell);f.Put(A+4*31,Clip);const auto p=f.Animation();CHECK(p);
        CHECK(p->Covers(p->binding.guid)&&p->Covers(p->shellBinding.guid));}
    return 0;
}
}
int main(){return Checks();}
