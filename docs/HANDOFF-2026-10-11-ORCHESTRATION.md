> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Handoff: 2026-10-11 fix orchestration, integration and test package

Owner: Claude orchestration session ("VR rendering and controller ownership issues").
Status update (later 2026-10-11, resumed by a second Claude session): **all Tier 1 fixes
and recoil/muzzle are done and merged; `main` was fast-forwarded to `integration/20261011`
`4b3c791`.** The final unselected test package is `build/playtest-4675318/BC2-VR`
(DLL `96fbaa80…`, host `6361cae5…`, zip `8bfc714a…`). The consolidated headset test plan,
all kill switches, the packaging recipe and the cleanup plan are in
`docs/INTEGRATION-20261011.md`. Per-fix docs: `DEATH-RESPAWN-RELOAD-20261011.md`,
`RECOIL-MUZZLE-20261011.md`, `HEADSET-FRAME-DELIVERY-20261011.md`,
`HAND-AUTHORITY-SUPPORT-GRIP-20261011.md`, `HAPTICS-HAND-STUCK-20261011.md`.
Not yet run in game or headset. Next: headset test, then the queued tier-2 work.

Original status: in progress. Fix agents run in isolated worktrees under
`<repo>\.claude\worktrees\`. Nothing has been pushed. Nothing below has
been run in game or in the headset unless it says so explicitly.

## User decisions (2026-10-11)

- Delegate work to subagents: Opus for most tasks, Sonnet for easier ones. The orchestrator merges.
- Priority order was accepted as proposed. **Do not move cue discoverability ahead of the props work.**
- Headset delivery: **try 80 Hz plus the pace-to-content mode**, both on by default with kill switches.
- Build **one integrated test package** when the fix agents finish.
- Reconcile all branches so the newest test build contains all the latest, best fixes, then merge branches to clean up the repo.

## Critical repo fact: two lineages

- **The installed playtest builds do NOT come from `main`.**
  - They come from `codex/bc2-resume-20261011` (HEAD `3be9c94`).
  - The installed package is `build/playtest-b74560c/BC2-VR`, built from `b74560c`.
  - Its selection record is `reports/BC2-Selected.private.json`, selected 2026-10-12 01:59 UTC.
  - Its DLL is `build/x86/BC2NativeProbe.dll`, SHA-256 `efd3c0d0df6e4a214716eb339f8734c0cca07b24bed13ff8fc0dd99ca3135df4`. This matches `package-manifest.json` and session `native-trace-20261012-020559-729`.
- When it was surveyed, the resume branch was 84 commits ahead of `main` and 36 behind.
  - Many of the main-only commits look like cherry-picked duplicates of portable-launcher and packaging work.
  - `docs/HANDOFF-PORTABLE-LAUNCHER-20261011.md` warns that `main` is not equivalent to the compiled candidate.
- Worktree builds can lack the private authored headers. A worktree build only proves compilation; it is not an installable candidate. The real packaging recipe must supply those inputs (the integration agent is establishing it).

## Priority list and assignments

| Tier | Item | Model | Branch | Status |
|---|---|---|---|---|
| 1 | Death/checkpoint crash + persistent reload authority (CRITICAL) | Opus | `fix/death-respawn-reload` (on resume `3be9c94`) | running |
| 1 | Primary-hand controller authority + support-grip acquisition | Opus | `fix/hand-authority-support-grip` `aaa36be` | **done**, compiles, TrackedRig/SupportGrip tests pass |
| 1 | Headset FPS/blur measurement + telemetry | Opus | `perf/headset-frame-delivery` `5f57c56` (main-based) | **done**, telemetry only |
| 1 | Stereo every frame / request starvation + 80 Hz + pace-to-content | Opus | `perf/stereo-every-frame` (resume + cherry-pick of `5f57c56`) | running |
| 1 | Haptics wrong hand / stuck | Sonnet | `fix/haptics-hand-stuck` `6eb7ddc` (main); `fix/haptics-hand-stuck-resume` `29bb11f` (resume) | **done**, both compile |
| 2 | Recoil pitching barrel down + muzzle flash / shell ejection attachment | Opus | `fix/recoil-muzzle` (on `aaa36be`) | running |
| — | Branch reconciliation, integration build, packaging recipe, cleanup plan | Opus | `integration/20261011` (from resume `3be9c94`) | running |
| 2 | SPAS pump driven by controller stroke; bolt pulls with native authored grasps | — | — | queued; starts after death/respawn lands (shares reload code) |
| 2 | Props: floating glow indicators, flickering untextured belt/ground mags, IK arm flashes, mags reappearing on support grip (XM8-MAG-002), held/dropped mag handoff | — | — | queued |
| 2 | Discoverability: belt supply, bolt/rack cues, XM8 bolt zone not lighting (RELOAD-001) | — | — | queued, stays after props per user |
| 3 | Fullscreen/window black flashes; OBS capture interleaving flat frames; startup 9–10 s (native setup ~6.2 s) | — | — | later |
| later | Binocular/cutscene presentation, either-hand weapons, dev spawn toolbox, native vs VR mechanism profiles, XM8 empty-reload bolt, weapon depth occlusion, BF3/BF4 roadmap | — | — | design/research |

## Findings and fixes

### Haptics (`6eb7ddc` main, `29bb11f` resume)
- **Main-line root cause:** every owned shot sent `GunFire` to the right hand, which is correct.
  - It also sent `PumpGunKnock` to the LEFT hand whenever `current->supported` was true.
  - That flag only means the shot frame is usable, so one-handed guns buzzed the left controller.
  - On full auto the overlapping pulses felt like constant vibration.
- Nothing stopped haptics on weapon switch, death or owner loss. The only stop was on focus or presence loss.
- Hand indices (0 = left, 1 = right), durations (finite ns, 10–100 ms) and event ids were all correct.
- **Fix:**
  - The support knock is gated on the support grip actually being held.
  - New `FeedbackKind::StopAll` (17) makes the host cancel the pending second peak and call `xrStopHapticFeedback` on both hands.
  - The DLL sends StopAll on weapon change and from `ClearReloadOwner`, which covers owner loss, death, pause and stale input. It only sends if a pulse went out in the last second.
  - The host clamps every pulse to 1–200 ms.
  - The tests' kind-range check moves from 17 to 18 (not built or run).
- **Resume-line difference:** `0524f7c` (KF2 weighted recoil and braced shot haptics) already sends the left pulse only when `tracking.supportHolding && supportToken`.
  - `0524f7c` is in the installed `b74560c`.
  - So on the installed build the left buzz implies either an older build or a false support-holding state. **The playtest must note whether one-handed fire still buzzes the left controller.**
  - StopAll, the clamp and the kill switch were ported. `FVR_BC2_GUN_HAPTICS=0` also covers the weighted and braced recoil pulses.
- This is distinct from the earlier missing-event reload-failure haptics crash (`8b6da5e`, untouched).
- **Kill switch:** `FVR_BC2_GUN_HAPTICS=0`.
- **Doc:** `docs/HAPTICS-HAND-STUCK-20261011.md`.
- **Test:**
  1. One-handed right gun fire: only the right controller vibrates.
  2. Hold full auto and switch weapons: vibration stops immediately.
  3. Hold the support grip and fire: the left controller gets a light knock.

### Hand authority and support grip (`aaa36be`)
- The installed `b74560c` contained the earlier acquisition patch (it keeps the neutral arm after a rejected trial; `docs/re/support-handoff-20261012.md`).
  - That session still recorded no support grab.
  - Guns used: XM8_sp_s, M416, MG36, VSS, 40mmgl.
  - 53 hand-claim rows, none of them `weapon_support`.
  - The game crashed on exit, so the final report is empty.
- **Root causes:**
  1. *(high confidence)* `OwnLeft` validated the support claim with the renderer's input sample, which runs about 5 samples behind live input. The ownership check rejects any sample older than the last grip release, so the first squeeze is always rejected. Run 005105 shows 70 rejections at 1–15 cm with full squeeze and one grab.
  2. *(high confidence)* Gameplay discarded the support contact unless the profile was verified for SupportGrip. Only SPAS12_sp and XM8_sp_s were, so other guns needed authored entries.
  3. *(medium confidence, not confirmed in a capture)* Hand rotation and aim change after a switch or cutscene:
     - On legacy-calibration rigs, `TrackedRig::Update` overwrote the wrist from the gun aim frame and fit.
     - The weapon fit was re-sampled from the native equip animation on every equip and after every cutscene reset.
- **Fixes:**
  - Support claims are validated against the current input sample. Contact still comes from the renderer frame for the same player and space.
  - The profile check is removed, so every gun with a published shot frame offers the front grip, using the original first-person left-wrist pose.
  - The gun is aligned to the aim frame and anchored on the controller wrist; the wrist is never derived from the gun.
  - The settled fit is cached per asset name and rig layout and reused on re-equip and cutscene return.
- **Not touched:** reload authority, the magazine lane, rack/bolt ownership. Support stays blocked while the magazine is out, a rack is owed, or the XM8 bolt holds the left hand.
- **Kill switches:**
  - `FVR_BC2_SUPPORT_CURRENT_EVIDENCE=0`
  - `FVR_BC2_SUPPORT_ALL_WEAPONS=0`
  - `FVR_BC2_WEAPON_FIT_CACHE=0`
  - `FVR_BC2_LEGACY_HAND_FROM_WEAPON=1`
- **Files:** `Bc2Gameplay.cpp`, `Bc2InteractionZones.h`, `Bc2RigPublication.cpp`, `TrackedRig.cpp/.h`, `tests/TrackedRigTests.cpp`.
- **Doc:** `docs/HAND-AUTHORITY-SUPPORT-GRIP-20261011.md`.
- **Worktree compile DLL:** SHA-256 `2826ff9c…1a93`, built without private headers, so it is not installable.
- **Test:**
  1. Confirm the DLL hash is no longer `efd3c0d0…`.
  2. XM8 with the magazine seated and no rack owed: squeeze on the handguard. It should grab immediately; repeat 5 times.
  3. Repeat on M416, MG36, VSS and one gun with no profile.
  4. Try squeeze-then-reach and reach-then-squeeze.
  5. Switch guns and back 3 times: no hand rotation, identical aim, no settle delay.
  6. Go through a cutscene and back: hand and aim unchanged.
  7. In the trace, `two_hand_support.grabs` > 0, `hand_ownership.claim_rejections` ≈ 0, and `weapon_support` claim rows are present.

### Headset delivery (`5f57c56`, telemetry)
- Evidence comes from on-disk logs only. The timings are not labelled cold or warm; they are whole-session aggregates, good for magnitude only.
- **b74560c session** (`build/playtest-b74560c/BC2-VR/reports/native-xr-session-20261012-020553-094`, SteamVR/oculus, 1832x1920 per eye):
  - 43,261 headset frames: 12,339 fresh (~35/s), 29,311 reused, 1,611 with no world layer.
  - Request to completed pair: mean 49.9 ms; 49% took over 50 ms.
  - Game frames: 16,182, of which ~3,800 (24%) rendered mono.
- **Older builds:** 52% of game frames rendered mono.
- **Sharing:** always mode 3 (fenced legacy).
- **Depth:** reported "active", but 0 depth pairs or frames ever reached the runtime.
- **Host:** keeps up with the runtime (99.6% of frame gaps are 5–11 ms).
- **Hypotheses:**
  - **H1, certain:** ~35 pairs/s at a ~120 Hz refresh. SteamVR never engages Motion Smoothing because the host submits every refresh, and rotation-only reprojection in a 3,4,3,4 cadence reads as judder and blur.
  - **H2, very likely:** request starvation. A request takes ~2.5 game frames, which pipeline depth 2 does not cover, so mono frames waste GPU. On main, the single legacy pair drops whole stereo frames.
  - **H2b, low probability, b74560c only:** the two legacy pairs share one private eye pair, so a busy try-lock could produce a mixed-frame pair.
  - **H3, plausible:** left-eye-only ghosting, fades and LOD differences come from mono frames using the native camera in the left eye.
  - **H4, certain:** no depth reaches the runtime. Depth needs keyed sharing and BC2 uses fenced legacy.
  - **H5:** 8% overscan costs ~7% linear density.
  - **H6:** the developer launch path defaults to 1920x1080 per eye. The 2026-10-11 06:03 playtest ran at that size.
- **Ruled out statically:** CPU readbacks, keyed-mutex stalls, synchronous host waits, host pacing to the game, a downscale step, sRGB mismatch.
- **Telemetry:**
  - Host: a `delivery` line in `xr-events.jsonl` once per second (`FVR_XR_DELIVERY_LOG=0` turns it off).
  - DLL: cumulative `session-telemetry.jsonl` once per second, covering stereo vs mono frames, sharing mode, export GPU time and `eye_frame_mismatch`.
- The bridge now refuses to publish mixed-frame pairs.
- **Doc:** `docs/HEADSET-FRAME-DELIVERY-20261011.md`.

### In progress (results pending)
- **`fix/death-respawn-reload`:**
  - Attribute the latest death/respawn crash to a process and fault address.
  - Fix stale pawn/weapon/inventory generation in persistent reload authority without losing or duplicating ammo. The earlier symptom: after checkpoint the XM8 reload can't finish, and after a switch no gun fires or ejects.
  - Doc: `docs/DEATH-RESPAWN-RELOAD-20261011.md`.
- **`perf/stereo-every-frame`:**
  - Eliminate starvation so every game frame renders stereo.
  - Give each pair its own eye textures.
  - Stop dropping stereo frames.
  - Developer-path default eye size becomes the runtime-recommended size.
  - 80 Hz via `XR_FB_display_refresh_rate` when available; otherwise document where to set it. It is a launcher setting defaulting to 80.
  - Pace-to-content host mode, launcher toggle default ON, with env kill switch.
- **`fix/recoil-muzzle`:**
  - Recoil sign, pivot and composition with the native sample and the new controller-anchored frame.
  - Muzzle flash and shells follow the rendered VR weapon's sockets (XM8-FX-001).
  - Reuse Codex `recoil-transfer-20261011` (`dd15673`, `23f37a6`) if it fits.
  - Doc: `docs/RECOIL-MUZZLE-20261011.md`.
- **`integration/20261011`:**
  - Merge `main` (`a62615d`), the hand fix, `haptics-resume` and worthwhile Codex branches into a branch from resume.
  - Codex branches: `bc2/proper-hands-wrists` +9, `bc2/empty-magwell-visibility` +7, `recoil-transfer-20261011` +2.
  - Do not merge `5f57c56` directly; it arrives via stereo.
  - Write the real packaging recipe and the cleanup plan to `docs/INTEGRATION-20261011.md`.

## Main checkout state
- `main` = `a62615d`. It contains:
  - The haptics fix (`6eb7ddc`).
  - The previously uncommitted working-tree edits: the AGENTS.md KF2 VR standard, the game-launch-lock removal in `DevLauncher.ps1`, `tools/Start-BC2LauncherTask.ps1` and `tools/Start-BC2Playtest.ps1`, and the playtest ledger plus grenade crash RE docs.
- `a.txt` and `d.py` (scratch capstone disassembly of `BFBC2Game_795745.exe`) are left untracked on purpose.

## Remaining plan
1. As each running agent finishes: review its diff, merge it into `integration/20261011` and rebuild incrementally.
2. Build the single integrated test package with the real recipe (private authored inputs, package hash checks). Record its hash and the per-fix test steps.
3. Fast-forward or merge `main` to the integration result.
4. Delete fully merged local branches and agent worktrees in `.claude/worktrees`. Leave the Codex worktrees under `%USERPROFILE%\Documents\Codex\` and their branches unless they are fully merged and confirmed idle.
5. Do not push to `origin`, only ever to `origin-bc2`, and only when asked.
6. Start the queued tier-2 work: the SPAS pump and bolt grasps, then props, then discoverability.

## Headset test notes for the integrated package
- Run `nvidia-smi` and check what holds VRAM before any run. Label every timing cold or warm.
- Compare pace-to-content on and off using the `delivery` lines (fresh/s, reused/s, age p95), the mono frame count and `eye_frame_mismatch`.
- Include a death → checkpoint → reload → weapon switch → fire sequence on the XM8.
