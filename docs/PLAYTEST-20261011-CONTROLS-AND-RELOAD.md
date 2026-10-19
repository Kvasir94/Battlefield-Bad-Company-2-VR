> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 playtest ledger — October 11, 2026

User observations relayed from the live/voice playtest. This ledger records
feedback and the agreed build scope; it does not establish root causes or claim
that fixes have been implemented. Exact weapon, build and session attribution
remain tentative unless stated otherwise below.

## Session provenance

- The first confirmed VDXR M9 attachment used gameplay source `66fc2bd` with
  launcher repair `40dc698`: session `native-xr-session-20261011-053042-223`.
  Attachment, real controller input and magazine-reload enablement were confirmed.
  This is not blanket acceptance of reload behavior or rendering quality.
- The earlier launch `20261011-051727` exited before VR attachment.
- Arsenal session `054307-166` was initially reported as running, then corrected
  to **failed**: the XR session was created, but native bootstrap exited with
  code 2. Catalog resolution of 305 weapons does not prove playable attachment.
- Later Arsenal attempts must have their own confirmed receipts. Do not attach
  the observations below to an Arsenal session merely because that mode was
  requested, or treat provisional success messages as successful playtests.
- Timestamps embedded in receipt/session names are UTC identifiers. The ledger
  date is the user's local date.

## Observations and hypotheses

| ID | Reported observation / player expectation | Attribution | Hypothesis and status |
|---|---|---|---|
| MAG-001 | The magazine pops to another position for roughly 1–2 frames, within a couple of feet of the gun/player, during animation transitions or hand movement. | Exact weapon, build, runtime and session unknown. | Transform ownership is the user's **unverified hypothesis**. No cause established; investigation and fix deferred. |
| M9-001 | The M9 repeats shots while the trigger remains held. The user expects semiautomatic behavior. | Weapon reported as M9; exact variant and session not independently tied to a receipt. | Cause unverified. Do not infer an input, native firing-mode or replication cause. Fix deferred. |
| RELOAD-001 | Earlier the user could not find the physical bolt/pull action and used another reload press after magazine insertion. **Latest feedback explicitly confirms the XM8 bolt works**, but its interaction/slide zone does not light up. | Latest weapon explicitly XM8; exact variant/session not independently bound. | Working bolt supersedes earlier functionality uncertainty. Interaction-zone feedback remains deficient; no cause established or fix performed by this ledger task. |
| CUTSCENE-001 | In an earlier first firing-range/level cutscene, the scripted camera moves into a preplaced world scope/zoom view. The user has to rotate his head to align with it and finds this clunky. Foggy/screen effects appear unstable or fail to persist during the segment, disrupting the view. | Earlier playtests; exact map, source and session unknown. | Dedicated VR cutscene-presentation review deferred. Whether the newer renderer improves this is unverified. This does not expand the immediate A-button build. |
| ARM-002 | Visible arm rig, mild recoil and lateral jitter were reported during the live test. | Weapon unknown; correlated by integration owner to SteamVR `4f94cac`, session `055633-472`. Local note: `task-4/playtest-notes/arm-recoil-jitter-002.json`. | Cause unknown. Do not infer a cause for shooting, a recoil defect, or a fix from this observation alone. |
| AMMO-001 | Earlier the user could not see/get magazines at the waist; the inability to obtain a waist magazine resolved after picking up ammo. Feedback about availability was unclear. Visible hip magazines and an interaction cue/zone remain essential VR discoverability: players need to know where and what to grab, not merely see decoration. | Exact weapon/session for the waist report not independently bound. | Reserve exhaustion may explain the earlier inability to obtain a magazine; this remains a hypothesis and does not establish the cause of visibility problems. High product priority; no runtime change from this note. |
| XM8-FX-001 | Muzzle flashes and visible bullets/shells appear above the XM8. The flash is not attached to the barrel, and its location moves oddly as the gun rotates; shell origin likewise varies with rotation instead of staying at the ejection port. Holding the Quest menu button to reset height did not fix alignment. | Weapon reported as XM8; exact variant/session not independently bound. User suspects other guns may be affected; cross-weapon scope remains unverified. | Visual muzzle/ejection-origin misalignment only: actual ballistic trajectory origin and socket/bone/VR transform or basis causes remain unverified. Later representative weapon checks needed. No investigation, code/runtime change or expansion of the quick hands/wrists test. |

