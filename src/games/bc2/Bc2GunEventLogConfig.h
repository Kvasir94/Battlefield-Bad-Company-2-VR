#pragma once
#include <cstdint>
namespace fvr::bc2 {
// NativeProbeConfig::gunEventLog (docs/GUN-EVENT-LOG.md, "Live producer").
// Explicit developer opt-in; the player launcher never sets it.
enum class GunEventLogMode:std::uint32_t {Disabled=0,Enabled=1};
// The log needs the gather (its tick source) and therefore the tracked
// controller stream: mode 9 with 0x800. It only observes, so it combines with
// tape replay, tape recording and every normal session. Native rows also need
// the reload-flow hooks, which exist only when hands (0x10000) or pose
// observation installs them; without them the file holds fvr rows only.
constexpr bool ValidGunEventLogConfig(GunEventLogMode mode,std::uint32_t flags)noexcept {
    if(mode==GunEventLogMode::Disabled)return true;
    return mode==GunEventLogMode::Enabled&&(flags&0xffu)==9&&(flags&0x800u);
}
// Written next to the report (the run folder); never overwrites.
inline constexpr const wchar_t* GunEventLogFileName=L"gun-events.jsonl";
}
