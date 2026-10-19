> **Historical archive.** Preserved on October 13, 2026. Implementation claims,
> priorities, agent assignments and workflow restrictions below belong to their
> recorded sessions. Use [current status](STATUS.md) and [development rules](../AGENTS.md)
> for new work. Technical evidence retains its original scope.

# Open work

## Reload guidance, audio and recoil — latest continuation

Visible pouch/charging-handle guidance is implemented and compiled; four focused
checks pass. Audio completion-origin handling is fixed, with exact-profile
audition and failure reporting available through the unified launcher. Native
recoil observation now includes VR baseline and shot composition; transfer and
audible event-specific cues remain unfinished. See
[current continuation](HANDOFF-RELOAD-GUIDANCE-20261011.md). The user's running
playtest and installed/selected package were left intact. Older guidance/origin
gap descriptions below are historical where superseded by this entry.

## Glove appearance and ten mesh variants — October 11

The almost-black Assault glove was caused by cross-variant UVs under its native
material. Variant-native attributes now preserve its fabric shading. Short wrist
cuts and smooth recessed caps replace the long sleeve. All ten installed FP mesh
variants have generated packs, including separate Russian glove/finger materials,
Vietnam bandages and explicit NVA/Beach vertex layouts. The set loader preserves
exact GPU source hashes, owned-player admission and both-eye frame activation.

See [hands readiness and test sequence](re/BC2_HANDS_READY_20261011.md) for coverage,
build/package evidence and limits. Assault has saved both-eye appearance proof;
other variants have source/layout and asset checks pending map/class coverage.
Real headset feel is still untested. The selected build remains unchanged.
Final unselected candidate: `<repo>/build/native-hands-ready-a0fa4fe`;
34/34 hashes, two focused checks and the ten-pack in-game loader pass.

## Native replacement completed - October 11, 20:08 UTC

Native replacement is implemented and proved in the Assault campaign fixture.
Compiled source `a005db1`:8 exact GPU source hashes,4,752 replacement submissions,
both-eye saved frames with original arms removed, probe exit0 and no new crash
report. The native DLL and two focused checks pass. Unselected candidate:
`<repo>/build/native-skin-a005db1-assault` (25/25 payload hashes).
See [implementation and live proof](re/BC2_NATIVE_SKIN_IMPLEMENTATION_20261011.md).
The asset enables replacement; `FVR_BC2_NATIVE_SKIN=0` disables it. Headset
appearance/feel and other character variants remain unaccepted. Selected build
`playtest-20261011-055537-4f94cac` and installed files are unchanged.

Older checkpoint sections below are historical where they describe hands as
unimplemented or the replacement seam as unproved.

## Resumed project review — October 11, 2026

Closeout: compiled source `99610cd`, affected x86 DLL plus seven focused checks
passed; combined package `playtest-20261011-191403-99610cd` is verified and
unselected. Audio v3 preparation is complete offline against the separate
isolated diagnostic package. The next interactive action requires separate
authorization; hands implementation awaits the remaining replacement-seam proof.

Work continues on `codex/bc2-resume-20261011` in the existing integration
checkout, based on verified `3aa78af`. Main `6449df3` and selected package
`4f94cac` are distinct. Preserve main's five untracked items. Current disposition
and player sequence: [candidate status](BC2_CANDIDATE_STATUS_20261011.md).

- The user approved the existing native skinned-instance route. The
  [static seam research](re/BC2_NATIVE_SKIN_SEAM_20261011.md) traces the selected
  soldier mesh through local draw descriptor, native bindings, palette remap
  and queued geometry. The descriptor's LOD pointer is the narrow candidate;
  shared LOD mutation and stack geometry clones are unsuitable. Remaining:
  live player/request association, geometry resource ownership/retirement,
  layout compatibility and complete pass coverage. No hands implementation.
  REA startup failure was diagnosed and its exact stuck process cleaned up;
  permitted read-only saved-project fallback completed the static research.
