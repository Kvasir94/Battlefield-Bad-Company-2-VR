#pragma once
#include <cstdint>

// Chamber and magazine-presence metadata for a magazine-fed gun.
//
// Ported from the KF2 VR mod, commit 43452e24:
//   native/adapter/MagazineFeedState.h   -> MagazineFeedState
//   native/adapter/EmptyMagazineState.h  -> EmptyMagazineState
//   native/adapter/tests/MagazineFeedStateTest.cpp -> tests/MagazineFeedStateTests.cpp
// Semantics are unchanged. Only the namespace, the noexcept spelling and the
// typed MagazineActionKind (KF2's integer ActionKind 0..3) are FrostbiteVR's.
//
// Ownership rule, as in KF2: the game owns every round. BC2 keeps one loaded
// count per firing copy (+0x7c) and no separate chamber count, so the chamber
// is VR-side state inferred from that count. No event returns an ammunition
// credit, writes a count or changes the observed native total. Callers feed
// the game's own loaded-count snapshots and read back availability only.
namespace fvr::interaction {

// Per-weapon action family. Values match KF2's VRReloadCatalog.ActionKind so
// catalogs can be ported row by row.
enum class MagazineActionKind : std::uint8_t {
    ClosedBolt=0, // Rifle/pistol/SMG with a chamber: empty reload needs a rack.
    OpenBolt=1,   // Fires from an open bolt: no chamber round, cock before or after seating.
    NoAction=2,   // Seating alone makes it ready (no rack required).
    NotchLock=3,  // Closed bolt whose handle locks in a notch on empty (HK-style).
};

struct MagazineFeedState {
    enum Flag : int { Ready=1, MagazineOut=2, ChamberLoaded=4, NeedsRack=8, ShotConfirmed=16 };
    enum class Event : int { Observe, Eject, Seat, Rack, Reset, EmptyChamber, Shot, ShotFlushed };
    int flags=0, lastAmmo=0;

    [[nodiscard]] bool Out() const noexcept { return (flags&(Ready|MagazineOut))==(Ready|MagazineOut); }
    [[nodiscard]] bool Chamber(int stockAmmo) const noexcept {
        return stockAmmo>0 && (flags&(Ready|ChamberLoaded))==(Ready|ChamberLoaded);
    }
    [[nodiscard]] bool BlocksFire(int stockAmmo) const noexcept {
        return (flags&Ready) && (Out() || (flags&NeedsRack)) && !Chamber(stockAmmo);
    }
    // With the magazine out only the chamber round is shown.
    [[nodiscard]] int DisplayAmmo(int stockAmmo) const noexcept { return Out() ? (Chamber(stockAmmo)?1:0) : stockAmmo; }

    bool Apply(Event event,int stockAmmo) noexcept {
        if(stockAmmo<0 || static_cast<int>(event)<0 || static_cast<int>(event)>7) return false;
        if(event==Event::Reset) { flags=0; lastAmmo=stockAmmo; return true; }
        if(!(flags&Ready)) {
            if(event!=Event::Observe && event!=Event::Eject) return false;
            flags=Ready|(stockAmmo>0?ChamberLoaded:0);
        }
        // A decrease confirms actual consumption, not merely trigger intent.
        // Replicated corrections fail closed while the magazine is out.
        if(stockAmmo==0 || (stockAmmo<lastAmmo && Out())) flags&=~ChamberLoaded;
        // Unaudited/button reloads retain ordinary native automatic feeding.
        if(stockAmmo>lastAmmo && !Out() && !(flags&NeedsRack)) flags|=ChamberLoaded;
        lastAmmo=stockAmmo;
        switch(event) {
        case Event::Eject: flags|=MagazineOut; break;
        case Event::Seat:
            flags&=~MagazineOut;
            if(!(flags&ChamberLoaded)) flags|=NeedsRack;
            break; // Seating cannot chamber.
        case Event::Rack:
            if(!Out() && stockAmmo>0) { flags|=ChamberLoaded; flags&=~NeedsRack; }
            break;
        case Event::EmptyChamber: flags&=~ChamberLoaded; flags|=NeedsRack; break;
        case Event::Shot:
            // Ammo-saving shots still empty the physical chamber while the
            // game keeps its own count.
            if(Out()) { flags&=~ChamberLoaded; flags|=ShotConfirmed; }
            break;
        case Event::ShotFlushed: flags&=~ShotConfirmed; break;
        default: break;
        }
        return true;
    }
};

// No-chamber removal metadata: unlike MagazineFeedState this never represents
// a chamber or permits a magazine-out shot. The game owns every round. Open
// bolts are the per-weapon exception: they may remove a loaded magazine and
// may be cocked before the replacement seats.
struct EmptyMagazineState {
    enum Flag : int { Removed=1, NeedsAction=2, Cocked=4 };
    enum class Event : int { Eject, Seat, Action, Reset };
    int flags=0;

    [[nodiscard]] bool BlocksFire() const noexcept { return (flags&(Removed|NeedsAction))!=0; }

    bool Apply(Event event,int stockAmmo,MagazineActionKind kind) noexcept {
        return Apply(event,stockAmmo,static_cast<int>(kind));
    }
    // Raw form kept for catalog/IPC values; rejects any kind outside 0..3.
    bool Apply(Event event,int stockAmmo,int actionKind) noexcept {
        if(stockAmmo<0 || actionKind<0 || actionKind>3 ||
           static_cast<int>(event)<0 || static_cast<int>(event)>3) return false;
        const bool openBolt=actionKind==static_cast<int>(MagazineActionKind::OpenBolt);
        const bool noAction=actionKind==static_cast<int>(MagazineActionKind::NoAction);
        if(event==Event::Reset) { flags=0; return true; }
        if(event==Event::Eject) {
            // Exact catalog open bolts may remove a loaded magazine too.
            // A loaded idle open bolt is cocked; an interrupted, unworked
            // empty reload must retain NeedsAction instead of inventing it.
            if(stockAmmo!=0 && !openBolt) return false;
            if(!(flags&Removed)) {
                if(stockAmmo>0) {
                    flags|=Removed;
                    if(!(flags&NeedsAction)) flags|=Cocked;
                } else flags=Removed|(noAction?0:NeedsAction);
            }
            return true;
        }
        if(event==Event::Seat) {
            if(!(flags&Removed) || stockAmmo==0) return false;
            flags&=~Removed;
            if(noAction || (openBolt && (flags&Cocked))) flags=0;
            return true;
        }
        if(flags&Removed) {
            if(!openBolt) return false;
            flags|=Cocked; // An open bolt reaches its sear before ammunition seats.
            return true;
        }
        if(!(flags&NeedsAction) || stockAmmo==0) return false;
        flags=0;
        return true;
    }
};
} // namespace fvr::interaction
