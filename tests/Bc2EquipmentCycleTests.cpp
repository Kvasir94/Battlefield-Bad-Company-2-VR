#include "Test.h"
#include "../src/games/bc2/Bc2EquipmentCycle.h"
#include <initializer_list>
#include <vector>
using namespace fvr::bc2;
namespace {
std::uint32_t Mask(std::initializer_list<unsigned> slots){std::uint32_t m=0;for(auto s:slots)m|=1u<<s;return m;}
bool Is(const EquipmentCyclePlan& p,std::initializer_list<unsigned> slots){
    if(p.count!=slots.size())return false;unsigned n=0;for(auto s:slots)if(p.slots[n++]!=s)return false;return true;
}
// SP_02 starting kit (marlowe.dbx, live test-range dump): XM8 0, XM320 2, LZ-537 4,
// knife 7, hand grenade 8. Slot 1 is empty until a second gun is picked up.
const auto Sp02=Mask({0,2,4,7,8});
int ForwardCycleVisitsEverythingCarried(){
    // Forward from the rifle: launcher, designator, knife, grenade, back to the rifle.
    CHECK(Is(PlanEquipmentCycle(Sp02,0,true),{2,4,7,8}));
    CHECK(Is(PlanEquipmentCycle(Sp02,2,true),{4,7,8,0}));
    CHECK(Is(PlanEquipmentCycle(Sp02,8,true),{0,2,4,7}));
    // Walking the first candidate each time returns to the start after five clicks.
    std::uint32_t at=0;std::vector<unsigned> visited;
    for(int i=0;i<5;++i){at=std::uint32_t(PlanEquipmentCycle(Sp02,at,true).First());visited.push_back(at);}
    CHECK((visited==std::vector<unsigned>{2,4,7,8,0}));
    // A second gun follows the first gun's launcher; its own launcher follows it.
    const auto two=Mask({0,1,2,3,4,5,7,8});
    CHECK(Is(PlanEquipmentCycle(two,0,true),{2,1,3,4,5,7,8}));
    CHECK(Is(PlanEquipmentCycle(two,1,true),{3,4,5,7,8,0,2}));
    return 0;
}
int BackwardCycleIsTheReverse(){
    CHECK(Is(PlanEquipmentCycle(Sp02,0,false),{8,7,4,2}));
    CHECK(Is(PlanEquipmentCycle(Sp02,4,false),{2,0,8,7}));
    std::uint32_t at=0;
    for(int i=0;i<5;++i)at=std::uint32_t(PlanEquipmentCycle(Sp02,at,false).First());
    CHECK(at==0);
    return 0;
}
int UnknownCurrentStartsAtTheEnds(){
    // Pseudo-slots 9/10 or an out-of-range selection: forward from the first
    // entry, backward from the last; nothing is skipped or duplicated.
    CHECK(Is(PlanEquipmentCycle(Sp02,9,true),{0,2,4,7,8}));
    CHECK(Is(PlanEquipmentCycle(Sp02,63,false),{8,7,4,2,0}));
    // Current slot not carried (a just-thrown last gadget): still deterministic.
    CHECK(Is(PlanEquipmentCycle(Mask({0,7}),5,true),{7,0}));
    CHECK(PlanEquipmentCycle(Mask({0}),0,true).count==0);
    CHECK(PlanEquipmentCycle(0,0,true).First()==-1);
    return 0;
}
int MeleeGrenadeSwitchKeepsThemOut(){
    EquipmentCycleSettings s;s.meleeAndGrenade=false;
    CHECK(Is(PlanEquipmentCycle(Sp02,0,true,s),{2,4}));
    CHECK(Is(PlanEquipmentCycle(Sp02,4,true,s),{0,2}));
    CHECK(Is(PlanEquipmentCycle(Sp02,0,false,s),{4,2}));
    return 0;
}
int KillSwitchRestoresOctoberEighth(){
    EquipmentCycleSettings off;off.enabled=false;
    // Equipment-only click: launcher then gadgets, then back to the last primary.
    CHECK(Is(PlanStickSwitch(StickSwitch::CycleForward,Sp02,0,0,off),{2,4}));
    CHECK(Is(PlanStickSwitch(StickSwitch::CycleForward,Sp02,4,0,off),{0}));
    CHECK(Is(PlanStickSwitch(StickSwitch::CycleForward,Mask({0,1,2,4}),2,1,off),{4,1,0}));
    // No backward cycle and never the knife or grenade.
    CHECK(PlanStickSwitch(StickSwitch::CycleBackward,Sp02,0,0,off).count==0);
    CHECK(Is(PlanStickSwitch(StickSwitch::CycleForward,Mask({0,7,8}),0,0,off),{}));
    return 0;
}
int FlicksStayGunsOnly(){
    // Single-gun SP_02 kit: a flick has nowhere to go (the worn failure in
    // native-trace-20261009-030348-709: 54 no_target, 0 gun switches).
    CHECK(PlanStickSwitch(StickSwitch::NextGun,Sp02,0,0).count==0);
    CHECK(PlanStickSwitch(StickSwitch::PreviousGun,Sp02,0,0).count==0);
    const auto two=Mask({0,1,2,4});
    CHECK(Is(PlanStickSwitch(StickSwitch::NextGun,two,0,0),{1}));
    CHECK(Is(PlanStickSwitch(StickSwitch::PreviousGun,two,1,0),{0}));
    // From equipment a flick returns to the last gun first.
    CHECK(Is(PlanStickSwitch(StickSwitch::NextGun,two,4,1),{1,0}));
    CHECK(Is(PlanStickSwitch(StickSwitch::NextGun,Sp02,7,0),{0}));
    return 0;
}
int ChainingFollowsTheRequestInFlight(){
    SwitchChainState s;
    CHECK(SwitchChainBase(s,0,1000)==0);
    s.pendingTarget=2;CHECK(SwitchChainBase(s,0,1000)==2);
    s.pendingTarget=-1;s.selectedTarget=4;s.selectedAtMs=1000;
    CHECK(SwitchChainBase(s,2,1100)==4);  // snapshot one update behind
    CHECK(SwitchChainBase(s,4,1100)==4);  // caught up
    CHECK(SwitchChainBase(s,2,1600)==2);  // stale selection evidence is ignored
    CHECK(SwitchChainBase(s,2,900)==2);   // clock reversal is ignored
    // Two quick clicks from the rifle go launcher -> designator, not launcher twice.
    SwitchChainState q;q.pendingTarget=PlanEquipmentCycle(Sp02,0,true).First();
    CHECK(PlanEquipmentCycle(Sp02,SwitchChainBase(q,0,0),true).First()==4);
    return 0;
}
int NativeValidationPolicy(){
    CHECK(!SlotNeedsNativeValidity(0)&&!SlotNeedsNativeValidity(1));
    for(unsigned s=2;s<EquipmentSlotCount;++s)CHECK(SlotNeedsNativeValidity(s));
    CHECK(!SlotSelectValidates(0)&&!SlotSelectValidates(4)&&SlotSelectValidates(7)&&SlotSelectValidates(8));
    EquipmentCyclePlan p;p.Add(3);p.Add(3);p.Add(9);p.Add(0);CHECK(Is(p,{3,0}));
    return 0;
}
}
int main(){
    CHECK(ForwardCycleVisitsEverythingCarried()==0);
    CHECK(BackwardCycleIsTheReverse()==0);
    CHECK(UnknownCurrentStartsAtTheEnds()==0);
    CHECK(MeleeGrenadeSwitchKeepsThemOut()==0);
    CHECK(KillSwitchRestoresOctoberEighth()==0);
    CHECK(FlicksStayGunsOnly()==0);
    CHECK(ChainingFollowsTheRequestInFlight()==0);
    CHECK(NativeValidationPolicy()==0);
    return 0;
}
