#include "fvr/interaction/PhysicalWeaponState.h"
#include "fvr/interaction/PhysicalWeaponReadiness.h"
#include <cstdio>
using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;}}while(false)
namespace {
constexpr PhysicalWeaponIdentity Owner{1,2,3,4};
using K=PhysicalWeaponCommandKind;
using R=PhysicalWeaponResultCode;
PhysicalWeaponCommand Command(const PhysicalWeaponState& state,K kind,std::uint64_t magazine=0,std::uint32_t rounds=0) {
    PhysicalWeaponCommand c;c.identity=Owner;c.id=state.NextCommandId();c.kind=kind;c.magazine=magazine;c.rounds=rounds;return c;
}
}
int main() {
    // KF2 pouch-first reload: drop the installed partial and offer a fresh
    // full magazine in one authority operation, keeping the live chamber.
    PhysicalWeaponState pouch;
    CHECK(pouch.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,90,true,true));
    const auto installed=pouch.Snapshot().insertedMagazine;
    auto draw=Command(pouch,K::DrawMagazine);
    const auto offered=pouch.Apply(draw);
    CHECK(offered.Applied()&&offered.magazine&&offered.droppedMagazine==installed);
    CHECK(pouch.Magazine(installed)->location==PhysicalMagazineLocation::Dropped&&pouch.Magazine(installed)->rounds==10);
    CHECK(pouch.Magazine(offered.magazine)->location==PhysicalMagazineLocation::Held&&pouch.Magazine(offered.magazine)->rounds==30);
    CHECK(pouch.Snapshot().loaded==1&&pouch.Snapshot().looseReserve==60&&pouch.CanFire()&&pouch.Conserved());
    CHECK(pouch.Apply(draw).code==R::Duplicate&&pouch.Snapshot().looseReserve==60);
    CHECK(pouch.Apply(Command(pouch,K::DrawMagazine)).code==R::Unavailable); // No second held magazine.
    CHECK(pouch.Apply(Command(pouch,K::RetainMagazine,offered.magazine)).Applied());
    const auto recovered=pouch.Apply(Command(pouch,K::DrawMagazine));
    CHECK(recovered.Applied()&&recovered.magazine==offered.magazine&&pouch.Magazine(recovered.magazine)->rounds==30);
    CHECK(pouch.Snapshot().looseReserve==60&&pouch.Conserved()); // A retained full magazine needs no new debit.

    // Empty pouch-start seating cannot manufacture a chamber or clear its
    // lock. Interrupted draws retain their ammunition for a later attempt.
    PhysicalWeaponState emptyPouch;
    CHECK(emptyPouch.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31,true},1,60,true,true));
    CHECK(emptyPouch.ReconcileAcceptedShots(Owner,1,0,60).Applied());
    const auto emptyOffer=emptyPouch.Apply(Command(emptyPouch,K::DrawMagazine));
    CHECK(emptyOffer.Applied()&&emptyPouch.Snapshot().actionLocked&&!emptyPouch.Snapshot().chamber);
    CHECK(emptyPouch.Apply(Command(emptyPouch,K::SeatMagazine,emptyOffer.magazine)).Applied());
    CHECK(!emptyPouch.CanFire()&&emptyPouch.Snapshot().needsRack&&emptyPouch.Snapshot().actionLocked);
    CHECK(emptyPouch.Apply(Command(emptyPouch,K::CloseAction)).Applied()&&emptyPouch.CanFire()&&emptyPouch.Conserved());

    // Loose reserve supplies a full replacement before a retained partial.
    PhysicalWeaponState partialPouch;
    CHECK(partialPouch.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,60,true,true));
    const auto partial=partialPouch.Snapshot().insertedMagazine;
    CHECK(partialPouch.Apply(Command(partialPouch,K::UnseatMagazine,partial)).Applied());
    CHECK(partialPouch.Apply(Command(partialPouch,K::RetainMagazine,partial)).Applied());
    const auto full=partialPouch.Apply(Command(partialPouch,K::DrawMagazine));
    CHECK(full.Applied()&&full.magazine!=partial&&partialPouch.Magazine(full.magazine)->rounds==30);
    CHECK(partialPouch.Magazine(partial)->rounds==10&&partialPouch.Magazine(partial)->location==PhysicalMagazineLocation::Retained);
    CHECK(partialPouch.Conserved());
    PhysicalWeaponState scarcePouch;
    CHECK(scarcePouch.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,5,true,true));
    const auto scarce=scarcePouch.Snapshot().insertedMagazine;
    CHECK(scarcePouch.Apply(Command(scarcePouch,K::UnseatMagazine,scarce)).Applied());
    CHECK(scarcePouch.Apply(Command(scarcePouch,K::RetainMagazine,scarce)).Applied());
    const auto toppedUp=scarcePouch.Apply(Command(scarcePouch,K::DrawMagazine));
    CHECK(toppedUp.Applied()&&toppedUp.magazine==scarce&&scarcePouch.Magazine(scarce)->rounds==15);
    CHECK(scarcePouch.Snapshot().looseReserve==0&&scarcePouch.Snapshot().chamber==1&&scarcePouch.Conserved());

    PhysicalWeaponState noSupply;
    CHECK(noSupply.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,0,false,true));
    const auto noOffer=noSupply.Apply(Command(noSupply,K::DrawMagazine));
    CHECK(noOffer.Applied()&&!noOffer.magazine&&noOffer.droppedMagazine&&!noSupply.Snapshot().insertedMagazine&&noSupply.Conserved());
    CHECK(noSupply.Apply(Command(noSupply,K::DrawMagazine)).code==R::Unavailable);

    // A tactical exchange retains the chamber and the old magazine's exact
    // partial count. Copy/commit makes a failed publication rollback trivial.
    PhysicalWeaponState gun;
    CHECK(gun.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,90,true,true));
    CHECK(ReadWeaponReadiness(gun.Snapshot()).cue==WeaponReadyCue::Ready);
    CHECK(WeaponReadinessAllowsFireInput(ReadWeaponReadiness(gun.Snapshot())));
    CHECK(!WeaponReadinessAllowsFireInput(ReadWeaponReadiness(gun.Snapshot(),false,true)));
    CHECK(!WeaponReadinessAllowsFireInput(ReadWeaponReadiness(gun.Snapshot(),false,false,true)));
    CHECK(!WeaponReadinessAllowsFireInput(ReadWeaponReadiness(gun.Snapshot(),true)));
    CHECK(ReadWeaponReadiness(gun.Snapshot(),false,true).cue==WeaponReadyCue::Settling);
    CHECK(ReadWeaponReadiness(gun.Snapshot(),true).cue==WeaponReadyCue::Unavailable);
    WeaponReadinessTransitions cues;
    CHECK(cues.Update(Owner,WeaponReadyCue::Ready));
    CHECK(!cues.Update(Owner,WeaponReadyCue::Ready)); // Repeated copy/notification.
    const auto old=gun.Snapshot().insertedMagazine;
    auto unseat=Command(gun,K::UnseatMagazine,old);
    auto candidate=gun;
    CHECK(candidate.Apply(unseat).Applied());
    CHECK(gun.Snapshot().loaded==11&&candidate.Snapshot().loaded==1);
    gun=candidate;
    CHECK(gun.Magazine(old)->rounds==10&&gun.CanFire()&&gun.Conserved());
    const auto magOut=ReadWeaponReadiness(gun.Snapshot());
    CHECK(magOut.cue==WeaponReadyCue::MagazineOut&&magOut.physicalCanFire);
    CHECK(cues.Update(Owner,magOut.cue));
    CHECK(!cues.Update(Owner,magOut.cue));
    CHECK(gun.Apply(unseat).code==R::Duplicate);
    auto collision=unseat;collision.kind=K::DropMagazine;
    CHECK(gun.Apply(collision).code==R::IdCollision);
    CHECK(gun.Apply(Command(gun,K::RetainMagazine,old)).Applied());
    auto acquire=Command(gun,K::AcquireMagazine);
    const auto acquired=gun.Apply(acquire);
    CHECK(acquired.Applied()&&acquired.magazine!=old&&gun.Snapshot().looseReserve==60);
    CHECK(gun.Apply(acquire).code==R::Duplicate&&gun.Snapshot().looseReserve==60);
    CHECK(gun.Apply(Command(gun,K::SeatMagazine,acquired.magazine)).Applied());
    CHECK(gun.Snapshot().loaded==31&&gun.Snapshot().chamber==1&&gun.Conserved());
    CHECK(gun.Apply(unseat).code==R::Stale);

    // Accepted automatic shots consume the authority delta once. The same
    // receipt cannot consume a second shot, nor can an ammo increase mint one.
    CHECK(gun.ReconcileAcceptedShots(Owner,1,28,60).shots==3);
    CHECK(gun.ReconcileAcceptedShots(Owner,1,28,60).code==R::Duplicate);
    CHECK(gun.ReconcileAcceptedShots(Owner,1,27,60).code==R::IdCollision);
    CHECK(gun.ReconcileAcceptedShots(Owner,2,29,60).code==R::UnexpectedNativeState);
    CHECK(gun.Snapshot().loaded==28&&gun.Conserved());
    auto wrong=Owner;++wrong.weaponGeneration;
    CHECK(gun.ReconcileAcceptedShots(wrong,3,27,60).code==R::IdentityMismatch);
    CHECK(gun.Snapshot().loaded==28);

    // One retained chamber shot while the magazine is absent; reserve and old
    // magazines survive. Seating after it requires an explicit action.
    CHECK(gun.Apply(Command(gun,K::UnseatMagazine,acquired.magazine)).Applied());
    CHECK(gun.ReconcileAcceptedShots(Owner,3,0,60).shots==1);
    CHECK(!gun.CanFire()&&gun.Snapshot().chamber==0);
    CHECK(gun.Apply(Command(gun,K::SeatMagazine,acquired.magazine)).Applied());
    CHECK(!gun.CanFire()&&gun.Snapshot().needsRack);
    CHECK(gun.ReconcileAcceptedShots(Owner,4,26,60).code==R::FireBlocked);
    CHECK(gun.Apply(Command(gun,K::CycleAction)).Applied());
    CHECK(gun.CanFire()&&gun.Snapshot().loaded==27&&gun.Conserved());

    // Explicit pickups/corrections preserve detached magazines and conservation.
    const auto grant=gun.NextCommandId();
    CHECK(gun.ObserveExternalAmmo(Owner,grant,27,90).Applied());
    CHECK(gun.ObserveExternalAmmo(Owner,grant,27,90).code==R::Duplicate);
    CHECK(gun.Magazine(old)->rounds==10&&gun.Conserved());
    CHECK(gun.Apply(Command(gun,K::RecoverMagazine,old)).Applied());
    CHECK(gun.Apply(Command(gun,K::DropMagazine,old)).Applied());
    CHECK(gun.Apply(Command(gun,K::ForgetDroppedMagazine,old)).Applied());
    CHECK(!gun.Magazine(old)&&gun.Snapshot().discarded==10&&gun.Conserved());
    CHECK(gun.ObserveExternalAmmo(Owner,gun.NextCommandId(),20,80).Applied());
    CHECK(gun.Snapshot().discarded==27&&gun.Conserved());

    // Open bolts have no invented chamber; cocking before seating is retained.
    PhysicalWeaponState open;
    CHECK(open.Initialize(Owner,{PhysicalWeaponFamily::OpenBoltMagazine,30,30},0,30,false,false));
    CHECK(open.Apply(Command(open,K::UnseatMagazine,open.Snapshot().insertedMagazine)).Applied());
    CHECK(open.Apply(Command(open,K::CycleAction)).Applied());
    const auto openMag=open.Apply(Command(open,K::AcquireMagazine)).magazine;
    CHECK(open.Apply(Command(open,K::SeatMagazine,openMag)).Applied());
    CHECK(open.CanFire()&&open.Snapshot().chamber==0);
    CHECK(open.ReconcileAcceptedShots(Owner,1,25,0).shots==5&&open.Conserved());

    // Pump state survives value copies (the adapter's stow/equip persistence).
    PhysicalWeaponState pump;
    CHECK(pump.Initialize(Owner,{PhysicalWeaponFamily::TubePump,7,8},3,10,true,true));
    CHECK(pump.ReconcileAcceptedShots(Owner,1,2,10).shots==1);
    auto stowed=pump;
    CHECK(stowed.Snapshot().pumpOwed&&!stowed.CanFire());
    CHECK(PhysicalPumpActionOwed(stowed.Snapshot()));
    CHECK(!WeaponReadinessAllowsFireInput(ReadWeaponReadiness(stowed.Snapshot())));
    CHECK(stowed.ReconcileAcceptedShots(Owner,2,1,10).code==R::FireBlocked);
    CHECK(stowed.Apply(Command(stowed,K::InsertRound)).Applied());
    CHECK(!stowed.CanFire()&&stowed.Snapshot().loaded==3);
    CHECK(stowed.Apply(Command(stowed,K::CycleAction)).Applied());
    CHECK(stowed.CanFire()&&!stowed.Snapshot().pumpOwed&&stowed.Snapshot().loaded==3&&stowed.Conserved());
    CHECK(WeaponReadinessAllowsFireInput(ReadWeaponReadiness(stowed.Snapshot())));
    CHECK(!PhysicalPumpActionOwed(stowed.Snapshot()));
    CHECK(stowed.Apply(Command(stowed,K::CycleAction)).code==R::Unavailable);
    CHECK(stowed.Snapshot().loaded==3&&stowed.Snapshot().discarded==0&&stowed.Conserved());

    // The last fired shell still owes one clearing stroke. Once cleared,
    // an empty tube cannot be pumped until a new shell has been inserted.
    PhysicalWeaponState lastShell;
    CHECK(lastShell.Initialize(Owner,{PhysicalWeaponFamily::TubePump,7,8},1,10,true,true));
    CHECK(!PhysicalPumpActionOwed(lastShell.Snapshot()));
    CHECK(lastShell.ReconcileAcceptedShots(Owner,1,0,10).shots==1);
    CHECK(PhysicalPumpActionOwed(lastShell.Snapshot()));
    CHECK(lastShell.Apply(Command(lastShell,K::CycleAction)).Applied());
    CHECK(!PhysicalPumpActionOwed(lastShell.Snapshot())&&!lastShell.CanFire());
    CHECK(lastShell.Apply(Command(lastShell,K::CycleAction)).code==R::Unavailable);
    CHECK(lastShell.Apply(Command(lastShell,K::InsertRound)).Applied());
    CHECK(PhysicalPumpActionOwed(lastShell.Snapshot())&&!lastShell.CanFire());
    auto reselected=lastShell;
    CHECK(reselected.Apply(Command(reselected,K::CycleAction)).Applied());
    CHECK(reselected.CanFire()&&!PhysicalPumpActionOwed(reselected.Snapshot()));
    CHECK(reselected.Snapshot().loaded==1&&reselected.Snapshot().discarded==0&&reselected.Conserved());

    // Unknown families stay unsupported, and finite magazine storage cannot
    // mint ammo or silently delete retained magazines when the table is full.
    PhysicalWeaponState invalid;
    CHECK(!invalid.Initialize(Owner,{},0,0,false,false));
    PhysicalWeaponState finite;
    CHECK(finite.Initialize(Owner,{PhysicalWeaponFamily::NoActionMagazine,1,1},1,100,false,true));
    for(std::size_t i=1;i<PhysicalWeaponState::MaxMagazines;++i)CHECK(finite.Apply(Command(finite,K::AcquireMagazine)).Applied());
    const auto before=finite.Snapshot();
    CHECK(finite.Apply(Command(finite,K::AcquireMagazine)).code==R::Capacity);
    CHECK(finite.Snapshot().looseReserve==before.looseReserve&&finite.Conserved());
    CHECK(finite.BeginNativeReload(Owner,1).code==R::Unavailable); // Held cleanup is still outstanding.

    // Explicit native reload owns the ledger until exact finite completion.
    // Replays, another incarnation and ordinary commands cannot refill twice.
    PhysicalWeaponState native;
    CHECK(native.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,90,false,true));
    const auto destination=native.Snapshot().insertedMagazine;
    CHECK(native.BeginNativeReload(wrong,1).code==R::IdentityMismatch);
    CHECK(native.BeginNativeReload(Owner,0).code==R::Invalid);
    CHECK(native.BeginNativeReload(Owner,1).Applied());
    CHECK(native.NativeReloadBusy()&&native.NativeReloadToken()==1&&!native.CanFire());
    CHECK(native.BeginNativeReload(Owner,1).code==R::Duplicate);
    CHECK(native.BeginNativeReload(Owner,2).code==R::Unavailable);
    CHECK(native.Apply(Command(native,K::AcquireMagazine)).code==R::Unavailable);
    CHECK(native.ReconcileAcceptedShots(Owner,1,0,90).code==R::FireBlocked);
    CHECK(native.FinishNativeReload(wrong,1,31,59,true).code==R::IdentityMismatch);
    CHECK(native.FinishNativeReload(Owner,2,31,59,true).code==R::Stale);
    CHECK(native.FinishNativeReload(Owner,1,31,90,true).code==R::UnexpectedNativeState);
    CHECK(native.FinishNativeReload(Owner,1,32,58,true).code==R::UnexpectedNativeState);
    CHECK(native.FinishNativeReload(Owner,1,0,PhysicalWeaponState::MaxReserve+1,true).code==R::UnexpectedNativeState);
    CHECK(native.NativeReloadBusy()&&native.Snapshot().loaded==0&&native.Conserved());
    CHECK(native.FinishNativeReload(Owner,1,31,59,true).Applied());
    CHECK(!native.NativeReloadBusy()&&native.CanFire()&&!native.Snapshot().needsRack);
    CHECK(native.Snapshot().insertedMagazine==destination&&native.Snapshot().chamber==1&&native.Snapshot().feed==30);
    CHECK(native.Snapshot().admitted==90&&native.Snapshot().discarded==0&&native.Conserved());
    CHECK(native.FinishNativeReload(Owner,1,31,59,true).code==R::Duplicate);
    CHECK(native.FinishNativeReload(Owner,1,30,60,true).code==R::IdCollision);
    CHECK(native.BeginNativeReload(Owner,1).code==R::Stale);

    // Pooled pocket ammunition is removed from retained magazines exactly
    // once. An absent well gains an empty shell; dropped magazines survive.
    PhysicalWeaponState pocket;
    CHECK(pocket.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,90,true,true));
    const auto ground=pocket.Snapshot().insertedMagazine;
    CHECK(pocket.Apply(Command(pocket,K::UnseatMagazine,ground)).Applied());
    CHECK(pocket.Apply(Command(pocket,K::DropMagazine,ground)).Applied());
    const auto retained=pocket.Apply(Command(pocket,K::AcquireMagazine)).magazine;
    CHECK(pocket.Apply(Command(pocket,K::RetainMagazine,retained)).Applied());
    CHECK(pocket.BeginNativeReload(Owner,1).Applied());
    CHECK(pocket.Snapshot().chamber==1&&pocket.Snapshot().loaded==1&&pocket.Snapshot().looseReserve==90);
    CHECK(pocket.Magazine(ground)&&pocket.Magazine(ground)->rounds==10&&pocket.Magazine(ground)->location==PhysicalMagazineLocation::Dropped);
    CHECK(pocket.Snapshot().insertedMagazine!=ground&&pocket.Snapshot().insertedMagazine!=retained);
    CHECK(!pocket.Magazine(retained)||pocket.Magazine(retained)->rounds==0);
    CHECK(pocket.FinishNativeReload(Owner,1,31,60,true).Applied());
    CHECK(pocket.Magazine(ground)->rounds==10&&pocket.Conserved()&&pocket.Snapshot().admitted==101);

    // Cancellation before native reload entry must still settle the prepared
    // transaction. Original counts/chamber survive; no refill is inferred.
    PhysicalWeaponState prepared;
    CHECK(prepared.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,90,true,true));
    const auto preEntry=prepared.Snapshot();CHECK(prepared.BeginNativeReload(Owner,1).Applied());
    CHECK(prepared.NativeReloadBusy()&&!prepared.CanFire());
    CHECK(!WeaponReadinessAllowsFireInput(ReadWeaponReadiness(prepared.Snapshot())));
    CHECK(prepared.FinishNativeReload(Owner,1,preEntry.loaded,preEntry.looseReserve,false).Applied());
    CHECK(!prepared.NativeReloadBusy()&&prepared.CanFire()&&prepared.Snapshot().chamber==preEntry.chamber);
    CHECK(prepared.Snapshot().loaded==preEntry.loaded&&prepared.Snapshot().looseReserve==preEntry.looseReserve&&prepared.Conserved());

    // Tactical native reload retains the chamber. The same transaction value
    // survives stow/equip; a replacement identity cannot complete it.
    PhysicalWeaponState tactical;
    CHECK(tactical.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},11,90,true,true));
    CHECK(tactical.BeginNativeReload(Owner,1).Applied());
    auto switched=tactical;
    CHECK(switched.NativeReloadBusy()&&!switched.CanFire());
    CHECK(switched.FinishNativeReload(wrong,1,31,70,true).code==R::IdentityMismatch);
    CHECK(switched.FinishNativeReload(Owner,1,10,91,true).code==R::UnexpectedNativeState);
    CHECK(switched.FinishNativeReload(Owner,1,31,70,true).Applied());
    CHECK(switched.Snapshot().chamber==1&&switched.Snapshot().feed==30&&switched.Conserved());

    // Start timeout/cancellation does not refund a pocket pool or manufacture
    // a chamber. An interrupted partial transfer remains finite and rackable.
    PhysicalWeaponState cancelled;
    CHECK(cancelled.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,30,31},0,60,false,true));
    CHECK(cancelled.BeginNativeReload(Owner,1).Applied());
    CHECK(cancelled.FinishNativeReload(Owner,1,0,60,false).Applied());
    CHECK(!cancelled.CanFire()&&!cancelled.NativeReloadBusy()&&cancelled.Snapshot().needsRack&&cancelled.Conserved());
    CHECK(cancelled.BeginNativeReload(Owner,2).Applied());
    CHECK(cancelled.FinishNativeReload(Owner,2,20,40,false).Applied());
    CHECK(cancelled.Snapshot().chamber==0&&cancelled.Snapshot().feed==20&&!cancelled.CanFire()&&cancelled.Conserved());
    CHECK(cancelled.Apply(Command(cancelled,K::CycleAction)).Applied());
    CHECK(cancelled.CanFire()&&cancelled.Snapshot().loaded==20&&cancelled.Snapshot().admitted==60);

    // Full native completion also proves pump readiness; no extra shell is
    // created when the physical pump previously owed an action.
    CHECK(stowed.ReconcileAcceptedShots(Owner,3,2,9).shots==1&&stowed.Snapshot().pumpOwed);
    CHECK(stowed.BeginNativeReload(Owner,1).Applied());
    CHECK(stowed.FinishNativeReload(Owner,1,8,3,true).Applied());
    CHECK(stowed.CanFire()&&!stowed.Snapshot().pumpOwed&&stowed.Snapshot().chamber==1&&stowed.Conserved());

    PhysicalWeaponState groundOnly;
    CHECK(groundOnly.Initialize(Owner,{PhysicalWeaponFamily::NoActionMagazine,1,1},1,100,false,true));
    const auto firstGround=groundOnly.Snapshot().insertedMagazine;
    CHECK(groundOnly.Apply(Command(groundOnly,K::UnseatMagazine,firstGround)).Applied());
    CHECK(groundOnly.Apply(Command(groundOnly,K::DropMagazine,firstGround)).Applied());
    for(std::size_t i=1;i<PhysicalWeaponState::MaxMagazines;++i){
        const auto mag=groundOnly.Apply(Command(groundOnly,K::AcquireMagazine)).magazine;
        CHECK(mag&&groundOnly.Apply(Command(groundOnly,K::DropMagazine,mag)).Applied());
    }
    CHECK(groundOnly.BeginNativeReload(Owner,1).code==R::Capacity);
    CHECK(!groundOnly.NativeReloadBusy()&&groundOnly.Magazine(firstGround)->rounds==1&&groundOnly.Conserved());
    // Pistol actions separate rearward ejection from forward feeding. Neither
    // repeated opening nor duplicate receipts can discard or feed twice.
    const PhysicalWeaponConfig pistolConfig{PhysicalWeaponFamily::ClosedBoltMagazine,15,16,true};
    CHECK(PhysicalWeaponState::ValidConfig(pistolConfig));
    CHECK(!PhysicalWeaponState::ValidConfig({PhysicalWeaponFamily::TubePump,7,8,true}));
    PhysicalWeaponState pistol;
    CHECK(pistol.Initialize(Owner,pistolConfig,16,30,true,true));
    const auto openedPistol=pistol.Apply(Command(pistol,K::OpenAction));
    CHECK(openedPistol.Applied()&&pistol.Snapshot().actionOpen&&!pistol.Snapshot().actionLocked);
    CHECK(pistol.Snapshot().chamber==0&&pistol.Snapshot().feed==15&&pistol.Snapshot().discarded==1&&!pistol.CanFire());
    auto repeatOpen=Command(pistol,K::OpenAction);
    CHECK(pistol.Apply(repeatOpen).Applied());
    CHECK(pistol.Apply(repeatOpen).code==R::Duplicate&&pistol.Snapshot().discarded==1);
    CHECK(pistol.ReconcileAcceptedShots(Owner,1,14,30).code==R::FireBlocked);
    CHECK(pistol.BeginNativeReload(Owner,1).code==R::Unavailable);
    auto close=Command(pistol,K::CloseAction);
    CHECK(pistol.Apply(close).Applied()&&pistol.CanFire());
    CHECK(pistol.Snapshot().chamber==1&&pistol.Snapshot().feed==14&&!pistol.Snapshot().actionOpen);
    CHECK(pistol.Apply(close).code==R::Duplicate&&pistol.Snapshot().feed==14);
    CHECK(pistol.Apply(Command(pistol,K::CloseAction)).code==R::Unavailable&&pistol.Conserved());

    // An accepted last shot locks an inserted empty magazine open. Seating a
    // replacement preserves that state; only forward action feeds its round.
    PhysicalWeaponState locked;
    CHECK(locked.Initialize(Owner,pistolConfig,1,30,true,true));
    CHECK(locked.ReconcileAcceptedShots(Owner,1,0,30).shots==1);
    CHECK(locked.Snapshot().actionOpen&&locked.Snapshot().actionLocked&&!locked.CanFire());
    CHECK(locked.ReconcileAcceptedShots(Owner,1,0,30).code==R::Duplicate&&locked.Snapshot().spent==1);
    CHECK(locked.Apply(Command(locked,K::CloseAction)).Applied());
    CHECK(locked.Snapshot().actionLocked&&locked.Snapshot().chamber==0);
    const auto emptyPistolMag=locked.Snapshot().insertedMagazine;
    CHECK(locked.Apply(Command(locked,K::UnseatMagazine,emptyPistolMag)).Applied());
    CHECK(locked.Apply(Command(locked,K::RetainMagazine,emptyPistolMag)).Applied());
    const auto fullPistolMag=locked.Apply(Command(locked,K::AcquireMagazine)).magazine;
    CHECK(fullPistolMag&&locked.Apply(Command(locked,K::SeatMagazine,fullPistolMag)).Applied());
    CHECK(locked.Snapshot().actionLocked&&locked.Snapshot().chamber==0&&locked.Snapshot().feed==15&&!locked.CanFire());
    CHECK(locked.Apply(Command(locked,K::CloseAction)).Applied());
    CHECK(locked.CanFire()&&!locked.Snapshot().actionLocked&&locked.Snapshot().chamber==1&&locked.Snapshot().feed==14&&locked.Conserved());

    // Tactical removal retains the chamber. Firing that single round without
    // a magazine does not invent a follower lock. Closing without a magazine
    // is permitted, but seating afterwards still owes a separate action.
    CHECK(locked.Apply(Command(locked,K::UnseatMagazine,fullPistolMag)).Applied());
    CHECK(locked.CanFire()&&locked.Snapshot().chamber==1);
    CHECK(locked.Apply(Command(locked,K::SeatMagazine,fullPistolMag)).Applied()&&locked.CanFire());
    CHECK(locked.Apply(Command(locked,K::UnseatMagazine,fullPistolMag)).Applied());
    CHECK(locked.ReconcileAcceptedShots(Owner,2,0,15).shots==1);
    CHECK(!locked.Snapshot().actionOpen&&!locked.Snapshot().actionLocked);
    CHECK(locked.Apply(Command(locked,K::OpenAction)).Applied());
    CHECK(locked.Apply(Command(locked,K::CloseAction)).Applied());
    CHECK(!locked.Snapshot().actionOpen&&locked.Snapshot().chamber==0);
    CHECK(locked.Apply(Command(locked,K::SeatMagazine,fullPistolMag)).Applied());
    CHECK(!locked.CanFire()&&locked.Snapshot().needsRack&&locked.Snapshot().feed==14);
    CHECK(locked.Apply(Command(locked,K::CycleAction)).Applied());
    CHECK(locked.CanFire()&&locked.Snapshot().feed==13&&locked.Conserved());

    // An explicit correction cannot release a hand-held slide or chamber its
    // added ammunition. A value copy retains mechanical state across stow.
    PhysicalWeaponState correctedPistol;
    CHECK(correctedPistol.Initialize(Owner,pistolConfig,0,30,false,true));
    CHECK(correctedPistol.Apply(Command(correctedPistol,K::OpenAction)).Applied());
    CHECK(correctedPistol.ObserveExternalAmmo(Owner,correctedPistol.NextCommandId(),10,30).Applied());
    auto stowedPistol=correctedPistol;
    CHECK(stowedPistol.Snapshot().actionOpen&&!stowedPistol.Snapshot().actionLocked&&stowedPistol.Snapshot().chamber==0);
    CHECK(!stowedPistol.CanFire()&&stowedPistol.BeginNativeReload(Owner,1).code==R::Unavailable);
    CHECK(stowedPistol.Apply(Command(stowedPistol,K::CloseAction)).Applied());
    CHECK(stowedPistol.CanFire()&&stowedPistol.Snapshot().loaded==10&&stowedPistol.Conserved());

    // Native reload may settle a latched slide, but cancellation/partial
    // transfer cannot manufacture forward action or fire readiness.
    PhysicalWeaponState nativePistol;
    CHECK(nativePistol.Initialize(Owner,pistolConfig,1,30,true,true));
    CHECK(nativePistol.ReconcileAcceptedShots(Owner,1,0,30).Applied());
    CHECK(nativePistol.BeginNativeReload(Owner,1).Applied());
    CHECK(nativePistol.Apply(Command(nativePistol,K::OpenAction)).code==R::Unavailable);
    CHECK(nativePistol.FinishNativeReload(Owner,1,0,30,false).Applied());
    CHECK(nativePistol.Snapshot().actionLocked&&!nativePistol.CanFire());
    CHECK(nativePistol.BeginNativeReload(Owner,2).Applied());
    CHECK(nativePistol.FinishNativeReload(Owner,2,10,20,false).Applied());
    CHECK(nativePistol.Snapshot().actionLocked&&nativePistol.Snapshot().chamber==0&&!nativePistol.CanFire());
    CHECK(nativePistol.BeginNativeReload(Owner,3).Applied());
    CHECK(nativePistol.FinishNativeReload(Owner,3,16,14,true).Applied());
    CHECK(!nativePistol.Snapshot().actionOpen&&!nativePistol.Snapshot().actionLocked&&nativePistol.CanFire()&&nativePistol.Conserved());
    CHECK(nativePistol.FinishNativeReload(Owner,3,16,14,true).code==R::Duplicate);
    PhysicalWeaponState emptyCompletion;
    CHECK(emptyCompletion.Initialize(Owner,pistolConfig,0,30,false,true));
    CHECK(emptyCompletion.BeginNativeReload(Owner,1).Applied());
    CHECK(emptyCompletion.FinishNativeReload(Owner,1,0,30,true).Applied());
    CHECK(emptyCompletion.Snapshot().actionLocked&&!emptyCompletion.CanFire()&&emptyCompletion.Conserved());
    const auto emptyCompletionMag=emptyCompletion.Snapshot().insertedMagazine;
    CHECK(emptyCompletion.Apply(Command(emptyCompletion,K::UnseatMagazine,emptyCompletionMag)).Applied());
    CHECK(emptyCompletion.Snapshot().actionLocked); // Removing the follower does not move the slide.
    CHECK(emptyCompletion.Apply(Command(emptyCompletion,K::CloseAction)).Applied());
    CHECK(!emptyCompletion.Snapshot().actionOpen&&!emptyCompletion.Snapshot().actionLocked&&emptyCompletion.Snapshot().chamber==0);

    // Opt-in policy does not change ordinary closed-bolt empty-shot behavior.
    PhysicalWeaponState legacyAction;
    CHECK(legacyAction.Initialize(Owner,{PhysicalWeaponFamily::ClosedBoltMagazine,15,16},1,0,true,true));
    CHECK(legacyAction.ReconcileAcceptedShots(Owner,1,0,0).Applied());
    CHECK(!legacyAction.Snapshot().actionOpen&&!legacyAction.Snapshot().actionLocked&&legacyAction.Conserved());
    CHECK(groundOnly.Apply(Command(groundOnly,K::OpenAction)).code==R::Unsupported);
    CHECK(groundOnly.Apply(Command(groundOnly,K::CloseAction)).code==R::Unsupported);
    std::puts("PhysicalWeaponState conservation, command receipts and persistent mechanics passed");
}