### Additional current playtest observations and control requirements

- **RELOAD-CONTROL-001:** Default magazine release must use A/the controller
  button. Physically grabbing magazines out of receivers should be an optional
  setting to prevent accidental removal. The user separately authorized focused
  controls reconciliation; the existing integration owner and controls worker
  handle it. This ledger task makes no implementation or runtime changes.
- **XM8-MAG-002:** With an empty magazine well, touching the front/support grip
  makes the magazine visibly reappear in the well, even after picking up ammo.
  This is a visual observation; actual magazine/ammo state and cause are
  unverified. Keep separate from AMMO-001 availability feedback.
- **SPRINT-001:** Sprinting sometimes unexpectedly goes sideways. The follow-up
  about rotating the stick was incomplete; no additional stick, head-direction
  or reproduction details are established. The owner subsequently said, "The more
  I work with sprint, it actually doesn't feel too bad." Retain the intermittent
  observation at lower priority; this is not a confirmed broadly broken sprint
  system. Cause and exact session unverified.
- **STEREO-CUTSCENE-002:** In a snowy cutscene area, one eye appears blurred as
  though looking through a scope while the other does not. Exact eye and scene
  are unknown. This adds a specific per-eye discrepancy to the earlier cutscene
  feedback without establishing a shared cause or fix.

- **RELOAD-FEEDBACK-001 — next-build request:** Reload/gun interaction sounds
  lack kinetic feel, and reload haptics are absent or very weak in the user's
  experience. Sample or split appropriate native sounds as needed. The owner wants
  actual extracted Arizona Sunshine 2 feedback timing, frequency, intensity and
  contexts consulted alongside working KF2 VR feedback before adjusting BC2
  reload feedback. This is requested reference work, not a claim that those
  measurements have already been recovered or reviewed. Later implementation
  should cover magazine drop, grab, insertion, seating, bolt and chamber actions,
  with sound and haptic coverage recorded for each. No live changes are requested
  or performed by this notes-only task.

- **MAG-MATERIAL-003:** Dropped magazines on the floor take on a pale whitish
  color. The owner reports being on a snowy map and wonders whether snow is related;
  that is his unconfirmed hypothesis, not a diagnosed cause. Weapon and exact
  scene are unspecified. Record as a visual material/lighting symptom; neither
  a missing texture nor a highlight defect is diagnosed. No live changes or
  investigation from this note.

### Current headset rendering acceptance and resolution follow-up

- **RENDER-ACCEPT-001:** the owner reports that damaged-building edges in the town
  now match between eyes, the prior town stereo problems appear fixed, and most
  visual eye effects, including explosions, are cleared. This is positive
  headset acceptance for those observed conditions, not blanket acceptance of
  every scene or effect. Edge/detail LOD changes remain visible as he approaches;
  record that behavior without classifying it as inherently broken.
- **RENDER-BLUR-004:** Overall blur is now the main remaining visual priority.
  Verify and optimize game rendering resolution/settings against SteamVR eye
  targets. The integration owner is already investigating; this notes-only task
  performs no duplicate investigation or live settings changes.
- Owner's read-only operational receipt for the current `4f94cac` floating
  session (game 27372): completed renderer checkpoint `57d7a24` is included, and
  live native/host paths and hashes were verified. The active path is D3D11
  native per-eye stereo through v8 shared pairs and the asynchronous XR host to
  full swapchain rectangles. The corrected final receipt reports depth extension,
  requested and active flags true with pipeline 2, but depth pairs/frames were
  zero: no actual depth-submission success is established.
- The owner's runtime view-configuration query recommended **1832 x 1920 per
  eye, sample count 1**, while both active eye allocations were **1920 x 1080**,
  verified from host readiness, command and native-size records. The allocation
  is 104.8% of recommended width, 56.25% of height and 58.95% of pixel count.
  This explicit override is relevant evidence for the blur investigation, not
  proof of its complete cause. The owner found full-resource copies and full XR
  image rectangles, with no intermediate bridge rescale/crop. Default 8% FOV
  overscan widens angles without increasing pixel allocation; final runtime warp
  was not quantified. The query created no session or GPU workload, and the
  live session was unchanged.
