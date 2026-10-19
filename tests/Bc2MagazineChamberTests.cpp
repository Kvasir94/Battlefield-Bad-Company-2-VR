#include "Bc2MagazineChamber.h"
#include "Bc2MagazineEquipmentProfile.h"
#include "Bc2FiringFunctionFields.h"
#include <cstring>
#include <map>
#include "Test.h"
#include <cstdio>
using namespace fvr::bc2;using fvr::interaction::MagazineActionKind;
namespace {
const MagazineChamberKey A{{1,2,3,4},{5,6}},B{{1,2,4,4},{9,7}},A2{{1,2,5,4},{5,8}},Respawned{{1,3,6,4},{5,9}};
// Live XM8 tape (2026-10-08): the pooled reserve read misses around every
// shot, which made the view flip to unknown ("reset" rows). The last count
// keeps the view; only a new key clears it.
int MissedReadKeepsTheView(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::ClosedBolt,false);CHECK(c.LastLoaded()==-1&&!c.View(c.LastLoaded()).known);
 c.Observe(29,1);CHECK(c.LastLoaded()==29);auto v=c.View(c.LastLoaded());CHECK(v.known&&v.loaded==29&&v.chamberLoaded);
 c.Observe(28,2);CHECK(c.View(c.LastLoaded()).loaded==28);
 c.Bind(B,MagazineActionKind::ClosedBolt,false);CHECK(c.LastLoaded()==-1&&!c.View(c.LastLoaded()).known);
 return 0;}
int PartialReloadKeepsChamberNoRack(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::ClosedBolt,false);
 CHECK(!c.View(27).known);c.Observe(27,1);auto v=c.View(27);CHECK(v.known&&v.chamberLoaded&&v.displayAmmo==27&&v.loaded==27);
 c.Eject(27);v=c.View(27);CHECK(v.magazineOut&&v.displayAmmo==1&&v.chamberLoaded);
 c.Observe(30,2); // native transfer while out: no chamber change
 c.Seat(30);v=c.View(30);CHECK(!v.magazineOut&&v.chamberLoaded&&!v.needsRack&&v.displayAmmo==30&&!c.BlocksFire(30));
 return 0;}
int EmptyReloadWithoutRackInputIsReadyOnSeat(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::ClosedBolt,false);c.Observe(3,1);c.Observe(0,2);
 c.Eject(0);auto v=c.View(0);CHECK(v.magazineOut&&v.displayAmmo==0&&!v.chamberLoaded);
 c.Seat(30);v=c.View(30);CHECK(!v.needsRack&&v.chamberLoaded&&!c.BlocksFire(30));
 return 0;}
int EmptyReloadWithRackInputBlocksUntilRack(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::ClosedBolt,true);c.Observe(0,1);c.Eject(0);
 CHECK(!c.BlocksFire(0)); // magazine-out hold is the game's to block
 c.Seat(30);CHECK(c.NeedsRack()&&c.BlocksFire(30)&&!c.BlocksFire(0));
 CHECK(c.View(30).displayAmmo==30&&!c.View(30).chamberLoaded);
 c.Observe(30,2);c.Observe(30,2);CHECK(c.BlocksFire(30)); // repeats and seating never chamber
 CHECK(c.Rack(30)&&!c.NeedsRack()&&!c.BlocksFire(30)&&c.View(30).chamberLoaded);
 CHECK(!c.Rack(30)); // nothing left to clear
 // A rack with the magazine out cannot chamber.
 Bc2MagazineChamber out;out.Bind(A,MagazineActionKind::ClosedBolt,true);out.Observe(0,1);out.Eject(0);CHECK(!out.Rack(0));
 out.Seat(30);CHECK(out.BlocksFire(30));
 return 0;}
int NoActionAndIdentityReset(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::NoAction,true);c.Observe(0,1);c.Eject(0);c.Seat(30);CHECK(!c.NeedsRack()&&!c.BlocksFire(30));
 Bc2MagazineChamber r;r.Bind(A,MagazineActionKind::ClosedBolt,true);r.Observe(0,1);r.Eject(0);r.Seat(30);CHECK(r.BlocksFire(30));
 r.Bind(A,MagazineActionKind::ClosedBolt,true);CHECK(r.BlocksFire(30));     // same identity keeps NeedsRack
 r.Bind(B,MagazineActionKind::ClosedBolt,true);CHECK(!r.BlocksFire(30)&&!r.View(30).known); // weapon/owner change resets
 r.Bind(A,MagazineActionKind::ClosedBolt,true);r.Observe(0,1);r.Eject(0);r.Seat(30);r.Reset();CHECK(!r.BlocksFire(30));
 r.Observe(-1,5);CHECK(!r.View(30).known);
 return 0;}
