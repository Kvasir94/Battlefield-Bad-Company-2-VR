> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# VR quality review and roadmap

Archived October 9, 2026 from the owner's requested VR Mod Quality Review conversation (private conversation, not linked).
The complete original report below is preserved, including its citations and evidence labels.
It is a review of pinned source and reported player observations, not a runtime
acceptance receipt or a list of changes already delivered.

## Use in the BC2 roadmap

Read this with [current status and priorities](STATUS.md#current-work-and-priorities),
[handoff](HANDOFF.md) and the [current controller card](CONTROLS-20260930.md#current-controller-card--october-9-2026).
The report supplies evaluation and acceptance goals; current status records source
implementation. KF2 and AS2 comparisons are research references, not authorization
to edit KF2, import upstream code or copy calibration constants without validation.

| Priority | Report findings | Next reviewable outcome | Acceptance boundary |
| --- | --- | --- | --- |
| Essential baseline | F1, F5–F7; Stage 1 | Exact-package rendering measurements and one continuous XM8 reload transaction using the existing authority. | Synthetic results remain separate from worn comfort, actual magazine removal and process stability. |
| Hands and feel | F2–F4, F8; Stage 2 | Tracking-owned free hands, bounded object-local contacts, coherent sight/shot calibration and identifiable feedback. | Native geometry or source code alone does not establish headset-visible contact continuity. |
| Recovery and controls | F6, F9–F10; Stages 1–2 | Predictable interrupted reload, stow, selection, menu and tracking recovery. | Proposals and KF2-specific teleport findings are not current BC2 bindings. |
| Broader mechanics | Stage 3 | Role-independent transfer and independent weapon state after the single-weapon loop meets acceptance. | No dual-wield, all-weapon or multiplayer pass is implied. |

Navigation: [comparison](#comparison), [prioritized findings](#prioritized-findings),
[ownership boundaries](#what-should-own-behaviorand-what-must-remain-protected),
[staged plan](#staged-improvement-plan),
[retired assumptions](#inherited-assumptions-to-retire-or-re-verify),
[five next actions](#the-five-most-valuable-next-actions).

## Later source context — kept separate from the original report

The report inspected BC2 `266fe0a`, KF2 `cd521600` and upstream `761b49a`;
its original citations remain pinned to those revisions. The documentation
integration checked BC2 main `65957dc` on October 9. Later BC2 commits
add rendering/request timing and controller lifecycle/profile diagnostics;
source diagnostics do not close the report's worn-headset or magazine-associated
failure. The source consumer still requires separate real-input and exact-package
acceptance. See controller timing/lifecycle change
and selected-profile diagnostic.

The separate composed headset mirror is now opt-in with `Play-BC2VR.ps1 -Mirror`;
the game's steady eye mirror remains enabled unless `-NoSteadyWindow` is supplied.
These are different diagnostic surfaces. No performance saving is inferred from
that default. [Exact launcher](../Play-BC2VR.ps1).
The current controller card also distinguishes gun flicks, equipment release,
physical Reload behavior and scoped vehicle support from future binding proposals.


Main `65957dc` additionally classifies rejected stereo pairs and
preserves transfer errors. This is diagnostic coverage, not a rendering or
physical-reload acceptance result.
Exact change.

## Original report — pinned source review

# VR quality and feel review

## Overall judgment

**BC2 is an advanced engineering prototype, but the tested XM8 experience is not yet a dependable VR gameplay loop.** Loading, seeing the weapon and successfully firing are meaningful progress. However, your reported head-turn blackness, severe visual catch-up and failure following magazine removal outweigh automated successes when judging current usability.

The source is further along than some inherited documentation suggests: BC2 already has persistent, per-weapon ammunition authority, receipt-based interaction completion and object-local contact primitives. The next step is **not another foundational ammo system**. It is making the existing authority, tracked presentation and physical interactions behave coherently under ordinary movement, imperfect tracking and interrupted actions. [Current weapon authority](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp), [physical interaction frontend](../src/games/bc2/Bc2PhysicalWeaponInteraction.h)

**KF2 is substantially more integrated, but it is not a finished “premium VR” reference implementation.** Its exact-item presenters, hand inventory, comfort handling and recovery logic are useful architectural examples. Its remaining compromises include inconsistent visual aiming cues, complicated control meanings, native animation accommodations, and optional interaction paths that still need human acceptance. Fresh-profile defaults also leave physical reloads, body holsters and threaded rendering off; implemented capability is not the same as the default experience. [Inventory architecture](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRHandInventory.uc#L1-L165), [default controls](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/VR_CONTROLS.md#L19-L32)

**My central judgment:** the awkwardness is less about missing features than about discontinuities between systems. Tracking, body collision, native animation, item ownership, ammunition authority and rendering each have defensible rules individually. The player feels the seams when those rules disagree about where something is, whether a gesture remains valid, or whether an action has completed.

## Scope and pinned revisions

All three repositories were accessible. These are the revisions inspected—not an assertion that they match the binaries used in your fresh test.

| Repository | Inspected revision | Scope |
|---|---|---|
| Your BC2 fork | `266fe0a214311b4eb6c197ffaef85defcf1b7cb7` | Current source, launch path, relevant tests and historical/current notes |
| Your KF2 mod | [`cd52160037c073ddfca121dd6a59b21cdc263ef1`](https://github.com/Kvasir94/Killing-Floor-2-VR/commit/cd52160037c073ddfca121dd6a59b21cdc263ef1) | Current interaction, presentation, input and runtime code; defaults, tests and AS2 notes |
| Friend’s Frostbite upstream | [`761b49a06a7b68c34c2996d990f30c1cefa4a6b6`](https://github.com/justiceofrip/FrostbiteVR/commit/761b49a06a7b68c34c2996d990f30c1cefa4a6b6) | Checkpoint/status context, not an independently audited replacement implementation |

**Exact limitations:** unpushed BC2 changes, deployed binaries/settings and fresh failure traces were not available in this repository review. BC2’s handoff also identifies excluded local traces/game content and missing private resource data needed for complete package closure. AS2 findings here are grounded in the repository’s reverse-engineering notes; I did not independently re-disassemble the installed AS2 assets. [BC2 handoff](HANDOFF.md)

“Verified” below means verified source behavior. “Observed” means your report. Neither means I ran a game or validated the deployed build.

## Comparison

The evidence column points to the detailed findings that follow.

| Player experience | BC2 behavior | KF2 behavior | Evidence | Gap and recommended direction |
|---|---|---|---|---|
| Head movement feels immediate | Asynchronous eye production; bounded retained-image presentation. Your test had blackness and severe catch-up. | Default serial rendering; experimental threaded route exists. | F1 | Establish measured worn-headset pacing before tuning perceived weapon weight or smoothing. |
| Walking, leaning and turning agree | Head-relative movement compensates for gun-relative native aim; body-follow yields during stick movement. | Swept body-follow plus bounded presentation lean and head-pivot turn handling. | F2 | Test combined movement and wall contact, not each feature separately. |
| Gun, sight, laser and shot agree | Right-controller aim drives native aim; XM8 firing observed, broader optics acceptance incomplete. | Fixed world-pitch convergence, while laser deliberately follows the bore. | F3 | Calibrate per weapon in weapon-local coordinates; make cue differences intentional. |
| Hands feel like the player’s hands | Tracked wrist targets still feed anatomy/native-rig presentation. Object-local contact support exists. | Per-item presenters and contact handling; native animation still requires compensating logic. | F4 | Floating BC2 hands should remove arm/anatomy authority, not merely hide forearms. |
| Quick reload motions remain reliable | New physical authority and slider insertion; extraction still has per-sample motion limits. | Guided insertion supports overshoot; swept entry is restricted to cylindrical rounds. | F5 | Make recognition cadence-aware and forgiving after intentional contact. |
| An interrupted reload remains understandable | Persistent ledger and cleanup obligations survive swaps; hand interaction remains explicitly left/right. | Exact-item inventory and ownership transactions; compatibility/fallback paths remain. | F6 | Extend existing identity/ownership models; do not create another ammo ledger. |
| Released magazines visibly leave the hand | Receipt-driven drop event, separate rendering path and optional prop-cache launch dependency. | More developed physical-reload model, but optional and not blanket headset-accepted. | F7 | Require a continuous held-to-world transition in the actual headset-composed image. |
| Feedback explains what happened | Distinct completion/failure events exist. | Weapon/support-dependent pulses; overlapping pulses merge by maxima. | F8 | Give events priorities and recognizable meanings, rather than adding more vibrations. |
| Everyday controls stay predictable | Several stick/button meanings and explicit rearming; menu entry invalidates retained world imagery. | Richer menus and tools, but overloaded buttons, reload-mode differences and buffered teleport behavior. | F9–F10 | Reduce surprises during repeated, interrupted and novice-use flows. |

# Prioritized findings

## F1 — P0: BC2’s presentation failure must be separated into measurable failure modes

**Observed:** terrible frame rate, black nothingness during head turns and slow catch-up. The delay was not measured.

**Verified mechanism:** `RemoteFrameProvider::TryGetCompletedPair` retains a request’s original tracking sample while production is pending. The host can reuse a completed image with its original render poses for reprojection. `RetainedPresentation` expires that image after a **250 ms policy limit** or a discontinuity; the host then omits the world projection layer when no acceptable image exists. That limit is **not a measurement of your latency or frame rate**. [Provider](../src/ipc/RemoteFrameProvider.cpp), [retention policy](../include/fvr/runtime/RetainedPresentation.h), [host consumer](../src/xr/OpenXrHost.cpp)

This creates distinguishable possibilities: old-image reprojection exposes previously unseen areas; the world layer disappears after expiry; or the streaming/runtime path fails independently. Source inspection cannot select among them.

KF2 is not an automatic performance solution. Its launcher and adapter support explicit serial versus threaded modes, while its threading document contains historical benchmarks and projected overlap gains—not proof of present headset latency. [Launch/adapter gates](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/tools/multiplayer/friends.py), [threading implementation status](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/THREADED_RENDERING.md#L216-L237)

**Focused validation:** correlate tracking sample, native render begin/end, pair completion, host submission, retained-image age, blank/reused frames and runtime/streaming telemetry during slow head sweeps. Test stationary rendering before adding firing or reloads. Compare the headset, composed mirror and native desktop window as separate surfaces.

**Direction:** find the limiting stage before changing budgets, threading or resolution. Do not conceal failure by indefinitely extending retention, substituting current poses for old images, or weakening GPU ownership checks.

The magazine-associated failure remains a separate **P0 acceptance blocker**. Its timing does not establish whether BC2, the XR host or VDXR failed, or whether rendering or reload logic caused it. I make no claim about the independent Astra investigation’s outcome.

## F2 — P1: Roomscale and locomotion have different operating regimes that need combined testing

**Verified:** BC2’s `RoomscaleFollow::Update` allows a 12 cm planar offset before requesting body motion, caps requested follow speed at 0.8 m/s, and stops automatic following during manual stick movement. It consumes actual native displacement rather than pretending blocked movement succeeded. Its tests explicitly exercise a wall-blocked body and retained physical head offset. [Implementation](../src/interaction/RoomscaleFollow.cpp), [tests](../tests/RoomscaleFollowTests.cpp)

The live gameplay consumer also compensates movement for controller-driven native weapon yaw. That is a strength: pointing the gun elsewhere should not silently redirect walking. [Gameplay consumer](../src/games/bc2/Bc2Gameplay.cpp)

KF2’s `HeadAim` requests bounded body movement and clamps the remaining presentation offset. Its default lean allowance is 5 cm, with a script-supplied allowance consumed by the adapter. `BoundRoomView` changes the presentation reference to enforce that bound. This maintains shared eye/hand coordinates, but can sacrifice one-to-one physical translation at a collision limit. [HeadAim policy](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/native/adapter/HeadAim.h), [movement and clamp implementation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/native/adapter/HeadAim.cpp#L220-L301)

**Likely player consequences, not confirmed defects:** BC2 may feel different immediately after releasing the stick as body-follow resumes; KF2 may feel as though the world resists or drags during blocked leaning.

**Focused validation:** walk physically while strafing, aim sideways, snap-turn, release the stick, approach a wall, crouch and recenter—first separately, then continuously. Record head/world displacement, actual capsule displacement and weapon pose. Include seated mode and looking steeply up/down.

**Direction:** preserve direct head tracking in unrestricted space. Treat body correction and collision discomfort as explicit policies. Do not smooth the HMD to make body-follow look smoother.

## F3 — P1: KF2’s aiming cues are demonstrably not one coherent calibration

**Verified:** `VRHandsBridge` adds `24` native rotator units to `FireRotation.Pitch` when sight convergence is enabled. This is a fixed **world-pitch** correction, not a per-weapon sight-height solution. `VRWeaponPresenter` inherits that placement path. Separately, the laser caller deliberately passes `LaserMuzzleRotation`, and `VRWeaponLaser::UpdateAim` draws that optical bore ray. Convergence and weapon lasers are both enabled in fresh defaults. [Aim producer](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRHandsBridge.uc), [presenter consumer](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRWeaponPresenter.uc#L621-L644), [laser implementation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRWeaponLaser.uc#L232-L291)

The repository’s later AS2 comparison correctly acknowledges this approximation. The correction does not rotate around the gun’s local sight axis when the gun rolls. [Explicit limitation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/re/ARIZONA_SUNSHINE_2_RIG.md#L260-L264)

**Player symptom:** the laser, sight picture and impact can provide different aiming advice. That is especially confusing when testing whether the controller fit is wrong.

BC2’s shooting difficulty cannot yet be assigned to calibration: severe presentation delay alone confounds the test. Its controller translator also requires a 0.75 trigger value to request firing, which is a concrete input characteristic worth checking—not an established cause. [BC2 input](../src/interaction/ControllerInput.cpp)

**Focused validation:** stationary single shots at several distances, with the weapon upright, rolled 90° and inverted. Compare controller aim, rendered bore, sight axis, laser ray and actual shot direction. Repeat close to cover to catch eye-origin or muzzle-obstruction errors.

**Direction:** define per-weapon, weapon-local sight calibration and a deliberate ballistic zero. Keep an optical laser optical—or explicitly present an impact predictor as something different. Do not hide disagreement with target magnetism.

## F4 — P1: Floating BC2 hands require removing anatomy’s authority over presentation

**Observed:** replay firing made the arms jiggle excessively.

**Verified:** `TrackedRig::Update` still builds anatomy/shoulder targets and weapon-to-wrist relationships from the native rig. It deliberately retains native child animation. BC2 also has a useful `GripAttachment` primitive that freezes an acquired object-local contact through native reload animation. These are reusable components, but not yet the floating-hand ownership model you described. [Tracked rig](../src/interaction/TrackedRig.cpp), [contact primitive](../include/fvr/interaction/GripAttachment.h)

KF2 illustrates the same underlying tension. `SupportAnimationImposed` and `RefreshSupportAnchorReference` accommodate native action animations by retaining a contact reference relative to the primary hand. Otherwise animation movement could be mistaken for the player withdrawing the supporting hand. [Current accommodation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRWeaponPresenter.uc#L420-L549)

**Inference:** native rig motion, recoil and retargeting are plausible contributors to BC2’s jiggle, but the replay observation does not isolate which transform is responsible.

**Direction:** preserve original glove geometry and authored finger/contact poses, but make the free wrist directly tracking-owned. At a contact, blend a bounded correction in the object’s local frame; on release, return smoothly to tracking. Capped wrists should replace the arm dependency, not conceal it.

Keep recoil as a deliberate weapon-local response with separately defined ballistic effects. Do not move the camera or duplicate a native recoil impulse through multiple visual layers. KF2’s existing hand-local kick and support smoothing are useful references, not missing features to reimplement. [Current feel implementation notes](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/re/ARIZONA_SUNSHINE_2_RIG.md#L260-L262)

**Focused validation:** hold both controllers still during repeated shots and log raw wrist, contact correction, weapon root, native animated root and final wrist. Each visible motion should have one identified owner.

## F5 — P1: BC2 extraction remains cadence-sensitive; KF2 insertion still has a sampling seam

**Verified BC2 weakness:** the current physical interaction’s pulling phase rejects a sample when hand displacement exceeds `maxPullStepMeters` or angular displacement exceeds `maxStepRadians`. Those checks are per sample, without elapsed-time scaling in that branch. The same physical pull can therefore be accepted at one update cadence and rejected at another. [Actual extraction consumer](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

This is particularly relevant beside your poor-pacing observation, but **it does not explain a process crash**. The verified consequence is gesture rejection.

The new BC2 path already forces slider insertion and prevents an extracted magazine from immediately recapturing itself until a deliberate inward reversal. It is not the older dwell-based reload model described in early notes. [Current mode selection](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp), [reinsertion handling](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

KF2’s guide accepts overshoot and permits assisted completion after deliberate insertion. However, swept mouth-crossing capture is conditional on cylindrical-round handling; ordinary magazines still depend on sampling an acceptable entry configuration. [Guide](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRReloadInsertGuide.uc#L58-L136)

**Focused validation:** replay identical physical trajectories at different sampling intervals, including pauses, quick pulls, overshoot, reverse withdrawal and inverted weapons. Then repeat by hand at stable rendering performance.

**Direction:** use time-aware motion plausibility and swept contact where appropriate. Preserve tracking-jump rejection and explicit reacquisition. Do not solve this by globally enlarging every grab volume or removing discontinuity guards.

## F6 — P1: Preserve the existing ammunition foundation; finish hand ownership and interruption contracts

BC2 already stores exact weapon identity, ammunition state, magazine resources and command receipts. It services cleanup for stowed weapons and retains structural ownership through transient inventory-publication gaps. `Bc2MagazineFrontend` selects one authority path; it does **not** run legacy and physical reload transactions simultaneously. [Runtime](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp), [router](../src/games/bc2/Bc2PhysicalWeaponInteraction.h)

That is a strength. The remaining limitation is that the current interaction adapter explicitly obtains the gun from `Right` and creates magazine/mechanism claims for `Left`. Persistent weapon state is further developed than role-independent interaction routing. [Claim implementation](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

A second concern is recovery UX. Cleanup obligations can remain pending, and `BlocksEquipment()` includes pending work, cleanup and pulling. `RetryCleanup` deliberately refuses to proceed while authority is suspended. This is defensible fail-closed behavior, but its player-visible recovery needs validation: a safe refusal must not become an inexplicable permanently unavailable hand or selector. [Blocking conditions](../src/games/bc2/Bc2PhysicalWeaponInteraction.h), [cleanup lifecycle](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

KF2’s exact-item registry and ownership revisions are the right comparison. Its compatibility/network routes still require separate acceptance; neither mod’s internal server-side bookkeeping proves all remote-multiplayer behavior. [KF2 ownership](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRHandInventory.uc#L1-L165)

**Focused validation:** interrupt each reload phase with stow, transfer, tracking loss, focus loss, death and weapon replacement. Verify conservation, one owner per object, no replayed command, and a recoverable visible state.

**Direction:** extend role routing around the existing ledger. Do not duplicate ammunition into separate left/right copies.

## F7 — P1: A correct magazine drop is not yet sufficient visual evidence of a drop

**Observed:** the replay’s released magazine did not clearly transition into a falling object.

The current interaction emits `physicalDrop` only after an applied drop receipt and distinguishes ejection from the well. That is newer than the handoff’s simple “starts at the left hand” limitation, so that historical statement should not be repeated as a confirmed current defect. [Drop completion](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

The rendering path still deserves scrutiny. Normal physical reload startup only adds the body-prop cache when it exists, whereas missing assets are treated more strictly for body inventory. Host props are composed separately, and the composed mirror is explicitly the headset’s left-eye image. The native game window is a different diagnostic surface. [Launcher](../Start-BC2VRSession.ps1), [host composition](../src/xr/OpenXrHost.cpp), [play command guidance](../Play-BC2VR.ps1)

**Uncertainty:** missing assets, pose continuity, authority-to-render timing, occlusion and looking at the wrong surface remain distinct possibilities.

**Focused validation:** follow one magazine identity through its last held pose, applied drop receipt, first world-prop pose and both eye images. Verify origin, rotation, scale, velocity and floor contact.

**Direction:** make required visual assets an explicit startup capability check. Maintain visual continuity across the authority acknowledgment without inventing ammunition completion. Earlier desktop flicker at 2–5 events per second is not a frame-rate measurement.

## F8 — P2: Haptics need semantic prioritization, not more undifferentiated pulses

KF2 already scales shot feedback by weapon recoil characteristics and distributes a softer pulse to the supporting hand. Its current policy **and backend** allow up to 300 ms; the older 100 ms conclusion is outdated. [Policy](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/native/adapter/WeaponHaptics.h), [backend](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/native/xr/src/OpenXrD3D11Backend.cpp#L141-L203)

However, overlapping pulses merge amplitude and duration independently by maximum. A strong short event and weak long event can therefore become a strong long event. That is a verified mixing policy, not proof that users dislike it.

BC2’s new reload path usefully emits seating feedback after completion, including a gun-hand seat event. [Completion feedback](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)

**Focused validation:** distinguish grab, release, seated, rack-required, dry-fire and failure without looking at diagnostics, including combinations with gunfire and damage.

**Direction:** define priorities and cancellation rules. Preserve stale-event rejection, focus gating and nonfatal handling of disconnected haptics. A pulse should explain a particular transition, not merely confirm that code executed.

## F9 — P2: KF2 teleport has improved reach, but delayed commitment and refusal remain UX risks

The old short-range conclusion is outdated. Current code uses smooth pitch-to-distance mapping, ceiling fallback and endpoint backtracking; shipped tuning targets 18 m reach rather than the old 6.5 m proposal. These are not freely adjustable saved preferences: the launcher rewrites the tuning keys. [Current arc implementation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRTeleportLocomotion.uc#L49-L95), [tuning policy](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/tools/vr-defaults.ps1)

The interaction still has a subtle behavior: returning the stick to center commits, while release within the final 0.3 seconds of recharge can buffer a later teleport. The code then cancels the aiming preview and revalidates the buffered destination before firing it. Revalidation is good; a delayed move after the player thought an attempt failed is the potential feel problem. [Actual input/commit consumer](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRTeleportLocomotion.uc#L160-L287)

**Focused validation:** commit just before and after recharge completion, re-aim, cancel, get grabbed, and have another actor occupy the destination. Ask the player to predict whether movement is pending.

**Direction:** distinguish valid, charging, queued, refused and canceled states unmistakably. Judge useful repositioning distance after collision/navigation validation—not the nominal range setting. Preserve the ability to arrive, turn and fight rather than forcing repeated corrective hops.

## F10 — P2: Defaults and overloaded controls increase the learning and recovery burden

KF2’s upper face buttons combine hold-to-open wheel, release/trigger confirmation, tap recall, double-tap behavior and proximity-dependent flashlight use. Physical reloads also have stock fallback cases, and movement behavior differs between accepted physical reloads and stock reload routes. These are documented behaviors, not hypothetical feature requests. [Controls](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/VR_CONTROLS.md#L35-L105), [reload policy](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/VR_PHYSICAL_RELOAD.md#L1-L100)

BC2 likewise shares stick controls between turning, equipment selection and recentering. It correctly spoils an equipment click when the recenter chord is used and rearms controllers independently after loss. These protections should remain. [Input arbitration](../src/interaction/ControllerInput.cpp)

**Likely symptom:** a player interprets a refused action, required neutral input or changed mode as unreliable tracking.

**Focused validation:** a novice must draw, reload, recover a dropped item, change a setting, close a stock popup and resume play without desktop intervention. Repeat after headset removal and controller occlusion. In multiplayer, verify that menus clearly communicate that the world continues.

**Direction:** provide contextual control hints, a persistent indication of the active interaction mode, and explicit recovery feedback. Menus, HUD and pointers should each have one input owner. BC2 menu entry resets retained world imagery, so resume also needs a deliberate fresh-frame transition rather than an unexplained blank interval. [Host menu transition](../src/xr/OpenXrHost.cpp)

# What should own behavior—and what must remain protected

The useful boundary is **gameplay/presentation ownership**, not “VR code good, native code bad.”

For supported physical interactions, VR should own the weapon root, hand role, contact transition, mechanical readiness and intended shot direction. Native first-person equip/reload/sprint/camera animation must not take over those same outputs. Authored finger poses, moving mechanism parts, sound events and necessary simulation rules can still be reused.

Conversely, retain scoped renderer restoration, native identity checks, GPU ownership, collision validation, focus/neutral rearming and receipt verification. KF2’s support-reference accommodation and BC2’s transient-publication retention are examples of safeguards solving real integration problems. Remove them only after their replacement makes the old condition impossible—not merely to simplify a diagram. [KF2 animation accommodation](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/script/KF2VR/Classes/VRWeaponPresenter.uc#L420-L549), [BC2 ownership retention](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp)

The AS2 notes are most valuable for **interaction structure**: object-relative handles, short transitions into and out of support, assisted completion after intentional insertion, and hints tied to the next required mechanical state. They are not justification for universal controller offsets, fixed sight correction or copying every timing constant. The notes themselves distinguish recovered runtime behavior, asset values and unknown formulas. [Handling research](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/re/ARIZONA_SUNSHINE_2_HANDLING.md), [reload research](https://github.com/Kvasir94/Killing-Floor-2-VR/blob/cd52160037c073ddfca121dd6a59b21cdc263ef1/docs/re/ARIZONA_SUNSHINE_2_RELOADS.md#L1-L155)

# Staged improvement plan

## Stage 1 — Essential foundations now

**First establish an exact-package XM8 baseline.** Record commit, local-diff identity, binary hashes, launch flags, active authority mode, runtime, refresh rate and asset availability. Keep the independent crash investigation separate, but require its resulting package to pass the same baseline before expanding testing.

Instrument frame production and presentation separately from interaction recognition. At a chosen refresh rate, use the corresponding frame budget as a target—for example, approximately 13.9 ms at 72 Hz—not an assumed achieved rate. Acceptance requires no unexplained black world frames or prolonged catch-up during sustained head movement.

**Then stabilize one physical transaction end to end:** original-magazine removal, deliberate discard or retention, replacement, seating, action completion and firing. Use the existing authority. Make extraction cadence-aware and make visual hand-to-world transitions continuous.

**Human acceptance:** twenty consecutive reload cycles, including interrupted cycles, with no lost/duplicated rounds, unavailable hand requiring a restart, or ambiguous dropped magazine. This is a proposed acceptance gate, not a claim of current reliability.

## Stage 2 — High-impact polish next

Build the floating-hand presentation layer using the original BC2 hand geometry, capped wrists and authored contact poses. Dependency: stable item identity and a documented transform owner at each stage.

Add gentle object-local contact blending, then tune weapon-local recoil. Calibrate the XM8 sight/shot relationship before using aim difficulty to tune magnetism. In KF2, replace the universal world-pitch approximation with explicit per-weapon calibration and validate the laser’s intended meaning.

Unify roomscale, turning and recenter acceptance around combined motions. Add clear pending/refused/recovery cues for interactions and teleport. Simplify control meanings where contextual feedback alone cannot make them predictable.

**Human acceptance:** the player can move both hands freely between contacts, rotate the weapon and magazine through inverted orientations, break and re-establish support without a jump, and complete reloads while turning or crouching. No canned action controls the whole weapon-hand motion. A novice can recover from cancellation and resume from menus without desktop assistance.

## Stage 3 — Later ambitions

Extend role-independent drawing, support and transfer around existing exact-item state. Only then enable two independently tracked weapons with separate firing directions and persistent per-weapon action state.

Detailed magwell guidance, broader weapon-family coverage, sophisticated object weight and additional mechanics come after the single-weapon loop is satisfying. KF2’s richer systems should be treated as patterns to evaluate, not a feature count BC2 must immediately match.

**Human acceptance:** swapping hands or stowing either weapon never changes the other weapon’s ammunition/action state; firing and recoil follow the correct hand; interruption and network cases remain understandable.

Across all stages, keep **human input, recording and automated replay as explicit separate modes**. Recording a human test is useful. A replay that validates ammo or stereo is not a substitute for a person judging comfort, motion latency or contact feel.

# Inherited assumptions to retire or re-verify

1. **“BC2 still needs its first persistent physical-ammo foundation.”** It already has one. Older native-reload-hold descriptions do not describe the active new frontend. Extend and validate it instead of creating a competing ledger.
2. **“KF2 lacks support smoothing, hand-local recoil, or haptics beyond 100 ms.”** Those conclusions are outdated. Their quality still needs human evaluation.
3. **“KF2 teleport is still the old short-range implementation.”** Current reach and pitch mapping differ. Evaluate reachable endpoints, recharge communication and repeated combat use.
4. **“Recovered AS2 values are universally correct and should be copied.”** Controller basis, per-weapon geometry, inactive settings and unrecovered formulas matter. Copy the interaction principle only after validating the consumer.
5. **“A green replay, a newer upstream checkpoint, or a clear desktop image proves headset readiness.”** None does. Upstream’s inspected checkpoint explicitly reports failed headset acceptance after passing shared tests; that is a separate SPAS/checkpoint report, not an explanation of your XM8 failure. [Upstream status](https://github.com/justiceofrip/FrostbiteVR/blob/761b49a06a7b68c34c2996d990f30c1cefa4a6b6/docs/STATUS.md#L1-L4)

## The five most valuable next actions

1. **Establish a measured, exact-package BC2 headset baseline.** Separate stale reprojection, blank world submission, native rendering stalls and streaming/runtime loss before tuning interaction feel.
2. **Finish the XM8 reload’s continuity contract.** Make extraction cadence-aware; preserve receipt-based ammo correctness; verify the complete held-magazine-to-falling-prop transition in both headset eyes.
3. **Implement the floating-hand ownership model—not just hidden arms.** Raw tracking owns free hands; object-local contacts own bounded corrections; one weapon-local layer owns visual recoil.
4. **Make aiming cues coherent in both mods.** Validate BC2’s XM8 under stable pacing and replace KF2’s universal world-pitch convergence approximation with deliberate per-weapon calibration.
5. **Run repeated, interrupted human flows before adding features.** Include movement, turning, inversion, stow/transfer, menus, tracking loss and recovery; retain recordings for replay while keeping human acceptance a separate gate.
