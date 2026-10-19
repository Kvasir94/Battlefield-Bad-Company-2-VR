#pragma once
#include <cstdint>
namespace fvr::bc2 {
// WorldRenderSettings (WorldRenderer+0x94) bytes +0xb8..+0xbf. Prepare 0xbb0ea0
// merges the first-person (foreground) list into worldMainOpaque when
// ForegroundEnable(+0xb8) is set and ForegroundDepthClearEnable(+0xbd) is clear,
// so the gun depth-tests against the world instead of drawing over walls.
// Only +0xbd is ever written; blur (+0xbb/+0xbe) and scope filter (+0xbf) stay native.
inline constexpr unsigned kForegroundFlagsOffset=0xb8,kForegroundFlagsSize=8,kForegroundDepthClear=0xbd-0xb8;
enum class ForegroundDepthAction : std::uint8_t {None,Write,Restore,Invalid};
struct ForegroundDepthOverride {
    std::uint32_t settings=0;std::uint8_t original=0;bool written=false;
    // Decide the +0xbd byte for this Prepare. `flags` is a fresh read of
    // +0xb8..+0xbf; on Write/Restore the caller stores `value` to +0xbd.
    ForegroundDepthAction Step(std::uint32_t address,const std::uint8_t (&flags)[kForegroundFlagsSize],bool want,std::uint8_t& value)noexcept {
        if(written&&address!=settings)written=false; // settings object replaced: nothing of ours to restore
        const auto enable=flags[0],clear=flags[kForegroundDepthClear];
        if(!address||enable>1||clear>1)return ForegroundDepthAction::Invalid;
        if(want){
            if(clear==0)return ForegroundDepthAction::None; // already merged (ours or native)
            settings=address;original=clear;written=true;value=0;return ForegroundDepthAction::Write;
        }
        if(!written)return ForegroundDepthAction::None;
        written=false;
        if(clear!=0)return ForegroundDepthAction::None; // a native write since ours stays
        value=original;return ForegroundDepthAction::Restore;
    }
};
}