// KF2 bSlideLockPending: an owed rack comes back with the same gun after a
// switch (new equip generation), not after a respawn or on another gun.
int OwedRackSurvivesSwitch(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::ClosedBolt,true);c.Observe(0,1);c.Eject(0);c.Seat(30);CHECK(c.BlocksFire(30));
 c.Bind(B,MagazineActionKind::ClosedBolt,true);CHECK(!c.BlocksFire(30)&&!c.View(30).known);
 c.Bind(A2,MagazineActionKind::ClosedBolt,true);CHECK(c.BlocksFire(30)&&c.NeedsRack());
 CHECK(c.Rack(30)&&!c.BlocksFire(30));
 c.Bind(B,MagazineActionKind::ClosedBolt,true);c.Bind(A,MagazineActionKind::ClosedBolt,true);CHECK(!c.BlocksFire(30)); // racked state came back
 Bc2MagazineChamber d;d.Bind(A,MagazineActionKind::ClosedBolt,true);d.Observe(0,1);d.Eject(0);d.Seat(30);
 d.Bind(Respawned,MagazineActionKind::ClosedBolt,true);CHECK(!d.BlocksFire(30)&&!d.View(30).known);return 0;}
int OpenBoltShowsZeroOutAndCocksBeforeSeat(){
 Bc2MagazineChamber c;c.Bind(A,MagazineActionKind::OpenBolt,true);c.Observe(20,1);
 c.Eject(20);auto v=c.View(20);CHECK(v.magazineOut&&v.displayAmmo==0&&!v.chamberLoaded);
 c.Seat(30);CHECK(!c.NeedsRack()&&!c.BlocksFire(30)); // a loaded idle open bolt was cocked
 Bc2MagazineChamber e;e.Bind(A,MagazineActionKind::OpenBolt,true);e.Observe(0,1);e.Eject(0);e.Seat(30);CHECK(e.NeedsRack()&&e.BlocksFire(30));
 CHECK(e.Rack(30)&&!e.BlocksFire(30));
 Bc2MagazineChamber pre;pre.Bind(A,MagazineActionKind::OpenBolt,true);pre.Observe(0,1);pre.Eject(0);CHECK(pre.Rack(0)); // cock before seating
 pre.Seat(30);CHECK(!pre.NeedsRack());
 Bc2MagazineChamber auto_;auto_.Bind(A,MagazineActionKind::OpenBolt,false);auto_.Observe(0,1);auto_.Eject(0);auto_.Seat(30);CHECK(!auto_.NeedsRack());
 return 0;}
int CatalogRowsFollowTheDocTable(){
 const auto* xm8=FindMagazineAction("Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh");
 CHECK(xm8&&xm8->kind==MagazineActionKind::ClosedBolt&&xm8->locksOpenOnEmpty&&!xm8->verified);
 const auto* aek=FindMagazineAction("RU_rgl_AEK971_Mesh.res");CHECK(aek&&!aek->locksOpenOnEmpty);
 CHECK(FindMagazineAction("BU_smg_IMIUZI_Mesh")->kind==MagazineActionKind::OpenBolt);
 CHECK(FindMagazineAction("bu_smg_ump_mesh.RES")->kind==MagazineActionKind::NotchLock);
 CHECK(FindMagazineAction("BU_RIF_MK14EBR_MESH")&&FindMagazineAction("BU_shg_USAS-12_Mesh")->locksOpenOnEmpty&&!FindMagazineAction("RU_shg_Saiga12_Mesh")->locksOpenOnEmpty);
 CHECK(!FindMagazineAction("RU_lmg_PKM_Mesh")&&!FindMagazineAction("UL_shg_SPAS-12_Mesh")&&!FindMagazineAction("")&&!FindMagazineAction("Objects/"));
 for(const auto& row:MagazineActionRows())CHECK(!row.verified&&FindMagazineAction(row.mesh)==&row);
 const auto& equipment=Xm8MagazineEquipment();
 CHECK(equipment.actionKind==MagazineActionKind::ClosedBolt&&equipment.locksOpenOnEmpty&&!equipment.actionVerified&&
  equipment.chargingHandle==&Xm8ChargingHandle&&fvr::interaction::ValidChargingHandleProfile(*equipment.chargingHandle));
 CHECK(FindChargingHandle("F2000_sp")&&fvr::interaction::ValidChargingHandleProfile(*FindChargingHandle("F2000_sp"))&&!FindChargingHandle("AEK971_sp"));
 CHECK(equipment.Ready()); // action metadata never gates admission
 return 0;}