- **Corrected provenance supplied by the integration owner:** These references
  are owner-reported evidence, not independently inspected by this ledger task.
  The directly queried per-eye recommendation is in
  `%USERPROFILE%\Documents\Codex\2026-10-09\task-4\playtest-notes\xr-per-eye-recommendations.json`,
  fields `views[0/1].recommended_width` / `recommended_height` = 1832 / 1920.
  This query created no session or GPU workload.
- **XR allocation, directly reported:** In selected package
  `<repo>\build\playtest-20261011-055537-4f94cac`, session directory
  `reports/native-xr-session-20261011-060330-535/ready.json` reports
  `width=1920`, `height=1080`; `finalhost.json` reports
  `eye_width=1920`, `eye_height=1080`. The owner cites `OpenXrHost.cpp:451-452`
  for both swapchains using requested dimensions and line 877 for full image
  rectangles. Source revision/file location should be retained with that owner
  receipt rather than assuming current main has identical line numbers.
- **Native renderer size, directly reported but limited:** `native-render.json`
  reads the native game renderer global through the Observer and reports
  1920 x 1080. It does not independently measure every internal per-eye scene
  or post-effect render target, nor establish the desktop backbuffer dimensions.
  The owner did not supply its full path in this correction.
- **Shared capture dimensions, inferred:** Successful strict dimension/copy
  descriptor guards and allocation requirements support matching dimensions;
  no live GPU texture `GetDesc` snapshot was recorded. The bridge uses full
  `CopyResource` without rescaling. Internal postprocessing, scope and cropping
  passes remain unmeasured. Thus the established comparison is XR allocation
  versus runtime recommendation, not a measurement of all internal scene targets
  or a proved cause of blur.
- Final host metadata also reports prediction enabled, overscan 0.08, late-pose
  lane enabled, 21,772 relatches and 23,050 overlapped operations. These are
  operational observations, not visual acceptance. The owner reports BC2 ended
  at 06:16:21 UTC and the KF1 handoff verified no remaining BC2 lease.
- Keep **STEREO-CUTSCENE-002**, the one-eye scope-like blur in the snowy cutscene,
  separately unresolved. The owner also tracks it as `stereo-blur-005`; these
  refer to the same reported symptom, not a new duplicate issue. Exact eye,
  scene and cause remain unknown.

### Floating-hands headset feedback and waist hint

- **HANDS-001:** the owner reports the floating hands/wrists almost work, but a
  black pixelated/splotchy mass appears on the back of the hand/wrist at certain
  rotations, most obviously when looking at the back of the wrist. Some geometry
  stretches toward the shoulder/arm. Attribution: `4f94cac`, SteamVR floating
  session `native-xr-session-20261011-060330-535`, game 27372, host 40972,
  trace 36564, as identified by the integration owner. Skinning or hidden arm
  remnants are hypotheses only; no cause is confirmed. This is partial headset
  feedback, not visual acceptance. The pinky observation was explicitly withdrawn
  and is not an active bug.
- **AMMO-001 temporary presentation approval:** A weapon-specific magazine model
  is the preferred waist hint. If extracting suitable props is difficult, the owner
  accepts a simple visible interaction zone for this playtest. Preserve the
  discoverability requirement: show where and what the player can grab. This
  records the approved fallback without making runtime edits or automatically
  starting another fix.

MAG-001 also has a local observation record at
`%USERPROFILE%\Documents\Codex\2026-10-09\task-4\playtest-notes\magazine-pop-001.json`.
Keep private runtime/native evidence local; this ledger contains user feedback
and operational provenance only.

## Immediate authorized next build

The user authorized only the following control remap for the immediate build:

1. Move reload/magazine release and the **existing chamber fallback** to **A**.
2. Resolve the previous A-jump conflict by moving jump to **right-stick UP**.

