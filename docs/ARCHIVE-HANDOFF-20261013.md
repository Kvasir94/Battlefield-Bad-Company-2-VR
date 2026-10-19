> **Historical archive.** Preserved on October 13, 2026. Implementation claims,
> priorities, agent assignments and workflow restrictions below belong to their
> recorded sessions. Use [current status](STATUS.md) and [development rules](../AGENTS.md)
> for new work. Technical evidence retains its original scope.

# Fork handoff

## Reload guidance, audio and recoil continuation

See [current implementation and remaining work](HANDOFF-RELOAD-GUIDANCE-20261011.md).
Pouch/handle guidance, audio-origin fixes and improved recoil/audio diagnostics
compile; four focused checks pass. No changes to the user's active playtest.

## Portable launcher checkpoint — October 11

See [portable launcher handoff](HANDOFF-PORTABLE-LAUNCHER-20261011.md) for packaging progress.

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

## Project resume — October 11, 2026

Compiled source `99610cd`: x86 DLL and seven focused controls/audio tests pass.
Combined `build/playtest-20261011-191403-99610cd` is prepared, unselected;
24/24 manifest hashes and VerifyOnly pass. Separate isolated audio diagnostic
`191013-5f8f1e6` has frozen v3 helpers ready for a separately authorized audition.
No interactive test or selection occurred. The user approved the existing
native skinned-instance route. [Static seam research](re/BC2_NATIVE_SKIN_SEAM_20261011.md)
now proves the soldier-to-draw-to-queued-geometry chain and identifies a private
LOD substitution field. Live ownership, resource lifetime and complete pass
coverage remain open; hands replacement is not implemented or enabled.

Use [current candidate status](BC2_CANDIDATE_STATUS_20261011.md) and the top of
[open work](OPEN_WORK.md) for the resumed integration branch, evidence matrix,
source changes and short player sequence. Hands implementation awaits the
remaining seam proof, not another architecture decision.
Selected package remains `playtest-20261011-055537-4f94cac`; neither a source
checkpoint nor an unselected diagnostic package changes that selection.

## Recovery checkpoint, October 11 2026 08:50 UTC (historical, carried from main)

**08:58 continuation:** integration42a9db0 scoped fixture lifetime repair compiled
and matching candidate `build/playtest-20261011-085504-42a9db0` is prepared,
unselected, rollback `reports/rollback-before-playtest-20261011-085504`.
Fixture SHA256 `6B8C315FD240F34861C8EA628BAF49FEAC60903925D4C146C55643567DF88474`;
native unchanged. New cleaned tag `checkpoint/combined-20261011-bde6dca` is
remotely verified at `bde6dcad32e875ef5eb2114dec0ef74b37125bcb`, identical
tracked tree. Native neutral M9 observation completed (observed/hooks disabled,
identity1, recorded input, no audio attempt); actual game release08:57:12.7002408
UTC with settings/six selected hashes restored. Private status is task-4
`m9-frame-observe-20261011-085548/status.json`. Identity/placement join pending;
no hand draw, audible cue phase or universal crash-fix acceptance is claimed.
This supersedes the latest candidate/source fields below; prior fault evidence
remains immutable. Exact evolving local handoff is updated in the same task-4 file.

The authoritative cumulative integration is detached
`491408d543e4d55f89b6c4f3d41ffdabe903485a`, tracked-clean, in the task-4 isolated
integration checkout. Main before this scoped documentation commit is `344c597`;
do not build main as though it contains every integration correction.
Cleaned tracked-source tag `checkpoint/combined-20261011-f242a18` is verified on
origin-bc2 at `f242a185d8746fdc23046c9e9d10a012b20ecf5c`, identical source tree.
No private game assets, profiles, recordings or native evidence were uploaded.

