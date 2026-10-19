> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Integration 2026-10-11: merges, packaging recipe, cleanup plan

Branch: `integration/20261011`, created from resume `3be9c94`
(`codex/bc2-resume-20261011`). It has not been pushed or selected.
No game was launched and nothing ran on the GPU while this was assembled.

## What is merged

| Merge | Source | Kind | Notes |
| --- | --- | --- | --- |
| `26282d5` | `main` `a62615d` | real merge | KF2 VR standard in AGENTS.md, launch-lock removal, Oct 11 ledger, grenade-crash RE |
| `72b714d` | `fix/hand-authority-support-grip` `aaa36be` | real merge | support grip acquirable on every gun; the primary hand stays controller-owned (controller-anchored wrist) |
| `586aca5` | `fix/haptics-hand-stuck-resume` `29bb11f` | real merge | resume-lineage port of `6eb7ddc` (support-knock gate, StopAll, clamped duration) |
| `b95a729` | `bc2/proper-hands-wrists` `0d0d44d` | ancestry only (`-s ours`) | all 6 commits already on resume, see below |
| `d534778` | `bc2/empty-magwell-visibility` `198ccba` | ancestry only (`-s ours`) | all 4 commits already on resume |
| `fe5b2cb` | `recoil-transfer-20261011` `dd15673` | ancestry only (`-s ours`) | both commits already on resume |
| `8fb773a` | (packaging) | commit | `tools/package_bc2vr.py` runtime scripts, `--body-ammo`, `--extract` |
| `e8f6944` | (docs) | commit | this document, first version |
| `e92bfee` | `fix/recoil-muzzle` `3645fee` | real merge | recoil stays muzzle-up along the drawn bore; effects anchor to any VR-written root (`docs/RECOIL-MUZZLE-20261011.md`) |
| `97e6494` | `perf/stereo-every-frame` `a48d24e` | real merge | stereo every game frame, depth-4 channel, per-pair eyes, 80 Hz, pace-to-content, launcher settings. Brings in `5f57c56` (delivery telemetry) and moves the **IPC wire to v9**, so DLL and host must come from the same build (`docs/HEADSET-FRAME-DELIVERY-20261011.md`, `docs/FRAME_CHANNEL.md`) |
| `4675318` | `fix/death-respawn-reload` `4c84cc5` | real merge | null client-weapon FOV guard; reload authority rebinds after death or checkpoint (`docs/DEATH-RESPAWN-RELOAD-20261011.md`) |

`perf/headset-frame-delivery` (`5f57c56`) was not merged on its own; it
arrives through `perf/stereo-every-frame`. The coordinator merged the last
three fix branches.

### Codex branches: already on the resume lineage

A trial merge of `bc2/proper-hands-wrists` gave 12 conflicts: 10 add/add in the native-hand capture, mesh, renderer and worker files, their tests and the contract tool, plus content conflicts in CMakeLists.txt and NativeProbe.cpp. The
cause is that every Codex commit was cherry-picked onto the resume lineage
earlier. The resume lineage then kept developing those files:
`5ef3c66` shared hand poses, `c7f1922` graphics-thread confinement, `a005db1`
geometry reuse and `aaa36be` controller-anchored wrists. A content merge would
only have reintroduced older versions of those files. Each branch was therefore
recorded with `-s ours`, which keeps the integration tree unchanged (identical to `586aca5`).

Equivalence evidence: `git patch-id --stable` for patch IDs, and `git range-diff` for the commit pairs.

