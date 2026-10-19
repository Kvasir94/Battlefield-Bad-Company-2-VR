#pragma once
#include "Bc2MagazineActionCatalog.h"
#include "Bc2WeaponMode.h"
#include <cmath>
#include <cstdint>
#include <optional>

// Per-weapon feel defaults read from the engine's own firing data, with the
// hand table (Bc2MagazineActionCatalog.h) as the fallback.
//
// Offline mapping, NOT verified live (docs/BC2_PHYSICAL_RELOAD.md, "Engine
// sources"): SoldierWeaponData+0x98 -> WeaponFiringData, +0x40 ->
// FiringFunctionData, whose fields are below. The engine has no chamber,
// slide-lock or seated-magazine data, so those stay VR-side; what it does say
// is whether the gun cycles per shot (bolt/pump) and whether it reloads by
// magazine or round. An open bolt, a notch lock or a last-round hold-open is
// not in this data and keeps coming from the catalog.
//
// Read-only and portable: the caller supplies the memory reader (the same
// WeaponModeMemory the family resolver uses). Nothing is wired in the DLL yet;
// MagazinePhysicalApi::firing stays null until these offsets are checked live.
namespace fvr::bc2 {
namespace firing_function_offsets {
inline constexpr std::uint32_t SoldierWeaponFiring=0x98,WeaponFiringFunction=0x40;
inline constexpr std::uint32_t ReloadTime=0x18,ReloadType=0x20,FireLogicType=0x24,
    BoltActionTime=0x50,BoltActionDelay=0x54,MagazineCapacity=0x184;
inline constexpr std::uint32_t WeaponStateIsPumpAction=0xc0; // WeaponStateData, noted only
}
enum class FiringReloadType : std::uint32_t {Single=0,Magazine=1};
enum class FiringLogicType : std::uint32_t {Single=0,BoltOrPump=1,Automatic=2,HoldRelease=3};
struct FiringFunctionFields {
    std::uint32_t function=0; // FiringFunctionData address the values came from
    std::int32_t magazineCapacity=0;
    FiringReloadType reloadType=FiringReloadType::Magazine;
    FiringLogicType fireLogic=FiringLogicType::Automatic;
    float reloadTime=0,boltActionTime=0,boltActionDelay=0;
};
// Pointer chain plus range checks; any implausible value rejects the read.
inline std::optional<FiringFunctionFields> ReadFiringFunctionFields(const WeaponModeMemory& m,std::uint32_t soldierWeaponData)noexcept {
    namespace o=firing_function_offsets;
    if(!m.read||!soldierWeaponData)return std::nullopt;
    const auto u32=[&](std::uint32_t at,std::uint32_t& out){return at>=0x10000&&m.read(m.context,at,&out,sizeof(out));};
    std::uint32_t firing=0,function=0;
    if(!u32(soldierWeaponData+o::SoldierWeaponFiring,firing)||!u32(firing+o::WeaponFiringFunction,function)||function<0x10000)return std::nullopt;
    if(m.type&&(!m.type(m.context,firing,"WeaponFiringData")||!m.type(m.context,function,"FiringFunctionData")))return std::nullopt;
    std::int32_t capacity=0;std::uint32_t reload=0,logic=0;float times[3]{};
    if(!m.read(m.context,function+o::MagazineCapacity,&capacity,sizeof(capacity))||
       !m.read(m.context,function+o::ReloadType,&reload,sizeof(reload))||!m.read(m.context,function+o::FireLogicType,&logic,sizeof(logic))||
       !m.read(m.context,function+o::ReloadTime,&times[0],sizeof(float))||!m.read(m.context,function+o::BoltActionTime,&times[1],sizeof(float))||
       !m.read(m.context,function+o::BoltActionDelay,&times[2],sizeof(float)))return std::nullopt;
    if(capacity<1||capacity>500||reload>1||logic>3)return std::nullopt;
    for(float t:times)if(!std::isfinite(t)||t<0||t>30)return std::nullopt;
    return FiringFunctionFields{function,capacity,FiringReloadType(reload),FiringLogicType(logic),times[0],times[1],times[2]};
}
enum class MagazineActionSource : std::uint8_t {Default,Catalog,Engine};
struct ResolvedMagazineAction {
    interaction::MagazineActionKind kind=interaction::MagazineActionKind::ClosedBolt;
    bool locksOpenOnEmpty=false;
    // The engine says this gun cycles per shot (bolt/pump) or loads by the
    // round: it belongs to WeaponCycle / shell insertion, not this chamber.
    bool cyclesPerShot=false,singleRoundReload=false;
    MagazineActionSource source=MagazineActionSource::Default;
};
// Engine data first where it has an answer, the catalog row for everything it
// cannot express (open bolt, notch lock, hold-open), ClosedBolt otherwise.
inline ResolvedMagazineAction ResolveMagazineAction(const std::optional<FiringFunctionFields>& engine,const MagazineActionRow* row)noexcept {
    ResolvedMagazineAction r;
    if(row){r.kind=row->kind;r.locksOpenOnEmpty=row->locksOpenOnEmpty;r.source=MagazineActionSource::Catalog;}
    if(engine){
        r.cyclesPerShot=engine->fireLogic==FiringLogicType::BoltOrPump;
        r.singleRoundReload=engine->reloadType==FiringReloadType::Single;
        // A bolt/pump gun's next round is chambered by its cycle, never by a
        // charging handle after a magazine seat.
        if(r.cyclesPerShot){r.kind=interaction::MagazineActionKind::NoAction;r.locksOpenOnEmpty=false;}
        r.source=MagazineActionSource::Engine;
    }
    return r;
}
} // namespace fvr::bc2
