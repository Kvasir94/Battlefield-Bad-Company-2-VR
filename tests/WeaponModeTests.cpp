#include "Test.h"
#include "Bc2WeaponMode.h"
#include "Bc2SelectedMeshes1p.h"
#include <array>
#include <cstring>
#include <string>
#include <unordered_map>
using namespace fvr;
namespace {
struct Fixture {
    std::array<std::byte,0x10000> bytes{};
    std::unordered_map<unsigned,std::string> types;
    unsigned currentReads=0;bool changeCurrent=false;
    static constexpr unsigned soldier=0x10000,inventory=0x11000,items=0x12000,
        rifle=0x13000,launcher=0x13200,rifleData=0x14000,launcherData=0x14400,
        persistent=0x15000,switching=0x17000,map=0x18000,forward=0x19000,backward=0x19100;
    void Word(unsigned at,unsigned value){std::memcpy(bytes.data()+at-0x10000,&value,4);}
    void Text(unsigned at,const char* value){std::memcpy(bytes.data()+at-0x10000,value,std::strlen(value)+1);}
    Fixture(){
        types={{soldier,"ClientSoldierEntity"},{rifleData,"SoldierWeaponData"},
            {launcherData,"SoldierWeaponData"},{persistent,"PersistentWeapon"},{switching,"WeaponSwitchingData"}};
        Word(soldier+0x248,inventory);Word(soldier+0x260,items);Word(soldier+0x264,items+9*4);
        Word(inventory+0x14c,1);Word(inventory+4,switching);
        Word(items+4,rifle);Word(items+12,launcher);
        Word(rifle+4,rifleData);Word(launcher+4,launcherData);
        Word(rifleData+0xc,0x16000);Text(0x16000,"XM8_sp_s");
        Word(launcherData+0xc,0x16100);Text(0x16100,"40mmgl");
        Word(rifleData+0x40,0x16200);Text(0x16200,"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped");
        Word(launcherData+0x40,0x16300);Text(0x16300,"Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM320_Scoped");
        Word(rifleData+0x64,persistent);Word(launcherData+0x64,persistent);
        Word(persistent+0x40,0x16400);Text(0x16400,"sp_xm8_s");
        Word(switching+0x18,map);Word(switching+0x1c,map+48);
        Word(map+8,forward);Word(map+12,forward+8);Word(map+16,33);Word(map+20,1);
        Word(forward,3);Word(forward+4,2);
        Word(map+24+8,backward);Word(map+24+12,backward+4);Word(map+24+16,36);Word(map+24+20,3);
        Word(backward,1);
    }
    bc2::WeaponModeMemory Memory(){
        return {this,[](void* ctx,unsigned at,void* out,std::size_t n){
            auto& f=*static_cast<Fixture*>(ctx);
            if(at<0x10000||std::uint64_t(at)+n>0x20000)return false;
            if(at==inventory+0x14c&&f.changeCurrent&&++f.currentReads>1){unsigned changed=3;std::memcpy(out,&changed,4);return true;}
            std::memcpy(out,f.bytes.data()+at-0x10000,n);return true;
        },[](void* ctx,unsigned at,const char* expected){
            const auto& f=*static_cast<Fixture*>(ctx);const auto found=f.types.find(at);
            return found!=f.types.end()&&found->second==expected;
        }};
    }
    bc2::SelectedMeshesSnapshot Visual(bool secondary=false){
        bc2::SelectedMeshesSnapshot s;s.owner={0x10100,soldier,7,secondary?launcher:rifle,8,9,10};
        s.sequence=1;s.observedNs=1000000000;s.deadlineNs=1200000000;s.inventory=inventory;s.selectedSlot=secondary?3:1;
        s.weaponData=secondary?launcherData:rifleData;s.stateTypeInfo=0x17100;s.meshTypeInfo=0x17200;
        const auto text=[](auto& dst,const char* value){std::memcpy(dst.data(),value,std::strlen(value)+1);};
        text(s.weaponName,secondary?"40mmgl":"XM8_sp_s");
        s.stateCount=1;s.soleConfiguredArray=0x17300;s.states[0].array=s.soleConfiguredArray;s.states[0].state=0x17400;s.states[0].count=1;
        auto& mesh=s.states[0].meshes[0];mesh.address=0x17500;mesh.namePointer=0x17600;mesh.typeInfo=s.meshTypeInfo;
        text(mesh.assetPath,"Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh");
        Word(inventory+0x14c,s.selectedSlot);return s;
    }
};
int VisualFamilyTests(){
    constexpr std::int64_t now=1100000000;
    {Fixture f;const auto s=f.Visual(true);const auto before=f.bytes;
        const auto family=bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now);
        CHECK(family&&family->selectedSecondary&&family->persistent==f.persistent&&family->family==&bc2::WeaponModeFamilies[0]);
        CHECK(f.bytes==before);}
    {Fixture f;const auto s=f.Visual();const auto family=bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now);
        CHECK(family&&!family->selectedSecondary);}
    // Configuration/geometry admission survives unavailable native switching
    // maps. It still cannot manufacture a native mode command.
    {Fixture f;const auto s=f.Visual(true);f.Word(f.switching+0x18,0);f.Word(f.switching+0x1c,0);
        CHECK(bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));}
    {Fixture f;const auto s=f.Visual();f.Word(f.items+8,0x13500);f.Word(f.forward,2);f.Word(f.forward+4,3);
        CHECK(bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.rifle));}
    {Fixture f;const auto s=f.Visual(true);f.Text(0x16300,"Objects/Weapons/Handheld/OtherLauncher");
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;const auto s=f.Visual(true);f.Text(0x16400,"sp_other");CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;const auto s=f.Visual(true);f.Word(f.launcherData+0x64,f.persistent+4);f.types[f.persistent+4]="PersistentWeapon";
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;auto s=f.Visual(true);std::strcpy(s.states[0].meshes[0].assetPath.data(),"Objects/Weapons/Handheld/OtherMesh");
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;auto s=f.Visual(true);s.states[0].count=2;s.states[0].meshes[1]=s.states[0].meshes[0];
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;auto s=f.Visual(true);++s.weaponData;CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;auto s=f.Visual(true);++s.owner.space;auto owner=s.owner;--owner.space;
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,owner,now));}
    {Fixture f;const auto s=f.Visual(true);CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,s.deadlineNs));}
    {Fixture f;const auto s=f.Visual();f.changeCurrent=true;CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    // A duplicated selected pointer or same-family counterpart is ambiguous.
    {Fixture f;const auto s=f.Visual(true);f.Word(f.items+20,f.launcher);CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    {Fixture f;const auto s=f.Visual(true);f.Word(f.items+20,f.rifle);CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    // A foreign item sharing the short rifle name cannot invalidate this exact
    // selected family or lend it a native switch command.
    {Fixture f;const auto s=f.Visual(true);f.Word(f.items+20,0x13500);f.Word(0x13504,0x14600);f.types[0x14600]="SoldierWeaponData";
        f.Word(0x1460c,0x16500);f.Text(0x16500,"XM8_sp_s");f.Word(0x14640,0x16600);f.Text(0x16600,"Objects/Weapons/Handheld/OtherXM8");
        CHECK(bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));}
    {Fixture f;auto s=f.Visual(true);f.Word(f.items+20,0x13500);f.Word(0x13504,0x14600);f.types[0x14600]="SoldierWeaponData";
        f.Word(0x1460c,0x16500);f.Text(0x16500,"40mmgl");f.Word(0x14640,0x16600);f.Text(0x16600,"Objects/Weapons/Handheld/OtherLauncher");
        f.Word(0x14664,f.persistent);
        CHECK(bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.launcher));
        // Selecting the unrelated identically named launcher must not borrow
        // the configured scoped-XM8 family present elsewhere in this inventory.
        f.Word(f.inventory+0x14c,5);s.owner.weapon=0x13500;s.weaponData=0x14600;s.selectedSlot=5;
        CHECK(!bc2::ResolveWeaponVisualFamily(f.Memory(),s,s.owner,now));}
    return 0;
}
}
int main(){
    CHECK(VisualFamilyTests()==0);
    Fixture f;const auto original=f.bytes;
    auto command=bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.rifle);
    CHECK(command&&command->action==33&&command->targetSlot==3&&command->targetWeapon==f.launcher);
    CHECK(command->inventory==f.inventory&&command->persistent==f.persistent&&f.bytes==original);
    f.Word(f.inventory+0x14c,3);
    command=bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.launcher);
    CHECK(command&&command->action==36&&command->targetSlot==1&&command->targetWeapon==f.rifle);
    CHECK(!bc2::ResolveWeaponMode(f.Memory(),f.soldier,f.rifle));
    {Fixture g;g.types[g.launcherData]="WeaponData";CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.launcherData+0x64,g.persistent+4);g.types[g.persistent+4]="PersistentWeapon";
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16400,"sp_other");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16300,"Objects/Weapons/Handheld/OtherLauncher");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Text(0x16100,"40mmgl_other");CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    // The first populated map target determines the action. Do not borrow a
    // later matching target after a different available item or pseudo-slot.
    {Fixture g;g.Word(g.items+8,0x13500);g.Word(g.forward,2);g.Word(g.forward+4,3);
        CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.forward,2);g.Word(g.forward+4,3);
        CHECK(bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));} // Absent slot skipped.
    {Fixture g;g.Word(g.forward,9);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.backward,0);g.Word(g.items,0x13500);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+16,38);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+24+16,33);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+20,0);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+24);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+49);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.switching+0x1c,g.map+129*24);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.map+12,g.forward+45);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.soldier+0x264,g.items+260);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.items+20,g.rifle);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.changeCurrent=true;CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;g.Word(g.soldier+0x248,0xfffffff0);CHECK(!bc2::ResolveWeaponMode(g.Memory(),g.soldier,g.rifle));}
    {Fixture g;auto mem=g.Memory();mem.type=nullptr;CHECK(!bc2::ResolveWeaponMode(mem,g.soldier,g.rifle));}
    return 0;
}