| Codex | Resume | Result |
| --- | --- | --- |
| `5bf0667` native-skinned hand renderer | `be06850` | same diff, CMake context only |
| `19c9999` hand draw contract capture | `bd0f6ce` | same diff, CMake context only |
| `b689f51` Blender hand source inspection | `f99d357` | identical patch id |
| `ff1d889` retry empty hand capture frames | `09b6338` | some hunks landed earlier via `6509324`; `StartEarlySdkObservers`, `SameSdkDevice`, `__uuidof` reflection and `deviceOwner` are all present on HEAD |
| `c5662c8` eye zero starts bilateral capture | `aaca89b` | identical |
| `0d0d44d` rejected rig workers observed | `64f83dd` | same diff, CMake context only |
| `e994556` empty-well magazine geometry | `aa09a9c` | identical |
| `296fa8a` graphics settings contract | `448bac6` | identical |
| `3c84226` suppress physical input during capture | `e2bfb27` | identical |
| `198ccba` block physical input, keep synthetic | `e16fbdd` | identical |
| `23f37a6` sway tick / rig publication order | `7f285ae` | range-diff `=` |
| `dd15673` sway filter + recoil ABI fixture | `733ae80` | range-diff `=` |

The recoil branch is also followed on resume by `2009111`, which transfers native
recoil, and `0524f7c` (KF2 hand kick). `fix/recoil-muzzle` can keep building on
those commits. Nothing in the Codex branches conflicts with the `aaa36be` wrist model.

## Packaging recipe (installable candidate)

A worktree build with an empty CMake cache only proves that the code compiles.
An installable candidate needs the private authored headers and the private
payloads listed below. None of them are tracked in Git. Never commit them.

### 1. Private inputs

All of them live under `<repo>/reports/five-weapons-20261011/`
(below, `<r>`) unless noted otherwise. They were identified from the CMake
cache that produced the installed DLL `efd3c0d0…`. That cache is the Codex
build `%USERPROFILE%/Documents/Codex/2026-10-09/task-4/arsenal-compat-build-20261011-055132/native-x86`.
The payload files were also matched against the installed package by SHA-256.

x86 CMake cache options (`BC2NativeProbe`):

| Option | Value |
| --- | --- |
| `BC2_NATIVE_ACTION_POSE_HEADER` | `<r>/profiles/native-action/NativeActionPoses-racks.private.h` |
| `BC2_AUTHORED_GRIP_HEADER` | `<r>/profiles/extra-smgs/CombinedGrip.local.h` |
| `BC2_EXPERIMENTAL_MAGAZINE_HEADER` | `<r>/profiles/native-action/baseline/CombinedGeometry-native-grasps.private.h` |
| `BC2_MAGAZINE_MECHANISM_HEADER` | `<r>/profiles/extra-smgs/CombinedMechanisms.private.h` |
| `BC2_REVIEWED_MAGAZINE_REGISTRY_HEADER` | `<r>/profiles/extra-smgs/CombinedNative.private.h` |
| `BC2_PISTOL_MECHANISM_HEADER` | `%USERPROFILE%/Documents/Codex/2026-10-09/task-4/recovery-audit-20261011-040735/provenance-and-m9/private-m9-preserved/M9Mechanisms.local.h` |
| `BC2_AUTHORED_SUPPORT_HEADER`, `BC2_DRAW_CATALOG_HEADER`, operation capabilities | empty |
| `CMAKE_BUILD_TYPE` / generator | `RelWithDebInfo` / Ninja, MSVC 14.50 Hostx64/x86 |

x64 host cache: only `BC2_EXPERIMENTAL_MAGAZINE_HEADER`, set to the same
grasps geometry header as x86. All other private options are empty.

Packaged private payloads:

| Package path | Source |
| --- | --- |
| `build/x86/authored-contacts-v1.txt` | `<r>/profiles/native-action/baseline/authored-contacts-native-combined.private.txt` |
| `build/x86/native-skin-props/*` (13 packs + manifest) | `<r>/materials/native-skin-props-verified/` |
| `build/body-ammo-assets.fvrprop` | `<r>/pouch-coverage/body-ammo-geometry.fvrprop` |
| `build/x86/native-skin-hands/*`, `BC2NativeTrace.exe`, `BC2Observe.exe`, OpenXR loader, XM8 bolt assets, the M9 and XM8 range configs | base candidate `<repo>/build/native-hands-ready-a0fa4fe` (its `reports/playtest-manifest.private.json` is hash-checked) |
| `stage/BC2TestRange.dll`, `stage/BC2TestRangeInject.exe` (static CRT) | Codex `arsenal-compat-build-20261011-055132/fixture-x86`, helper source `422960b`. `tools/testrange` is unchanged from `422960b` through `8fb773a`. Rebuild the helper only if that changes. |