- [Rendering reconciliation](BC2_RENDERING_RECONCILIATION_20261011.md) confirms
  R1–R5 are integrated. This batch adds bounded diagnostic readback device
  guards; it does not claim full device recovery or resolve cutscene/blur/pacing
  observations.
- [Controls reconciliation](BC2_CONTROLS_RECONCILIATION_20261011.md) adds
  button-default magazine removal with `FVR_BC2_MAGAZINE_GRAB_REMOVE=1` opt-in.
  Existing empty-magwell/support and haptic repairs are retained. Waist/handle
  visual guidance and delayed action-origin handling remain explicit work.
- [Audio recovery](BC2_AUDIO_RESUME_20261011.md): isolated source `5f8f1e6`
  compiles and both focused checks pass. Diagnostic package
  `playtest-20261011-191013-5f8f1e6` is prepared and unselected. Historical
  rejected cue/recording is not audible-success evidence.

Parent owns commits, combined compilation and packaging. No new interactive
test, installation or selection is authorized in this continuation. The
selected working build and rollback remain intact. Earlier entries below are
historical and must not supersede this source/package distinction.

This is a live list of work in flight and work that is ready to pick up. The
newest entries go at the top of each section. Work that has finished moves to the
handoff document for that wave.

## Combined BC2 overnight continuation, 2026-10-11 07:36 UTC

**08:55 UTC continuation:** scoped integration commit `42a9db0` owns copied
archive strings across fixture callbacks and uses bounded existing guarded reads
for current archive/name before comparison, hashing and logging. The affected
fixture compiled; unchanged native DLL compile reused the same hash. Compiler
released 08:54:51.7236196 UTC. Prior focused4/4 checks are inherited unchanged;
no fresh runtime fix acceptance yet. Matching unselected package is
`build/playtest-20261011-085504-42a9db0`.
One neutral M9 observation is reserved after coordination, no audio/fire action.

**08:57 UTC live result:** repaired fixture progressed beyond Give into native
observation; exact identity metadata and recorded input exist, but completion and
placement join remain pending. Actual owned game7688/driver22240 acquired
game lease08:55:48 UTC; see local task-4 latest-m9-frame-observe pointer/status
before any competing launch. New cleaned source tag
`checkpoint/combined-20261011-bde6dca` is pushed and remotely verified at
`bde6dcad32e875ef5eb2114dec0ef74b37125bcb`, identical tracked tree to
integration42a9db0. No private uploads; no runtime acceptance yet.

**08:58 UTC completed observation:** neutral M9 capture finished as observed with
hooks disabled; one exact identity row, recorded input and complete native report
are available. No cue attempt/play/stop occurred. Owned game7688/receiver/trace
exited; actual game lease released08:57:12.7002408 UTC, protected settings/stage/
prepared record and six selected hashes restored. Scope is usable observation,
not hands/audio acceptance or proof of a universal fixture crash fix. Existing
authorer worker checks the exact identity/placement/generation join locally next.
No BC2 game/compiler lease held. Evidence: local task-4
`m9-frame-observe-20261011-085548/status.json` and pointed native report.

**09:02 UTC handoff:** local authorer identity/placement/input admission passed
256/256 exact joined settled hand rows. Ordinary production authority adapter
proved the actor/item/native-copy joins, matching counts/capacity and closed-bolt
ready state. New local synthetic geometry-guided rack tape has721 ticks, with
fixture/provenance manifest; it is unexecuted, not native animation or audio
acceptance. Exact paths are in task-4 private handoff and the existing worker's
handoff. All four subworker handoffs/root recovery JSON are saved. No BC2 lease;
KF2 holds the coordinated game slot from09:01:07 UTC. Next is separately scoped
native rack cue audition using the exact profile, then verified endpoint recording.
No seat/button mapping or default audio enable before actual phase evidence.