This is an intended next-build mapping, not a statement about controls in the
already-tested `66fc2bd` package. Preserve the existing reload/chamber behavior
while moving its button. The implementation and packaging owners establish the
actual source checkpoint and installed state separately.

### Selected focused build (integration-owner report)

The integration owner subsequently reported selection of
`<repo>\build\playtest-20261011-055537-4f94cac`, source
`4f94cac0ff2e3e2bc5c966e2046bf3a338fec55e`: isolated compatibility change
`d7ba44f` plus controls change `b1447ba`.

- **A short press:** existing physical magazine release/chamber fallback.
- **A held 600 ms:** automatic reload.
- **Right-stick UP:** jump; **DOWN:** other primary gun.
- No new radial menu or IPC changes included.
- Owner reports native/fixture compilation, M9 capability and ABI-export checks,
  and ControllerInput/Bc2ReloadInteraction checks passing (2/2).

The package launcher exists on disk. Build/test results above are attributed to
the integration owner's receipt; this documentation task did not rerun them.
SteamVR launch was reported imminent, with no successful session or frame receipt
yet supplied for this build. Existing IK is retained. Hands being enabled does
not prove visible gloves, and the floating-hands option is not a demonstrated
fix for absent gloves. Do not mark hands restored without headset evidence.

### Confirmed SteamVR Arsenal attachment

The integration owner then confirmed `4f94cac` live and rendering:

- Session: `reports/native-xr-session-20261011-055633-472` in the selected package.
- Trace: `native-trace-20261011-055635-051`.
- Processes at confirmation: game 44716, host 10592, trace 19520, launcher 23792.
- At 05:56:55 UTC: 466 fresh / 1,156 reused XR pairs, 2,402 ended frames,
  2,401 tracked-hand samples, zero transfer errors. Loaded probe hash matched
  the selected package, bootstrap error output was empty and fixture handoff
  was logged.

This confirms attachment/rendering, not visual quality or restored hand meshes.
The owner's video test is now **notes-only**: no automated input, live changes or
teardown until he ends it. Earlier MAG-001, M9-001 and RELOAD-001 attribution
remains tentative; do not retroactively assign those reports to this session.

The later rendering-owner audit confirms that the hand rendering path exists
(native wrist/finger posing and private palette) and was already enabled in
the earlier IK build. Tracked-hand counts establish input activity only, not
visible glove rendering. Floating mode reuses native wrists with collapsed arms;
the capped export is not drawn. Neither an unimplemented hand-rendering claim
nor a verified restoration claim is supported. Default IK remains selected.

The owner saved attachment evidence in package
`reports/live-attachment.private.json` and manifest `runtime_checks`. At
05:57:24 UTC the owner reported 1,439 fresh / 3,553 reused world pairs and live
game/host/trace processes. No further runtime changes are planned during the
notes-only video test. Source/runtime/attachment verification remains with the
integration owner; this documentation task does not start another session.

At 05:58:37 UTC the integration owner verified the visible arm/gun segment was
the final VR `tracked_native_ipc_stream` session on SteamVR `4f94cac`, with the
same game/host/trace processes alive. Real controllers were enabled and synthetic
hands disabled. Rig-pulse, death, equip, reload-hold, round, request, XM8-magazine,
physical-magazine, physical-reload-repeat and body-holster-fire probes were all
off; no replay or test-scene switches were configured. No automated firing or
reload input was configured. This establishes configuration, not the cause of
observed shooting. A-reload was active. The latest reported frame sample at
05:58:12 UTC was 3,069 fresh / 7,698 reused pairs. No headset acceptance or fix
is implied, and no new launch, replacement or teardown was requested here.

### Subsequent scope correction

Parent explicitly stopped the odd-motion investigation: ARM-002 is nonblocking
and remains an unconfirmed observation. Controller contact or a user-input
quirk are possibilities only, not diagnoses. No fix is requested for it now.
No automation was confirmed; the owner checked configured tracked-XR/controller
stream arguments and disabled probes, without inspecting raw angles or poses.

The next separately owned effort is a fast candidate using the existing floating
hands/wrists path, pending the hand worker. Preserve A-reload/right-stick-UP jump
and SteamVR Arsenal behavior. The owner reported game 44716 still running
unchanged; this ledger entry authorizes no live modification or teardown.

