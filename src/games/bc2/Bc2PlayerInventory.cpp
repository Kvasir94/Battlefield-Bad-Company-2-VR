#include "Bc2PlayerInventory.h"
#include <algorithm>
#include <cmath>

namespace fvr::bc2 {
namespace {
// Build 795745: ServerItem ctor 0x561b20 stores owner at +8, data at +4,
// vtable 0x14172f0 and a native u16 instance serial at +0x20. Inventory ctor
// 0x6ec700 stores its listener at +0x148. Server listener vf0 0x6b1770 is
// the soldier+0x168 subobject (vtable 0x1424718).
constexpr std::uint32_t ItemVtableRva=0x10172f0,InventoryVtableRva=0x1025b88,ListenerVtableRva=0x1024718;
struct Reader {
    const ReloadStateMemory& memory;
    bool Read(std::uint64_t at,void* out,std::size_t size)const noexcept {
        return memory.read&&at>=0x10000&&size&&size<=4096&&at+size<=UINT32_MAX&&
            memory.read(memory.context,std::uint32_t(at),out,size);
    }
    template<class T>bool Get(std::uint64_t at,T& value)const noexcept{return Read(at,&value,sizeof value);}
    bool Eq(std::uint64_t at,std::uint32_t value)const noexcept{std::uint32_t actual=0;return Get(at,actual)&&actual==value;}
    bool Type(std::uint32_t at,const char* name)const noexcept{return memory.type&&at>=0x10000&&memory.type(memory.context,at,name);}
};
struct Readback {
    PlayerInventorySnapshot snapshot{};
    std::uint32_t manager=0,players=0,capacity=0,playerId=0,playerTable=0,soldierTable=0,soldierData=0;
    std::uint32_t begin=0,end=0,clientBegin=0,clientEnd=0,clientCount=0,clientInventoryData=0;
    std::array<std::uint32_t,64> items{},clientItems{};
    bool operator==(const Readback&)const=default;
};
bool Valid(const ReloadServerBinding& server,const ReloadStateBinding& firing,std::uint32_t base,const PlayerInventoryAnchor& anchor)noexcept {
    return base&&base==server.preferredBase&&base==firing.preferredBase&&server.imageSize==firing.imageSize&&
        std::uint64_t(base)+server.imageSize<=UINT32_MAX&&anchor.player>=0x10000&&anchor.soldier>=0x10000&&
        anchor.soldier<=UINT32_MAX-0x168&&anchor.weak>=0x10000&&anchor.actorGeneration&&
        std::uint64_t(server.contextRva)+12<=server.imageSize&&std::uint64_t(server.managerVtableRva)+4<=server.imageSize&&
        server.controlledGetterRva<server.imageSize&&std::uint64_t(firing.firingVtableRva)+4<=server.imageSize&&
        ItemVtableRva+4<=server.imageSize&&InventoryVtableRva+4<=server.imageSize&&ListenerVtableRva+8<=server.imageSize;
}
bool Vector(const Reader& r,std::uint32_t owner,unsigned offset,std::uint32_t& begin,std::uint32_t& end,
    std::uint32_t& count,std::array<std::uint32_t,64>& values)noexcept {
    if(!r.Get(std::uint64_t(owner)+offset,begin)||!r.Get(std::uint64_t(owner)+offset+4,end)||end<begin||
        end-begin>values.size()*sizeof(std::uint32_t)||(end-begin)%sizeof(std::uint32_t))return false;
    count=(end-begin)/sizeof(std::uint32_t);
    return !count||(begin>=0x10000&&r.Read(begin,values.data(),end-begin));
}
bool Firing(const Reader& r,std::uint32_t base,const ReloadStateBinding& binding,std::uint32_t object,
    std::uint32_t data,std::uint32_t ammo)noexcept {
    return object>=0x10000&&data>=0x10000&&ammo>=0x10000&&r.Eq(object,base+binding.firingVtableRva)&&
        r.Eq(std::uint64_t(object)+8,data)&&r.Eq(std::uint64_t(object)+12,ammo);
}
bool ReadOnce(const Reader& r,const ReloadServerBinding& binding,const ReloadStateBinding& firing,std::uint32_t base,
    const PlayerInventoryAnchor& anchor,Readback& out)noexcept {
    auto& s=out.snapshot;s.anchor=anchor;
    std::uint8_t playerFlags=0,soldierFlags=0;
    if(!r.Type(anchor.soldier,"ClientSoldierEntity")||!r.Get(std::uint64_t(anchor.player)+0xccd,playerFlags)||!(playerFlags&8)||
        !r.Eq(std::uint64_t(anchor.player)+0xc54,anchor.weak)||!r.Eq(anchor.weak,anchor.soldier+4)||
        !r.Eq(std::uint64_t(anchor.player)+0xc68,anchor.soldier)||!r.Eq(std::uint64_t(anchor.soldier)+0x220,anchor.player)||
        !r.Get(std::uint64_t(anchor.soldier)+0x114,soldierFlags)||
        !r.Get(std::uint64_t(anchor.soldier)+((soldierFlags&1)?0x24c:0x248),s.clientInventory)||
        !r.Get(std::uint64_t(s.clientInventory)+4,out.clientInventoryData)||!r.Type(out.clientInventoryData,"WeaponSwitchingData")||
        !r.Get(std::uint64_t(s.clientInventory)+0x14c,s.clientSelectedSlot)||
        !Vector(r,anchor.soldier,0x260,out.clientBegin,out.clientEnd,out.clientCount,out.clientItems))return false;
    if(!r.Get(std::uint64_t(base)+binding.contextRva+8,out.manager)||!r.Eq(out.manager,base+binding.managerVtableRva)||
        !r.Get(std::uint64_t(anchor.player)+0x154,out.playerId)||!r.Get(std::uint64_t(out.manager)+4,out.capacity)||
        !out.capacity||out.capacity>256||out.playerId>=out.capacity||!r.Get(std::uint64_t(out.manager)+0x6c,out.players)||
        !r.Get(std::uint64_t(out.players)+out.playerId*4,s.serverPlayer)||!r.Eq(std::uint64_t(s.serverPlayer)+0x154,out.playerId)||
        !r.Get(s.serverPlayer,out.playerTable)||out.playerTable<base||std::uint64_t(out.playerTable)+0x34>std::uint64_t(base)+binding.imageSize||
        !r.Eq(std::uint64_t(out.playerTable)+0x30,base+binding.controlledGetterRva)||
        !r.Get(std::uint64_t(s.serverPlayer)+0xc3c,s.serverSoldier)||!r.Eq(std::uint64_t(s.serverPlayer)+0xc6c,s.serverSoldier)||
        !r.Type(s.serverSoldier,"ServerSoldierEntity")||!r.Get(s.serverSoldier,out.soldierTable)||
        !r.Get(std::uint64_t(s.serverSoldier)+0xc,out.soldierData)||out.soldierData<0x10000||
        !r.Eq(std::uint64_t(anchor.soldier)+0xc,out.soldierData)||!r.Eq(std::uint64_t(s.serverSoldier)+0x220,s.serverPlayer)||
        !r.Get(std::uint64_t(s.serverSoldier)+0x2b4,s.inventory)||!r.Eq(s.inventory,base+InventoryVtableRva)||
        !r.Eq(std::uint64_t(s.inventory)+0x148,s.serverSoldier+0x168)||!r.Eq(std::uint64_t(s.serverSoldier)+0x168,base+ListenerVtableRva)||
        !r.Get(std::uint64_t(s.inventory)+4,s.inventoryData)||!r.Type(s.inventoryData,"WeaponSwitchingData")||
        !r.Get(std::uint64_t(s.inventory)+0x14c,s.selectedSlot)||!r.Get(std::uint64_t(s.inventory)+0x154,s.lastPrimary)||
        !Vector(r,s.serverSoldier,0x2b8,out.begin,out.end,s.count,out.items))return false;
    for(std::uint32_t slot=0;slot<s.count;++slot){
        auto& item=s.slots[slot];item.slot=slot;item.item=out.items[slot];if(!item.item)continue;
        // Non-null malformed or aliased items reject the snapshot; an ordinary
        // equipment item with no compatible firing object does not.
        if(std::count(out.items.begin(),out.items.begin()+s.count,item.item)!=1||!r.Eq(item.item,base+ItemVtableRva)||
            !r.Eq(std::uint64_t(item.item)+8,s.serverSoldier)||!r.Get(std::uint64_t(item.item)+4,item.data)||
            !r.Type(item.data,"SoldierWeaponData")||!r.Get(std::uint64_t(item.item)+0x20,item.serial)||
            !r.Get(std::uint64_t(item.item)+0xc,item.effects)||!r.Get(std::uint64_t(item.item)+0x10,item.firing))return false;
        item.owned=true;
        if(r.Get(std::uint64_t(item.data)+0x98,item.firingData)&&r.Type(item.firingData,"WeaponFiringData")&&
            r.Get(std::uint64_t(item.firingData)+0x40,item.primaryFire)&&r.Type(item.primaryFire,"FiringFunctionData")&&
            std::uint64_t(item.primaryFire)+0x170<=UINT32_MAX){
            item.ammoAddress=item.primaryFire+0x170;
            std::int32_t logic=-1,reload=-1;
            if(r.Get(std::uint64_t(item.primaryFire)+0x24,logic)&&logic>=0&&logic<=255)item.fireLogicType=logic;
            if(r.Get(std::uint64_t(item.primaryFire)+0x20,reload)&&reload>=0&&reload<=255)item.reloadType=reload;
            item.serverFiringVerified=Firing(r,base,firing,item.firing,item.firingData,item.ammoAddress);
            if(item.serverFiringVerified){
                std::int32_t capacityOverride=-1,baseCapacity=-1;float multiplier=0;
                const bool haveOverride=r.Get(std::uint64_t(item.firing)+0x98,capacityOverride);
                if(r.Get(std::uint64_t(item.primaryFire)+0x184,baseCapacity)&&baseCapacity>=0&&baseCapacity<=1000000){
                    item.authoredBaseCapacity=baseCapacity;
                    if(r.Get(std::uint64_t(item.firing)+0x74,multiplier)&&std::isfinite(multiplier)&&multiplier>=0&&multiplier<=1024){
                        const double capacity=double(baseCapacity)*multiplier;
                        // Match the existing observer: no invented native rounding.
                        if(capacity<=1000000&&std::floor(capacity)==capacity)item.authoredCapacity=std::int32_t(capacity);
                    }
                }
                if(haveOverride&&capacityOverride>=0&&capacityOverride<=1000000)item.effectiveCapacity=capacityOverride;
                else if(haveOverride&&capacityOverride==-1)item.effectiveCapacity=item.authoredCapacity;
            }
            std::uint32_t callback=0;
            item.effectsVerified=r.Type(item.effects,"ServerWeaponFiringEffects")&&r.Eq(std::uint64_t(item.effects)+0x10,item.firingData)&&
                r.Eq(std::uint64_t(item.effects)+0x128,s.serverPlayer)&&r.Get(std::uint64_t(item.effects)+0xd4,callback)&&
                r.Eq(std::uint64_t(callback)+4,item.firingData);
        }
        const auto wrapper=slot<out.clientCount?out.clientItems[slot]:0;
        if(wrapper&&std::count(out.clientItems.begin(),out.clientItems.begin()+out.clientCount,wrapper)==1&&
            r.Eq(std::uint64_t(wrapper)+4,item.data)){
            item.clientWeapon=wrapper;
            if(r.Get(std::uint64_t(wrapper)+0x3c,item.clientFiring[0])&&r.Get(std::uint64_t(wrapper)+0x40,item.clientFiring[1]))
                item.clientFiringVerified=item.serverFiringVerified&&item.clientFiring[0]!=item.clientFiring[1]&&
                    item.clientFiring[0]!=item.firing&&item.clientFiring[1]!=item.firing&&
                    Firing(r,base,firing,item.clientFiring[0],item.firingData,item.ammoAddress)&&
                    Firing(r,base,firing,item.clientFiring[1],item.firingData,item.ammoAddress);
        }
    }
    return true;
}
}
std::optional<PlayerInventorySnapshot> ReadPlayerInventory(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& firing,std::uint32_t base,const PlayerInventoryAnchor& anchor)noexcept {
    if(!memory.read||!memory.type||!Valid(server,firing,base,anchor))return {};
    const Reader reader{memory};Readback before{},after{};
    if(!ReadOnce(reader,server,firing,base,anchor,before)||!ReadOnce(reader,server,firing,base,anchor,after)||before!=after)return {};
    return after.snapshot;
}
std::optional<PlayerInventorySnapshot> RevalidatePlayerInventory(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& firing,std::uint32_t base,const PlayerInventorySnapshot& previous)noexcept {
    const auto current=ReadPlayerInventory(memory,server,firing,base,previous.anchor);
    if(!current||!SamePlayerInventoryAuthority(*current,previous))return {};
    return current;
}
std::optional<PlayerInventoryCandidate> Candidate(const PlayerInventorySnapshot& snapshot,std::uint32_t slot)noexcept {
    if(!snapshot.anchor.actorGeneration||!snapshot.serverPlayer||!snapshot.serverSoldier||!snapshot.inventory||
        slot>=snapshot.count||slot>=snapshot.slots.size())return {};
    const auto& item=snapshot.slots[slot];
    if(!item.owned||!item.item||!item.data||item.slot!=slot)return {};
    return PlayerInventoryCandidate{snapshot.anchor,snapshot.serverPlayer,snapshot.serverSoldier,snapshot.inventory,item};
}
std::optional<PlayerInventoryCandidate> RevalidatePlayerInventoryCandidate(const ReloadStateMemory& memory,const ReloadServerBinding& server,
    const ReloadStateBinding& firing,std::uint32_t base,const PlayerInventoryCandidate& previous)noexcept {
    const auto current=ReadPlayerInventory(memory,server,firing,base,previous.anchor);if(!current)return {};
    const auto candidate=Candidate(*current,previous.item.slot);
    if(!candidate||candidate->serverPlayer!=previous.serverPlayer||candidate->serverSoldier!=previous.serverSoldier||
        candidate->inventory!=previous.inventory||candidate->item.item!=previous.item.item||candidate->item.data!=previous.item.data||
        candidate->item.serial!=previous.item.serial)return {};
    return candidate;
}
}
