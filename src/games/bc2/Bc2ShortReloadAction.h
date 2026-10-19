#pragma once
// What a short B (Reload) release does on a detachable-magazine weapon under
// the physical weapon authority. Hold B 600 ms is the automatic native reload
// (Bc2ReloadButtonGesture); a short release is the next physical step.
//
// Playtest 2026-10-09 (docs/RELOAD-SHORT-B-AFTER-INSERT-20261009.md): after a
// physical insertion the user pressed B to finish the reload and the magazine
// was ejected again. The authority had a round chambered (an earlier rack, or
// a tactical reload that kept the chamber), so the old rule "seated and no
// rack owed -> eject" fired. After an insertion, short B now completes the
// reload: rack when a rack is owed, otherwise a confirmation tick, never an
// eject. A press while the seat receipt is still in flight is latched and
// resolved by that receipt; it is dropped, never turned into an eject.
//
// Environment (read once per process by Bc2Gameplay):
//   FVR_BC2_SHORT_B_RACK_AFTER_INSERT=0  old behaviour: short B racks only when
//   a rack is owed, else ejects any seated magazine; presses during a seat are dropped.
#include <cstdint>
#include <string_view>

namespace fvr::bc2 {
template<class Getter>
bool ParseShortReloadRackAfterInsert(Getter&& get)noexcept {
    const auto v=get("FVR_BC2_SHORT_B_RACK_AFTER_INSERT");return v.empty()||v[0]!='0';
}
// After our own seat receipt (or a rack inside that window) short B never
// ejects for this long. A shot, an unseat or a weapon change ends it sooner.
inline constexpr std::int64_t ShortReloadAfterInsertNs=5000000000;
// A press made while the seat is settling or its receipt is in flight waits
// this long for the receipt (the authority expires commands at 500 ms).
inline constexpr std::int64_t ShortReloadLatchNs=1000000000;

enum class ShortReloadAction : std::uint8_t {
    None,    // nothing to do (or a latched press that resolved to nothing)
    Rack,    // CycleAction: chamber a round from the seated magazine
    Eject,   // UnseatMagazine and drop it from the well
    Confirm, // after an insertion with the weapon already ready: tick only
    Latch,   // seat in flight: keep the press until its receipt
};
struct ShortReloadInput {
    bool pressed=false;      // a short release this gather
    bool latched=false;      // an earlier press waits for a seat receipt
    bool seatInFlight=false; // insertion settling, finishing on release, or SeatMagazine pending
    bool busy=false;         // another command pending, pulling, or a magazine in hand
    bool inserted=false,needsRack=false;
    std::uint32_t loaded=0;
    bool afterInsert=false;  // inside ShortReloadAfterInsertNs of our seat (or a following rack)
};
constexpr ShortReloadAction DecideShortReload(const ShortReloadInput& in,bool rackAfterInsert)noexcept {
    using A=ShortReloadAction;
    if(!rackAfterInsert){
        // Legacy order (aca6ffd): any in-flight command or held magazine drops the press.
        if(!in.pressed||in.busy||in.seatInFlight)return A::None;
        if(in.needsRack&&in.loaded)return A::Rack;
        return in.inserted?A::Eject:A::None;
    }
    if(!in.pressed&&!in.latched)return A::None;
    if(in.seatInFlight)return A::Latch;
    if(in.busy)return A::None;
    if(in.needsRack&&in.loaded)return A::Rack;
    if(in.inserted&&in.afterInsert)return A::Confirm;
    // A latched press belongs to an insertion. If that seat did not land, do nothing.
    if(in.latched&&!in.pressed)return A::None;
    return in.inserted?A::Eject:A::None;
}
} // namespace fvr::bc2