### Hands requirement and preview intent

The requested disembodied hands/wrists must use the game's skeleton and poses
sampled or borrowed from native game animations, following the requested KF2
approach. This is a product requirement for the separately scoped quick hands
candidate, not proof that the current presentation meets it.

The upcoming test is intended as an early public preview. That intent does not
constitute release acceptance or authorize public uploads/publication. AMMO-001
adds a notes-only observation and does not expand the current runtime-fix scope.

The integration owner subsequently verified the existing native floating-hands
path in ancestor commits `6db1db4`, `e40c8c0` and `3a3ffb8`; no code change or
rebuild was needed. It preserves authored native glove/wrist/finger/contact
skeleton data and collapses the upper arm/forearm; the capped export is not drawn.
The old IK game 44716 closed normally with exit 0 and its launcher released at
06:02:28.252 UTC. One fresh `4f94cac` SteamVR Arsenal launch with
`-HandsMode floating` began at 06:02:55 UTC. At this report, VR attachment and
appearance acceptance were still pending. Source/profile remained unchanged,
and the new desktop shortcut explicitly selected floating mode. ARM-002
investigation remains stopped.

### Confirmed floating-mode attachment

At 06:03:55 UTC the integration owner confirmed the fresh SteamVR Arsenal
session `native-xr-session-20261011-060330-535`, trace
`native-trace-20261011-060332-953`, in the same selected `4f94cac` package.
The launch manifest recorded `HandsMode floating` before fresh game creation.
Reported processes were game 27372 (started 06:02:57.101 UTC), host 40972,
trace 36564 and launcher 3268. The loaded probe hash matched the selected
package; 486 fresh / 1,360 reused pairs were reported.

This supersedes the pending-attachment status above, but visual appearance and
actual control acceptance remain pending. No C++ rebuild was required. Native
gloves, game skeleton and authored contact profiles remain preserved; no capped
mesh was added. BodyInventory remains off, with waist-path diagnosis separately
logged and no feature enabled. This ledger update performs no runtime changes,
input injection or teardown; the odd-motion investigation remains stopped.

All other fixes, new radial/loadout features and feedback work are deferred to
the following build. Existing compatibility/startup work has separate ownership;
these observations do not expand the immediate controls change into bug fixes.

During the owner's video test, do not modify the runtime, inject automated input or
tear down the session. This documentation task performs no investigation,
implementation, build, deployment or game launch.

## Latest authorized next-build priorities

These priorities supersede the earlier quick-candidate scope for subsequent
development; they do not authorize changes to the current live session.

1. **Independent hands/wrists:** Implement proper independent hand/wrist models
   retaining native sampled poses and gun interactions. Collapsed or hidden
   full-arm IK geometry does not satisfy this revised requirement. Current
   floating hands are partially functional, not visually accepted.
2. **Gun and reload feedback:** Address RELOAD-FEEDBACK-001 with stronger,
   appropriate gun/magazine/reload haptics and sounds, informed by the requested
   AS2 measurements and working KF2 VR feedback. Reference extraction/review
   remains work to verify, not an already completed finding.
3. **Empty-magwell visual state:** Fix XM8-MAG-002, where touching the
   front/support grip visibly resurrects a magazine in an empty well.

**HANDS-002:** the owner additionally reports random arm-rig flashing while firing.
Its relationship to HANDS-001 shoulder-stretched vertices is unproven; preserve
it as a distinct observation without assuming a shared cause. Exact weapon and
event timing are not established by this note. The withdrawn pinky complaint
remains inactive.

The owner's final video-review assessment explicitly rejects IK arms flashing or
warping during firing and wrist vertices stretching toward the shoulder. These
are **next-build independent-hands acceptance failures**, not optional cosmetic
polish. Preserve both observations without asserting a common technical cause.

**PERF-001 — deferred observation:** the owner suspects FPS is lower than in his
old-renderer video. No measured regression is established. He explicitly wants
this retained for later, with no investigation or performance gate added to the
current work.

