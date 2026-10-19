#pragma once
#include <array>
#include <cstdint>

namespace fvr::bc2 {
// Stick weapon selection plans over BC2 WeaponSwitchingEnum slots (reflected
// enum, docs/re/BC2_TYPEINFO_HARNESS_TYPES.md): 0 Primary1, 1 Primary2,
// 2 GrenadeLauncher1, 3 GrenadeLauncher2, 4 StaticGadget, 5 DynamicGadget1,
// 6 DynamicGadget2, 7 MeleeWeapon, 8 HandGrenade. 9/10 are pseudo-slots
// (last/other primary) and never a selection target.
//
// Pure planning only: the caller supplies which slots hold an item it has
// independently verified, and the native server callback revalidates every
// candidate (identity, native validity) before selecting it. A plan is an
// ordered fallback list, not permission.
enum class StickSwitch : int {
    PreviousGun=-1, NextGun=1,          // vertical flick: guns only (owner decision, 2026-10-08)
    CycleBackward=-2, CycleForward=2,   // right stick click: everything carried
};
inline constexpr std::uint32_t EquipmentSlotCount=9;
// Each gun is followed by its own underbarrel launcher. The live SP_02 switch
// map pairs them: from Primary1 EiaGrenadeLauncher -> [2 3], from Primary2
// -> [3 2]. Gadgets follow, then knife and hand grenade (the map's own
// EiaMeleeAttack -> [7] and EiaThrowGrenade -> [8] targets).
inline constexpr std::array<std::uint8_t,EquipmentSlotCount> EquipmentCycleOrder{0,2,1,3,4,5,6,7,8};
struct EquipmentCycleSettings {
    bool enabled=true;          // FVR_BC2_STICK_CYCLE=0 restores the October 8 equipment-only click
    bool meleeAndGrenade=true;  // FVR_BC2_STICK_CYCLE_MELEE_GRENADE=0 keeps the knife and grenade out
};
struct EquipmentCyclePlan {
    std::array<std::uint8_t,EquipmentSlotCount> slots{};
    unsigned count=0;
    void Add(std::uint32_t slot)noexcept {
        if(slot>=EquipmentSlotCount||count>=slots.size())return;
        for(unsigned n=0;n<count;++n)if(slots[n]==slot)return;
        slots[count++]=std::uint8_t(slot);
    }
    int First()const noexcept{return count?int(slots[0]):-1;}
};
inline bool SlotOccupied(std::uint32_t occupied,std::uint32_t slot)noexcept{return slot<EquipmentSlotCount&&(occupied>>slot&1u);}
// Every carried item after `from`, in cycle order, wrapping once; `from` itself
// is never a candidate. An unknown/pseudo current slot starts at the first
// (forward) or last (backward) entry of the order.
inline EquipmentCyclePlan PlanEquipmentCycle(std::uint32_t occupied,std::uint32_t from,bool forward,
    const EquipmentCycleSettings& settings={})noexcept {
    EquipmentCyclePlan plan;
    const auto allowed=[&](std::uint32_t slot){return SlotOccupied(occupied,slot)&&slot!=from&&
        (settings.meleeAndGrenade||(slot!=7&&slot!=8));};
    constexpr int n=int(EquipmentCycleOrder.size());
    int at=-1;for(int i=0;i<n;++i)if(EquipmentCycleOrder[i]==from)at=i;
    if(at<0)at=forward?-1:n;
    for(int step=1;step<=n;++step){
        const int index=((at+(forward?step:-step))%n+n)%n;
        if(allowed(EquipmentCycleOrder[index]))plan.Add(EquipmentCycleOrder[index]);
    }
    return plan;
}
// Kill-switch behavior (October 8): the next carried launcher/gadget 2..6 after
// the current slot; from a gun only 2..6; past the last equipment, back to the
// last primary (inventory+0x154), else slot 0, else slot 1.
inline EquipmentCyclePlan PlanLegacyEquipment(std::uint32_t occupied,std::uint32_t from,std::uint32_t lastPrimary)noexcept {
    EquipmentCyclePlan plan;
    const auto ok=[&](std::uint32_t slot){return slot!=from&&SlotOccupied(occupied,slot);};
    for(std::uint32_t slot=from>=2&&from<=6?from+1:2;slot<=6;++slot)if(ok(slot))plan.Add(slot);
    if(from>=2&&from<=6){if(lastPrimary<=1&&ok(lastPrimary))plan.Add(lastPrimary);if(ok(0))plan.Add(0);if(ok(1))plan.Add(1);}
    return plan;
}
// Vertical flick: the other gun from a gun; from anything else, back to the last
// primary, else slot 0, else slot 1. Empty when there is no other gun to go to.
inline EquipmentCyclePlan PlanGunFlick(std::uint32_t occupied,std::uint32_t from,std::uint32_t lastPrimary)noexcept {
    EquipmentCyclePlan plan;
    const auto ok=[&](std::uint32_t slot){return slot!=from&&SlotOccupied(occupied,slot);};
    if(from<=1){if(ok(1-from))plan.Add(1-from);return plan;}
    if(lastPrimary<=1&&ok(lastPrimary))plan.Add(lastPrimary);if(ok(0))plan.Add(0);if(ok(1))plan.Add(1);
    return plan;
}
inline EquipmentCyclePlan PlanStickSwitch(StickSwitch request,std::uint32_t occupied,std::uint32_t from,std::uint32_t lastPrimary,
    const EquipmentCycleSettings& settings={})noexcept {
    switch(request){
    case StickSwitch::NextGun:case StickSwitch::PreviousGun:return PlanGunFlick(occupied,from,lastPrimary);
    case StickSwitch::CycleForward:
        return settings.enabled?PlanEquipmentCycle(occupied,from,true,settings):PlanLegacyEquipment(occupied,from,lastPrimary);
    case StickSwitch::CycleBackward:
        return settings.enabled?PlanEquipmentCycle(occupied,from,false,settings):EquipmentCyclePlan{};
    }
    return {};
}
// Repeated clicks chain from the slot the previous request is heading to, not
// from a native selection that has not caught up yet. The published inventory
// snapshot is taken before the server callback selects, so it lags one update.
struct SwitchChainState {
    int pendingTarget=-1;              // first candidate of a latched, unconsumed request
    int selectedTarget=-1;             // last slot the server callback actually selected
    std::uint64_t selectedAtMs=0;
};
inline std::uint32_t SwitchChainBase(const SwitchChainState& s,std::uint32_t observedSlot,std::uint64_t nowMs,
    std::uint64_t catchUpMs=500)noexcept {
    if(s.pendingTarget>=0&&s.pendingTarget<int(EquipmentSlotCount))return std::uint32_t(s.pendingTarget);
    if(s.selectedTarget>=0&&s.selectedTarget<int(EquipmentSlotCount)&&std::uint32_t(s.selectedTarget)!=observedSlot&&
       nowMs>=s.selectedAtMs&&nowMs-s.selectedAtMs<catchUpMs)return std::uint32_t(s.selectedTarget);
    return observedSlot;
}
// Gun slots keep the physical runtime's empty-weapon/cooldown waiver; every
// other slot must pass the inventory's own validity callback first, and the
// knife/grenade are selected with native validation on.
inline bool SlotNeedsNativeValidity(std::uint32_t slot)noexcept{return slot>=2;}
inline bool SlotSelectValidates(std::uint32_t slot)noexcept{return slot==7||slot==8;}
}
