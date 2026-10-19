#include "Test.h"
#include "Bc2PlayerInventory.h"
#include "fvr/interaction/PhysicalWeaponState.h"
#include <cstring>
#include <vector>
using namespace fvr::bc2;
namespace {
struct Fixture {
    static constexpr unsigned base=0x400000,cp=0x10000,cs=0x12000,weak=0x13000,ci=0x14000,ca=0x15000,cw=0x16000,
        data=0x17000,fd=0x18000,primary=0x19000,cf0=0x1a000,cf1=0x1b000,manager=0x20000,players=0x21000,
        sp=0x22000,ss=0x24000,si=0x26000,sa=0x27000,sw=0x28000,effects=0x29000,firing=0x2b000,callback=0x2c000,
        soldierData=0x2d000,inventoryData=0x2e000,gadget=0x2f000,gadgetData=0x30000,ss2=0x32000,si2=0x34000;
    std::vector<std::byte> bytes=std::vector<std::byte>(0x30000),image=std::vector<std::byte>(0x10000);
    ReloadServerBinding server{};ReloadStateBinding state{};PlayerInventoryAnchor actor{cp,cs,weak,7};
    unsigned raceReads=0;bool race=false;
    std::byte* At(unsigned at){return at>=base?image.data()+at-base:bytes.data()+at-0x10000;}
    template<class T>void Put(unsigned at,T value){std::memcpy(At(at),&value,sizeof value);}
    Fixture(){
        server.preferredBase=base;server.imageSize=0x1913000;server.contextRva=0x100;server.managerVtableRva=0x200;server.controlledGetterRva=0x300;
        state.preferredBase=base;state.imageSize=server.imageSize;state.firingVtableRva=0x500;
        Put<std::uint8_t>(cp+0xccd,8);Put(cp+0xc54,weak);Put(weak,cs+4);Put(cs+0x220,cp);Put(cp+0xc68,cs);Put<std::uint8_t>(cs+0x114,1);
        Put(cs+0x24c,ci);Put(ci+4,inventoryData);Put(cs+0x260,ca);Put(cs+0x264,ca+8);Put(ca,cw);Put(ci+0x14c,0u);Put(cw+4,data);
        Put(cw+0x3c,cf0);Put(cw+0x40,cf1);Put(data+0x98,fd);Put(fd+0x40,primary);Put(primary+0x24,2);Put(primary+0x20,1);Put(primary+0x184,30);
        Put(base+0x108,manager);Put(manager,base+0x200);Put(manager+4,64u);Put(manager+0x6c,players);Put(players,sp);Put(cp+0x154,0u);Put(sp+0x154,0u);
        Put(sp,base+0x400);Put(base+0x430,base+0x300);Put(sp+0xc3c,ss);Put(sp+0xc6c,ss);Put(ss,base+0x600);Put(ss+0xc,soldierData);Put(cs+0xc,soldierData);Put(ss+0x220,sp);
        Put(ss+0x2b4,si);Put(si,0x1425b88u);Put(si+4,inventoryData);Put(si+0x148,ss+0x168);Put(ss+0x168,0x1424718u);
        Put(ss+0x2b8,sa);Put(ss+0x2bc,sa+8);Put(sa,sw);Put(sa+4,gadget);Put(si+0x14c,1u);Put(si+0x154,0u);
        Put(sw,0x14172f0u);Put(sw+8,ss);Put(sw+4,data);Put<std::uint16_t>(sw+0x20,41);Put(sw+0xc,effects);Put(sw+0x10,firing);
        Put(effects+0x10,fd);Put(effects+0x128,sp);Put(effects+0xd4,callback);Put(callback+4,fd);
        Put(gadget,0x14172f0u);Put(gadget+8,ss);Put(gadget+4,gadgetData);Put<std::uint16_t>(gadget+0x20,42);
        for(auto object:{firing,cf0,cf1}){Put(object,base+0x500);Put(object+8,fd);Put(object+12,primary+0x170);}
        Put(firing+0x98,-1);Put(firing+0x74,1.f);
    }
    ReloadStateMemory Memory(){return {this,[](void* context,unsigned at,void* out,std::size_t size){auto& f=*static_cast<Fixture*>(context);
        if(!size||!((at>=0x10000&&std::uint64_t(at)+size<=0x40000)||(at>=base&&std::uint64_t(at)+size<=base+0x10000)))return false;
        if(f.race&&at==si+0x14c&&++f.raceReads==2)f.Put<std::uint16_t>(sw+0x20,43);
        std::memcpy(out,f.At(at),size);return true;
    },[](void*,unsigned at,const char* name){
        return (at==cs&&!std::strcmp(name,"ClientSoldierEntity"))||((at==ss||at==ss2)&&!std::strcmp(name,"ServerSoldierEntity"))||
            (at==inventoryData&&!std::strcmp(name,"WeaponSwitchingData"))||
            ((at==data||at==gadgetData)&&!std::strcmp(name,"SoldierWeaponData"))||
            (at==fd&&!std::strcmp(name,"WeaponFiringData"))||(at==primary&&!std::strcmp(name,"FiringFunctionData"))||
            (at==effects&&!std::strcmp(name,"ServerWeaponFiringEffects"));}};}
    std::optional<PlayerInventorySnapshot> Read(){return ReadPlayerInventory(Memory(),server,state,base,actor);}
};
int IndependentOfSelectionAndReload(){
    Fixture f;const auto before=f.bytes;const auto snapshot=f.Read();CHECK(snapshot&&f.bytes==before);
    CHECK(snapshot->selectedSlot==1&&snapshot->clientSelectedSlot==0&&snapshot->count==2);
    CHECK(snapshot->slots[0].serverFiringVerified&&snapshot->slots[0].clientFiringVerified&&snapshot->slots[0].effectsVerified);
    CHECK(snapshot->slots[0].effectiveCapacity==30&&snapshot->slots[0].fireLogicType==2&&snapshot->slots[0].reloadType==1);
    CHECK(snapshot->slots[1].owned&&!snapshot->slots[1].serverFiringVerified&&Candidate(*snapshot,1));
    CHECK(!Candidate(*snapshot,2));
    f.Put(f.ca,0u);const auto noClient=f.Read();CHECK(noClient&&Candidate(*noClient,0)&&!noClient->slots[0].clientWeapon);
    f.Put(f.si+0x14c,0u);const auto selected=RevalidatePlayerInventory(f.Memory(),f.server,f.state,f.base,*snapshot);CHECK(selected&&selected->selectedSlot==0);
    return 0;
}
int IncarnationAndOwnership(){
    Fixture f;const auto s=f.Read();CHECK(s);const auto c=Candidate(*s,0);CHECK(c);
    CHECK(RevalidatePlayerInventoryCandidate(f.Memory(),f.server,f.state,f.base,*c));
    f.Put<std::uint16_t>(f.sw+0x20,42);CHECK(f.Read());CHECK(!RevalidatePlayerInventoryCandidate(f.Memory(),f.server,f.state,f.base,*c));
    f.Put(f.sw+8,f.ss+4);CHECK(!f.Read());
    {Fixture g;g.Put(g.sa+4,g.sw);CHECK(!g.Read());}
    {Fixture g;g.Put(g.si+0x148,g.ss+0x16c);CHECK(!g.Read());}
    {Fixture g;g.actor.actorGeneration=0;CHECK(!g.Read());}
    {Fixture g;g.Put(g.ss+0x2bc,g.sa+257);CHECK(!g.Read());}
    {Fixture g;g.Put(g.cp+0xc54,g.weak+4);CHECK(!g.Read());}
    return 0;
}
int CapabilityFailuresAndRaces(){
    {Fixture f;f.Put(f.firing+8,f.fd+4);const auto s=f.Read();CHECK(s&&Candidate(*s,0)&&!s->slots[0].serverFiringVerified);}
    {Fixture f;f.Put(f.cw+4,f.gadgetData);const auto s=f.Read();CHECK(s&&s->slots[0].serverFiringVerified&&!s->slots[0].clientWeapon);}
    {Fixture f;f.Put(f.effects+0x128,f.sp+4);const auto s=f.Read();CHECK(s&&!s->slots[0].effectsVerified&&Candidate(*s,0));}
    {Fixture f;f.Put(f.cf1+8,f.fd+4);const auto s=f.Read();CHECK(s&&!s->slots[0].clientFiringVerified&&s->slots[0].serverFiringVerified);}
    {Fixture f;f.race=true;CHECK(!f.Read());}
    return 0;
}
int AuthorityReplacementAndRestoredCapacity(){
    Fixture f;const auto before=f.Read();CHECK(before);
    // The native server pawn changes while client anchor and item addresses,
    // serials and firing copies are reused. Fresh ownership still validates.
    std::memcpy(f.At(f.ss2),f.At(f.ss),0x1000);
    f.Put(f.sp+0xc3c,f.ss2);f.Put(f.sp+0xc6c,f.ss2);
    f.Put(f.si+0x148,f.ss2+0x168);f.Put(f.sw+8,f.ss2);f.Put(f.gadget+8,f.ss2);
    const auto replacement=f.Read();CHECK(replacement&&replacement->anchor==before->anchor);
    CHECK(replacement->slots==before->slots&&!SamePlayerInventoryAuthority(*before,*replacement));
    CHECK(!RevalidatePlayerInventory(f.Memory(),f.server,f.state,f.base,*before));
    // A selection change or client replication gap cannot retire authority.
    auto selection=*replacement;selection.selectedSlot=0;selection.clientInventory+=4;
    selection.slots[0].clientWeapon=0;CHECK(SamePlayerInventoryAuthority(*replacement,selection));
    std::memcpy(f.At(f.si2),f.At(f.si),0x1000);f.Put(f.ss2+0x2b4,f.si2);
    const auto replacedInventory=f.Read();CHECK(replacedInventory&&replacedInventory->slots==replacement->slots);
    CHECK(!SamePlayerInventoryAuthority(*replacement,*replacedInventory));

    // A restored total override must not become a larger magazine feed.
    f.Put(f.firing+0x98,31);const auto saved=f.Read();CHECK(saved);
    const auto& slot=saved->slots[0];CHECK(slot.effectiveCapacity==31&&slot.authoredBaseCapacity==30&&slot.authoredCapacity==30);
    using namespace fvr::interaction;PhysicalWeaponState restored;
    CHECK(restored.Initialize({saved->serverSoldier,saved->anchor.actorGeneration,slot.item,2},
        {PhysicalWeaponFamily::ClosedBoltMagazine,std::uint32_t(*slot.authoredCapacity),std::uint32_t(*slot.authoredCapacity)+1},31,60,true,true));
    CHECK(restored.Snapshot().loaded==31&&restored.Snapshot().feed==30&&restored.Snapshot().looseReserve==60&&restored.Conserved());
    f.Put(f.firing+0x74,1.5f);const auto multiplied=f.Read();CHECK(multiplied&&multiplied->slots[0].authoredCapacity==45);
    f.Put(f.firing+0x74,.333f);const auto fractional=f.Read();CHECK(fractional&&!fractional->slots[0].authoredCapacity);
    return 0;
}
}
int main(){return IndependentOfSelectionAndReload()||IncarnationAndOwnership()||CapabilityFailuresAndRaces()||AuthorityReplacementAndRestoredCapacity();}
