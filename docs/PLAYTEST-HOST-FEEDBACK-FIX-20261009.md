> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Host feedback crash follow-up — 9 October 2026

Technical follow-up to the [existing-IK worn playtest](PLAYTEST-20261009-EXISTING-IK.md).

## Prioritized open work — resumed after the October 9 playtest

This is the integration queue for the resumed owners. Detailed evidence stays in
the dated entries below and the [earlier worn playtest](PLAYTEST-20261009-EXISTING-IK.md).
**Observed failure** means a reported or recorded result with unresolved cause.
**Proven cause / source fix** applies only to the host-feedback null dereference
described below; its candidate tests do not establish live acceptance.
**Proposal / prepared change** is not an enabled or accepted feature.

| Priority and existing lane | Evidence and state | Next scoped work / acceptance |
| --- | --- | --- |
| **1. Native stereo / runtime owner** | **Observed:** unequal eye detail/ghosting, left-eye-only cutscene fade, and repeated black blinking after leaving fullscreen. Scene/effect and resize identities remain unresolved. | Preserve/correlate eye-pair scene/pose, geometry/LOD/culling/destruction state, effect coverage and swapchain/backbuffer/XR dimensions. Recover the window-mode failure from captured evidence, then verify consistent eyes and transitions in the exact candidate. No blanket fade removal or assumed LOD cause. |
| **2. Native gameplay / runtime owner** | **Observed:** seated-XM8 trigger refusal, later inability to shoot, unreliable sprint after turns and failed vertical gun switching despite support release. One earlier reload/resumed-fire success does not close the failures. | Correlate trigger and authoritative chamber/ammo/shot gating; reload transaction and custody; sprint intent/orientation; switch axes/reset/context, inventory and native completion. Keep the proven host-feedback fix **fixed in source, live acceptance pending**. |
| **3. Existing bindings + native recovery lanes, next build** | **Prepared by report:** narrow selectable smooth-turn patch preserving gun-switch behavior; integration/acceptance unverified. **Requested:** gun-hand reload hold invokes native automatic reload, short press retains physical action. | Bindings owner supplies the smooth-turn patch through the existing integration lane. Native owner owns reload recovery/ledger integration; prevent duplicated ammo or overlapping physical transactions. Verify both behaviors and retained short-press/flick/recenter semantics in the selected build. |
| **4. Weapon presentation / interaction lanes** | **Observed:** phantom magwell magazine on support grip, invisible dropped magazines despite retained authoritative rounds, provisional belt-grab failure, missing belt/mechanism cues, wrong-hand haptics and muzzle/highlight/magwell misalignment or stutter. | Correlate visuals/contact arbitration with authority and custody. Verify visible extraction/drop/insertion, discoverable belt/charging-handle contacts, correct feedback hand and attachment alignment during motion. No ammunition-loss or client/server cause inferred from visual mismatch. |
| **5. Existing hands and vehicle-toolbox lanes** | Existing tracked/support paths and scoped PBL driver behavior are source-supported; broader hand transitions, equipment/tool actions, seats and campaign coverage remain incomplete. | Resume the already assigned work with native build/runtime coordination. Validate hand claims/alignment/interruption and item-specific tool/vehicle behavior; selecting a gadget or sampling generic vehicle input does not prove functional coverage. See [controls and gaps](CONTROLS-20260930.md#current-controller-card--october-9-2026) and [reticle source limits](VEHICLE-RETICLE-PRODUCER.md). |
| **Separate recording lane** | **Observed:** steady-looking PC window but OBS Window Capture interleaves native flat and VR frames; earlier Game Capture failed in an unspecified way. Recording acceptance is open independently of headset stereo. | Identify the captured window and correlate presentation/compositor timing with native/eye overwrite paths once source/video evidence is available. No assumed OBS fault or Game Capture hook cause; keep the extra mirror opt-in. |
| **Later: weapon-wall depth / occlusion** | **Observed, explicitly low priority:** weapon draws over walls despite spatial intersection/occlusion. First-person/world-space hypothesis unproven. | Inspect depth/occlusion/layer ownership after the higher priorities; do not blindly move all first-person geometry. |

Startup is a bounded follow-up using the timings below: no unnecessary fixed wait
was identified inside the measured 6203 ms native setup. Add subphase/publication/
complete-pair timestamps during a later normal launch through the existing owner.
Launcher staging is separate; the requested near-five-second target is unproven.

Documentation owns this queue only. Native implementation, build/runtime and
recovery stay with the existing owner; bindings, hands and vehicle-toolbox owners
retain their reserved scopes. No new runtime owner or competing test campaign is
created by this handoff.

The current-session XR host exited with `0xC0000005` while BC2 remained alive.
The native owner's dump analysis proved a null event dereference in
`OpenXrInput::Feedback` during delayed reload-failure feedback. The owner
reproduced the failure and fixed the regression in
`8b6da5e22cc96d1e6177b630e67344237dd6de93`.
The source fix uses the returned pulse's event identity and kind for bookkeeping.

The owner reports the candidate installed and **five focused tests passed**.
This documentation task did not run those tests. **Live acceptance remains
pending**, including the separate firing refusal and worn physical reload.
This proven current host failure does not establish the earlier worn failure's
cause.

## Preserved-trace findings — 9 October 2026

**Recorded facts:**

- Four seated-XM8 intervals contained **274 fresh, focused, tracked trigger ticks above 0.75**, with native ammunition loaded and no corroborated shots. This supports a firing failure in those intervals.
- The final insertion had **no trigger attempt before B-eject**. Its recorded state was **31 loaded, chamber loaded, no rack required**; ejecting on B is expected for that state. That final sequence is not another demonstrated trigger refusal.
- **Seven authoritative dropped magazines retained 209 rounds**, while the visual dropped-prop count was **zero**. This is a presentation defect distinct from ammunition loss.
- The crash-triggering feedback event was **event 43, `ReloadFailed`**.
- The native shot log also counts ammunition decreases caused by magazine ejection. Those entries alone are **not proof of successful firing**.

**Strong inference, still unconfirmed:** event 43 followed a belt-grab attempt
after loose reserve reached zero. The exact command-failure reason is missing,
so that inferred sequence must not be promoted to a confirmed reason.

This trace analysis establishes no new firing/presentation fix or runtime
acceptance. The previously recorded host-feedback fix remains separate from
these unresolved gameplay and visual issues.

## Live human observation — approximately 03:05 UTC, 9 October 2026

**Session and build identity are unconfirmed**, pending the runtime owner's
receipt. The owner is actively testing; this documentation update does not
interrupt or restart the session.

- After putting the headset back on and resetting height, the owner reported that the experience was **“way better.”** This is a subjective improvement, not a measured frame-rate or latency result.
- With the **XM8**, he removed the magazine, inserted a replacement, pressed **B**, and could shoot again. He explicitly reported that **B worked on this attempt**. This is a successful one-attempt reload/resumed-fire report, not universal acceptance or proof of the recorded chamber/rack state.
- He reported that the **left hand vibrates while the gun is held in the right hand**. This is a reported wrong-hand haptic issue; the exact action association—shot versus reload feedback—remains to be correlated. No cause is inferred.

The earlier failed intervals remain separately recorded. This success does not
establish that every B action racks the gun or erase the prior firing refusal,
magazine-visual issues or failed human check. Append exact identity and haptic
correlation when available.

## Live human observations — approximately 03:06–03:07 UTC

Same ongoing test by report; exact session/build identity remains unconfirmed.

1. The owner removed the XM8 magazine, leaving it visibly absent.
2. Grabbing the **front support grip** made a magazine visibly pop back into the magwell unexpectedly.
3. The weapon **did not behave as if a magazine was present**. This is observed visual/state disagreement, not a successful insertion or proof of an ammunition/custody change.
4. He then thought belt magazines had stopped being grabbable. This remains **provisional**; depletion of loose reserve, custody and contact arbitration need correlation before assigning a cause.

Investigate support-pose, native animation and mesh-visibility restoration
against the authoritative magazine state. That is an investigation direction,
not an identified cause or a change performed by this documentation task.

The owner still cannot locate a bolt/charging-handle grab point and is unsure
whether the XM8 has an obvious native mechanism. Mechanism availability and
discoverability remain open questions; no mechanism is added on this evidence.
The test continues without interruption or session mutation.

## Live observation and next-playtest requirement — approximately 03:08 UTC

- Reaching/grabbing at the belt for magazines sometimes briefly flashes a magazine into the gun's magwell as well. This extends the observed phantom-magazine sequence; it does not establish a real insertion or ammunition transfer.
- The owner hypothesizes that client visual state and server state disagree. This remains **unconfirmed**. Correlate animation visibility, pose, custody and authoritative state before selecting that explanation.
- **Explicit requirement for the next playtest:** selectable smooth turning, like the KF2 mod, while preserving the snap-turn option. The bindings owner is assigned the narrow implementation with a reservation. This records requested future behavior, not an implemented or accepted setting in the current live build.

No change is made to the ongoing test by this documentation update.

## Provisional locomotion note — approximately 03:09 UTC

The transcript says **“Printing is unreliable.”** This likely refers to
**sprinting**, but voice requested clarification and the intended word remains
**unconfirmed**. The owner reported unreliable behavior apparently related to input
direction and not matching his expectations; the exact action and directional
conditions are unresolved.

The documented existing controller mapping is held left-stick click with
head-relative movement. That mapping is context, not a confirmed explanation of
this observation. Investigate actual native directional sprint constraints only
after the action is clarified. No runtime edit is made during the test.

## Confirmed locomotion and weapon-switch observations — approximately 03:10 UTC

The owner explicitly confirmed **sprinting**, resolving the wording ambiguity in
the 03:09 note. He reports that it seems dependent on HMD/player direction and
may stop working entirely after substantial turning. The cause remains unknown.

He also reports that **right-stick up/down weapon-switch flicks do not work on
Quest 2**, despite trying them. This is an observed failure to switch in the
current test, not a classified hardware or binding defect.

**Next-build investigation:** correlate sprint intent with actual held-click,
HMD/player orientation and native directional behavior. For switching, correlate
actual stick axes, threshold and neutral-reset/context state, availability of a
second weapon in the loadout, and native switch dispatch/readback. Verify those
conditions before assigning a cause. The existing test is not altered and the
same controller instruction is not repeated here.

### Additional live weapon-switch detail

With the support hand gripping the gun, **right-stick down makes the support
hand pop off as if switching begins, but the weapon never changes**. Some
interaction response is observable; this must not be reduced to an unbound
stick or hardware failure. Trace support-release arbitration, native selection
request/completion and available inventory. The observation does not prove
which stage failed.

The bindings owner confirms its narrow next-build smooth-turn source patch
preserves gun-switch behavior and makes no live configuration or documentation
change. Source mapping is not proof that this worn test passed.

### Timing and equipment-selection clarification

The owner did not test gun switching early in the session. The failure may have
developed later; this is **not proof of a startup regression**.

He can bring out a **grenade launcher by clicking a button**. The exact button
is unconfirmed; equipment-cycle is a likely interpretation, not a verified
input identity. He does not know how to fire it. Voice suggested right trigger
from the documented selected-weapon mapping; the outcome is **pending**, so no
grenade-launcher firing failure is recorded. Keep reported equipment selection
success and control discoverability separate from gun-slot switching.

## Live firing, recovery request and campaign effect — approximately 03:12 UTC

**Observed:** the owner now reports that he **cannot shoot at all**. This is a later
failure report, separate from the earlier one-attempt XM8 reload/resumed-fire
success. Exact state, session/build identity and cause still require correlation.
The right-stick-down support-hand release without perceived weapon change has
not been correlated to a specific trace gesture.

**Requested next-build recovery, not current implementation:** holding reload
on the gun-owning hand should invoke native automatic reload while retaining the
short-press physical action. Implementation requires a reserved scope and
coordination with native authority and the persistent ledger. It must not create
duplicated ammunition or run alongside a simultaneous physical transaction.
The existing native owner retains runtime/implementation ownership; this
documentation task does not implement or enable the request during the test.
The bindings owner confirms its prepared change remains smooth turning only.

**Campaign presentation observation:** after progression, a suspected binocular,
monocular or cutscene fade caused one eye to appear blurred/covered, with recurring
odd screen effects. The exact scene and effect are unknown. The owner favors removing
intrusive flat-screen overlays/camera takeover and letting the VR player stand at
the viewing point, or later using physical binoculars. These are requested design
directions, not identified effects or completed changes. Establish effect identity
and stereo behavior before broadly removing mission fades.

A restart request was already in flight. Preserve the latest state before that
restart through the existing runtime owner. This note records the sequence; it
does not issue a restart or mutate the session.

### More specific cutscene observation and startup investigation request

The owner specifically reports that **cutscene fade-to-black affects only the left
eye**. This stronger observation is recorded separately from the earlier uncertain
binocular/monocular interpretation; the exact scene/effect identity is still open.

He also requests a small investigation of why VR takes so long to start after the
game itself is already running. Use existing launch-phase timestamps to separate
**renderer readiness, XR readiness, connection/attachment, and the first complete
stereo pair**. No competing runtime test or broad redesign is requested. This is
an investigation request; no measured phase durations are established in this
record yet.

### Read-only startup source check

At source `8b6da5e`, [the play launcher](../Play-BC2VR.ps1)
invokes the test range with a configured 30-second run before calling the VR
session script. [The session startup](../Start-BC2VRSession.ps1)
waits for renderer size, then XR readiness, then starts probe attachment. Its
30-second XR-ready bound is a timeout, not an observed delay. The session source
records overall start/end UTC, without separate UTC fields for every readiness
phase. Actual phase durations and first-pair timing still require existing-evidence
correlation through the runtime owner; no new run was started here.


## Post-restart stereo detail — approximately 03:15 UTC, 2026-10-09

In restart session **031303-101** (approximately October 8, 22:15 CDT),
The owner reports that damaged building corners have detail in the right eye that
is absent or lower-detail in the left eye, producing ghosting. This is a separate
observation from the left-eye-only cutscene fade.

His LOD hypothesis remains unproven. “The left looks old” could describe stale
imagery or simply different detail; no frame-age measurement establishes either.
Correlate pair scene/pose identity, LOD and culling, destruction geometry, and
geometry/cache state before assigning a cause.

The owner estimates VR startup at **10–30 seconds after the game starts**, and wants
it nearer five seconds. This is a subjective estimate and requested target.
A level-wait hypothesis remains unproven.

## Existing-record startup timings — 2026-10-09

The diagnostic owner correlated existing records from sessions **030346** and
**031303** in the same game process, with reported host source `8b6da5e`.
No new launch or runtime change was performed for this measurement.

| Interval | 030346 | 031303 |
| --- | ---: | ---: |
| Session start → renderer report | 1.484 s | 2.071 s |
| Renderer report → XR ready | 0.997 s | 1.240 s |
| XR ready → trace manifest | 0.190 s | 0.256 s |
| Trace manifest → first native sampling telemetry | 6.613 s | 6.305 s |
| First host fresh complete pair, bounded after session start | (8.522, 9.534] s | (9.381, 10.385] s |

Renderer and XR-ready endpoints use the **original file LastWriteTimeUtc as
proxies**; those reports have no dedicated UTC field. The first interval includes
window recovery and two observer reads. Manifest-to-sampling includes bootstrap,
DLL loading and native setup, so it cannot be labeled pure IPC delay. The
complete-pair intervals are telemetry bounds, not exact first-pair timestamps.

For completed session 030346, native `entry_timing.setup_ms=6203` is confirmed.
The active restart's final native report was unavailable, so its exact setup
duration remains unknown. The source defines this duration from
[entryTick at line 1881](../src/games/bc2/NativeProbe.cpp)
to [samplingTick at line 2181](../src/games/bc2/NativeProbe.cpp),
and [serializes it at line 2215](../src/games/bc2/NativeProbe.cpp).
[The trace script writes the manifest UTC before launching the probe](../Start-NativeTrace.ps1).

The dominant measured delay after XR readiness is the native setup segment.
Its internal culprit is unresolved: there are no separate load/connect/hook
timestamps. The diagnostic owner's bounded source review found **no clearly
unnecessary fixed wait within the measured setup interval**. It reported
NativeProbe.cpp unchanged between `c9f2c73` and `8b6da5e`; setup covers executable
read/profile discovery and ABI verification, gameplay/rig installation, private
geometry loading, hook creation/menu channel and global enable. Explicit sleeps
were outside this interval (after sampling, shutdown or diagnostic hitch paths).
MinHook lock yields of 0/1 ms require contention; there is no timing evidence
of contention here.

Separately, game-process start to session 030346 start measured **33.771 seconds**.
The owner verified the staged launcher uses a **3-second injection delay plus a
30-second range loop after injection**. [The pinned play launcher passes both
settings before starting VR](../Play-BC2VR.ps1).
This supports a separate staging wait outside native setup; it does not establish
exact visible-ready timing, a necessary level wait, or behavior of every entry
path.

**Later normal-launch investigation:** timestamp these setup subphases, first
native publication and first host complete pair. This is a recommendation, not
instrumentation already added or a new test campaign. The five-second target
is not yet demonstrated.


## Window-mode transition failure — approximately 03:19 UTC

The owner reports taking the game out of fullscreen, after which the headset
immediately began repeatedly blinking black and rendering became badly broken.
The exact new window mode, resize and resulting dimensions are unknown.
Last known session is **031303-101**; actual active state requires verification.
Unequal per-eye detail may extend beyond buildings, but a common rendering cause
is still a hypothesis.

Voice advised removing the headset and leaving the window unchanged. No restart
was requested. Preserve existing evidence before recovery: window and backbuffer
dimensions, format/device/swapchain changes, XR eye dimensions and pair-rejection
counters. The sole runtime owner should select the smallest safe recovery after
capture; this documentation task starts no game, changes no resolution and
interrupts no process.


## Low-priority weapon occlusion observation

The owner reports that the weapon always draws in front of walls even when
spatially behind or intersecting them. Record this as a **depth/occlusion/layer
issue**, not merely physical collision. First-person render space versus world
space/depth is his hypothesis, not an established cause.

Do not blindly move all first-person geometry. The owner explicitly places this
below per-eye consistency, one-eye fades and window-mode recovery.

## Separate OBS capture observation — post-playtest follow-up

The owner reports that the main PC game/mirror window appears steady, while **OBS
Window Capture records flickering native flat frames interleaved with VR frames**.
He first tried **Game Capture**, which did not work; the exact failure is
unspecified. The capture method is established and should not be asked again.

Stable monitor appearance is not recording acceptance. Inspect the actual
captured window identity, compositor/presentation timing and native/eye overwrite
paths. Do not assume an OBS fault or a Game Capture hook cause for Window Capture.
This is separate from the headset stereo observations. Keep the extra composed
mirror opt-in; no major new test is needed before source/video evidence is
available.

## Worn candidate c2083ba — approximately 03:51 UTC, 2026-10-09

Reported candidate **`c2083ba`**, session **034359-790**. These are live human
observations; this documentation task has not independently reconciled the
selected binary or measured telemetry. Preserve existing records through the
sole runtime owner without interrupting the ongoing test.

- **Head turns:** black borders on both sides are visible again.
- **Weapon motion:** guns feel twitchy.
- **Performance:** FPS feels low; the owner cannot say whether it is better or worse.
  No measured frame-rate or comparative performance result is established.
- **Muzzle flash:** near the correct position, but floats/rotates to the side as
  the hand rotates instead of remaining rigid with the muzzle. A transform cause
  is unproven.
- **Movement:** exact wording is **“smooth locomotion is functioning.”** This is
  not confirmation of smooth turning or acceptance of a turning setting.
- **Sprint:** sometimes dashes sideways. The owner suspects a body/world rotation
  mismatch; that remains a hypothesis pending intent/orientation correlation.
- **Hold-B automatic reload:** visually **“looking pretty good.”** This candidate
  observation advances the earlier request to a reported visual result only.
  Subsequent firing, ammunition conservation and transaction acceptance are
  **not yet confirmed**.
- **Stereo detail:** the damaged building edge still differs between eyes.
- **New post-explosion stereo failure:** building and wall lines retain a
  persistent ghosted image **only in the left eye** after an explosion.

The user suspects a screen-space effect or displacement for the last observation.
Consider per-eye temporal history, postprocessing, shake/destruction state and
pair age as investigation dimensions; none is a declared cause. Keep it distinct
from the earlier left-eye-only cutscene fade and window-mode blinking until
evidence establishes a common mechanism.

The priority queue above records the pre-candidate integration state. This entry
adds a visual hold-reload observation and ongoing failures; it does not upgrade
source fixes, rendering, smooth turning or full reload/fire behavior to headset
acceptance. The diagnostic lead reports the observations saved privately with
an **18-file SHA manifest**. This is an owner-reported preservation receipt,
not an independent verification or a new capture by this documentation task.
Machine-specific evidence locations are intentionally omitted. No runtime change
or test interruption was performed here.


## Delayed final c2083ba observations and restart context

The owner reports that selecting the **grenade launcher** makes the weapon strongly
sideways/canted — **“almost gangster”** — and grenades launch at an odd angle.
The exact launcher profile, gesture/time and cause are unknown. Correlate
model/grip transforms with projectile origin/direction; neither a visual-transform
defect nor a projectile-aim discrepancy is established yet.

He then said the game was paused and he planned a PC restart. The **03:59 UTC
disconnect / 04:03 UTC reconnect** may reflect that intentional reboot and is
**not crash proof**.

The diagnostic lead's **04:04 UTC read-only check** reports no game, host, trace
or build process running. Session **034359-790** ended at
**03:55:38.990 UTC**, with `game_exited=true`, `host_exit=0` and `error=null`.
Those endpoints establish session termination, **not its cause**. The lead also
reports main clean at `c2083ba`, private preservation of the delayed notes and
no relaunch. These are relayed owner receipts, not independent checks by this
documentation task; machine-specific evidence paths remain private.

Verify actual current game/session state before runtime work. Reconnection alone
does not authorize an automatic relaunch. Previously authorized offline fixes
continue through the existing native owner with exclusive runtime/build ownership.