**09:10 UTC audition preflight:** root caught total-capacity versus native replay
feed-capacity mismatch before execution. Existing authorer corrected only private
metadata; all216 motion rows/721ticks unchanged. New private feed-capacity tape
hash `4ABE17C9B53D5486F4C91DED613BFB38DA879869211CBB2440B759A234FB9FA2`.
Root verified fixed head/right hand/orientations, controls zero except LH squeeze,
native feed precondition and compiled physical Gather suppression with separate
replay input. Prepared private driver parses cleanly, exact-PID render endpoint
required with no default fallback, 24-second loopback and15-second native pass.
No tape or audio execution yet; KF1's bounded25-second measurement holds game
slot. Independent glove worker implements source-only rejected-worker147 pack
diagnostic in its isolated checkout; no producer admission or N-1 relabeling.

**09:15 UTC outcome:** first bounded audition failed before replay: native loader
refused the tape, hooks disabled. All216 rows copied a native64-bit space into
the tape's bounded logical epoch, violating the existing uint32 parser limit.
Root compiled a private offline console linked to the exact production loader;
it reproduces `invalid record at line6`. Existing authorer corrects logical epoch
to0 while preserving native space in private provenance; geometry/controls stay
unchanged. Exact production Load and numeric preconditions must pass offline
before next execution. Game32448/receiver/trace/recorder exited; settings/six
hashes restored, actual lease released09:11:15.8198369 UTC. Audio recorder opened
on exact game endpoint but no cue or emitted-energy acceptance is claimed.
Private parser compile lease released09:14:20.7663691 UTC. No BC2 lease held.

Independent hands source checkpoint `0d0d44d` is clean in its isolated checkout:
rejected-worker pack diagnostics with mandatory post-original identity readback,
no producer/draw admission changes. Eight new CPU cases queued, uncompiled;
existing gates/root candidate remain frozen42a9db0 for audio. Private handoff
updated. Integrate and gate this diagnostic after the current audio pass.

**09:18 UTC checkpoint:** hands diagnostic is integrated as `64f83dd`, tracked
clean. Affected NativeProbe compiled; NativeHandWorkerSample (eight cases),
RigWorkerBinding and NativeHandCapture passed3/3 suites. Native DLL SHA256
`89A20B4C8CAC78A66DD1C6F0A43FED1407BEF880F583B9D65057096593DF71F0`.
Actual compiler released09:17:06.3616909 UTC. Matching private package is prepared,
unselected; frozen42a9db0 package remains pinned for audio, selected4f94cac intact.
Copied lease relation defaults unavailable in this diagnostic; no current-frame
eye0 palette/draw or independent glove readiness claim.

Corrected private epoch0 rack tape now passes exact production Load and live-count
preconditions offline:721ticks, only logical epoch changes, original pose/control
rows and private capture provenance unchanged. Hash
`E149A24D62D475DAB2CDD4E52FB17A987D87838AC2A60C929087A08EB39C7E74`.
Prepared bounded audition driver passes AST/preflight and pins older package.
KF2 is sole shared runtime-recovery owner; BC2 has no game/compiler/recovery
lease and waits its actual final release. No parallel SteamVR restart.

**08:50 UTC recovery baseline:** integration detached HEAD
`491408d543e4d55f89b6c4f3d41ffdabe903485a` is tracked-clean. Main is
`344c597dc72eec6d3aec6d95bfce333664cb47b1` before this documentation checkpoint;
main is not the cumulative integration executable tree. Cleaned source tag
`checkpoint/combined-20261011-f242a18` is remotely verified at
`f242a185d8746fdc23046c9e9d10a012b20ecf5c`, with identical tracked tree to
integration. Only cleaned tracked source was pushed to origin-bc2. Private assets,
profiles, GUID records, native traces and audio stay local. No binary release.

Candidate `build/playtest-20261011-083734-491408d` is prepared, unselected, and
not recording-ready. Native DLL SHA256 is
`3407CF652D01F845351D960F6DC907A2D197BDA0DFE776B6C6F5133ACC80A65B`.
Affected x86 NativeProbe compiled; HandCapture, RigWorkerBinding,
ReloadSoundGate and ReloadAudioCue passed 4/4. Compiler released
08:37:22.6539638 UTC. Unchanged host/helper/fixture provenance is retained in
`reports/playtest-manifest.private.json`; no fresh x64 gate is claimed.
Rollback is `reports/rollback-before-playtest-20261011-083734/Restore-Previous-BC2.ps1`.
Selected `build/playtest-20261011-055537-4f94cac` and six protected hashes remain
unchanged. See the new HANDOFF recovery section and local task-4
`BC2-HANDOFF-20261011-0850.private.md` for exact commands and private paths.

