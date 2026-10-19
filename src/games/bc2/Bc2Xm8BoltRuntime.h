#pragma once
#include "Bc2Xm8BoltAsset.h"
#include "Bc2Xm8BoltCycle.h"
#include "Bc2Xm8BoltPresentation.h"
#include <filesystem>
#include <ostream>
struct ID3D11DeviceContext;

// In-process owner of the XM8 bolt feature (BC2NativeProbe only).
//   FVR_XM8_BOLT=0         disables everything (no load, no draw, no palette edit, no cycle).
//   FVR_XM8_BOLT_ASSET=... overrides the render-input JSON path.
// Default: reports/xm8-bolt-intake/xm8-bolt-v1-render-input.json found by
// walking up from the session report folder and from the probe DLL folder
// (the launcher's repository root). A missing/invalid asset fails soft: the
// feature stays off and the reason is in the report's "xm8_bolt" object.
namespace fvr::bc2::xm8BoltRuntime {
void Configure(const std::filesystem::path& reportPath)noexcept;
bool Active()noexcept; // Switch on and asset loaded.
std::shared_ptr<const Xm8BoltAsset> Asset()noexcept;
std::int64_t NowNs()noexcept; // QPC nanoseconds (same domain as the probe's evidence clocks).

// Published once per retargeted pose by rig publication; empty clears it.
struct RenderSource {
    math::Matrix4 world{}; // Xm8BoltPartWorld of the posed carrier: canonical world, native units.
    std::uint64_t actor=0,actorGeneration=0,weapon=0,equipmentGeneration=0,space=0,input=0;
    std::int64_t observedNs=0,deadlineNs=0;
    float travel=0; // Observed carrier travel along the asset axis (telemetry).
    bool cardCollapsed=false;
};
void PublishSource(const std::optional<RenderSource>&)noexcept;
// Both eyes of a recent frame drew the bolt: the card may be collapsed.
bool CardCollapseAllowed(std::int64_t nowNs)noexcept;
void NoteCardDecision(Xm8BoltCardReason)noexcept;

// Post-eye boundary (after that eye's native draw returned), graphics thread.
// view/projection are the canonical eye (CanonicalBodyPropEye).
void DrawEye(ID3D11DeviceContext*,unsigned eye,std::uint64_t world,std::uint64_t request,std::uint64_t frame,
    const math::Matrix4& view,const math::Matrix4& projection,std::int64_t nowNs)noexcept;

// Gameplay thread: the left-hand charging-handle cycle (presentation + gating).
Xm8BoltCycleResult TickCycle(const Xm8BoltCycleSample&,interaction::HandInteraction&,std::uint64_t& intent)noexcept;
// Travel (metres) as a fraction of the existing handle presentation stroke.
float HandleFraction(float travelMetres,float presentationStrokeMetres)noexcept;

void RequestStop()noexcept;
void PumpStop()noexcept; // Graphics thread; releases D3D objects on their owning thread.
void Report(std::ostream&);
} // namespace fvr::bc2::xm8BoltRuntime
