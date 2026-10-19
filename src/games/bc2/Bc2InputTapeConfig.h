#pragma once
#include <cstddef>
#include <cstdint>
namespace fvr::bc2 {
// NativeProbeConfig::tapeMode. Explicit developer opt-in only; no launcher enables replay by default.
enum class InputTapeMode:std::uint32_t {Disabled=0,Replay=1,Record=2};
inline constexpr std::size_t InputTapePathChars=260;
// Replay replaces the gather's IPC input with an absolute-path tape, so it needs
// the controller stream (0x800, mode 9) and is exclusive with every synthetic
// probe that also authors input or native requests: the magazine/physical
// fixtures (0x4000000, 0x20000000, sessions 1,2,4,5,6), hold/round/request probes
// (0x400000, 0x800000, 0x1000000), death/equip/rig pulses (0x20000, 0x40000, 0x8000).
// Record observes the normal controller stream and combines with any normal session.
// path is UTF-16 (char16_t, portable wire text); must be NUL-terminated within InputTapePathChars, absolute (drive or UNC).
inline bool ValidInputTapeConfig(InputTapeMode mode,std::uint32_t flags,std::uint32_t magazineSession,const char16_t* path,std::size_t chars)noexcept {
    if(!path||chars!=InputTapePathChars||path[chars-1])return false;
    if(mode==InputTapeMode::Disabled)return path[0]==0;
    if(mode!=InputTapeMode::Replay&&mode!=InputTapeMode::Record)return false;
    const bool absolute=(((path[0]>=u'A'&&path[0]<=u'Z')||(path[0]>=u'a'&&path[0]<=u'z'))&&path[1]==u':'&&(path[2]==u'\\'||path[2]==u'/'))||(path[0]==u'\\'&&path[1]==u'\\');
    if(!absolute||(flags&0xffu)!=9||!(flags&0x800u))return false;
    if(mode==InputTapeMode::Record)return true;
    constexpr std::uint32_t synthetic=0x4000000u|0x20000000u|0x400000u|0x800000u|0x1000000u|0x20000u|0x40000u|0x8000u;
    return !(flags&synthetic)&&(magazineSession==0||magazineSession==3);
}
}
