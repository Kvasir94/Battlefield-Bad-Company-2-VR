#pragma once
#include "fvr/interaction/InteractionZones.h"
#include "Bc2FloatingHands.h"
#include "Bc2Profile.h"
#include "Bc2Camera.h"
#include "Bc2SelectedMeshes1p.h"
#include "Bc2WeaponVisibilityProbe.h"
#include "Bc2BodyHolster.h"
#include "Bc2BodyHolsterObservation.h"
#include "Bc2BodyHolsterProbe.h"
#include "fvr/interaction/BodyAnchors.h"
#include <memory>
#include "fvr/runtime/FrameCoordinator.h"
#include "fvr/ipc/FrameChannel.h"
#include "fvr/ipc/ReloadAudioProtocol.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/graphics/BodyPropFrame.h"
#include <ostream>
namespace fvr::bc2::gameplay {
using InputReader=ipc::ChannelResult(*)(interaction::InputFrame&,std::int64_t&)noexcept;
using InputObserver=void(*)(const interaction::InputFrame&,std::int64_t deadline,std::int64_t consumedQpc)noexcept;
// Records one private input per gather, including explicit fixture gestures.
// Read-only callback; configure after Install and before Start.
bool SetInputObserver(InputObserver)noexcept;
using FeedbackWriter=bool(*)(const interaction::FeedbackEvent&)noexcept;
// Optional one-way notification, configured after Install and before Start.
// A failed nonblocking send is dropped; feedback never changes native reload.
bool SetFeedbackWriter(FeedbackWriter)noexcept;
using ReloadAudioWriter=bool(*)(const ipc::ReloadAudioEvent&)noexcept;
using ReloadAudioOwnerWriter=bool(*)(std::uint64_t,std::uint64_t,std::int64_t)noexcept;
using ReloadAudioReadyReader=std::int64_t(*)(std::uint64_t,std::uint64_t)noexcept;
bool SetReloadAudioWriters(ReloadAudioWriter,ReloadAudioOwnerWriter,ReloadAudioReadyReader)noexcept;
// Explicit driver-only native head-aim/camera opt-in, after Install before Start.
// Fire can remain disabled for the bounded signed-axis response diagnostic.
bool EnableBoatHeadAim(bool enableFire=false)noexcept;
// Called once after this module's MinHook initialization, before hook enable.
// InputReader is nonblocking and enforces the IPC producer-side 100ms deadline.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,InputReader,bool motionAim=false,bool bodyFollow=false,bool observePoses=false,bool rigPulse=false,bool handPoses=false,bool deathProbe=false,bool equipProbe=false,bool muzzleFire=false,bool twoHandGrip=false,bool enableSightFlip=false,bool reloadHoldProbe=false,bool reloadRoundProbe=false,bool reloadRequestProbe=false,bool physicalReload=false,bool physicalReloadProbe=false,unsigned magazineReloadSession=0);
// Explicit opt-in after Install and before Start; persistent is for the reload consumer.
bool EnableSelectedMeshesObservation(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,bool persistent=false)noexcept;
// Explicit opt-in after Install, before Start. Shoulder switching uses native
// selection and ordinary rig presentation; weapon hiding is a separate gate.
bool EnableBodyInventory(interaction::BodyAnchorConfig config={})noexcept;
// Future explicit native acceptance only; no CLI flag supplies these defaults.
// Called after EnableBodyInventory and before Start. Each mesh profile is opt-in.
bool EnableBodyHolsters(BodyHolsterCapabilities acceptance={})noexcept;
// Explicit single bounded SPAS diagnostic, after EnableBodyInventory/before Start.
// Does not accept production capabilities. A later call cannot renew the trial.
bool EnableBodyHolsterDiagnostic(std::uint32_t lifetimeMs=15000,BodyHolsterDiagnosticProfile profile=BodyHolsterDiagnosticProfile::Spas)noexcept;
bool EnableBodyHolsterFixture(BodyHolsterDiagnosticProfile profile=BodyHolsterDiagnosticProfile::Spas)noexcept;
std::shared_ptr<const BodyHolsterFixtureSample> ReadBodyHolsterFixture(std::int64_t nowNs)noexcept;
std::shared_ptr<const BodyHolsterProbeSample> ReadBodyHolsterProbe(std::int64_t nowNs)noexcept;
// Isolated bounded diagnostic, before Start. No inventory acknowledgement.
bool EnableEmptyFireProbe()noexcept; // Explicit input-only diagnostic before Start.
bool EnableWeaponVisibilityProbe()noexcept;
std::shared_ptr<const WeaponVisibilityProbeSample> ReadWeaponVisibilityProbe(std::int64_t nowNs)noexcept;
// Diagnostic-only, after Install and before Start; normal one-shell fixture is unchanged.
bool EnablePhysicalReloadProbeRepeat()noexcept;
// Configured arrays only: no submitted skin, active-state or render authority.
std::shared_ptr<const SelectedMeshesSnapshot> ReadSelectedMeshes(const ReloadStateOwner&,std::int64_t nowNs)noexcept;
std::shared_ptr<const SelectedMeshesSnapshot> ReadCurrentSelectedMeshes(std::int64_t nowNs)noexcept;
// Process interaction zone table (env-resolved once; Bc2InteractionZones.h).
const interaction::InteractionZoneTable& InteractionZoneTable()noexcept;
// Process hands presentation mode (FVR_HANDS_MODE, Bc2FloatingHands.h).
HandsPresentationMode HandsMode()noexcept;
bool BodyDisplayNativeCurrent(const BodyInventoryDisplay&)noexcept;
bool AdjustViewBase(std::array<RenderViewCopy,2>&,const runtime::TrackingFrame&)noexcept;
// Bounded read-only plan and post-setter camera evidence; no native writes.
bool ObserveVehicleCameraPlan(unsigned,const runtime::TrackingFrame&,const RenderViewCopy&,const RenderViewCopy&,const std::array<RenderViewCopy,2>&)noexcept;
void ObserveVehicleCameraApplied(unsigned,unsigned,const RenderViewCopy&)noexcept;
void Start()noexcept;
bool Stop()noexcept;
void Report(std::ostream&);
// Session thread only, after Start: immutable configuration and atomic counters.
void ReportControls(std::ostream&);
// Periodic: 1P weapon effect anchor counters plus up to 32 drained launcher
// admission / placement / effect-spawn / shot-basis rows (Bc2WeaponEffects.h).
void ReportWeaponEffects(std::ostream&);
// Gather tick (the gun-event log's `tick`) for rows produced off the gather thread.
std::uint64_t CurrentGatherTick()noexcept;
// M6 dropped magazines, render-only: appends the live dropped props (up to 3)
// to an eye's host composition packet when they belong to this space.
// F5 reload tips, render-only: per-part hint alpha (magazine, pouch, magwell,
// handle, pump; 0..0.6) from the last gather, fresh for 100 ms, with the
// magazine part's geometry key and seated world pose (native units) when the
// weapon is supported. For a per-instance tint in the host compositor.
struct ReloadHintSnapshot {std::array<float,5> alpha{};std::int64_t observedNs=0,deadlineNs=0;std::uint64_t space=0,inputSequence=0;
    interaction::HandInteractionOwner owner{};
    graphics::RigidPropGeometryKey magazineGeometry{};std::optional<math::Matrix4> magazineWorld;};
std::optional<ReloadHintSnapshot> ReadReloadHints(std::int64_t now)noexcept;
bool ReloadGuidanceEnabled()noexcept;
void AppendReloadGuidanceProps(std::int64_t now,std::uint64_t space,std::uint64_t frame,unsigned eye,
    bool pouchGeometryAvailable,graphics::BodyPropEye& props)noexcept;
void AppendDroppedMagazineProps(std::int64_t now,std::uint64_t space,std::uint64_t frame,unsigned eye,graphics::BodyPropEye& props)noexcept;
}