The 08:40 M9 neutral observation failed before native attachment: no identity,
tape or usable native report, and no audio attempt. Matching owned fixture DLL/PDB
symbols resolve a pre-probe fault to `FileName` called by `EntryFilter` in
`tools/testrange/BC2TestRange.cpp`. The retained archive name is a borrowed pointer
without lifetime ownership. Which argument failed and whether that fault caused
final process exit remain unproved. Immutable private crash XML and symbol proof
are preserved; do not infer a M9 mechanism, hand or audio defect. Root driver now
pins the exact process handle and classifies early game exit separately from timeout.
Game/receiver/trace exited and settings/six hashes restored; game lease released
08:40:56.7728739 UTC. No BC2 process, game lease or compiler lease is held.

Hands remain blocked by exact current-frame left palette/draw ownership; retained
main work from N-1 cannot be relabeled current-frame. Early verified diagnostic
lease publication is compiled but runtime-unproved. Independent native gloves,
recoil transfer and event-timed native audio are not accepted. M9 identity JSONL
and local rack-tape authorer are prepared; successful identity/placement capture
must precede an exact-profile cue audition. WASAPI helpers remain runtime-unverified.
Use native IK for diagnosis, audio disabled, physical Gather input blocked and
explicit neutral synthetic input; preserve contacts/profile and BodyInventory off.

Unrelated untracked `.claude/`, `a.txt`, `d.py`, playtest controls ledger and NAM
crash note remain preserved. Deferred radial/loadout prototype remains stash
`f9d4110f96115ebbdd7f53d10800f38fdf506894`, incomplete/unbuilt; do not restore.
Existing subworkers have been asked to checkpoint their own evidence/status.
KF1/KF2 use the same separate game and compiler mutexes; last reported KF1 game
released 08:48:47.4447056 UTC with restoration, no BC2 runtime is reserved.
Next: fix the demonstrated fixture string-lifetime gap in a scoped source commit,
compile the affected fixture, then coordinate one bounded M9 neutral observation.
Do not retry the failed candidate blindly or claim audio silence from zero evidence.

**08:32 UTC current disposition:** cleaned source checkpoint
`checkpoint/combined-20261011-daebe4e` is pushed and remotely verified at
`daebe4e20043bc297f24d3973950d6c6a03cd250`, identical tracked tree to integration
`a1d1d8af6fb1627cd65788d2f248222348997af6`. Audit of 84 changed files found no
new binary paths, personal-host paths or credentials. Only tracked source was
uploaded with noreply metadata; private assets, profiles and captures remain local.
The affected native DLL and RigWorkerBinding/NativeHandCapture checks passed
(2/2), compiler released 08:25:48.2805439 UTC. Matching private package
`build/playtest-20261011-082604-a1d1d8a` and rollback are prepared, unselected.
Existing selected `4f94cac` remains unchanged.

Two subsequent bounded native captures completed with all settings and six
selected hashes restored, final game release 08:28:02.3867179 UTC. Diagnostic
association no longer depends on the camera-write phase, but left worker admission
still failed: 2,484 lease-key mismatches and 112 expired leases, versus 262 accepted
right workers. No pack or hand draw was admitted. Local ordering review found
retained main work from native frame N-1 beside paired-frame right work; do not
relabel that work as current-frame evidence. Root checkpoint `55a0302` publishes
copied, type/owner/frame-verified diagnostic leases before native update and adds
scalar timing counters; it is not yet compiled or runtime-proven. Draw and native
replacement admission remain unchanged. Independent native gloves are not done.