Prepared candidate: `build/playtest-20261011-083734-491408d`, unselected.
Manifest: its `reports/playtest-manifest.private.json`. Rollback:
`reports/rollback-before-playtest-20261011-083734/Restore-Previous-BC2.ps1`.
Selected build remains `build/playtest-20261011-055537-4f94cac`, unchanged.
Native DLL SHA256:
`3407CF652D01F845351D960F6DC907A2D197BDA0DFE776B6C6F5133ACC80A65B`.
Affected x86 compile and 4/4 HandCapture/RigWorkerBinding/ReloadSoundGate/
ReloadAudioCue checks passed; reused host/helper/fixture sources are explicit in
the manifest. There is no new headset acceptance or recording-ready claim.

Included work: M9 physical reload/contact source; A reload and right-stick UP
jump; raw-input LT Arsenal correction; renderer/depth pairing; seat/completion
haptics; owned-listener audio cancellation; opt-in exact identity/cue/capture
diagnostics and local physical-input suppression with separate synthetic replay.
Deferred: independent native glove draw, actual recoil transfer, audible rack
phase, seat/button audio mapping, normal XR allocation/quality acceptance.

Latest M9 observation failed before native probe attachment, yielding zero identity,
tape and usable report. Owned exact PDB resolves pre-probe fixture fault frames
to FileName/EntryFilter archive basename comparison. Retained borrowed archive
storage has an ownership gap; invalid argument and final exit causality remain
unproved. All evidence is local. Last BC2 game lease released
08:40:56.7728739 UTC after process exit/settings and six selected hashes restoration.
No root compiler/game is active. Driver-only early-exit classification is corrected.

Parent requested recovery at 8% usage. Exact local paths, source ownership,
commands, mutable/uncommitted inventory and evidence are preserved in task-4
`BC2-HANDOFF-20261011-0850.private.md`; OPEN_WORK has the same current disposition.
First next task is the scoped fixture string-lifetime repair and affected compile.
Only after successful bounded M9 identity/placement capture may the prepared local
authorer generate a rack tape for an explicit native audio audition. Keep audio
off in neutral hand captures. No speculative cue seek/splitting or proxy hands.


## Test build 8614b4e (October 9 2026)

The selected launcher binaries in `build/x86` and `build/x64` are now `main` at
`8614b4e`. They were built with MSVC, Ninja and RelWithDebInfo, using the same
configuration as `Build.ps1`. They passed 37 focused CPU, fake-runtime and GPU-bridge
checks offline. They have **not** been run in game or in the headset yet. The receipt,
with every deployed SHA256, the private asset hashes, the tests and the rollback, is the
ignored `reports/HEADSET-CANDIDATE-8614b4e.json`. The previous `c2083ba` files are
preserved in `reports/headset-candidate-rollback-c2083ba-20261009-054019/`.

| File | SHA256 |
| --- | --- |
| `build/x64/BC2XrHost.exe` | `79EB4F54DD93D0D9F153BE7E7ED00EFDAA554F09031B5F8C034E2A7FC1E69EAC` |
| `build/x64/BC2Observe.exe` | `822571993AEF2CD4C55CEDAC80D3A34100CD4225B4C776D36D80B9C2EB71B67A` |
| `build/x86/BC2NativeProbe.dll` | `2C0AFC048742F0BE87D6EC94E52B542010D4C68A007CC540283D65986F9FA2EB` |
| `build/x86/BC2NativeTrace.exe` | `C958FEA0EF2D35392CC67646B4583F6C039E79112DB1F4FD9F936A4A3B663CEA` |

Contents:
- The cfc62a5 launcher visual-admission fix and its telemetry.
- The copy-timeout retain fix and bounded late stereo capture.
- Stick-cycle weapon switching.
- The weapon-effect anchor and launcher telemetry.
- Interaction zones, magnetism and hover cues.
- Authored hand contacts and the floating-hands mode (the default stays `ik`).
- Dropped-magazine physics.
- The XM8 VR bolt with the left-hand charging-handle cycle.
- Short B after an insertion completes the reload.
- The Bc2ContextInteract fixture fix.

Start it with the private, ignored wrapper. It hash-checks every binary and private asset,
writes a launch manifest and restores the shell environment afterwards:

