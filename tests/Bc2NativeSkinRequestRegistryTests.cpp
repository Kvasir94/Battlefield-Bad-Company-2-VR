// Synthetic allocation addresses and equipment only; no native memory access.
#include "Bc2NativeSkinRequestRegistry.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);std::exit(1);}}while(0)
using namespace fvr::bc2;
namespace {
NativeSkinOwner Owner(){NativeSkinOwner owner{1,2,3,4,5,6,{}};
    owner.equipment.weapon=3;owner.equipment.data=7;owner.equipment.persistence=8;
    owner.equipment.asset[0]='x';return owner;}
void OwnershipAndReuse(){Bc2NativeSkinRequestRegistry registry;const auto owner=Owner();
    const auto token=registry.RecordAllocation(0x1000,0xd0,owner);CHECK(token);
    const auto record=registry.Read(0x1000,owner);CHECK(record&&record->token==*token);CHECK(registry.Matches(*record,owner));
    CHECK(!registry.Read(0x1001,owner));
    for(unsigned n=0;n<10;++n){auto changed=owner;
        switch(n){case 0:++changed.actor;break;case 1:++changed.weak;break;case 2:++changed.weapon;break;
        case 3:++changed.ownerGeneration;break;case 4:++changed.space;break;case 5:++changed.equipmentGeneration;break;
        case 6:++changed.equipment.weapon;break;case 7:++changed.equipment.data;break;
        case 8:++changed.equipment.persistence;break;case 9:changed.equipment.asset[0]='y';break;}
        CHECK(!registry.Read(0x1000,changed));CHECK(!registry.Matches(*record,changed));}
    CHECK(!registry.RecordAllocation(0x1000,0xd0));CHECK(!registry.Read(0x1000,owner));CHECK(!registry.Matches(*record,owner));
    const auto replacement=registry.RecordAllocation(0x1000,0xd0,owner);CHECK(replacement&&*replacement>*token);
    CHECK(!registry.Matches(*record,owner));registry.Erase(*token);CHECK(registry.Read(0x1000,owner));
    registry.Erase(*replacement);CHECK(!registry.Read(0x1000,owner));
    CHECK(registry.RecordAllocation(0x1000,0xd0,owner));registry.Clear();CHECK(!registry.Read(0x1000,owner));
    const auto afterClear=registry.RecordAllocation(0x1000,0xd0,owner);CHECK(afterClear&&*afterClear>*replacement);
}
void OverlapAndBounds(){const auto owner=Owner();Bc2NativeSkinRequestRegistry registry;
    for(const auto range:std::array<std::array<std::uint64_t,2>,6>{{{0x1000,1},{0x10cf,1},{0xff0,0x20},
        {0x10c0,0x20},{0x1010,2},{0xff0,0x100}}}){
        CHECK(registry.RecordAllocation(0x1000,0xd0,owner));
        CHECK(!registry.RecordAllocation(range[0],range[1]));CHECK(!registry.Read(0x1000,owner));}
    CHECK(registry.RecordAllocation(0x1000,0xd0,owner));
    CHECK(!registry.RecordAllocation(0xff0,0x10));CHECK(registry.Read(0x1000,owner));
    CHECK(!registry.RecordAllocation(0x10d0,1));CHECK(registry.Read(0x1000,owner));
    CHECK(registry.RecordAllocation(0x1200,0xd0,owner));
    CHECK(!registry.RecordAllocation(0x1080,0x190));CHECK(!registry.Read(0x1000,owner));CHECK(!registry.Read(0x1200,owner));
    for(const auto range:std::array<std::array<std::uint64_t,2>,3>{{{0,1},{0x5000,0},
        {std::numeric_limits<std::uint64_t>::max()-2,3}}}){
        CHECK(registry.RecordAllocation(0x1000,0xd0,owner));
        CHECK(!registry.RecordAllocation(range[0],range[1],owner));CHECK(!registry.Read(0x1000,owner));}
    CHECK(registry.RecordAllocation(0x1000,0xd0,owner));auto bad=owner;bad.equipmentGeneration=0;
    CHECK(!registry.RecordAllocation(0x1000,0xd0,bad));CHECK(!registry.Read(0x1000,owner));
    CHECK(!registry.RecordAllocation(0x1000,0xd1,owner));CHECK(!registry.Read(0x1000,owner));
}
void EvictionAndConcurrency(){Bc2NativeSkinRequestRegistry registry;const auto owner=Owner();
    CHECK(registry.RecordAllocation(0x1000,0xd0,owner));const auto oldest=registry.Read(0x1000,owner);CHECK(oldest);
    for(unsigned n=1;n<=registry.Slots;++n)CHECK(registry.RecordAllocation(0x1000+n*0x100,0xd0,owner));
    CHECK(!registry.Read(0x1000,owner));CHECK(!registry.Matches(*oldest,owner));
    CHECK(registry.Read(0x1000+registry.Slots*0x100,owner));registry.Clear();
    // Distinct active allocations must remain readable under concurrent hooks.
    std::array<std::thread,4> workers;
    for(unsigned i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{const auto address=0x1000+i*0x100;
        for(unsigned n=0;n<500;++n){const auto token=registry.RecordAllocation(address,0xd0,owner);CHECK(token);
            const auto record=registry.Read(address,owner);CHECK(record&&record->token==*token);CHECK(registry.Matches(*record,owner));}});
    for(auto& worker:workers)worker.join();
}
}
int main(){OwnershipAndReuse();OverlapAndBounds();EvictionAndConcurrency();}
