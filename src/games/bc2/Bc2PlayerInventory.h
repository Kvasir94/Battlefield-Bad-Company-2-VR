#pragma once
#include "Bc2ReloadServer.h"

namespace fvr::bc2 {
// Actor identity supplied by the local-player resolver. Generation is advanced
// by that resolver on actor replacement; this reader never invents one.
struct PlayerInventoryAnchor {
    std::uint32_t player=0,soldier=0,weak=0;
    std::uint64_t actorGeneration=0;
    bool operator==(const PlayerInventoryAnchor&)const=default;
};
struct PlayerInventorySlot {
    std::uint32_t slot=0,item=0,data=0,effects=0,firing=0,firingData=0,primaryFire=0,ammoAddress=0;
    std::uint32_t clientWeapon=0;
    std::uint16_t serial=0;
    std::array<std::uint32_t,2> clientFiring{};
    std::int32_t fireLogicType=-1,reloadType=-1;
    std::optional<std::int32_t> effectiveCapacity;
    // Independently retain the authored feed capacity even when firing+98
    // contains a saved per-instance TOTAL-capacity override from this mod.
    std::optional<std::int32_t> authoredBaseCapacity,authoredCapacity;
    // Item ownership does not imply that the firing layout or both client
    // copies exist. Equipment remains selectable without a reload profile.
    bool owned=false,effectsVerified=false,serverFiringVerified=false,clientFiringVerified=false;
    bool operator==(const PlayerInventorySlot&)const=default;
};
struct PlayerInventorySnapshot {
    PlayerInventoryAnchor anchor{};
    std::uint32_t serverPlayer=0,serverSoldier=0,inventory=0,inventoryData=0;
    std::uint32_t selectedSlot=0,lastPrimary=0,clientInventory=0,clientSelectedSlot=0,count=0;
    std::array<PlayerInventorySlot,64> slots{};
    bool operator==(const PlayerInventorySnapshot&)const=default;
};
// A coherent native authority replacement retires ledger incarnations even
// if the client pawn and recycled item addresses/serials remain unchanged.
// Selection, ammo, input gaps and client replication are not replacements.
inline bool SamePlayerInventoryAuthority(const PlayerInventorySnapshot& a,const PlayerInventorySnapshot& b)noexcept {
    return a.anchor==b.anchor&&a.serverPlayer==b.serverPlayer&&a.serverSoldier==b.serverSoldier&&
        a.inventory==b.inventory&&a.inventoryData==b.inventoryData;
}
// A bounded, non-null server item belonging to this exact actor/inventory.
// This is structural selection evidence, not permission to skip native
// eligibility or to write ammunition. Revalidate on the server callback.
struct PlayerInventoryCandidate {
    PlayerInventoryAnchor anchor{};
    std::uint32_t serverPlayer=0,serverSoldier=0,inventory=0;
    PlayerInventorySlot item{};
};
// Requires already discovered and live-validated bindings for this module.
// Reads only: no reflection/native calls except the supplied type callback,
// no hooks and no writes. Selection disagreement is valid during replication.
std::optional<PlayerInventorySnapshot> ReadPlayerInventory(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const PlayerInventoryAnchor&)noexcept;
// Re-resolves the local actor and inventory. Selected slots and membership may
// change; callers must separately compare a previously requested item's identity.
std::optional<PlayerInventorySnapshot> RevalidatePlayerInventory(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const PlayerInventorySnapshot&)noexcept;
std::optional<PlayerInventoryCandidate> Candidate(const PlayerInventorySnapshot&,std::uint32_t slot)noexcept;
// Re-resolves and requires the same slot, item, serial and data. Does not require
// that the item is selected or that the client has replicated its wrapper yet.
std::optional<PlayerInventoryCandidate> RevalidatePlayerInventoryCandidate(const ReloadStateMemory&,const ReloadServerBinding&,
    const ReloadStateBinding&,std::uint32_t base,const PlayerInventoryCandidate&)noexcept;
}