```powershell
.\reports\Start-Headset-Candidate-next.ps1 -Runtime VDXR      # or -Runtime SteamVR
```

The runtime selection is explicit and process-local; the wrapper never touches the
registry. The render size is 1920x1080. Every switch is set explicitly, and a wrapper
parameter is the kill switch:

- **Locomotion and frames:**
  - `FVR_TURN_MODE=smooth`, `FVR_SMOOTH_TURN_DEG_PER_SEC=90`.
  - `FVR_XR_ASYNC_CONSUMER_COPY=1`, `FVR_BC2_VIEWPORT_SYNC=1`, `FVR_XR_RETAIN_ON_COPY_TIMEOUT=1`.
- **Stereo capture** (wrapper default ByRequest; the code default is off):
  - `FVR_BC2_STEREO_CAPTURE=2`, `_AUTO=1`, `_MAX_ARMS=6`.
  - To arm it, create `stereo-capture.request` in the session's `reports\native-trace-<utc>\` folder.
- **Weapon switching and effects:**
  - `FVR_BC2_STICK_CYCLE=1`, `_MELEE_GRENADE=1`, `_BACK_MS=450`.
  - `FVR_BC2_EFFECT_ANCHOR=1`, `_MS=400`.
- **Magazines and interaction:**
  - `FVR_BC2_DROPPED_MAGS=1`, `_LIFETIME_S=30`, `_MAX=4`, `_VISIBLE_M=8`.
  - `FVR_INTERACT_ZONES=1`, `FVR_INTERACT_ZONE_SCALE=1`, `FVR_INTERACT_MAGNET=1`, `FVR_INTERACT_HOVER_CUES=1`.
  - `FVR_BC2_SUPPORT_KEEP_ON_FLICK=1`.
- **Hands, contacts and bolt:**
  - `FVR_HANDS_MODE=ik` (`-HandsMode floating` switches it).
  - `FVR_AUTHORED_CONTACTS` holds the absolute path of the private `build/x86/authored-contacts-v1.txt`. An absolute path is needed because the DLL loads from the native-trace folder, not `build/x86`.
  - `FVR_XM8_BOLT=1` and `FVR_XM8_BOLT_ASSET` holds the absolute path of the private `reports/xm8-bolt-intake/xm8-bolt-v1-render-input.json`.
- **Reload:** `FVR_BC2_SHORT_B_RACK_AFTER_INSERT=1`.

Headset checklist. Record observations separately from hypotheses, and use real controllers only:

1. Record which runtime was used. Compare the async-copy timeout count with the c2083ba
   counts: VDXR 0, SteamVR 130 of 15,371.
2. Confirm the effective turn mode and smooth turning from the `controls` telemetry.
3. XM8 reloads:
   - A 600 ms B hold triggers the native reload, and the weapon refires afterwards.
   - A short B after an insertion completes the reload rather than ejecting.
   - Dropped magazines fall, rest and expire.
4. XM8 bolt: check its visibility and depth in both eyes, and that the left-hand
   charging-handle cycle works.
5. Stick-cycle switching, including the knife and grenade slots. Check that the support grip
   is kept on a flick.
6. Grenade launcher: check its cant, and the muzzle flash and projectile direction with the
   effect anchor on. If needed, compare a run with `-NoEffectAnchor`.
7. Check interaction zones, magnetism and hover cues by feel, along with authored-contact
   hand poses.
8. After an explosion, if ghosting appears only in the left eye, create
   `stereo-capture.request` once, and note whether the capture costs frame rate.
9. Optional second run with `-HandsMode floating`. Its GPU draw is unverified.

Still unverified: the bolt's depth and visibility, floating mode, the effect anchor, the
knife and grenade cycle slots, and the cost of stereo capture.

## Latest checkpoint October 8 2026 late evening

Read [BC2 VR development handoff](HANDOFF-2026-10-08-LATE.md) first. Main is at
implementation commit `cfc62a5`; its offline-tested DLL is not deployed. Selected
binaries remain `c2083ba`. Both recent sessions ended, and the newest SteamVR
report differs materially from the earlier Virtual Desktop run. The sections
below are historical and do not supersede that checkpoint.