Native audio is a separate pending action pass. The existing worker is adding an
explicit-path, bounded exact admitted M9 identity record to join settled native
hand evidence to recorded synthetic input. Geometry-guided slide motion must use
the accepted presented weapon transform, not guessed controller offsets. Local
WASAPI endpoint selection/recording helpers are prepared but runtime-unverified;
endpoint mixed energy is not cue identity or audible-phase acceptance. No seat or
button cue claim, audio remains disabled in neutral hand captures. Recoil transfer
remains deferred. Resolution measurements from diagnostic 1920x1080 bound render
targets remain separate from normal XR recommendation/allocation acceptance.

**08:05 UTC checkpoint:** cumulative integration `09b6338dd7692911c114d6474e1b402f4141dfdd`
compiled the affected x86 native DLL; focused hand-capture and input-binding
checks passed (2/2). Compiler lease released08:01:59.7343021 UTC. Private package
`build/playtest-20261011-080209-09b6338` is unselected, with exact source/artifact
manifest and byte-preserving rollback. Existing selected4f94cac remains intact.
Cleaned tracked-source checkpoint `checkpoint/combined-20261011-a3728ef` is
remotely verified at `a3728efa90d6c368562668637589e085cc68b932`, identical tree to
integration. Privacy audit found no new binaries, private files, personal machine
paths or credential findings; native assets/profiles/captures remain local.

The second neutral real-engine capture completed and drained without launcher
timeout: observed native report, hooks disabled,9029 world calls/9459 draw calls,
zero capture errors. Public SDK observers attached before level load; native
world/gameplay hooks attached only after exact world admission. Hand capture
retained one paired pack, but still no exact hand draws: admitted/completed eye
mask2, one indexed catalog-count candidate rejected by layout, no instanced
candidates. Recent retention recorded1589 layout and3573 shader evictions, with
zero pinned-full or unclassified-count failures. No empty retry was eligible.
Existing worker investigates the eye-admission asymmetry and layout rejection;
there is no independent glove replacement or functional hands claim.

Actual game34380/receiver/trace exited; protected settings and six selected
hashes restored; game lease released08:04:10.2497837 UTC. Evidence is private
under package `reports/native-trace-20261011-080240-323/native-hands-contract`;
root status is `neutral-hands-20261011-080231/curated-outcome.json`. KF2 has the
next game slot; BC2 source/build work continues independently. Native audio
audition remains disabled during neutral capture, with audible phase and
seat/button event audio still unproved. Recoil transfer remains deferred.

Task-4 owns integration, builds, packaging, deployment and BC2 game admission.
The owner authorizes autonomous bounded real-engine and chair-headset checks while
independent source/build/modeling work continues. Subjective headset feel remains
human acceptance. Use the actual zero-wait `Local\KFVR_GameTest` for games and
separate `Local\KF2VR_NativeBuild` for compiles, maximum two jobs.

Selected `build/playtest-20261011-055537-4f94cac` remains unchanged. New private
packages are cumulative, unselected diagnostic checkpoints with exact source,
artifact manifests and byte-preserving rollback. Latest prepared package is
`build/playtest-20261011-072954-b7e349b`; it is not recording-ready.

Published cleaned source-only checkpoint `checkpoint/combined-20261011-2a24b58`
is an earlier checkpoint; it excludes subsequent capture/audio corrections.
Only tracked source was uploaded, using noreply commit metadata. Private assets,
identity profiles, native captures and reports remain local. A corrected cumulative
source checkpoint follows the current affected gate; no public binary release.

**07:49 UTC evidence update:** integration source `fc29655` includes separate
synthetic-command/local-cache suppression `198ccba` (integration `e16fbdd`),
not yet compiled; prior affected probe `eacb662`, fixture `902c6db`, IPC helper
`6c1eb67` and host `0c589c9` compiled. Latest private package is
`build/playtest-20261011-074027-eacb662`, unselected. New cleaned source checkpoint
`checkpoint/combined-20261011-255bf2d` was pushed and remotely verified; it is an
earlier source-only snapshot, not the subsequent fixture/input repairs.