struct FakeMemory {
 std::map<std::uint32_t,std::uint32_t> words;bool typesOk=true;
 void Put(std::uint32_t at,std::uint32_t v){words[at]=v;}
 void PutF(std::uint32_t at,float v){std::uint32_t u;std::memcpy(&u,&v,4);words[at]=u;}
 WeaponModeMemory Memory(bool withTypes=true){return {this,[](void* c,std::uint32_t at,void* dst,std::size_t n){auto& f=*static_cast<FakeMemory*>(c);
   const auto it=f.words.find(at);if(n!=4||it==f.words.end())return false;std::memcpy(dst,&it->second,4);return true;},
  withTypes?+[](void* c,std::uint32_t,const char*){return static_cast<FakeMemory*>(c)->typesOk;}:nullptr};}
};
FakeMemory Firing(std::uint32_t reload,std::uint32_t logic,std::int32_t capacity=30){
 namespace o=firing_function_offsets;FakeMemory m;const std::uint32_t swd=0x100000,wfd=0x200000,ffd=0x300000;
 m.Put(swd+o::SoldierWeaponFiring,wfd);m.Put(wfd+o::WeaponFiringFunction,ffd);m.Put(ffd+o::MagazineCapacity,std::uint32_t(capacity));
 m.Put(ffd+o::ReloadType,reload);m.Put(ffd+o::FireLogicType,logic);m.PutF(ffd+o::ReloadTime,3.2f);m.PutF(ffd+o::BoltActionTime,.9f);m.PutF(ffd+o::BoltActionDelay,.1f);
 return m;}
int EngineFiringDataFirstCatalogFallback(){
 auto rifle=Firing(1,2);auto f=ReadFiringFunctionFields(rifle.Memory(),0x100000);
 CHECK(f&&f->function==0x300000&&f->magazineCapacity==30&&f->reloadType==FiringReloadType::Magazine&&f->fireLogic==FiringLogicType::Automatic&&Near(f->reloadTime,3.2f));
 CHECK(!ReadFiringFunctionFields(rifle.Memory(),0)&&!ReadFiringFunctionFields(rifle.Memory(),0x100004));
 rifle.typesOk=false;CHECK(!ReadFiringFunctionFields(rifle.Memory(),0x100000)&&ReadFiringFunctionFields(rifle.Memory(false),0x100000));
 auto bad=Firing(2,2);CHECK(!ReadFiringFunctionFields(bad.Memory(),0x100000));
 bad=Firing(1,4);CHECK(!ReadFiringFunctionFields(bad.Memory(),0x100000));
 bad=Firing(1,2,0);CHECK(!ReadFiringFunctionFields(bad.Memory(),0x100000));
 bad=Firing(1,2);bad.PutF(0x300000+firing_function_offsets::BoltActionTime,-1);CHECK(!ReadFiringFunctionFields(bad.Memory(),0x100000));
 const auto* uzi=FindMagazineAction("BU_smg_IMIUZI_Mesh");
 auto r=ResolveMagazineAction(f,uzi);CHECK(r.kind==MagazineActionKind::OpenBolt&&r.source==MagazineActionSource::Engine&&!r.cyclesPerShot);
 r=ResolveMagazineAction(std::nullopt,uzi);CHECK(r.kind==MagazineActionKind::OpenBolt&&r.source==MagazineActionSource::Catalog);
 r=ResolveMagazineAction(std::nullopt,nullptr);CHECK(r.kind==MagazineActionKind::ClosedBolt&&r.source==MagazineActionSource::Default);
 auto bolt=Firing(1,1,5);const auto boltFields=ReadFiringFunctionFields(bolt.Memory(),0x100000);
 r=ResolveMagazineAction(boltFields,FindMagazineAction("US_rgl_XM8_Mesh"));CHECK(r.cyclesPerShot&&r.kind==MagazineActionKind::NoAction&&!r.locksOpenOnEmpty);
 auto pump=Firing(0,1,8);r=ResolveMagazineAction(ReadFiringFunctionFields(pump.Memory(),0x100000),nullptr);CHECK(r.singleRoundReload&&r.cyclesPerShot);
 return 0;}
}
int main(){
 if(MissedReadKeepsTheView()||PartialReloadKeepsChamberNoRack()||EmptyReloadWithoutRackInputIsReadyOnSeat()||EmptyReloadWithRackInputBlocksUntilRack()||
    NoActionAndIdentityReset()||OwedRackSurvivesSwitch()||OpenBoltShowsZeroOutAndCocksBeforeSeat()||CatalogRowsFollowTheDocTable()||EngineFiringDataFirstCatalogFallback())return 1;
 std::puts("Bc2MagazineChamber: 9 chamber/catalog/engine-field groups passed.");return 0;
}
