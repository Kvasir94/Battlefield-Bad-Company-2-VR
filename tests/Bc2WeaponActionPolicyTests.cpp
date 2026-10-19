#include "../src/games/bc2/Bc2PhysicalWeaponRuntime.h"
#include "Test.h"
using namespace fvr::bc2::physicalWeaponRuntime;
int main(){
    ActionAdmission a;
    CHECK(a.BlocksSelection()); // Missing publication cannot grant a switch.
    a.weapon=WeaponAdmission::Unmanaged;CHECK(!a.BlocksSelection());
    a.weapon=WeaponAdmission::Initializing;CHECK(a.BlocksSelection());
    a.weapon=WeaponAdmission::Managed;CHECK(!a.BlocksSelection());
    a.pendingCommand=true;CHECK(a.BlocksSelection());
    a.pendingCommand=false;CHECK(!a.BlocksSelection()); // Receipt drained.
    a.nativeBusy=true;a.nativePhase=NativeReloadPhase::Running;CHECK(a.BlocksSelection());
    a.nativeBusy=false;a.suspended=true;CHECK(!a.BlocksSelection()); // Settled quarantine permits leaving this gun.
    a.pendingCommand=true;CHECK(a.BlocksSelection()); // Suspended cleanup still settles first.
    a.pendingCommand=false;a.nativeBusy=true;CHECK(a.BlocksSelection()); // Native transaction still owns selection.
    a.nativeBusy=false;CHECK(!a.BlocksSelection());
    fvr::interaction::PhysicalWeaponSnapshot committed;
    committed.initialized=true;committed.loaded=11;committed.looseReserve=60;
    committed.chamber=1;committed.pumpOwed=true; // Agreement never resets mechanics.
    CHECK(SuspendedWeaponAgrees(committed,11,60,true,false));
    CHECK(!SuspendedWeaponAgrees(committed,10,60,true,false));
    CHECK(!SuspendedWeaponAgrees(committed,11,61,true,false));
    CHECK(!SuspendedWeaponAgrees(committed,11,60,false,false));
    CHECK(!SuspendedWeaponAgrees(committed,11,60,true,true));
    committed.initialized=false;CHECK(!SuspendedWeaponAgrees(committed,11,60,true,false));
    CHECK(NativeReloadSettlementAgrees(31,30,31,30,true,false));
    CHECK(!NativeReloadSettlementAgrees(30,30,31,30,true,false));
    CHECK(!NativeReloadSettlementAgrees(31,31,31,30,true,false));
    CHECK(!NativeReloadSettlementAgrees(31,30,31,30,false,false));
    CHECK(!NativeReloadSettlementAgrees(31,30,31,30,true,true));
    // A completed gesture interrupted by a weapon switch remains observable
    // while newer commands are submitted and consumed by another producer.
    CommandReceiptStore results;
    CHECK(results.Reserve(1));CHECK(results.Complete(1,CommandStatus::Applied,7,3));
    CHECK(results.Reserve(2));CHECK(results.Take(2)->status==CommandStatus::Pending);
    for(std::uint64_t id=3;id<CommandReceiptStore::Capacity*4;++id){
        CHECK(results.Reserve(id));CHECK(results.Complete(id,CommandStatus::Rejected));
        const auto result=results.Take(id);CHECK(result&&result->id==id&&result->status==CommandStatus::Rejected);
    }
    const auto old=results.Take(1);CHECK(old&&old->magazine==7&&old->droppedMagazine==3);
    CHECK(!results.Take(1)); // Terminal outcomes are consumed once.
    CHECK(results.Complete(2,CommandStatus::Applied));CHECK(results.Take(2)->status==CommandStatus::Applied);
    for(std::uint64_t id=1;id<=CommandReceiptStore::Capacity;++id)CHECK(results.Reserve(id));
    CHECK(!results.Reserve(CommandReceiptStore::Capacity+1));
    CHECK(results.Complete(1,CommandStatus::Rejected));CHECK(results.Take(1));
    CHECK(results.Reserve(CommandReceiptStore::Capacity+1));
    CHECK(NormalTransferCountsAdmitted(29,100,30,1));
    CHECK(NormalTransferCountsAdmitted(30,100,30,1)); // Zero magazine deficit is harmless.
    CHECK(!NormalTransferCountsAdmitted(31,100,30,1)); // Would return ammo to reserve.
    CHECK(NormalTransferCountsAdmitted(5,1,6,0));
    CHECK(!NormalTransferCountsAdmitted(6,1,6,0)); // Native shell transfer lacks this guard.
    CHECK(!NormalTransferCountsAdmitted(1,UINT32_MAX,6,0));
    CHECK(!NormalTransferCountsAdmitted(1,1,6,2)); // No invented special-family contract.
    return 0;
}
