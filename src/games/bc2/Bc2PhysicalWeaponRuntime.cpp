#include "Bc2PhysicalWeaponRuntime.h"
#include "Bc2PhysicalWeaponNative.h"
#include "Bc2PhysicalShotOrder.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#include <windows.h>
#endif

namespace fvr::bc2::physicalWeaponRuntime {
namespace {
using namespace interaction;
std::mutex mutex;
std::atomic<bool> running=false;
std::atomic<bool> cameraShotDiagnostic=false;
ReloadStateMemory memory{};
ReloadServerBinding serverBinding{};
ReloadStateBinding stateBinding{};
std::uint32_t base=0;
bool installed=false;
PlayerInventoryAnchor anchor{};
std::optional<PlayerInventorySnapshot> inventory;
bool inventoryFresh=false;
struct Weapon {
    PlayerInventorySlot native{};
    PhysicalWeaponFamily family=PhysicalWeaponFamily::Unsupported;
    bool locksOpenOnEmpty=false;
    std::optional<std::int32_t> feedCapacity;
    PhysicalWeaponState state{};
    PhysicalShotOrder shots{};
    std::uint64_t incarnation=0,revision=0;
    AcceptedShotReceipt accepted{};
    bool suspended=false;
    struct NativeReload {
        std::uint64_t token=0,requestedAt=0,preparedAt=0;
        NativeReloadPhase phase=NativeReloadPhase::Rejected;
        std::uint32_t loaded=0,reserve=0;
        bool cancelled=false,entered=false,transferred=false,transferOpen=false;
        bool Busy()const noexcept{return token&&(phase==NativeReloadPhase::Queued||phase==NativeReloadPhase::Ready||phase==NativeReloadPhase::Running);}
    } reload{};
};
std::array<Weapon,64> weapons{};
struct Pending {PhysicalWeaponCommand command{};std::uint64_t expires=0;};
std::array<Pending,128> commands{};
CommandReceiptStore receipts;
std::uint64_t nextCommand=0,nextIncarnation=0,nextShot=0,applied=0,rejected=0,shotCount=0,writeFailures=0,unexplained=0;
std::uint64_t clientShots=0,clientUnmatched=0,clientBlocked=0;
std::uint64_t capacityWrites=0,capacityFailures=0;
std::uint64_t authorityRetirements=0;
// Server-side death/checkpoint evidence. The server soldier's inventory stops
// being readable for the anchored actor while it is dead or being rebuilt; a
// checkpoint restore can then bring back the same soldier, inventory, item
// addresses and serials with restored ammunition. Counts the native restored
// are authoritative, so a server-soldier loss spanning at least LossRetireMs
// (first to last failed read with the server player no longer owning the
// anchored server soldier) retires the ledger
// (it is re-initialized from those native counts: nothing is refunded or lost
// by the mod). Shorter gaps (selection replication) keep the mapping.
// FVR_BC2_ACTOR_LOSS_REBIND=0 disables this together with the client rebind.
constexpr std::uint64_t LossRetireMs=500;
std::uint64_t inventoryLostAt=0,inventoryLostLast=0,lossRetirements=0;
bool lossRetire=true;
std::uint64_t suspendedMetadataCleanups=0;
std::uint64_t suspensionRecoveries=0;
std::uint64_t Now()noexcept {
#ifdef _WIN32
    return GetTickCount64();
#else
    return 0;
#endif
}
template<class T>bool Get(std::uint32_t address,T& value)noexcept {
    return address>=0x10000&&std::uint64_t(address)+sizeof(T)<=UINT32_MAX&&memory.read&&
        memory.read(memory.context,address,&value,sizeof value);
}
bool SameItem(const PlayerInventorySlot& a,const PlayerInventorySlot& b)noexcept {
    return a.owned&&b.owned&&a.slot==b.slot&&a.item==b.item&&a.serial==b.serial&&a.data==b.data&&
        a.firing==b.firing&&a.firingData==b.firingData&&a.ammoAddress==b.ammoAddress;
}
Weapon* Find(std::uint32_t firing)noexcept {
    if(!inventory||!firing)return nullptr;
    for(auto& w:weapons)if(w.state.Snapshot().initialized&&
        (w.native.firing==firing||w.native.clientFiring[0]==firing||w.native.clientFiring[1]==firing))return &w;
    return nullptr;
}
bool Current(const Weapon& w,std::uint32_t firing)noexcept {
    if(!inventory||inventory->anchor!=anchor||w.native.slot>=inventory->count||
       !SameItem(w.native,inventory->slots[w.native.slot]))return false;
    // Callback-time structural ownership recheck, independent of selected slot,
    // input freshness, reload state, diagnostics and client convergence.
    std::uint32_t value=0,begin=0,end=0,manager=0,players=0,capacity=0,playerId=0;
    std::uint8_t playerFlags=0;
    if(!Get(anchor.player+0xc54,value)||value!=anchor.weak||!Get(anchor.weak,value)||value!=anchor.soldier+4||
       !Get(anchor.player+0xccd,playerFlags)||!(playerFlags&8)||
       !Get(anchor.player+0xc68,value)||value!=anchor.soldier||
       !Get(anchor.soldier+0x220,value)||value!=anchor.player||
       !Get(base+serverBinding.contextRva+8,manager)||!Get(manager,value)||value!=base+serverBinding.managerVtableRva||
       !Get(anchor.player+0x154,playerId)||!Get(manager+4,capacity)||!capacity||capacity>256||playerId>=capacity||
       !Get(manager+0x6c,players)||players<0x10000||std::uint64_t(players)+capacity*4>UINT32_MAX||
       !Get(players+playerId*4,value)||value!=inventory->serverPlayer||
       !Get(inventory->serverPlayer+0x154,value)||value!=playerId||
       !Get(inventory->serverPlayer+0xc3c,value)||value!=inventory->serverSoldier||
       !Get(inventory->serverPlayer+0xc6c,value)||value!=inventory->serverSoldier||
       !Get(inventory->serverSoldier+0x220,value)||value!=inventory->serverPlayer||
       !Get(inventory->serverSoldier+0x2b4,value)||value!=inventory->inventory||
       !Get(inventory->serverSoldier+0x2b8,begin)||!Get(inventory->serverSoldier+0x2bc,end)||
       begin<0x10000||end<begin||end-begin>256||(end-begin)%4||w.native.slot>=(end-begin)/4||
       !Get(begin+w.native.slot*4,value)||value!=w.native.item||
       !Get(w.native.item+8,value)||value!=inventory->serverSoldier||
       !Get(w.native.item+4,value)||value!=w.native.data||
       !Get(w.native.item+0x10,value)||value!=w.native.firing)return false;
    std::uint16_t serial=0;
    if(!Get(w.native.item+0x20,serial)||serial!=w.native.serial)return false;
    if(firing!=w.native.firing){
        const auto offset=firing==w.native.clientFiring[0]?0x3cu:firing==w.native.clientFiring[1]?0x40u:0u;
        if(!offset||!Get(anchor.soldier+0x260,begin)||!Get(anchor.soldier+0x264,end)||
           begin<0x10000||end<begin||end-begin>256||(end-begin)%4||w.native.slot>=(end-begin)/4||
           !Get(begin+w.native.slot*4,value)||value!=w.native.clientWeapon||
           !Get(w.native.clientWeapon+4,value)||value!=w.native.data||
           !Get(w.native.clientWeapon+offset,value)||value!=firing)return false;
    }
    return Get(firing,value)&&value==base+stateBinding.firingVtableRva&&
        Get(firing+8,value)&&value==w.native.firingData&&Get(firing+12,value)&&value==w.native.ammoAddress;
}
bool Counts(const Weapon& w,std::uint32_t& loaded,std::uint32_t& reserve)noexcept {
    return Get(w.native.firing+0x7c,loaded)&&Get(w.native.firing+0x80,reserve)&&
        loaded<=PhysicalWeaponState::MaxCapacity&&reserve<=PhysicalWeaponState::MaxReserve;
}
bool Store(std::uint32_t address,const void* data,std::size_t size)noexcept {
#if defined(_MSC_VER) && defined(_M_IX86)
    MEMORY_BASIC_INFORMATION page{};
    if(!VirtualQuery(reinterpret_cast<void*>(address),&page,sizeof page)||page.State!=MEM_COMMIT||
       (page.Protect&(PAGE_GUARD|PAGE_NOACCESS))||
       !(page.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))||
       std::uint64_t(address)+size>std::uint64_t(reinterpret_cast<std::uintptr_t>(page.BaseAddress))+page.RegionSize)return false;
    __try {std::memcpy(reinterpret_cast<void*>(address),data,size);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
#else
    (void)address;(void)data;(void)size;return false;
#endif
}
bool EnsureCapacity(Weapon& w,std::uint32_t firing)noexcept {
    const auto state=w.state.Snapshot();
    if(state.config.family!=PhysicalWeaponFamily::ClosedBoltMagazine)return true;
    // Own the TOTAL native capacity (magazine + chamber), not shared authored
    // feed data. Restore serializes this override at snapshot+2c and writes it
    // back to firing+98, so reassert each copy at its own Update boundary.
    // The ledger's feedCapacity was captured before any override and remains
    // immutable even when later inventory snapshots report maxLoaded here.
    if(!w.native.serverFiringVerified||!w.native.clientFiringVerified||
       !Current(w,w.native.firing)||!Current(w,w.native.clientFiring[0])||!Current(w,w.native.clientFiring[1]))return false;
    std::int32_t observed=0;const auto wanted=std::int32_t(state.config.maxLoaded);
    if(Get(firing+0x98,observed)&&observed==wanted)return true;
    if(!Store(firing+0x98,&wanted,sizeof wanted)||!Get(firing+0x98,observed)||observed!=wanted){
        w.suspended=true;++capacityFailures;return false;
    }
    ++capacityWrites;return true;
}
bool Publish(Weapon& w,const PhysicalWeaponState& candidate)noexcept {
    if(!Current(w,w.native.firing))return false;
    const auto before=w.state.Snapshot(),after=candidate.Snapshot();
    std::uint32_t loaded=0,reserve=0;
    if(!Counts(w,loaded,reserve)||loaded!=before.loaded||reserve!=before.looseReserve){w.suspended=true;++unexplained;return false;}
    if(loaded!=after.loaded||reserve!=after.looseReserve){
        const std::array<std::uint32_t,2> counts{after.loaded,after.looseReserve};
        if(!Store(w.native.firing+0x7c,counts.data(),sizeof counts)||!Counts(w,loaded,reserve)||
           loaded!=after.loaded||reserve!=after.looseReserve){++writeFailures;w.suspended=true;return false;}
    }
    w.state=candidate;w.shots.MechanicalBoundary();++w.revision;return true;
}
bool ReloadIdle(const Weapon& w)noexcept {
    std::uint32_t current=0,next=0;std::uint8_t flags=0;
    return Get(w.native.firing+0x3c,current)&&Get(w.native.firing+0x44,next)&&
        Get(w.native.firing+0xa8,flags)&&current==2&&next==2&&!(flags&(8|16));
}
bool ReloadSelected(const Weapon& w)noexcept {
    std::uint32_t server=0,client=0;
    return inventory&&Get(inventory->inventory+0x14c,server)&&Get(inventory->clientInventory+0x14c,client)&&
        server==w.native.slot&&client==w.native.slot;
}
bool FinishReload(Weapon& w,bool ready,NativeReloadPhase phase)noexcept {
    if(!Current(w,w.native.firing)||w.reload.transferOpen)return false;
    std::uint32_t loaded=0,reserve=0;
    if(!Counts(w,loaded,reserve)||loaded!=w.reload.loaded||reserve!=w.reload.reserve){w.suspended=true;++unexplained;return false;}
    auto candidate=w.state;
    if(!candidate.FinishNativeReload(candidate.Snapshot().identity,w.reload.token,loaded,reserve,ready).Applied()||!candidate.Conserved()){
        w.suspended=true;++unexplained;return false;
    }
    // Native owns the transfer; there is no projection write or refund here.
    w.state=candidate;w.shots.MechanicalBoundary();++w.revision;w.reload.phase=phase;return true;
}
void Complete(std::uint64_t id,CommandStatus status,std::uint64_t magazine=0,std::uint64_t dropped=0)noexcept {
    receipts.Complete(id,status,magazine,dropped);
    if(status==CommandStatus::Applied)++applied;else if(status==CommandStatus::Rejected)++rejected;
}
void ExpireCommands()noexcept {
    const auto now=Now();
    for(auto& p:commands)if(p.command.id&&now>=p.expires){Complete(p.command.id,CommandStatus::Rejected);p={};}
}
void RejectCommands(const PhysicalWeaponIdentity& identity)noexcept {
    for(auto& p:commands)if(p.command.id&&p.command.identity==identity){Complete(p.command.id,CommandStatus::Rejected);p={};}
}
bool Initialize(Weapon& w)noexcept {
    if(w.state.Snapshot().initialized)return true;
    const auto& slot=w.native;
    const auto feed=w.feedCapacity?w.feedCapacity:slot.effectiveCapacity;
    if(w.family==PhysicalWeaponFamily::Unsupported||!feed||!slot.serverFiringVerified||
       !slot.clientFiringVerified||*feed<=0||*feed>=int(PhysicalWeaponState::MaxCapacity)||
       !Current(w,slot.firing))return false;
    std::uint32_t loaded=0,reserve=0,current=0,next=0;std::uint8_t flags=0;
    if(!Counts(w,loaded,reserve)||!Get(slot.firing+0x3c,current)||!Get(slot.firing+0x44,next)||
       !Get(slot.firing+0xa8,flags)||(flags&(8|16))||current>2||next>2)return false;
    const bool chamber=w.family==PhysicalWeaponFamily::ClosedBoltMagazine||w.family==PhysicalWeaponFamily::TubePump||w.family==PhysicalWeaponFamily::TubeAutomatic;
    const auto capacity=std::uint32_t(*feed);
    const bool tube=w.family==PhysicalWeaponFamily::TubePump||w.family==PhysicalWeaponFamily::TubeAutomatic;
    if(tube&&capacity<2)return false;
    // Detachable closed-bolt magazines retain one chambered round in addition
    // to a full native-sized magazine. Tubes preserve the original total-shell
    // capacity: e.g. native eight becomes seven in the tube plus one chamber.
    const PhysicalWeaponConfig config{w.family,tube?capacity-1:capacity,
        capacity+(w.family==PhysicalWeaponFamily::ClosedBoltMagazine?1u:0u),w.locksOpenOnEmpty};
    const PhysicalWeaponIdentity id{inventory->serverSoldier,anchor.actorGeneration,slot.item,w.incarnation};
    if(!w.state.Initialize(id,config,loaded,reserve,chamber&&loaded>0,true))return false;
    ++w.revision;return true;
}
void AcceptedShot(void*,std::uint32_t firing)noexcept {
    if(!running.load(std::memory_order_acquire))return;
    std::lock_guard lock(mutex);auto* w=Find(firing);
    if(!w||w->suspended||!Current(*w,firing))return;
    std::uint32_t input=0;
    if(!Get(firing+0x94,input)){w->suspended=true;return;}
    if(firing!=w->native.firing){
        std::uint32_t loaded=0;
        const auto copy=firing==w->native.clientFiring[0]?0u:1u;
        if(Get(firing+0x7c,loaded)&&w->shots.ClientAccepted(copy,input,loaded))++clientShots;
        else ++clientUnmatched;
        return;
    }
    std::uint32_t loaded=0,reserve=0;
    if(!Counts(*w,loaded,reserve)){w->suspended=true;return;}
    if(nextShot==UINT64_MAX){w->suspended=true;return;}
    const auto before=w->state.Snapshot();
    const auto result=w->state.ReconcileAcceptedShots(before.identity,++nextShot,loaded,reserve);
    if(!result.Applied()||result.shots!=1||!w->shots.ServerAccepted({input,before.loaded})){w->suspended=true;++unexplained;}
    else{shotCount+=result.shots;++w->revision;
        {
            std::int64_t qpc=0;
#ifdef _WIN32
            LARGE_INTEGER stamp{};if(QueryPerformanceCounter(&stamp))qpc=stamp.QuadPart;
#endif
            w->accepted={nextShot,qpc,input,before.loaded,loaded,reserve};
        }
    }
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uint32_t image,const ReloadStateMemory& access){
    if(installed)return false;
#ifdef _WIN32
    char off[4]{};if(GetEnvironmentVariableA("FVR_BC2_PHYSICAL_WEAPON_AUTHORITY",off,sizeof off)&&off[0]=='0')return true;
    {char rebind[4]{};if(GetEnvironmentVariableA("FVR_BC2_ACTOR_LOSS_REBIND",rebind,sizeof rebind)&&rebind[0]=='0')lossRetire=false;}
#endif
    const auto s=DiscoverReloadState(bytes,pe);const auto server=DiscoverReloadServer(bytes,pe);
    if(!s||!server||!ValidateReloadStateLive(access,*s,image)||!ValidateReloadServerLive(access,*server,image))return false;
    memory=access;stateBinding=*s;serverBinding=*server;base=image;
    physicalWeaponNative::Callbacks callbacks{};
    callbacks.permitFire=[](void*,std::uint32_t f)noexcept{return PermitFire(f);};
    callbacks.acceptedShot=AcceptedShot;
    callbacks.blockReload=[](void*,std::uint32_t f)noexcept{return BlockNativeReload(f);};
    installed=physicalWeaponNative::Install(bytes,pe,image,access,callbacks);return installed;
}
void Start()noexcept {if(installed&&physicalWeaponNative::Start())running.store(true,std::memory_order_release);}
bool Stop()noexcept {running.store(false,std::memory_order_release);return !installed||physicalWeaponNative::Stop();}
bool Enabled()noexcept{return running.load(std::memory_order_acquire);}
void EnableCameraShotDiagnostic(bool enabled)noexcept {cameraShotDiagnostic.store(enabled,std::memory_order_release);}
std::optional<CameraShotObservation> ReadCameraShotObservation()noexcept {
    if(!cameraShotDiagnostic.load(std::memory_order_acquire))return {};
    return ReadAcceptedShotObservation();
}
std::optional<CameraShotObservation> ReadAcceptedShotObservation()noexcept {
    if(!Enabled())return {};
    std::unique_lock lock(mutex,std::try_to_lock);
    if(!lock.owns_lock()||!inventoryFresh||!inventory||inventory->anchor!=anchor||inventory->selectedSlot>=inventory->count)return {};
    const auto& selected=inventory->slots[inventory->selectedSlot];
    for(const auto& w:weapons){const auto state=w.state.Snapshot();
        if(w.native==selected&&w.native.owned&&state.initialized&&!w.suspended)
            return CameraShotObservation{state.identity,anchor,w.native.clientWeapon,w.native.firing,w.accepted};
    }
    return {};
}
bool Installed()noexcept{return installed;}
void PublishActor(const PlayerInventoryAnchor& a)noexcept {
    std::lock_guard lock(mutex);
    if(anchor==a)return;
    anchor=a;inventory.reset();inventoryFresh=false;weapons={};inventoryLostAt=inventoryLostLast=0;
    for(auto& c:commands)if(c.command.id){Complete(c.command.id,CommandStatus::Rejected);c={};}
}
void ClearActor()noexcept {PublishActor({});}
void InventoryUpdate(std::uint32_t self)noexcept {
    if(!Enabled())return;
    PlayerInventoryAnchor a;{std::lock_guard lock(mutex);a=anchor;}
    if(!a.player)return;
    auto fresh=ReadPlayerInventory(memory,serverBinding,stateBinding,base,a);
    std::unique_lock lock(mutex);if(anchor!=a)return;
    ExpireCommands();
    // A transient client/server disagreement is not loss of ownership. Keep
    // the last mapping; every hook rechecks structural membership in Current.
    // Clearing this snapshot would reopen empty auto-reload on all three copies.
    if(!fresh){
        inventoryFresh=false;
        // Only a server player that no longer owns the anchored server soldier
        // (+0xc3c, also checked by Current) is a loss. A vehicle seat changes
        // only the controlled entity (+0xc6c) and keeps the ledger.
        std::uint32_t soldier=0;
        if(inventory&&(!Get(inventory->serverPlayer+0xc3c,soldier)||soldier!=inventory->serverSoldier)){
            const auto now=Now();if(!inventoryLostAt)inventoryLostAt=now;inventoryLostLast=now;}
        return;
    }
    if(fresh->inventory!=self)return; // Another soldier's callback is not ours.
    const bool lost=lossRetire&&inventoryLostAt&&inventoryLostLast-inventoryLostAt>=LossRetireMs;
    inventoryLostAt=inventoryLostLast=0;
    const bool sameAuthority=inventory&&SamePlayerInventoryAuthority(*inventory,*fresh);
    if(inventory&&sameAuthority&&lost)++lossRetirements;
    if(inventory&&(!sameAuthority||lost)){
        // A verified server pawn/inventory replacement is a new authority,
        // including checkpoint restore that recycles item addresses/serials.
        // Never infer this from ammo deltas or a transient failed read.
        for(const auto& w:weapons)RejectCommands(w.state.Snapshot().identity);
        weapons={};++authorityRetirements;
    }
    inventory=std::move(fresh);inventoryFresh=true;
    std::array<std::uint32_t,64> ownedFiring{};
    for(unsigned n=0;n<weapons.size();++n){
        auto& w=weapons[n];
        if(n>=inventory->count||!inventory->slots[n].owned){RejectCommands(w.state.Snapshot().identity);w={};continue;}
        const auto slot=inventory->slots[n];
        if(!SameItem(w.native,slot)){
            RejectCommands(w.state.Snapshot().identity);w={};
            if(nextIncarnation==UINT64_MAX)continue;
            w.incarnation=++nextIncarnation;
        }
        w.native=slot;
        Initialize(w);
        if(w.state.Snapshot().initialized)ownedFiring[n]=w.native.firing;
    }
    // Stowed weapons may not receive firing updates. Complete their queued
    // magazine cleanup on this same server thread too; selection must not
    // strand a Held magazine or make its command expire. Each call revalidates
    // structural ownership after releasing the publication lock.
    lock.unlock();
    for(const auto firing:ownedFiring)if(firing)BeforeUpdate(firing);
}
std::optional<PlayerInventorySnapshot> Inventory()noexcept {std::lock_guard lock(mutex);return inventoryFresh?inventory:std::nullopt;}
bool ConfigureWeapon(std::uint32_t client,PhysicalWeaponFamily family,bool locksOpenOnEmpty,std::optional<std::int32_t> authoredBaseCapacity)noexcept {
    if(!Enabled()||family<=PhysicalWeaponFamily::Unsupported||family>PhysicalWeaponFamily::TubeAutomatic)return false;
    std::lock_guard lock(mutex);if(!inventory)return false;
    for(auto& w:weapons)if(w.native.clientWeapon==client&&w.native.owned){
        if(authoredBaseCapacity&&w.native.authoredBaseCapacity!=authoredBaseCapacity)return false;
        if(w.state.Snapshot().initialized)return w.family==family&&w.locksOpenOnEmpty==locksOpenOnEmpty;
        // Do not reinterpret gadgets/detonators as conventional weapons.
        const bool magazine=family>=PhysicalWeaponFamily::ClosedBoltMagazine&&family<=PhysicalWeaponFamily::NoActionMagazine;
        if((magazine&&w.native.reloadType!=1)||(!magazine&&w.native.reloadType!=0)||
           w.native.fireLogicType<0||w.native.fireLogicType>2||
           (family==PhysicalWeaponFamily::TubePump&&w.native.fireLogicType!=1)||
           (family==PhysicalWeaponFamily::TubeAutomatic&&w.native.fireLogicType==1))return false;
        if(locksOpenOnEmpty&&family!=PhysicalWeaponFamily::ClosedBoltMagazine)return false;
        if(family==PhysicalWeaponFamily::ClosedBoltMagazine&&authoredBaseCapacity){
            // The exact admitted descriptor supplies BaseCapacity. Native
            // multiplier gives feed capacity; +98 may already include our +1
            // chamber override restored into a newly created firing instance.
            if(!w.native.authoredCapacity||*w.native.authoredCapacity<=0||
               *w.native.authoredCapacity>=int(PhysicalWeaponState::MaxCapacity))return false;
            w.feedCapacity=w.native.authoredCapacity;
        }
        w.family=family;w.locksOpenOnEmpty=locksOpenOnEmpty;return true;
    }return false;
}
std::optional<View> Read(std::uint32_t client)noexcept {
    if(!Enabled())return {};
    std::lock_guard lock(mutex);
    for(auto& w:weapons)if(w.native.clientWeapon==client&&w.state.Snapshot().initialized&&Current(w,w.native.firing)){
        bool pending=false;
        for(const auto& c:commands)if(c.command.id&&c.command.identity==w.state.Snapshot().identity){pending=true;break;}
        return View{w.state.Snapshot().identity,w.state.Snapshot(),w.state.Magazines(),w.native,w.revision,w.suspended,
            ReadWeaponReadiness(w.state.Snapshot(),w.suspended,w.reload.Busy(),pending)};
    }
    return {};
}
ActionAdmission ReadActionAdmission(std::uint32_t client,std::uint32_t expectedData)noexcept {
    if(!Enabled()||!client)return {};
    std::lock_guard lock(mutex);
    if(!inventoryFresh||!inventory||inventory->anchor!=anchor)return {};
    for(const auto& w:weapons)if(w.native.clientWeapon==client&&w.native.owned&&
        (!expectedData||w.native.data==expectedData)){
        if(w.native.slot>=inventory->count||!SameItem(w.native,inventory->slots[w.native.slot]))return {};
        ActionAdmission a;a.incarnation=w.incarnation;
        const bool firingVerified=w.native.serverFiringVerified&&w.native.clientFiringVerified;
        if(firingVerified&&!Current(w,w.native.firing))return {};
        a.nativeReloadInput=firingVerified;
        a.weapon=w.family==PhysicalWeaponFamily::Unsupported?WeaponAdmission::Unmanaged:
            w.state.Snapshot().initialized?WeaponAdmission::Managed:WeaponAdmission::Initializing;
        a.suspended=w.suspended;a.nativeBusy=w.reload.Busy();a.nativePhase=w.reload.phase;
        for(const auto& c:commands)if(c.command.id&&c.command.identity==w.state.Snapshot().identity){a.pendingCommand=true;break;}
        return a;
    }
    return {};
}
std::uint64_t Submit(const PhysicalWeaponIdentity& id,PhysicalWeaponCommandKind kind,std::uint64_t magazine,int rounds)noexcept {
    // Supply corrections are internal observations, not user interaction
    // commands. In particular a default-constructed ReconcileExternalAmmo
    // would otherwise project zero counts into the player's weapon.
    if(!Enabled()||rounds<0||(kind>=PhysicalWeaponCommandKind::AddReserve&&
       kind!=PhysicalWeaponCommandKind::OpenAction&&kind!=PhysicalWeaponCommandKind::CloseAction&&
       kind!=PhysicalWeaponCommandKind::DrawMagazine))return 0;
    std::lock_guard lock(mutex);
    ExpireCommands();
    for(auto& w:weapons)if(w.state.Snapshot().identity==id&&w.state.Snapshot().initialized&&
       (!w.suspended||PhysicalMagazineMetadataCleanup(kind))&&
       (!w.reload.Busy()||(w.suspended&&PhysicalMagazineMetadataCleanup(kind)))){
        if(!Current(w,w.native.firing))return 0;
        for(auto& c:commands)if(!c.command.id){
            if(nextCommand==UINT64_MAX)return 0;
            const auto serial=++nextCommand;
            if(!receipts.Reserve(serial))return 0;
            c={{id,serial,kind,magazine,std::uint32_t(rounds)},Now()+500};
            return serial;
        }return 0;
    }return 0;
}
std::optional<Receipt> Result(std::uint64_t id)noexcept {
    std::lock_guard lock(mutex);ExpireCommands();return receipts.Take(id);
}
bool RequestNativeReload(const PhysicalWeaponIdentity& id,std::uint64_t token)noexcept {
    if(!Enabled()||!token)return false;
    std::lock_guard lock(mutex);ExpireCommands();
    for(auto& w:weapons)if(w.state.Snapshot().identity==id&&w.state.Snapshot().initialized){
        if(w.suspended||!Current(w,w.native.firing)||!ReloadSelected(w))return false;
        if(w.reload.Busy())return w.reload.token==token;
        if(token<=w.reload.token)return false;
        for(const auto& p:commands)if(p.command.id&&p.command.identity==id)return false;
        for(const auto& m:w.state.Magazines())if(m.location==PhysicalMagazineLocation::Held)return false;
        w.reload={};w.reload.token=token;w.reload.requestedAt=Now();w.reload.phase=NativeReloadPhase::Queued;return true;
    }return false;
}
std::optional<NativeReloadStatus> ReadNativeReload(const PhysicalWeaponIdentity& id,std::uint64_t token)noexcept {
    std::lock_guard lock(mutex);
    for(const auto& w:weapons)if(w.state.Snapshot().initialized&&w.state.Snapshot().identity==id&&w.reload.token&&
       (!token||token==w.reload.token)&&Current(w,w.native.firing))
        return NativeReloadStatus{w.reload.token,w.reload.phase,w.reload.Busy(),
            !w.suspended&&!w.reload.cancelled&&ReloadSelected(w)&&w.reload.phase==NativeReloadPhase::Ready};
    return {};
}
bool CancelNativeReload(const PhysicalWeaponIdentity& id,std::uint64_t token)noexcept {
    std::lock_guard lock(mutex);
    for(auto& w:weapons)if(w.state.Snapshot().identity==id&&w.reload.token==token&&token&&Current(w,w.native.firing)){
        if(w.reload.Busy())w.reload.cancelled=true;return true;
    }return false;
}
bool BlockNativeReload(std::uint32_t firing)noexcept {
    if(!Enabled())return false;
    std::lock_guard lock(mutex);const auto* w=Find(firing);
    if(!w||!Current(*w,firing))return false;
    // Keep the native three-copy state flow, but only the server transfer below
    // can own ammunition. Client Restore propagates its authoritative result.
    return w->suspended||!(ReloadSelected(*w)&&w->reload.Busy()&&w->reload.phase!=NativeReloadPhase::Queued&&
        !w->reload.cancelled);
}
std::optional<NativeReloadTransfer> BeginNativeReloadTransfer(std::uint32_t firing,std::uint32_t caller)noexcept {
    if(!Enabled())return {};
    std::lock_guard lock(mutex);auto* w=Find(firing);
    if(!w||w->suspended||firing!=w->native.firing||!Current(*w,firing)||!w->state.NativeReloadBusy()||
       !w->reload.Busy()||w->reload.transferOpen||caller!=base+stateBinding.code[1].rva+0x561)return {};
    std::uint32_t loaded=0,reserve=0,current=0;
    if(!Get(firing+0x3c,current)||current!=12||!Counts(*w,loaded,reserve)||
       loaded!=w->reload.loaded||reserve!=w->reload.reserve){w->suspended=true;++unexplained;return {};}
    // Native Transfer itself neither clamps a negative magazine deficit nor
    // checks a full tube. Enforce the owned capacity before entering it.
    // Tick interruption remains a different, unadmitted transaction route:
    // it can transfer in state2/2 with a positive phase timer.
    if(!NormalTransferCountsAdmitted(loaded,reserve,w->state.Snapshot().config.maxLoaded,w->native.reloadType))return {};
    w->reload.transferOpen=true;w->reload.entered=true;w->reload.phase=NativeReloadPhase::Running;
    return NativeReloadTransfer{w->state.Snapshot().identity,w->reload.token,firing,loaded,reserve};
}
void FinishNativeReloadTransfer(const NativeReloadTransfer& transfer)noexcept {
    std::lock_guard lock(mutex);auto* w=Find(transfer.firing);
    if(!w||w->state.Snapshot().identity!=transfer.identity||w->reload.token!=transfer.token||
       !w->reload.transferOpen||!Current(*w,transfer.firing))return;
    w->reload.transferOpen=false;std::uint32_t loaded=0,reserve=0;
    if(!Counts(*w,loaded,reserve)||loaded> w->state.Snapshot().config.maxLoaded||loaded<transfer.loaded||reserve>transfer.reserve||
       std::uint64_t(loaded)+reserve!=std::uint64_t(transfer.loaded)+transfer.reserve){w->suspended=true;++unexplained;return;}
    w->reload.loaded=loaded;w->reload.reserve=reserve;w->reload.transferred|=loaded>transfer.loaded;
}
void AfterUpdate(std::uint32_t firing)noexcept {
    if(!Enabled())return;
    std::lock_guard lock(mutex);auto* w=Find(firing);
    if(!w||w->suspended||firing!=w->native.firing||!Current(*w,firing)||!w->reload.Busy()||
       !w->state.NativeReloadBusy()||!ReloadIdle(*w))return;
    if(w->reload.entered){
        FinishReload(*w,w->reload.transferred,w->reload.cancelled?NativeReloadPhase::Cancelled:
            w->reload.transferred?NativeReloadPhase::Completed:NativeReloadPhase::Cancelled);
    }else if(w->reload.cancelled||Now()-w->reload.preparedAt>=1500)
        FinishReload(*w,false,NativeReloadPhase::Cancelled);
}
bool Managed(std::uint32_t firing)noexcept {
    if(!Enabled())return false;
    std::lock_guard lock(mutex);const auto* w=Find(firing);return w&&Current(*w,firing);
}
bool PermitFire(std::uint32_t firing)noexcept {
    if(!Enabled())return true;
    std::lock_guard lock(mutex);auto* w=Find(firing);
    if(!w||!Current(*w,firing))return true;
    if(w->suspended)return false;
    if(w->reload.Busy()){
        if(w->reload.transferOpen)return false;
        if(w->reload.cancelled||Now()-w->reload.preparedAt>=1500||Now()-w->reload.requestedAt>=1500){
            w->reload.cancelled=true;
            // A prepared ledger transaction still needs owned idle/count
            // settlement in AfterUpdate. Making the phase terminal here
            // would skip that path and leave nativeReloadActive stuck.
            if(!w->reload.entered&&!w->state.NativeReloadBusy())w->reload.phase=NativeReloadPhase::Cancelled;
        }else if(!w->reload.entered&&w->state.CanFire()){
            w->reload.cancelled=true;
            w->reload.phase=NativeReloadPhase::Cancelled;
        }else {
            return false;
        }
    }
    if(firing==w->native.firing)return w->state.CanFire();
    std::uint32_t input=0,loaded=0;
    if(!Get(firing+0x94,input)||!Get(firing+0x7c,loaded)||loaded>PhysicalWeaponState::MaxCapacity)return false;
    const auto s=w->state.Snapshot();
    const auto budget=s.canFire?(s.config.family==PhysicalWeaponFamily::TubePump?1u:s.loaded):0u;
    const auto copy=firing==w->native.clientFiring[0]?0u:1u;
    const bool allowed=w->shots.Permit(copy,{input,loaded},budget);
    if(!allowed)++clientBlocked;
    return allowed;
}
void BeforeUpdate(std::uint32_t firing)noexcept {
    if(!Enabled())return;
    std::lock_guard lock(mutex);ExpireCommands();auto* w=Find(firing);
    if(!w||!Current(*w,firing))return;
    if(w->suspended&&firing==w->native.firing){
        // A transient read/observer failure can recover without inventing an
        // ammo baseline. The exact owned server must be idle and agree with
        // the recorded transaction, then with the committed weapon state.
        std::uint32_t loaded=0,reserve=0;
        if(Counts(*w,loaded,reserve)){
            const bool idle=ReloadIdle(*w);
            if(w->reload.phase==NativeReloadPhase::Queued&&!w->state.NativeReloadBusy()&&
               !w->reload.transferOpen&&SuspendedWeaponAgrees(w->state.Snapshot(),loaded,reserve,idle,false)){
                // Queued intent has not prepared a ledger transaction or
                // admitted native reload input. Retire only that intent;
                // there is no ammunition transfer or action to undo.
                w->reload.cancelled=true;w->reload.phase=NativeReloadPhase::Cancelled;
            }
            if(w->reload.Busy()&&w->state.NativeReloadBusy()&&NativeReloadSettlementAgrees(
                loaded,reserve,w->reload.loaded,w->reload.reserve,idle,w->reload.transferOpen)){
                // Use the same settlement as AfterUpdate. A real captured
                // transfer may finish; an untransferred/aborted reload keeps
                // its previous chamber and action. FinishReload rechecks
                // ownership, counts and conservation before committing.
                FinishReload(*w,w->reload.transferred,w->reload.cancelled?NativeReloadPhase::Cancelled:
                    w->reload.transferred?NativeReloadPhase::Completed:NativeReloadPhase::Cancelled);
            }
            if(SuspendedWeaponAgrees(w->state.Snapshot(),loaded,reserve,
                idle,w->reload.Busy()||w->state.NativeReloadBusy())&&EnsureCapacity(*w,firing)){
                w->suspended=false;w->shots.MechanicalBoundary();++w->revision;++suspensionRecoveries;
            }
        }
    }
    if(w->suspended){
        if(firing!=w->native.firing)return;
        // Retire a held resource's hand obligation without trusting the
        // unexplained native counts or reopening this weapon for firing.
        // No native capacity/count read or Store runs in this branch.
        for(auto& p:commands)if(p.command.id&&p.command.identity==w->state.Snapshot().identity&&
           PhysicalMagazineMetadataCleanup(p.command.kind)){
            const auto before=w->state.Snapshot();auto candidate=w->state;auto command=p.command;
            command.id=candidate.NextCommandId();
            const auto result=command.id?candidate.Apply(command):PhysicalWeaponResult{};
            const auto after=candidate.Snapshot();
            const bool metadataOnly=before.loaded==after.loaded&&before.feed==after.feed&&before.chamber==after.chamber&&
                before.looseReserve==after.looseReserve&&before.live==after.live&&before.spent==after.spent&&
                before.discarded==after.discarded&&before.admitted==after.admitted&&candidate.Conserved();
            if(result.Applied()&&metadataOnly){w->state=candidate;++w->revision;++suspendedMetadataCleanups;
                Complete(p.command.id,CommandStatus::Applied,result.magazine);}
            else Complete(p.command.id,CommandStatus::Rejected);
            p={};
        }
        return;
    }
    if(!EnsureCapacity(*w,firing))return;
    if(firing!=w->native.firing)return;
    std::uint32_t loaded=0,reserve=0;
    if(!Counts(*w,loaded,reserve)){w->suspended=true;return;}
    if(w->reload.Busy()) {
        if(w->reload.phase==NativeReloadPhase::Queued){
            if(w->reload.cancelled||Now()-w->reload.requestedAt>=1500){w->reload.phase=NativeReloadPhase::Cancelled;return;}
            if(!ReloadIdle(*w)||!ReloadSelected(*w))return;
            auto candidate=w->state;
            if(!candidate.BeginNativeReload(candidate.Snapshot().identity,w->reload.token).Applied()){
                w->reload.phase=NativeReloadPhase::Rejected;return;
            }
            if(!Publish(*w,candidate)){w->reload.phase=NativeReloadPhase::Rejected;return;}
            const auto prepared=w->state.Snapshot();w->reload.loaded=prepared.loaded;w->reload.reserve=prepared.looseReserve;
            w->reload.preparedAt=Now();w->reload.phase=NativeReloadPhase::Ready;
        }else if(!w->reload.transferOpen&&(loaded!=w->reload.loaded||reserve!=w->reload.reserve)){
            w->suspended=true;++unexplained;
        }
        if(w->reload.phase==NativeReloadPhase::Running&&Now()-w->reload.preparedAt>=15000)
            w->reload.cancelled=true; // Stop entry/pulses; settle actual counts at native idle.
        // No physical command publication or generic pickup reconciliation may
        // race the native transaction. Switches retain this obligation.
        return;
    }
    auto before=w->state.Snapshot();
    if(loaded!=before.loaded){w->suspended=true;++unexplained;return;}
    if(reserve!=before.looseReserve){
        // Native reserve supplies (ammo boxes/pickups) remain authoritative.
        // Native reload transfers are blocked for managed weapons. Never infer
        // a chamber or refill from an unexplained loaded-count change.
        const auto id=w->state.NextCommandId();
        if(!id){w->suspended=true;RejectCommands(before.identity);return;}
        const auto r=w->state.ObserveExternalAmmo(before.identity,id,loaded,reserve);
        if(!r.Applied()){w->suspended=true;++unexplained;return;}++w->revision;
    }
    for(;;){
        Pending* pending=nullptr;
        // Array holes are reused by Submit. Storage position is therefore not
        // submission order (new work can occupy a hole before an older seat).
        for(auto& p:commands)if(p.command.id&&p.command.identity==before.identity&&
            (!pending||p.command.id<pending->command.id))pending=&p;
        if(!pending)break;
        auto& p=*pending;auto command=p.command;
        // Public IDs are receipts, while the ledger has its own sequence.
        // An intervening verified native supply update cannot stale a queued
        // seat/rack command merely by consuming a newer global receipt ID.
        command.id=w->state.NextCommandId();
        if(!command.id){w->suspended=true;RejectCommands(before.identity);break;}
        auto candidate=w->state;const auto r=candidate.Apply(command);
        if(r.Applied()&&Publish(*w,candidate))Complete(p.command.id,CommandStatus::Applied,r.magazine,r.droppedMagazine);
        else {
            // Apply consumes rejected ledger IDs too. No native write is
            // needed for a rejected physical operation's unchanged counts.
            if(!r.Applied()){w->state=candidate;++w->revision;}
            Complete(p.command.id,CommandStatus::Rejected);
        }
        p={};if(w->suspended){RejectCommands(before.identity);break;}
    }
}
void AfterStep(std::uint32_t firing,std::uint32_t previous)noexcept {
    if(!Enabled())return;
    std::lock_guard lock(mutex);auto* w=Find(firing);
    if(!w||!Current(*w,firing))return;
    std::uint32_t next=0;if(!Get(firing+0x44,next))return;
    if(firing==w->native.firing&&w->reload.Busy()&&w->state.NativeReloadBusy()&&
       (previous==10||previous==11||previous==12||next==10||next==11||next==12)){
        w->reload.entered=true;w->reload.phase=NativeReloadPhase::Running;
    }
    // Native activation/listeners still run. Physical readiness replaces the
    // firing deploy delay; changing the per-instance timer leaves shared data alone.
    if(previous==0&&next==2){const float zero=0;Store(firing+0x50,&zero,4);}
    // Belt/hold-release families aren't admitted yet; defensive suppression
    // must also cover the separate state15 reload branch if one is added.
    if(previous==15&&next==10){const std::uint32_t ready=1;Store(firing+0x44,&ready,4);}
}
void WriteTelemetry(std::ostream& out){
    struct CachedWeapon {
        PhysicalWeaponSnapshot state{};
        Weapon::NativeReload reload{};
        std::uint32_t slot=0,clientWeapon=0;
        std::uint64_t revision=0,serverShots=0;
        bool suspended=false,pending=false;
    };
    std::array<CachedWeapon,64> cached{};std::size_t count=0;
    std::uint64_t shots=0,clientAccepted=0,failures=0,unexpected=0,retirements=0,lossRetired=0,recovered=0;
    bool enabled=false,fresh=false;
    {
        std::unique_lock lock(mutex,std::try_to_lock);
        if(!lock.owns_lock()){
            out<<"{\"available\":false,\"reason\":\"authority_busy\"}";return;
        }
        enabled=running.load(std::memory_order_acquire);fresh=inventoryFresh;
        shots=shotCount;clientAccepted=clientShots;failures=writeFailures;unexpected=unexplained;
        retirements=authorityRetirements;lossRetired=lossRetirements;
        recovered=suspensionRecoveries;
        for(const auto& w:weapons){
            const auto state=w.state.Snapshot();if(!state.initialized)continue;
            bool pending=false;
            for(const auto& c:commands)if(c.command.id&&c.command.identity==state.identity){pending=true;break;}
            cached[count++]={state,w.reload,w.native.slot,w.native.clientWeapon,w.revision,w.shots.Server(),w.suspended,pending};
        }
    }
    // Everything below is a by-value cached observation. No structural owner
    // recheck, firing count read, game function or I/O holds the authority lock.
    out<<"{\"available\":true,\"source\":\"cached_authority\",\"enabled\":"<<(enabled?"true":"false")
       <<",\"inventory_fresh\":"<<(fresh?"true":"false")<<",\"shots\":"<<shots<<",\"client_shots\":"<<clientAccepted
       <<",\"write_failures\":"<<failures<<",\"unexpected_ammo\":"<<unexpected
       <<",\"authority_retirements\":"<<retirements<<",\"loss_retirements\":"<<lossRetired
       <<",\"suspension_recoveries\":"<<recovered<<",\"weapons\":[";
    for(std::size_t n=0;n<count;++n){
        if(n)out<<',';const auto& w=cached[n];const auto& s=w.state;const auto& r=w.reload;
        const auto ready=ReadWeaponReadiness(s,w.suspended,r.Busy(),w.pending);
        const char* phase="rejected";
        switch(r.phase){
        case NativeReloadPhase::Queued:phase="queued";break;case NativeReloadPhase::Ready:phase="ready";break;
        case NativeReloadPhase::Running:phase="running";break;case NativeReloadPhase::Completed:phase="completed";break;
        case NativeReloadPhase::Cancelled:phase="cancelled";break;case NativeReloadPhase::Rejected:break;
        }
        out<<"{\"identity\":{\"actor\":"<<s.identity.actor<<",\"actor_generation\":"<<s.identity.actorGeneration
           <<",\"weapon\":"<<s.identity.weapon<<",\"weapon_generation\":"<<s.identity.weaponGeneration<<'}'
           <<",\"slot\":"<<w.slot<<",\"client_weapon\":"<<w.clientWeapon<<",\"revision\":"<<w.revision
           <<",\"ready_cue\":\""<<WeaponReadyCueName(ready.cue)<<"\",\"projection_pending\":"<<(ready.projectionPending?"true":"false")
           <<",\"loaded\":"<<s.loaded<<",\"reserve\":"<<s.looseReserve<<",\"feed\":"<<s.feed<<",\"chamber\":"<<unsigned(s.chamber)
           <<",\"magazine\":"<<s.insertedMagazine<<",\"needs_rack\":"<<(s.needsRack?"true":"false")
           <<",\"pump_owed\":"<<(s.pumpOwed?"true":"false")<<",\"can_fire\":"<<(s.canFire?"true":"false")
           <<",\"suspended\":"<<(w.suspended?"true":"false")<<",\"server_shot_sequence\":"<<w.serverShots
           <<",\"live_rounds\":"<<s.live<<",\"spent_rounds\":"<<s.spent<<",\"discarded_rounds\":"<<s.discarded
           <<",\"admitted_rounds\":"<<s.admitted<<",\"conserved\":"<<(s.live+s.spent+s.discarded==s.admitted?"true":"false")
           <<",\"native_reload\":{\"token\":"<<r.token<<",\"phase\":\""<<phase<<"\",\"busy\":"<<(r.Busy()?"true":"false")
           <<",\"cancelled\":"<<(r.cancelled?"true":"false")<<",\"entered\":"<<(r.entered?"true":"false")
           <<",\"transferred\":"<<(r.transferred?"true":"false")<<",\"observed_loaded\":"<<r.loaded
           <<",\"observed_reserve\":"<<r.reserve<<"}}";
    }
    out<<"]}";
}
void Report(std::ostream& out){
    std::lock_guard lock(mutex);
    out<<"{\"installed\":"<<(installed?"true":"false")<<",\"enabled\":"<<(Enabled()?"true":"false")<<",\"applied\":"<<applied<<",\"rejected\":"<<rejected
       <<",\"shots\":"<<shotCount<<",\"client_shots\":"<<clientShots<<",\"client_unmatched\":"<<clientUnmatched
       <<",\"client_blocked\":"<<clientBlocked<<",\"inventory_fresh\":"<<(inventoryFresh?"true":"false")
       <<",\"capacity_writes\":"<<capacityWrites<<",\"capacity_failures\":"<<capacityFailures
       <<",\"authority_retirements\":"<<authorityRetirements<<",\"loss_retirements\":"<<lossRetirements
       <<",\"loss_retire_enabled\":"<<(lossRetire?"true":"false")
       <<",\"suspended_metadata_cleanups\":"<<suspendedMetadataCleanups
       <<",\"suspension_recoveries\":"<<suspensionRecoveries
       <<",\"write_failures\":"<<writeFailures<<",\"unexpected_ammo\":"<<unexplained
       <<",\"selected_slot\":"<<(inventory?int(inventory->selectedSlot):-1)
       <<",\"client_selected_slot\":"<<(inventory?int(inventory->clientSelectedSlot):-1)<<",\"weapons\":[";
    bool first=true;for(const auto& w:weapons){const auto s=w.state.Snapshot();if(!s.initialized)continue;
        if(!first)out<<',';first=false;out<<"{\"item\":"<<w.native.item<<",\"slot\":"<<w.native.slot<<",\"incarnation\":"<<w.incarnation
        <<",\"loaded\":"<<s.loaded<<",\"reserve\":"<<s.looseReserve<<",\"chamber\":"<<unsigned(s.chamber)<<",\"magazine\":"<<s.insertedMagazine
        <<",\"needs_rack\":"<<(s.needsRack?"true":"false")<<",\"pump_owed\":"<<(s.pumpOwed?"true":"false")
        <<",\"feed_capacity\":"<<s.config.feedCapacity<<",\"max_loaded\":"<<s.config.maxLoaded
        <<",\"native_capacity_override_owned\":"<<(s.config.family==PhysicalWeaponFamily::ClosedBoltMagazine?"true":"false")
        <<",\"managed\":"<<(Current(w,w.native.firing)?"true":"false")
        <<",\"healthy\":"<<(!w.suspended&&Current(w,w.native.firing)&&w.state.Conserved()?"true":"false")
        <<",\"server_shot_sequence\":"<<w.shots.Server()<<",\"client_shot_sequences\":["<<w.shots.Client(0)<<','<<w.shots.Client(1)<<']'
        <<",\"live_rounds\":"<<s.live<<",\"spent_rounds\":"<<s.spent<<",\"discarded_rounds\":"<<s.discarded<<",\"admitted_rounds\":"<<s.admitted
        <<",\"conserved\":"<<(w.state.Conserved()?"true":"false")<<",\"suspended\":"<<(w.suspended?"true":"false")
        <<",\"native_reload\":{\"token\":"<<w.reload.token<<",\"phase\":"<<unsigned(w.reload.phase)
        <<",\"busy\":"<<(w.reload.Busy()?"true":"false")<<",\"cancelled\":"<<(w.reload.cancelled?"true":"false")
        <<",\"entered\":"<<(w.reload.entered?"true":"false")<<",\"transferred\":"<<(w.reload.transferred?"true":"false")
        <<",\"observed_loaded\":"<<w.reload.loaded<<",\"observed_reserve\":"<<w.reload.reserve<<'}'
        <<",\"native_copies\":[";
        unsigned copy=0;for(const auto firing:{w.native.firing,w.native.clientFiring[0],w.native.clientFiring[1]}){
            if(copy++)out<<',';std::uint32_t loaded=0,reserve=0;std::int32_t capacity=0;
            if(Current(w,firing)&&Get(firing+0x7c,loaded)&&Get(firing+0x80,reserve)&&Get(firing+0x98,capacity))
                out<<"{\"firing\":"<<firing<<",\"loaded\":"<<loaded<<",\"reserve\":"<<reserve<<",\"capacity_override\":"<<capacity<<'}';
            else out<<"null";
        }
        out<<"],\"magazines\":[";bool firstMagazine=true;
        for(const auto& magazine:w.state.Magazines())if(magazine.location!=PhysicalMagazineLocation::Unused){
            if(!firstMagazine)out<<',';firstMagazine=false;
            out<<"{\"id\":"<<magazine.id<<",\"rounds\":"<<magazine.rounds<<",\"location\":"<<unsigned(magazine.location)<<'}';
        }
        out<<"]}";}
    out<<"]}";
}
}