### 2. Build (incremental, two jobs, under the shared build mutex)

Run this from the integration checkout. Hold `Local\KF2VR_NativeBuild` while it
runs; `tools/Start-BC2LauncherTask.ps1` uses the same mutex name.

```powershell
# x86 DLL; the cache already holds the private options above (configure once with -D... if fresh)
.\Build.ps1 -Architecture x86 -Jobs 2 -BuildDirectory build\integration\x86
# x64 host; a fresh cache takes the grasps geometry header
.\Build.ps1 -Architecture x64 -Jobs 2 -BuildDirectory build\integration\x64 `
  -MagazineGeometryHeader <repo>/reports/five-weapons-20261011/profiles/native-action/baseline/CombinedGeometry-native-grasps.private.h
```

A fresh x86 cache needs the six `-D` options from the table. `Build.ps1` only
sets the geometry header itself, so pass the others with `cmake -S . -B
build\integration\x86 -D...`, or reuse an existing cache. Confirm the defines
reached the build: `build.ninja` must contain `FVR_BC2_NATIVE_ACTION_POSE_HEADER=`,
`FVR_BC2_AUTHORED_GRIP_HEADER=` and the other options from the table.

### 3. Package (not selected)

```powershell
$r='<repo>\reports\five-weapons-20261011'; $out='<repo>\build\playtest-<short>'
python -B tools/package_bc2vr.py --candidate <repo>\build\native-hands-ready-a0fa4fe `
  --output "$out\BC2-VR-<short>.zip" --extract $out `
  --stage-dir <codex-build>\fixture-x86 --stage-commit 422960ba9706ff7fdc2180eb0ab1b00daa5399c3 `
  --native-dll build\integration\x86\BC2NativeProbe.dll --host-exe build\integration\x64\BC2XrHost.exe `
  --source-commit <full HEAD> `
  --authored-contacts "$r\profiles\native-action\baseline\authored-contacts-native-combined.private.txt" `
  --native-props "$r\materials\native-skin-props-verified" `
  --body-ammo "$r\pouch-coverage\body-ammo-geometry.fvrprop"
```

The packager hash-checks every base-candidate file against its private
receipt, and checks both PE architectures. It requires static-CRT x86 helpers
and rejects any machine-local path in a text payload. It writes
`package-manifest.json` with `"selected": false` and records the source,
host, helper and launcher commits.

With a rebuilt pair, the packager now takes the session scripts
(`Play-BC2VR.ps1`, `Start-BC2VRSession.ps1`, `Stop-BC2VRSession.ps1`,
`Start-NativeTrace.ps1`) and all candidate `tools/` scripts from the same
source. Before this, a new DLL shipped with the base candidate's older
`Start-BC2VRSession.ps1` and `Start-NativeTrace.ps1`.

### 4. Verify without launching

```powershell
. <pkg>\tools\BC2Package.ps1; Read-BC2Package <pkg>   # every payload hash, path containment
```

The launcher runs the same check (`Read-BC2Package` / `Start-BC2Game.ps1` read
`package-manifest.json`) before Play.

### 5. Selection: what makes a package the active candidate

These steps are deliberately not done for the integration package:

- `reports/BC2-Selected.private.json` in the main checkout (package root, manifest, rollback).
- The `package` field in `%LOCALAPPDATA%/BC2VR/playtest-settings.json`.
- The desktop shortcuts, which point at the package's `Launch-BC2VR.vbs`.
- A portable package's launcher selects its own folder when it opens, and the
  GUI's Verify Build saves it. Do not open or verify the new package from its
  own launcher until it should become the selected one.

