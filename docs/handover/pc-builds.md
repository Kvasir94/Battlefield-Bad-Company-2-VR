# PC builds / test-loop handoff — 2026-10-08 (America/Chicago)

The thread that owned builds, the game slot, the receiver/harness, probe fixtures,
the test range and renderer/occlusion work on the dev PC. Written for a fresh
session with no context.

## Standing orders in force (the owner, 2026-10-08 ~22:08Z)

- Stop all threads. Regroup on all open work.
- **No more playtesting and no emulated testing** (no scripted or synthetic-hand
  runs, no replay tapes) **until all work is merged to `main` and all other
  branches are deleted.**
- Repo rules are being rewritten (coordinator note, KF2-vs-BC2 thread). The goal is
  native feel with modern VR controls; the mod may own game state. The safety rules
  are signature checks and writing only to the player's own objects. Features go on
  by default once they work, with an env kill switch. Headset testing is not
  mandatory yet.
- Push to `origin-bc2` (the private development repository); `origin`
  (`justiceofrip/FrostbiteVR`) is upstream and must not be pushed to. Merge, never
  rebase, on `proto/reload`. The OpenXR loader in `runtime/` is gitignored and must
  never be committed.

## State at stop

- **Everything is committed and pushed.** `<old-worktree>` on `proto/reload`
  at **5812241**, with no tree changes. That includes the reload session's latest
  (583b08c) and the harness scripts under `tools/harness/` (5812241).
- No BC2, receiver or host process is running (checked after the stop).
- The last x86 build is DLL `CA4298E519D1` (the 742546c sources, built 16:57 local).
- `proto/reload` is **192 commits ahead of `origin-bc2/main`** (6de115d). Merging it
  to main is the main open item.
- Stash `stash@{0}` "flick-remap (unproven: stick -> actions 36/25)" is
  obsolete. It was superseded by the server-side weapon select. Drop it.
- Disk: D: has **10.7 GB free**. `<old-worktree>\reports` holds **29 GB in 714
  run folders** (gitignored). Most of it can be deleted; keep the folders named in
  this file if you want the evidence. The launch and trace scripts refuse to start
  below 5 GB free, and image writers skip below 2 GB.

### Worktrees and local branches on this PC (for the "delete all other branches" step)

`git worktree list` from `<old-worktree>`:

