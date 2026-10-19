#pragma once
// Per-eye post-process history (FVR_BC2_PER_EYE_POST, default on; =0 disables).
//
// WorldRenderer+0x70 is the PostProcess object. Its per-view history (exposure
// adaptation, luminance readback rings, ring index, blur history) lives in a
// PostProcessView. The WorldRenderer creates exactly one ("Shared", WR+0x7c)
// and every view's draw passes it to PostProcess::process, so with two eyes the
// ring index advances twice per frame and each eye reads the other eye's
// luminance. This module creates a second PostProcessView through the native
// factory (PostProcess vf+8) and swaps it into WR+0x7c for the eye view's draw
// only. PostProcess::update advances every registered view's dt, so the eye
// view's exposure adapts at the native rate from its own history.
// Evidence: docs/re/BC2_EYE_PARITY_POST_RE.md.
#include "fvr/engine/PeImage.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace fvr::bc2::perEyePost {
struct Candidates { // RVAs
    std::uint32_t postVtable=0,viewVtable=0,factory=0,viewCtor=0,viewDtor=0,viewDeletingDtor=0,update=0,process=0;
    std::uint32_t postAddRef=0,postRelease=0,viewAddRef=0,viewRelease=0;
    std::uint32_t drawSelect=0,drawProcess=0,worldUpdateSite=0,worldCtorSite=0;
};
// drawViewRva/worldUpdateRva: the already-verified WorldRenderer draw (0xbb1db0)
// and WorldUpdate (0xbb4980) entries, used to bound the PPV use sites.
std::optional<Candidates> Discover(std::span<const std::byte>,const engine::PeImage&,std::uint32_t drawViewRva,std::uint32_t worldUpdateRva);
std::vector<std::pair<std::uint32_t,std::uint32_t>> LiveSpans(const Candidates&);
bool EnabledByEnvironment()noexcept;
void Bind(const Candidates&,std::uintptr_t imageBase,std::uint32_t worldRendererVtableRva)noexcept;
bool Bound()noexcept;
// Main thread, before WorldUpdate. eyeArmed: the eye view of this world is live
// and will be drawn this frame. Creates, keeps, or retires the eye PPV.
void Update(std::uintptr_t world,bool eyeArmed)noexcept;
// Release the eye PPV now if possible (call while the world is still alive).
void Retire()noexcept;
// Render thread, around the native draw of one view. Swaps WR+0x7c to the eye
// PPV only when eyeDraw is true and the PPV belongs to this world.
class DrawScope {
public:
    DrawScope(std::uintptr_t world,bool eyeDraw)noexcept;
    ~DrawScope();
    DrawScope(const DrawScope&)=delete;DrawScope& operator=(const DrawScope&)=delete;
private:
    std::uintptr_t world_=0;std::uint32_t prior_=0,swapped_=0;bool entered_=false;
};
struct Status {bool enabled=false,bound=false;std::uint64_t created=0,released=0,deferred=0,swaps=0,failures=0;std::uint32_t view=0,width=0,height=0;};
Status Snapshot()noexcept;
}