The installed `playtest-b74560c` manifest also carries `selected: true` and
`render_repair_helper_sha256`. Both were added when it was selected, not by the packager.

## Result: final integration candidate (all fixes)

- **Package:** `<repo>/build/playtest-4675318/BC2-VR`.
- **Archive:** `<repo>/build/playtest-4675318/BC2-VR-4675318.zip`, SHA-256
  `8bfc714a75f0367ffbcfb500999efdc84fedb41f8afe9427655fab014d383e28`, 66 files.
- **Source:** native, host and launcher all from
  `4675318ac7488c9e940a83de755e9f04c04ad8ab` (integration HEAD, which has every
  fix merged). The helper comes from `422960b`; `tools/testrange` has not
  changed since then.
- **Binaries:**

  | File | SHA-256 |
  | --- | --- |
  | `BC2NativeProbe.dll` | `96fbaa8049c3f8a72c003bbd56242f8eafd1b8c4ec6c2c7825d371045d639b02` |
  | `BC2XrHost.exe` | `6361cae5e23df5dc7847456e967c9c8e577b8667dd526280022bff6f9041f396` |

  Both come from the same build, which IPC wire v9 requires. For comparison,
  the installed DLL is `efd3c0d0df6e4a214716eb339f8734c0cca07b24bed13ff8fc0dd99ca3135df4`
  and the installed host is `1bd32e012ac24d72…`.
- **Build:** incremental, two jobs, under the build mutex
  (`build/integration/x86`, `build/integration/x64`). The private defines are
  confirmed in both `build.ninja` files. The focused tests AsyncFrameChannel,
  StagedFrameProducer and Bc2NativeRecoilTransfer all passed (3/3).
- **Verification:** `Read-BC2Package` verified all 65 payloads.
- **Difference from the installed `playtest-b74560c`:** the file set is the
  same. Only the DLL, the host and the stereo branch's launcher and session
  scripts differ: `DevLauncher.ps1`, `tools/BC2PlaytestPanel.ps1`,
  `tools/Start-BC2Playtest.ps1`, `Play-BC2VR.ps1` and `Start-BC2VRSession.ps1`.
  Each of these is byte-identical to HEAD. Every private payload and asset is
  unchanged.
- **Status:** not selected and not installed. No headset test or game launch
  has happened.
- **Superseded:** `build/playtest-8fb773a` (an interim build without the last
  three fixes; DLL `384f0e26…`) is replaced by this package and can be deleted.
## Consolidated headset test plan (final integration package)

None of the steps below has been run yet. They collect the per-fix plans from
`HAND-AUTHORITY-SUPPORT-GRIP-20261011.md`, `HAPTICS-HAND-STUCK-20261011.md`,
`RECOIL-MUZZLE-20261011.md`, `HEADSET-FRAME-DELIVERY-20261011.md` and
`DEATH-RESPAWN-RELOAD-20261011.md`. Those docs keep the full trace-field detail.

### 0. Before launch: machine state and labelling (global rule)

- Run `nvidia-smi` and find out **what** holds VRAM: ComfyUI, songd's worker,
  the ACE service, a browser or a game. Note free system RAM, and check the
  queue for another project's job. Starting on a card at 11.5 of 12 GB measures
  the spill to system RAM, not the build. Free the card, or record what was on it.
- **Label every timing and every FPS number cold or warm.** The first 60 to 90 s
  after the level loads are cold. If one arm of an A/B was cold and the other
  warm, the comparison measures load state, not the change. Re-run it, or say
  plainly that the arms are not comparable. A warm host with a cold game (or the
  reverse) is a mixed state; describe it as one.
- Select the package through the launcher. The hash check must show the new
  DLL (see "Result"), not `efd3c0d0…`. The DLL and host must come from the same
  package, because the IPC wire is v9.

### 1. Stereo, 72 Hz and pace-to-content