Focused controls reconciliation and overall blur/resolution verification remain
open. The existing owners retain those efforts. This ledger update performs no
code edits, build, runtime changes or automatic new fixes.

## Subsequent readiness receipt — integration owner

The integration owner reports catalog, empty-magwell and haptic sources
checkpointed/integrated at root commit `aa09a9c`. Runtime-recommended resolution
and maximum-quality launcher implementation is underway. This is source status,
not a claim that a new playable package or headset acceptance is complete.

The independent-hands backend is authored, but exact glove UV/material and owner
color/depth association evidence is still missing. If cached static proof is
insufficient, a bounded native draw capture is needed. No BC2 game capture may
overlap the owner's KF2 playtest; this receipt starts no capture.

The bounded recoil source reviewer reports a documentation-only contract:
GunSway post-tick matrix verification is reported, while tick/publication epoch
and firing compensation remain unsupported without exact observation. Recoil
compensation is not implemented. No private native evidence is reproduced here.

At this receipt there is no BC2 live session or build and no automatic launch.
The 07:25 UTC recording goal (02:25 America/Chicago on October 11) is a priority,
not a readiness guarantee. Remaining gaps must be stated rather than silently
omitted from the candidate's scope.

### Updated owner status at 06:40 UTC

- Corrected test fixture integrated at `0c589c9`; native probe compilation and
  all five focused CPU checks passed (reported test duration 0.22 seconds).
  The x64 host compile is now running under the actual shared mutex, superseding
  the earlier no-build status. These results are owner-reported, not rerun here.
- Runtime profile helper and high-quality checks passed. `Aniso=4` uses the
  native exponent convention to request 16x; the actual engine cap has not been
  measured. `MainQuality=custom` is required to avoid preset overwrites. These
  settings checks do not establish live visual quality.
- Independent hands are **not integrated**. Exact GPU material/layout/upload
  owner evidence remains missing. The earliest estimated capture-source window
  is 06:55–07:05 UTC; this is not completion or capture authorization during KF2.
- An opt-in recoil observer is targeted for 06:55 UTC. Actual recoil transfer
  remains unsupported without epoch and shot-consumer proof; neither the target
  time nor observer work establishes implemented compensation.
- The audio owner is preparing an ownership gate and cancellation/stop repair;
  discrete cue binding remains unverified. Earlier haptic-source integration
  does not establish complete sound/feedback acceptance.
- No BC2 game launch while the owner plays KF2. Authorized builds may continue.
  The 07:25 UTC recording readiness goal still cannot be guaranteed. Parent
  direct-message delivery remains unavailable to the reporting owner
  (`thread-not-found`); this status is retained for the parent handoff.

### Updated critical path at 06:54 UTC

- Integration source `2d67ec7` is reported clean. The x86 DLL, fresh x64 host
  and nine focused CPU tests passed across two gates.
- Interim package `playtest-20261011-064809-332c89e` is verified but
  **unselected and not recording-ready**; the `4f94cac` baseline remains intact.
  Its reported scope includes A/UP controls, catalog input using raw left trigger,
  empty-magwell repair, haptics and the exact audio-owner gate. Test/source status
  does not establish live acceptance of those changes.
- Fresh runtime-recommended dimensions and the High/custom quality wrapper
  (`Aniso=4`, `MSAA=0`) were verified offline. No new native render-size proof
  was obtained.
- The proper native glove backend compiled but has no active substitution.
  Original draw/material/upload capture source is nearing a targeted freeze
  around 07:05 UTC. Required native evidence joins remain open.
- Recoil is an opt-in observer only; actual transfer is excluded. The owner
  reports historical M9 racks emitted no `anim_sound`; next audio work requires
  a validated, owned bolt-graph audition. This is not evidence of working cues.
- No BC2 game while KF2 is live. A coordinated approximately 15-second in-level
  hands capture after early-menu injection/startup is proposed; the exact plan
  is still to follow. This receipt does not start or authorize an overlapping
  capture. The 07:25 UTC recording goal remains at risk pending native evidence
  joins and audio audition.