## Documentation lane handoff — October 9, 2026, source 1764186


The [archived VR quality review and staged acceptance plan](VR-QUALITY-REVIEW.md)
preserves the complete source comparison and its pinned-revision limitations.
Use its Stage 1 XM8 continuity and measured-headset gates to guide the roadmap;
Stages 2–3 are planned polish and broader ambitions, not implemented acceptance.

The current source contains persistent physical weapon authority, independently
published player inventory and server selection retry/readback. Physical
magazine/tube consumers now use this authority when enabled; the earlier
native state-11 hold/advance records below are historical, not the sole current
architecture. See [STATUS.md](STATUS.md),
[server command consumption](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp) and
[selection hook](../src/games/bc2/Bc2Gameplay.cpp).

The controls integrator maintains the [current controller card](CONTROLS-20260930.md#current-controller-card--october-9-2026).
B is a reload semantic whose supported physical-magazine consumer can eject or
complete a required rack; do not advertise universal automatic reload. Launch
composition matters. Current scripts also request and verify native render size;
[BUILDING.md](../BUILDING.md) records the portable parameters and remaining
machine-local configuration requirements.


**Later source changes checked October 9:** main `97a5b71` adds
controller sampling/focus and selected-profile diagnostics after the gameplay
snapshot above. These changes do not establish real-controller reload acceptance.
The separate composed headset mirror is opt-in via `Play-BC2VR.ps1 -Mirror`;
the game's steady eye mirror remains enabled unless `-NoSteadyWindow` is used.
No performance saving is inferred from that default. See
[launcher](../Play-BC2VR.ps1) and
profile diagnostic change.

Main `65957dc` additionally classifies rejected stereo pairs and
preserves transfer errors. This is diagnostic coverage, not a rendering or
physical-reload acceptance result.
Exact change.

Runtime/build/selection and controller/crash investigation belong to the active
native owner. Documentation and read-only design work must not start competing
runs or infer headset acceptance/crash closure from scripted or chair diagnostics.
Confirm live reservations before editing these paths; old “no agents/sessions”
statements are not coordination authority.


## Historical October 8, 2026: proto/reload headless harness and reload features

PR #9 merged `proto/reload`
into `main` at `2b77bc1`; the October 8 main `e1f86e9` included it. The ledger
records scoped headless/scripted checks and gaps, not universal validation.
The new reload, sounds, haptics, dropped-magazine and mirror composition still
needs headset acceptance. Details, dated evidence and gaps are in
[2142-TRANSFER.md](2142-TRANSFER.md#october-8-headless-verification-harness-pc-builds-protoreload).

- **Play:** `Play-BC2VR.ps1` fast-starts SP_02 through the private test-range DLL
  (branch test/range-merged), then starts the VR session with the mirror window.
- **Verify without a headset:**
  - Mode 7 (XM8: fire, dry fire, reload) and mode 8 (eject) through
    `Start-NativeTrace`.
  - The SPAS tapes in tools/replay/tapes.
  - BC2NativeIpcProbe `--body-props-cache` for headset-equivalent images, checked
    with tools/replay/prop_flicker.py.
- **Open:**
  - The eject start pose (the magazine starts at the left hand).
  - Broader second-weapon native/headset acceptance. F2000 is already included:
    [native profile](../src/games/bc2/Bc2MagazineNativeProfile.h) and
    [geometry](../profiles/checkpoint204/Bc2ExperimentalMagazineGeometry.h).
    Its original candidate/data-only evidence in the ledger remains historical.
  - Dropped-prop ground height (30 cm too high in SP_02); the in-game
    draw's depth occlusion itself is proven (see the ledger).
  - Every headset check.

## Independent capability audit — October 7, 2026 (America/Chicago)

[Capability audit 203](CAPABILITY-AUDIT-203.md) records the 35 path-bound visibility
configurations, 11 pathless rows and two builtin magazine registrations. All four
reporting gaps are corrected: the probe exercises synthetic common-operation
bindings with negative controls; exact-package stow requires verified configuration
identity; component rejection/role evidence survives reporting; and cache summaries
include operation-proof inputs. Optional probe hashes remain explicitly unattested.
Gameplay and the native harness are unchanged; the probe now has a CTest entry.

All 123 focused weapon-pipeline Python tests pass; all five new regression methods
reject the pre-fix report. Full Python discovery passes 804 tests with one intentional
installed-game skip. The unchanged checkpoint203 operation-source receipt verifies.
Both architecture builds now pass all 186 CTest suites on D: (x86 default, x64
checkpoint configuration). The corrected probes report 0 versus 35 common-operation
rows, both retain two builtin rows, and checkpoint negative controls pass. The
audit records an initial probe-control failure and its successful correction, plus
the earlier disk-failure history. No new game/headset acceptance is claimed.
Private resource JSON needed to reproduce complete package/component closure is
absent from this checkout.

## Historical runtime checkpoint 203

The earlier development added [checkpoint 203](RELOAD-TRANSITIONS-203.md): a reproduced
cross-profile magazine-retirement deadlock is corrected in the shared consumer.
All 185 public C++ suites pass on each architecture, and the rebuilt module passes
the existing strict native original-return/replacement sequence. Cross-profile
recovery itself is CPU-tested; a combined native transition sequence remains next.
`Build-Checkpoint.ps1` selects the 203 operation receipt and 202 calibrations.

This public source snapshot starts at checkpoint 202. Read [STATUS.md](STATUS.md)
for current acceptance and [../BUILDING.md](../BUILDING.md) for ordinary and
checkpoint builds. The latest human headset feedback predates the automated fixes.

The shared magazine startup pulse now has immutable original input timing and
requires genuinely fresh native contexts before Holding. A bounded diagnostic
completion journal retains exact End data when the record gate is contended.
The actual original-magazine return then replacement sequence passed its strict
native audit. See [SIMULATED-PLAYER-202.md](SIMULATED-PLAYER-202.md).

Do not revive old absolute launch paths or treat historical research notes as
current release instructions. Raw traces, media, game content and local config
are intentionally excluded. The small checkpoint calibration headers and exact
source-bound operation receipt are in `profiles/checkpoint202`.

Branch `feel/chamber-and-insert` adds portable KF2/AS2 feel policies: chamber
state, a magwell-slider insertion feel and a motion guard. Branch
`feat/feel-wiring` wires them into the BC2 magazine adapter: XM8 (and the
generated AEK row) on the slider with the hand latch, VR chamber metadata,
the seat haptic and gun-event rows. The initial wiring record described a default-off charging-handle rack.
It remains default-on for configured magazine profiles on the current source;
`FVR_BC2_CHAMBER_RACK=0` opts out
([source](../src/games/bc2/Bc2Gameplay.cpp)). Status, offline checks and the PC/headset steps still owed are in
[BC2_PHYSICAL_RELOAD.md](BC2_PHYSICAL_RELOAD.md#adapter-wiring-featfeel-wiring).

SPAS physical pumping is wired into the physical-reload path, now selecting the
physical tube authority when enabled
([consumer](../src/games/bc2/Bc2Gameplay.cpp)).
Next work is broader native reload/inventory transition coverage, remaining
mechanism integration and exact-package headset acceptance. See [STATUS.md](STATUS.md)
for current scope. Coordinate with active checkout/runtime owners before editing;
this handoff does not establish that no local sessions or agents exist.


## Hands pause checkpoint — 2026-10-11

Shared-pose groundwork and observation-only material capture are checkpointed;
independent replacement remains disabled. See
[BC2 hands checkpoint](re/BC2_HANDS_CHECKPOINT_20261011.md) for exact validation,
private captures, native-rendering RE leads, remaining categories and ownership.
Selected build is unchanged. Resume through the new parent after reorganization.