1. Launch from the Playtest tab with the defaults (refresh 80, "Pace headset to
   game frames" checked). In `host.json` and the `xr-events.jsonl`
   `display_refresh` line, confirm `current_hz` = 80. If the line shows
   `extension:false`, set 72 Hz in the runtime's own UI and note it.
2. After warm-up, play 60 s standing and looking around, then 60 s strafing and
   firing. Read the per-second `"delivery"` lines (`xr_fps`, `fresh_per_s`,
   `reused_per_s`, `game_frames_per_pair`, age p95 and fresh age p95,
   `skipped_refreshes`). Then read the DLL `frame_delivery` deltas
   (`bridge_no_lease` = mono frames, `eye_frame_mismatch`, `gate_drops`,
   `pairs_busy`, `export_unqueued_refusals`).
3. Expected: `bridge_no_lease` grows by about 0 per second,
   `game_frames_per_pair` is about 1.0, `eye_frame_mismatch` = 0, and
   `fresh_per_s` is about the game's own frame rate.
4. **A/B pacing, both arms warm with the card in the same state.** A is pacing
   on; B is pacing off (untick the box, or `FVR_XR_PACE_TO_CONTENT=0`). For
   each arm, record `xr_fps`, `fresh_per_s`, `reused_per_s`, age p95, mono
   frames per minute, `eye_frame_mismatch`, the `frame_timing` pace
   waits/timeouts, and the subjective judder when strafing. Expected for A:
   much lower `reused_per_s` and a smoother strafe, at the cost of a slightly
   higher age p95.
5. Optional single-variable A/Bs, all warm: `FVR_XR_PIPELINE_DEPTH` 2 vs 4,
   `FVR_BC2_PER_PAIR_EYES=0`, refresh 72 vs 80 vs 90, `FVR_XR_OVERSCAN=0`.

### 2. Hand authority and support grip

1. XM8 with the magazine seated and no rack owed: put the left hand on the
   handguard and squeeze once. It should grab immediately. Release and re-grab
   5 times quickly.
2. Repeat on M416, MG36, VSS and one gun with no profile (AKS-74U or F2000).
3. Reach while already squeezing, then squeeze before reaching. Both must grab.
4. Hold support, fire a burst, and flick the weapon stick without switching.
   The hold persists.
5. Note the right controller angle and where the barrel points. Switch to
   another gun and back three times. The hand must not rotate relative to the
   controller, and the returning gun must sit exactly as before with no settle
   delay. Do a cutscene round trip as well.
6. Trace: `gameplay.two_hand_support.grabs` above 0,
   `hand_ownership.claim_rejections` near 0, and gun-events `claim` rows with
   `"kind":"weapon_support"`.

### 3. Haptics

1. Right-hand rifle, left hand off the gun, fire bursts: only the right
   controller pulses. **Record whether one-handed fire still buzzes the left
   controller.**
2. With the support grip held, fire: the left controller gets the light knock
   only while the grip is held.
3. Hold full auto and switch weapons mid-burst: the buzz stops immediately on
   both controllers.
4. Die, or open pause, while firing: no vibration remains.

### 4. Recoil and muzzle

1. XM8 hip fire, single shots with the gun level: the barrel kicks **up** and
   back, then returns to the hand. It never dips below. Repeat on full auto.
2. Repeat on M416, MG36, VSS and one gun with no profile, including with the
   gun rolled about 45° in the hand. Recoil follows the gun's own up direction.
3. Hold the support grip and fire: the left hand stays on the handguard.
4. Sweep and roll while firing: the flash stays at the barrel tip, and shells
   leave the ejection port on every gun.
5. Trace: `native_recoil_transfer.muzzle_down_rectified` is above 0 on auto
   fire. `model_bore_mismatch` is 0 on profiled guns; if not, record which gun.
   Under `weapon_effects`, `fov_compensation_bypassed` is about 2 × shots.

### 5. Death, checkpoint, reload, switch, fire (XM8)

1. Fire part of an XM8 magazine and note loaded/reserve in
   `physical_weapon_authority`. Pull the magazine, then die with it out or
   mid-reload.
2. Let the checkpoint reload. There must be no crash. If the null-slot case
   recurs, `null_weapon_guard_hits` is above 0.
3. After spawn: `actor_loss_rebinds` or `loss_retirements` is at least 1, there
   is a new `actor_generation`, `suspended=false`, loaded/reserve equal the
   checkpoint's native counts, and `unexpected_ammo` has not increased.
4. Reload the XM8 completely: the phase reaches `completed`, and the bolt and
   rack behave normally.
5. Switch weapons at least twice, once with the stick cycle and once with the
   shoulder holster. Each gun fires and ejects, `shots` rises, and
   `can_fire=true`.
6. Repeat death and checkpoint with no reload in flight. Ammunition equals the
   native restored values, with no duplicate magazine and no lost reserve.
7. Ride a vehicle for more than 1 s and leave. The rebind and retirement
   counters do **not** increase, and an owed rack survives.

### Kill switches (set to isolate a regression; defaults are the new behaviour)

| Area | Variable | Default | Setting it restores |
| --- | --- | --- | --- |
| Hands | `FVR_BC2_SUPPORT_CURRENT_EVIDENCE=0` | on | the support claim waits for the renderer packet again |
| Hands | `FVR_BC2_SUPPORT_ALL_WEAPONS=0` | on | the support grip only on profile-verified or authored guns |
| Hands | `FVR_BC2_WEAPON_FIT_CACHE=0` | on | the native weapon fit is re-sampled on every equip |
| Hands | `FVR_BC2_LEGACY_HAND_FROM_WEAPON=1` | off | the profile aim frame may rotate the primary wrist |
| Haptics | `FVR_BC2_GUN_HAPTICS=0` | on | no gun fire, support-knock, weighted or braced haptics |
| Recoil | `FVR_BC2_RECOIL_MUZZLE_UP_ONLY=0` | on | the raw native recoil, including the downward overshoot |
| Recoil | `FVR_BC2_RECOIL_MUZZLE_BORE=0` | on | the recoil basis from model axes or XR aim only (`b74560c`) |
| Recoil | `FVR_BC2_NATIVE_RECOIL_TRANSFER=0` | on | no native recoil on the held gun |
| Effects | `FVR_BC2_EFFECT_WRITTEN_ROOT=0` | on | the FOV bypass and re-anchor need a current shot frame |
| Effects | `FVR_BC2_EFFECT_ANCHOR=0` | on | the native fire-and-forget effects, with no bypass |
| Death | `FVR_BC2_NULL_WEAPON_GUARD=0` | on | the FOV getters unhooked (the native crash path) |
| Death | `FVR_BC2_ACTOR_LOSS_REBIND=0` | on | pointer-only actor generation and server retirement |
| Death | `FVR_BC2_PHYSICAL_WEAPON_AUTHORITY=0` | on | the whole reload ledger disabled |
| Render | `FVR_XR_PACE_TO_CONTENT=0` | on (launcher checkbox) | the retained pair re-submitted every refresh |
| Render | `FVR_XR_PACE_MAX_WAIT_MS` | one display period | 1 to 50 ms pace wait limit |
| Render | `FVR_XR_REFRESH_HZ` | 72 (launcher field) | `0` keeps the runtime's rate; other values pick the nearest supported rate |
| Render | `FVR_XR_PIPELINE_DEPTH` | 4 | `2` the previous default, `1` a single exchange; read by both processes |
| Render | `FVR_BC2_PER_PAIR_EYES=0` | on | one shared private eye set (H2b guard) |
| Render | `FVR_BC2_EXPORT_QUEUE_AT_PUBLISH=0` | on | the legacy copy queued at the next pump |
| Render | `FVR_BC2_BRIDGE_LOCK_SPIN_US` | 500 | `0` drops a rendered frame on a busy gate |
| Render | `FVR_XR_OVERSCAN` | 0.08 | `0` no tangent overscan |
| Telemetry | `FVR_XR_DELIVERY_LOG=0` | on | no per-second `delivery` lines |

## Branch and worktree cleanup plan

Do this after the orchestrator has merged the remaining fix branches and
`main` has been fast-forwarded or merged to the integration result. Never
delete anything that is dirty or still owned by a running agent.

### Fully merged into integration, safe to delete once main has the result

- `codex/bc2-resume-20261011` (`3be9c94`), `fix/hand-authority-support-grip`,
  `fix/haptics-hand-stuck-resume`, `fix/haptics-hand-stuck` (`6eb7ddc`, main lineage).
- `bc2/proper-hands-wrists`, `bc2/empty-magwell-visibility`, `recoil-transfer-20261011`.
- `render/depth-submit`, `render/eye-parity`, `render/latency-quality`,
  `render/host-robust`, `render/context-retarget`. None has commits beyond
  resume; all are ancestors of integration.
- Older merged branches: `codex/physical-weapon-authority`,
  `polish-zones-hands`, `reload-b-after-insert`, `stick-cycle-equipment`.
- `worktree-agent-{a1d836f3f4c2ce975,a61dfa07ee45c8d9e,aa2595afbbf421546,ae09a625db2917dcd}` (merged).

### Placeholder branches, delete without merging

Fifteen `worktree-agent-*` branches sit at `761b49a`, which is upstream
`origin/main` (justiceofrip, "Checkpoint 245"). The agent harness created them
as worktree bases. Their 6 commits are upstream history, not our work. Do not
merge them into integration. Deleting the branch refs loses nothing because
`origin/main` still has the commits.

### `.claude/worktrees` (remove with `git worktree remove` once the branch is merged and the agent is idle)

| Worktree | Branch | State | Action |
| --- | --- | --- | --- |
| agent-a32c449412ca5ed94 | render/depth-submit | clean, merged | remove |
| agent-a3734f07b88f2759b | render/eye-parity | clean, merged | remove |
| agent-a61b500e91ea827f3 | render/latency-quality | clean, merged | remove |
| agent-a8b0f0abd05326a52 | render/host-robust | clean, merged | remove |
| agent-ac55e316b1144cd94 | render/context-retarget | clean, merged | remove |
| agent-a76a1d2db870a1982 | fix/haptics-hand-stuck-resume | clean, merged | remove |
| agent-ad131993f90ed36f4 | fix/hand-authority-support-grip | clean, merged, **locked** | unlock and remove after the owner confirms |
| agent-af61c9ce97fe1eff0 | perf/headset-frame-delivery | clean, not merged | keep until stereo lands |
| agent-ac931b6ec00adc444 | perf/stereo-every-frame | 17 modified | keep (in progress) |
| agent-a432debfa70d1bec5 | fix/death-respawn-reload | 1 modified | keep (in progress) |
| agent-af919f758453e4c5e | fix/recoil-muzzle | 5 modified | keep (in progress) |
| agent-a9922e67e9d1a7952 | integration/20261011 | this branch | keep until main has it; `build/integration` holds the configured private caches |

### Codex worktrees under `%USERPROFILE%/Documents/Codex/`

- `task-4/proper-hands-source`, `empty-magwell-source` and
  `recoil-transfer-source` are clean and have been idle since 2026-10-11
  01:30, and their branches are now merged. They can be removed after the
  user confirms that Codex is not using them.
- Keep `task-4/arsenal-compat-source-40dc698` (resume, clean). Its build
  folder `arsenal-compat-build-20261011-055132` holds the reference private
  caches, the `fixture-x86` helper binaries and the source ZIPs.
- Leave `2026-10-11/task-6/audio-isolated-source-private` (detached `5f8f1e6`) alone.

### Main checkout

Untracked `a.txt`, `d.py`, `.claude/` and
`docs/HANDOFF-2026-10-11-ORCHESTRATION.md` belong to their owners; do not stage them.