- Source-only privacy audit/push checkpoint work is underway according to the
  owner; no private artifacts were uploaded. No completed push is claimed here.
  Direct parent messaging still reports `thread-not-found`; this ledger/final
  handoff preserves the critical path for parent review.

### Subsequent source checkpoint and interim package receipt

- The integration owner reports the cleaned source-only checkpoint pushed and
  verified at
  the private development repository, tag `checkpoint/combined-20261011-2a24b58`
  in the private repository. Its tree matches `2d67ec7`, with new noreply
  metadata. The owner's audit reported zero new binary, private-receipt, token
  or personal-path findings. This task did not independently inspect or push it.
- The newer interim package is
  `<repo>/build/playtest-20261011-065616-2d67ec7`, still **unselected**.
  The owner reports 24-file hash verification and eight PowerShell AST chain
  checks passing. Previous `4f94cac` selection is preserved, with rollback at
  `reports/rollback-before-playtest-20261011-065616`.
- Exact runtime eye dimensions and High/custom settings propagate through the
  wrapper and Play launcher to the native window and XR configuration. This is
  an offline propagation check: no new game run or runtime probe was performed.
- Audio source work now implements an explicitly opt-in M9 bolt audition with
  loaded-tree, map and owner validation. Historical two-rack observations had
  no native callbacks. The audition is not a seat/button default and has not
  yet established working live feedback.
- Hands capture source remains in preparation. No BC2 game while KF2 is live.
  Real native hands draw/material association and audio audition remain the
  critical path; recoil remains observer-only. These receipts do not establish
  a recording-ready or visually accepted build.

### Independent-hands critical-path and capture-plan correction

The hands worker explicitly states that **wired and accepted independent hands
cannot honestly be delivered by 07:25 UTC (02:25 America/Chicago)**. This is a
remaining implementation/evidence dependency, not simply a compilation delay.
After the capture receipt, work still includes asset-reader normal/UV/material
interpretation, the private loader, same-frame bilateral replacement, and
removal of the original color/depth draws. Do not label the interim package
recording-ready on the strength of compilation or capture alone.

The updated owner plan targets a capture-source checkpoint around 07:00 UTC,
followed by root compilation; neither is claimed complete by this note. The
proposed capture has a 60-second menu-to-level window to allow loading, with
actual byte admission expiring 15 seconds after the first scoped pack and capped
at 32 draws / 16 pairs. This supersedes the earlier simplified approximately
15-second in-level plan.

Attachment must precede Standard-glove level loading. Cached resources can still
prevent observation of shader/layout creation. A late in-level 15-second capture
can provide geometry, texture and upload facts but does not guarantee layout
proof. The owner will report compiled-capture status and exact remaining gaps.
This is a proposed plan only: no BC2 game while KF2 is live, and no capture or
runtime change is performed by this documentation task.

### Observer build receipt at 07:05:52 UTC

The integration owner reports the hands-observer DLL and three focused CPU
suites passed, with the compiler lease released at 07:05:52.4078992 UTC.
The reported compiled native checkpoint is `0593e07`, including an SDK reflection
UUID fix. Ownership notes were updated in main and root, and the capture runbook
was copied into main. This documentation task did not rerun those checks.

The parent reports a bad KF1 renderer result and the user exiting. The BC2 owner
has requested actual lease release and will not launch BC2 unexpectedly. The
proposed observer slot remains 60 seconds from the menu for loading, with 15
seconds of selected-data admission, one-frame bilateral evidence, and caps of
32 draws / 16 pairs. The evidence baseline is neutral poses, with no reload,
equip or recenter during the first selected frame. Awake physical controllers
were the proposed human-driven route, not a universal capture requirement.
Wrist rotation, finger opening/closing and grip motions belong to later pose
acceptance, not this contract capture. Exact prepared-package
identity and steps are still to follow; no capture is claimed started here.

Native independent-hands replacement remains **not wired**. This observer is for
UV/material, palette and draw evidence. Explicit M9 bolt-audition source remains
uncompiled; passive audio cannot prove the graph, so a combined audition-ready
claim is unsupported. No live changes are performed by this ledger update.

### Prepared observer package and coordination status