Real neutral SP02 capture attached public SDK observers before level load, then
installed verified world/gameplay hooks. Native report is observed, hooks disabled,
hand capture drained/written. Operational counts:9839 world calls,440 published
eyes,220 consumed pairs, zero camera-restoration failures,35118 pose calls with
zero pose rejection,131 paired producer publications. Local hand metadata has a
frozen owner and one paired pack, but zero admitted hand draws;64 layouts and96
shaders hit creation caps. The enclosing launcher timed out at82 seconds during
completion, so do not label the entire launch gate passed. Game/receiver/trace all
exited; six selected hashes/settings restored; game mutex released
07:42:05.2609355 UTC. Private evidence remains in the package's
`reports/native-trace-20261011-074039-691/native-hands-contract`.

The existing hands worker is implementing bounded retries only after both eligible
eyes finish with zero candidates, plus bounded recent creation retention. Root
will cover the existing public indexed-instanced draw seam and preserve original
calls exactly once. Geometry/owner/palette/material checks remain exact. KF1 then
KF2 have the next game slots; BC2 has no active runtime or compiler reservation.
No independent hands replacement or subjective acceptance has been achieved.

| Work | Verified current state | Remaining functional work |
| --- | --- | --- |
| Controls / magazine | A reload, RS-UP jump, raw-LT catalog fix and empty-magwell support-pose fix compiled and focused checks passed. | Actual cumulative behavior proof, including native server and both firing clients. |
| Haptics | Seat .5/100 ms with fresh XR host; physical rack both hands, button completion gun hand; focused checks pass. | Actual event/feel verification. |
| Native audio | Exact owner/listener/cancel repair compiled. Single exact-profile audition and generic max64-profile extension compiled; audio/input/capture four focused checks pass. Private exact WeaponData/tree/animation/sound/map guards, one physical-rack attempt/process, no name fallback. | Audible action-phase suitability remains unknown; seat/button audio unproved. Earlier six substring-selected candidates were M95 rifles and were invalidated without playing. Exact M9 pistol uses a shared animation association; broad199-config inventory reduces to only two shared action templates, not weapon-specific sound proof. |
| Independent hands/wrists | Backend and bounded native capture compile and focused tests pass. Blender inspected original147-bone rig and capped866-vertex/1728-triangle hands; three exact sampled pose cases match, closed caps and normalized wrist/finger weights verified. | Material/UV, direct palette upload, exact native draw ownership, bilateral standalone skinning and original color/depth removal. No proxy or arm-collapse substitute accepted. |
| Capture isolation | Diagnostic Gather suppression zeroes11 verified scalar fields/all49 actions and preserves synthetic tracked poses, including stale IPC; focused input tests pass. No XR physical-controller producer in neutral harness. | Frontend/OS input is outside that verified local router. Preserve this explicit limitation. |
| Recoil | Optional observer/ABI checks compiled; transfer remains disabled. | Selected GunSway tick/publication timing and verified composition; no rotational transfer claim. |
| Eye size / quality | Fresh runtime recommendation forwarded; high-compatible native settings preserve rollback. | Actual native/XR eye dimensions and effective filtering/performance next runtime proof. |

The first real menu-attachment attempt at07:30:55 failed closed because the native
world subsystem does not exist at the menu. No level loaded. Game/receiver/trace
exited, six selected hashes and settings restored; game mutex released
07:31:15.7963243 UTC. Earlier preparation/neutral-helper argument failures were
not accepted as captures.

Diagnostic continuation observes only six verified public D3D11 SDK entries at
menu, then waits at most40 seconds for nonzero exact subsystem/world/request
ownership before enabling native world/gameplay hooks. Device/context identity is
revalidated after load. Total capture admission remains60 seconds from entry;
selected draw admission remains15 seconds, one frozen native frame/both eyes,
32draws/16pairs. Current source integrates generic audio as `9e3fe64`; affected
probe/helper gate `6c1eb67` passed4/4 at07:35:10 UTC. Subsequent SDK lifetime/drain
correction is under one final incremental check. Root's neutral capture driver
loads SP02 only after actual SDK readiness, leaves audio audition disabled and
restores all protected files. KF1/KF2 retain their next coordinated game slots.