| Path | Branch |
|---|---|
| <repo> | main (0243ba5, behind origin-bc2/main 6de115d) |
| <old-worktree> | proto/reload (742546c) |
| <worktree-reload2> | proto-work (583b08c, the reload session's) |
| <worktree-reload> | wip/pump2 |
| <worktree-tr> | test/range-merged |
| <worktree-re> | re/pc-give-path-ghidra |
| <worktree-ci> | detached 9d32297 |
| %USERPROFILE%/worktree-anyloadout | test/any-loadout |
| %USERPROFILE%/worktree-feel | feat/feel-wiring |
| %USERPROFILE%/worktree-range | test/range |
| %USERPROFILE%/worktree-typeinfo | tools/typeinfo-dump |

Other local branches: docs/engine-mapping-oct8, feat/reload-parity-docs,
feat/xm8-slider-probe, fix/reserve-lease-diagnostics, gate-default-flip, wip/haptics,
wip/pump, wip/sounds, wip/sounds2, wip/spas-as2. Each needs a merged/not-merged
check against main before deletion. Worktrees must be removed (`git worktree
remove`) before their branches can be deleted.

## Done this session (all on proto/reload)

| What | Commit | Evidence |
|---|---|---|
| Stick weapon switch, selected on the server inventory update 0x6f1980 (hook after the original, return 0x68dc7e, local server inventory only, setter 0x6d7c80(slot,1)) | 87bc623 | switch-only 191939, empty-switch 192105 |
| Guns-only flick (the owner's choice); right stick click (on release, so the recenter chord never switches) cycles launcher → gadgets → last primary | 07e23c5, 0b54020 | switch-only 200728, empty-switch 200857 (XM8↔F2000 both ways); equipment 201456: XM8→40mmgl→LZ-537→XM8, flick→F2000 |
| On equipment the hook keeps the inventory it selected (the reload runtime stops publishing it off a gun) | 0b54020 | 201456 `equipment_owned` 1, no_inventory 0 |
| Dropped-mag occlusion proven: the in-game draw's depth test works | 7f1689a | pre/post color: unsunk 504/596 px change (211951); sunk 50 cm drawn but 0 px (211818). Image `dropped-mag-occlusion.png` |
| Cause of the "floating" mags: drop floor y 174.02 vs depth floor 173.71 (30 cm) | 7f1689a (ledger) | 205643, 211951, 211818 unprojection |
| Depth-derived ground height `fvr::bc2::ground::Latest(nowQpcNs,maxAgeNs)` in `src/games/bc2/Bc2GroundHeight.h`, behind `FVR_BC2_GROUND_PROBE=1` | bb206ef | level 212744: y 173.78–173.83 at 2.8 m; looking down 212918: 173.714 at 0.74 m (matches the floor under the mag); `Bc2GroundHeightTests` |
| Harness: hitch modes `N,AFTER,stop` / `N,AFTER,hold`, hitch QPC timestamps, receiver `input_publish` gap log, `FVR_BC2_TEST_DEPTH_CAPTURE=2` props mode, HUD-copy write, ground-probe cost counters | 0b54020, 7f1689a, 742546c | below |
| Ledger/handoff entries for stick switching and occlusion | 0b54020, 7f1689a, bb206ef | docs/2142-TRANSFER.md, docs/HANDOFF.md |

## Results not yet reported (coordinator hasn't seen these)

### HUD-copy check (for Option B: credit rounds straight to the server copy) — DONE

Run `native-trace-20261008-215834-618` (DLL CA4298E519D1). Setup: gate scenario
`empty-only` (XM8 fired empty, closed empty gate blocks the native auto reload),
`FVR_BC2_TEST_SERVER_LOADED=30,8000`. That wrote **only the server copy's**
`firing+0x7c` (the field the pickup setter 0x6cd520 writes) from 0 to 30 at
11895 ms. Native reload stayed blocked, with no transfer events.

- Per-copy samples: before, at 9118 ms, client0, client1 and server all read
  (0, 210). At **11993 ms all three read (30, 210)**. The clients followed the
  server within one ~200 ms sample period (sample cadence limits the precision; it
  was ≤98 ms after the write).
- HUD (window screenshots `shots/gate-00..07.png`, crop `hud-strip.png` in the
  run folder): the counter goes **000/210 → 030/210** and stays.
- Conclusion: a server-only loaded write is replicated to both client copies and
  shown by the HUD. Option B's "credit to the server at the seat" shows correctly
  on the HUD. Not checked: the first-person magazine model count (no per-round
  mesh observation was taken), and the reserve setter 0x6cd850 (only the loaded
  count was written).

### Dropped-mag floor fix 4e98828 — NOT WORKING YET

Runs `220001-970` (sink 0) and `220133-937` (sink 80 mm), DLL CA4298E519D1,
`FVR_BC2_GROUND_PROBE=1`, `FVR_BC2_INGAME_PROPS=1`, `FVR_BC2_TEST_DEPTH_CAPTURE=2`,
head pitch −60° after 12.5 s:

- `gameplay...dropped_magazines.floor` = `{"ground":0,"offset":0,"play_space":0,"fixed":1,"offset_observed":0,"offset_rejected":0,"offset_m":null}`
  in both runs. The drop took the **fixed** floor path, not drop_ground/offset.
- The mag still rests at y **174.025** (sunk 80 mm: 173.945). The depth floor under
  it is 173.717. So the sink-80 mag is still 23 cm above the floor and visible
  (expected, given the fixed floor).
- The ground probe was publishing all along (39–40 samples, y 173.714–173.83, at
  0.74 m once looking down). So the reload side's floor code either didn't read
  `ground::Latest` at the drop, or rejected it (age, distance, or a gate). The drop
  happens around 11.9 s, before the pitch at 12.5 s. At that time the samples came
  from the level view (2.8 m ahead, y ≈ 173.78–173.83). Next step: the reload
  session checks why `fixed` was chosen.

### Ground probe frame cost — negligible

Plain XM8 rows (`run-mode7.ps1`). Each row was a fresh game launch, same machine
state, timings from the game's own frame counter over 24.8 s (warm within each run,
cold game start for each):

| Run | Probe | Native fps | CPU in probe (mean / max) |
|---|---|---|---|
| 220306-016 | off | 160.5 (6.23 ms) | – |
| 220437-748 | on | 160.0 (6.25 ms) | 2 µs / 293 µs |
| 220610-343 | off | 151.0 (6.62 ms) | – |

Run-to-run spread with the probe off (151–160.5 fps) is larger than any probe
effect. 0 busy maps in every run. The first-use max of about 1 ms (220001) is
the staging allocation. The fourth row (probe on, repeat) was aborted at the stop.
Verdict: fine to make default-on with `FVR_BC2_GROUND_PROBE=0` as a kill switch.
The code currently reads `=1` to enable, so flip it.

### SPAS rows on 96e9036 (16 runs) — reported

13 clean passes. Early cancels: cancel-between 214609 (keepalive_rejected, before
any credit) and abandon 214744 (missing_source after the first credit). Abandon +
hitch 215536 passed on product outcome, but the probe failed with failure 1 (a
fixture post-check). Details were sent to the coordinator.

## In flight / aborted at the stop

- **583b08c rows were never run** (aborted). They are: SPAS abandon + hitch
  mid-hold (`run-spas-probe.ps1 -Rounds 4 -AbandonAfter 2 -Hitch '90,1000,hold'`,
  pass = no Timing/UnsafeInput cancel and the shells still credit), SPAS three
  shells, and an XM8 plain row. The script is `tools/harness/r583-batch.ps1`.
- The fourth frame-cost row (probe on, repeat) was aborted. It isn't needed.

## Open items (next step, how to run)

1. **Merge proto/reload to main, delete the other branches** (the owner's gate for any
   further testing). proto/reload is 192 ahead of origin-bc2/main. Remove the
   worktrees first. Check each listed branch for unmerged work.
2. **Dropped-mag floor** (reload session): find why `floor.fixed` was chosen while
   `ground::Latest` had fresh samples. To verify afterwards, run
   `run-mode7.ps1 -InGameProps -DepthCapture -SinkMm 0|80 -Pitch -60 -PitchAfterMs 12500`
   with `FVR_BC2_GROUND_PROBE=1`, then `python tools/harness/occl.py <trace>`. Pass:
   floor `ground`≥1, mag y ≈ 173.71, sunk-80 changes ~0 px, unsunk ~500 px.
3. **Ground probe default-on**: invert the env read in `NativeProbe.cpp`
   (`FVR_BC2_GROUND_PROBE`), since the cost data supports it.
4. **SPAS intermittent early cancels**: keepalive_rejected before the first credit
   (cancel_reason 11 → 6 → 9 ring), and missing_source after a credit (ring r0
   cancel 6, grip F, inside_primary T). These belong to the reload session. Run the
   4×4 matrix with `tools/harness/e9-batch.ps1` and view it with
   `python tools/harness/e9_view.py <e9-batch.txt> <old-worktree>\reports`.
5. **SPAS abort with a stall**: fixed in 73b4b65 (XM8 203849 and SPAS rows pass,
   stall_rebases ≥1). Only the 583b08c mid-hold hitch row remains (above).
6. **Option B follow-ups**: first-person magazine round count under a server-only
   write is not observed. Reserve setter 0x6cd850 is untested.
7. **Stale input packets (~101 ms) behind unsafe_input cancels**: the receiver never
   gapped over 50 ms in 24 runs (`input_publish.max_gap_ms` 32–47), so the receiver
   isn't the source. The game side has no packet-age field in the trace. Adding one
   at the cancel site is the next diagnostic.
8. **Headset**: nothing in this file was checked in the headset.
9. Ledger (docs/2142-TRANSFER.md) is current for stick switching, occlusion and the
   ground probe. It doesn't have the HUD-copy result or the floor-fix failure.

## The harness (how to run things, once testing is allowed again)

Everything below is local to the dev PC.

- **Repo / worktree:** `<old-worktree>` (proto/reload). Scripts in
  `tools/harness/` default to `-Repo <old-worktree>` and
  `-Stage %USERPROFILE%\fvr-testrange-stage`.
- **Build:**
  - `tools/harness/qb.ps1 -Root <old-worktree>` does a quick x86 build of
    `build\x86`. It prints `BUILD OK <DLL sha256 prefix> <time>`. The vswhere
    warning is harmless.
  - `qb64.ps1 -Root ... [-Target BC2NativeIpcProbe] [-Tests <ctest regex>]` does
    the x64 build (receiver and unit tests). The receiver exe is locked while a run
    is in progress.
  - The owner asked for no CI and no full x86+x64 test-suite builds per change.
  - The test-range DLL is built with `build-testrange.ps1`
    (`build\testrange-x86`, staged to the Stage folder).
- **Game:** `C:\Program Files (x86)\Electronic Arts\Battlefield Bad Company 2`. Never
  modify the install. The test range is injected by
  `Stage\Start-BC2TestRange.ps1` with `Stage\ranges\<range>.cfg` (`sp02-xm8-only`,
  `sp02-xm8-f2000`, `sp02-xm8-spas`, `sp02-f2000`).
- **Runners** (each one launches BC2 into SP_02, starts the x64 receiver
  `build\x64\BC2NativeIpcProbe.exe` with scripted static-pose input, runs
  `Start-NativeTrace.ps1`, then closes the game):
  - `run-mode7.ps1`: XM8 magazine slider-release probe.
    - Switches: `-Cancel hold|untracked|arming|arming-untracked|head-lost`, `-Rack`,
      `-BoltRelease`, `-FlickAtHold`, `-Hitch 'N,AFTER[,stop|,hold]'`,
      `-InGameProps`, `-DepthCapture`, `-SinkMm N`, `-Pitch DEG -PitchAfterMs MS`,
      `-Range`, `-InputGapAt/-InputGapMs`, `-SaveFrom/-SaveTo` (receiver image
      range; use `1 0` for none), `-FailClosed`, `-DesktopCapture`.
  - `run-spas-probe.ps1`: SPAS shell-by-shell probe.
    - Switches: `-Rounds 1..4`, `-Cancel between|arming|after-seat`,
      `-FireAfter N`, `-AbandonAfter N`, `-GrabAfterShotMs N`, `-Hitch`.
  - `run-gate-scenario.ps1 -Scenario empty-switch|reload-no-left|switch-only|empty-only|equipment`:
    closed-gate and weapon-switch rows. Takes window screenshots `Stage\gate-00..07.png`
    from 11 s.
  - `run-support.ps1`, `run-spas-pump.ps1`: support-hand and pump rows.
- **Batch examples:**
  - `e9-batch.ps1` is the SPAS 4×4 matrix.
  - `floor-batch.ps1` covers occlusion, sink and frame cost.
  - `hud-batch.ps1` is the HUD-copy check.
  - `r583-batch.ps1` holds the open rows.
- **Env vars** (set before a runner; the runners clear their own probe variables):
  - Copy and gate:
    - `FVR_BC2_COPY_AUTHORITY=1|2`.
    - `FVR_BC2_EMPTY_GATE_FAILCLOSED=0` (the closed empty gate is on by default).
    - `FVR_BC2_CHAMBER_RACK` (on by default; the runner sets it to 0 unless `-Rack`).
  - Rendering:
    - `FVR_BC2_INGAME_PROPS=1`.
    - `FVR_BC2_GROUND_PROBE=1`.
    - `FVR_BC2_TEST_DEPTH_CAPTURE=1|2` (2 means only frames with an untinted dropped mag).
    - `FVR_BC2_TEST_PROP_SINK_MM=N`.
  - Test faults and logs:
    - `FVR_BC2_TEST_HITCH_MS=N[,AFTER][,stop|,hold]`.
    - `FVR_BC2_TEST_SERVER_LOADED=N,AFTER`.
    - `FVR_BC2_TEST_INPUT_LOG=1`.
  - Probe legs:
    - `FVR_BC2_PROBE_CANCEL`, `FVR_BC2_PROBE_BOLT_RELEASE`, `FVR_BC2_PROBE_FLICK_AT_HOLD`.
    - `FVR_BC2_PROBE_SPAS_ROUNDS`, `FVR_BC2_PROBE_SPAS_FIRE_AFTER`,
      `FVR_BC2_PROBE_SPAS_ABANDON_AFTER`, `FVR_BC2_PROBE_SPAS_GRAB_AFTER_SHOT_MS`.
- **Receiver options** (x64): `--gate-scenario <name>`, `--input-gap AT MS`,
  `--prep-shots 3..4`, `--head-pitch-deg D --head-pitch-after-ms MS`,
  `--head-yaw-deg`. `result.json` has `input_publish{max_gap_ms,max_gap_at_ms,gaps_over_50}`,
  `async_*`, `consumed_pairs`.
- **Output** goes under `<old-worktree>\reports\` (gitignored):
  - Each run makes `native-trace-YYYYMMDD-HHMMSS-mmm\` plus a receiver folder
    (`slider-rx-*`, `spasprobe-rx-*`, `gate-rx-*`).
  - The trace folder holds `BC2NativeProbe.dll` (hash it to prove the build),
    `native-trace.json`, `gun-events.jsonl` (per-copy events: firing_state,
    transfer, credit, begin/end/cancel, sample every ~200 ms with
    loaded/reserve), `depth/` (when depth capture is on) and
    `BC2TestRange.log`.
- **Report fields used most:**
  - `gameplay.weapon_switch{hooked,latched,selected,no_target,gated,expired,no_inventory,last_from,last_target,equipment_owned}`.
  - `gameplay.reload_flow.native_abort_cleanup{armed,calls,claimed_mask,completed_mask,failure,stall_rebases,cycle_history[]}`.
  - `gameplay.reload_flow.closed_empty_gate`.
  - `gameplay.test_hitch{fired,begin_qpc_ns,end_qpc_ns}`, `gameplay.test_server_loaded`.
  - `ingame_props{drawn,host_fallback,...}`.
  - `ground_probe{copies,maps,busy,rejected,published,min_y,max_y,last,cost_*}`.
  - The physical-reload block: `deferred_waits`, `held_gap_waits`,
    `pending_gap_waits`, `spas_submit_failure`, `submit_retries`, …
  - `dropped_magazines.floor`.
- **Viewers** in `tools/harness/`:
  - `spas_report.py <trace>` for the SPAS summary plus the frozen supply ring.
  - `gate_report.py <trace> <rx>` for XM8/gate rows.
  - `switch_view.py <trace>`.
  - `stall_view.py <trace>` for abort claims vs the hitch.
  - `occl.py <trace>` for per-pixel depth, floor unprojection and pre/post color.
  - `frametime.py <trace> <rx> ...`.
  - `e9_view.py`.
- **Before any GPU run** (the user's global rule): run `nvidia-smi` and check what
  holds the 12 GB card (ComfyUI, songd, ACE, browser). Label every timing cold or
  warm.