- Compiled hands-observer DLL is packaged at
  `<repo>/build/playtest-20261011-070738-b519af0`, **unselected**.
  The owner reports three focused CPU suites and eight launcher checks passed.
  The single-injection menu-to-level sequence is being checked before any
  coordinated launch; no BC2 game is running in this receipt.
- The hands worker verified the local Blender addon 5.2.1 connection, currently
  with a default cube and no armature. Explicitly authorized native-asset
  inspection is proceeding locally. Addon connectivity does not establish an
  imported or working hand model. Native replacement remains unwired; material/UV
  interpretation, private loading, bilateral draws and cap work remain.
- Explicit M9 audition tests and a new cue profile are being added. That source
  is written but uncommitted and uncompiled; only the previous ownership gate
  is compiled. Do not claim a ready combined audio audition.
- KF1 runtime lease release at 07:06:00.735843 UTC was verified free. KF2 reports
  a repair offline gate under the compiler mutex. BC2's compiler lease was
  released at 07:05:52 UTC. These are time-specific ownership receipts, not
  permission to bypass a fresh check or start an uncoordinated game.
- The owner identifies main `OPEN_WORK` at `8a21378` as the authoritative gap
  and runbook record, including native hands preferred over slicing and recoil
  deferred. The fresh-runtime-eye/High-custom wrapper is packaged and verified
  offline; no new actual-size GPU test has occurred. Parent coordination remains
  required for the proposed live capture; this ledger task launches nothing.

### Capture-action and launcher correction after source review

The hands contract capture freezes **one native frame across both eyes**. Use
neutral poses with no reload, equip or recenter during the first selected frame.
Awake physical controllers apply only to the proposed human-driven route.
Earlier motion suggestions are superseded for this
capture and apply only to later pose acceptance. The 60-second observer window
allows loading; byte admission lasts at most 15 seconds, with most selected data
expected from the first frame. Existing draw/pair caps remain unchanged.

Package `playtest-20261011-070738-b519af0` has three of three native-hands checks
passing, but the exact early-menu attachment sequence requires disabling
automatic continuous injection. The root owner is checking that launcher path;
**a ready one-command capture is not yet established**. No surprise BC2 launch.

Audio audition remains uncompiled. The owner is reviewing dispatch cadence:
historical racks showed no `anim_sound` callback within plus/minus one second,
and a callback-only queue with a 250 ms lifetime could expire. This is an
identified design risk under review, not a validated cause or working audition.
No runtime changes or additional investigation are performed by this ledger task.

### Emulated real-engine capture route under evaluation

The owner clarified that an emulated real-engine test with physical controller input
blocked may remove the need for awake, neutral physical controllers. Neutral
poses remain the evidence baseline; requiring a person or awake hardware is not
established as necessary. The existing owner is evaluating, and is authorized to
run, the existing deterministic route if viable.

Neither the physical-controller route nor the emulated route is claimed proven
until the owner validates actual native draw capture and input isolation. This
clarification does not establish a completed capture, a ready launcher, or
authorization for this documentation task to launch anything.

### Historical emulated harness recovery and audio correction

The existing `task-3/rea-review/bc2-recoil-investigation/`
`run-static-recoil-capture.local.ps1` was located and its command paths sent to
the integration owner. Its historical `capture-20261010T085212Z` receipt reports
completion without headset use and restored settings. It starts a static neutral
IPC publisher but uses a recoil action tape and attaches after a 30-second level
loading wait. **Do not rerun unchanged for neutral pre-map hands capture.**
Successful historical replay does not prove current physical-input isolation,
early-map attachment or native glove draw/material capture. Those remain for
the integration owner to validate. No game or harness was run during recovery;
only command paths and operational metadata were returned, without raw evidence.

The owner corrects the earlier audio-profile assumption: six purported M9
profiles were actually M95 and are invalid for the intended M9 audition. No
audition-ready binding exists and no cue was played. Earlier audio readiness
references must not be interpreted as validated M9 sound selection.

## Follow-up recording

For the later fixes, retain the observation IDs above and add confirmed weapon
variant, source/launcher checkpoint, runtime, session and reproduction details
when available. Record any diagnosis separately from the original observation,
and record headset results separately from offline checks.