Deferred by the owner: odd-motion/autopilot investigation remains stopped; radial
prototype stash remains intact. Prior positive town/stereo observations are
scoped evidence. Capped wrists, recoil, event audio and snowy blur/LOD are not
silently declared accepted.

## Rendering wave, started 2026-10-10

The background is in [RENDERING-AUDIT-20261010.md](RENDERING-AUDIT-20261010.md),
with engine addresses in
[re/BC2_RENDER_PIPELINE_RE_20261010.md](re/BC2_RENDER_PIPELINE_RE_20261010.md).
Progress is tracked in
[HANDOFF-2026-10-10-RENDERING.md](HANDOFF-2026-10-10-RENDERING.md).

Design decision (the owner, 2026-10-10): **mirror every native effect into both
eyes** and make post-processing fully correct per eye. Whether any effect is
uncomfortable gets decided later, in the headset. Effects are not suppressed by
default. Each feature gets an env kill switch.

| Lane | Branch | Scope | Audit items | State |
| --- | --- | --- | --- | --- |
| R1 Eye-view parity + per-eye post | `render/eye-parity` | Mirror the main view's effect state into the eye view, give each eye its own PostProcessView (exposure, luminance, blur history), and run eye adaptation once per frame | 1, 2, 5, 6 | merged `0747589` (headset check pending) |
| R2 Context retarget + first-person depth | `render/context-retarget` | Make `ScopedContextEye` heap- or thread-backed, retarget worker contexts with a general rule, give first-person passes the full eye projection with Z, decide on the foreground depth clear | 3, 4, 7, worker contexts | merged `54a44fd` (headset check pending) |
| R3 Host/transport robustness | `render/host-robust` | Survive recoverable XR errors, retain the image in VISIBLE and during tracking blips, remove the wall-clock fault, reclaim the pair instead of `Reset`, run the menu copy async, split `Frame()` | 8, 9, 10, 14 | merged `d8f59b4` (headset check pending; STOPPING/EXITING/lost-session paths untested) |
| R4 Latency + quality | `render/latency-quality` | FOV overscan, request pose predicted for display time, pipeline depth 2, headset-aspect resolution, render-thread late latch at `0xbb4d80`, depth submission (`XR_KHR_composition_layer_depth`) | 11, 12, 13, 15 | merged `9f33bdf`: late latch, prediction, overscan, `-HeadsetAspect`. Pipeline depth 2 and depth submit moved to R5 |
| R5 Depth submit + pipeline depth 2 (R4 follow-ups) | `render/depth-submit` | Per-eye scene depth to a second keyed R32F pair, `XR_KHR_composition_layer_depth` layers when the runtime has it, two request slots and two shared pairs so the game fills N+1 while the host owns N. Frame channel v8 | 11, 13 | built x86/x64 and IPC tests pass, not merged; on by default, kill switches `FVR_XR_DEPTH_SUBMIT=0` and `FVR_XR_PIPELINE_DEPTH=1`; headset check pending (checklist in the handoff log) |

Ready, not started:

- **Shadow cascade per-eye consistency.** Needs a live capture comparing the cascade matrices of the two eyes.

- Fix the floating muzzle flash (audit #16). Effect telemetry comparison is
  needed first.
- Cut copies on the producer side: capture directly into the keyed shared
  texture, and mirror from `eyes_[0]`. Do this after R5 merges, because it
  reworks the bridge (two pairs, depth pair).
- Validate everything in the headset.


## Hands pause checkpoint — 2026-10-11

Shared-pose groundwork and observation-only material capture are checkpointed;
independent replacement remains disabled. See
[BC2 hands checkpoint](re/BC2_HANDS_CHECKPOINT_20261011.md) for exact validation,
private captures, native-rendering RE leads, remaining categories and ownership.
Selected build is unchanged. Resume through the new parent after reorganization.
