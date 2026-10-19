#pragma once
// Eye-view effect parity (FVR_BC2_EYE_PARITY, default on; =0 disables).
//
// The native subsystem build (0x5de070) fills the main WorldRenderView's effect
// state every frame through the WorldRenderView vtable setters and a few raw
// fields. The second (eye) view never gets that state, so fades, colour grading,
// DynamicPostProcess, LOD scale and friends only reach one eye. Mirror copies the
// main view's state into the eye view with the same native setters, once per
// frame, after the subsystem build and before WorldUpdate (0xbb4980).
// Evidence: docs/re/BC2_EYE_PARITY_POST_RE.md.
#include "fvr/engine/PeImage.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>
namespace fvr::bc2::eyeParity {
struct Candidates {
    std::uint32_t vtable=0,colourCopy=0,matrixCopy=0; // RVAs
    std::array<std::uint32_t,60> slot{};               // RVA of each verified accessor slot, 0 if unused
    std::array<std::uint16_t,60> length{};             // verified body length per slot
};
// Static proof: every mirrored accessor of the view vtable matches its exact
// plain-store/plain-load body (or the ref-counted setter body).
std::optional<Candidates> Discover(std::span<const std::byte>,const engine::PeImage&,std::uint32_t viewVtableRva);
// (rva,length) spans the caller must find byte-identical in the loaded image.
std::vector<std::pair<std::uint32_t,std::uint32_t>> LiveSpans(const Candidates&);
bool EnabledByEnvironment()noexcept;
void Bind(const Candidates&,std::uintptr_t imageBase)noexcept;
bool Bound()noexcept;
// Main thread only. Both views must already be verified as live WorldRenderViews
// owned by the same request (vtable + owner). Returns false if not bound.
bool Mirror(std::uintptr_t main,std::uintptr_t eye)noexcept;
struct Status {bool enabled=false,bound=false;std::uint64_t mirrors=0,initialised=0;std::uint32_t lastEye=0;};
Status Snapshot()noexcept;
}
