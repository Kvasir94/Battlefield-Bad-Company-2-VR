> **Historical archive.** Preserved on October 13, 2026. Implementation claims,
> priorities, agent assignments and workflow restrictions below belong to their
> recorded sessions. Use [current status](STATUS.md) and [development rules](../AGENTS.md)
> for new work. Technical evidence retains its original scope.

# BC2 VR open work log

This is the single canonical work doc for the BC2 VR mod built on FrostbiteVR.
It is updated as tasks finish or findings land. [STATUS.md](STATUS.md) and
[HANDOFF.md](HANDOFF.md) describe the inherited FrostbiteVR checkpoint; this
file tracks the work on this repo since then. Research threads may keep their
own logs, but this file is the canonical one.

## Goal and requirements

Build on and polish FrostbiteVR for Battlefield: Bad Company 2 (BC2). The main
deliverable is a fully polished, native physical reload system modeled on
Arizona Sunshine 2 (AS2) and the KF2 VR mod (`Kvasir94/Killing-Floor-2-VR`).

- FrostbiteVR is a reference, not a specification. Assess it against KF2 VR and
  the AS2 data already extracted in `kf2-vr`. Never decompile AS2 again.
- Work is BC2-specific and should match the shape and quality of the KF2 VR and
  AS2 docs.
- Single-player only. Multiplayer is not a priority.
- Main difficulty: Frostbite 1.5 is barely mapped.
- Test harness, following KF2 VR's replay harness: modular and sturdy; tests
  any gun; forces the loadout under test; spawns quickly into an empty space
  with no missions or cutscenes; tests guns in-engine and logs gun events.
- Priority (the owner, 2026-10-08 05:15Z): "We dont need to sim a bunch of
  stuff. Get me a build or replays ready asap." Sim work is stopped. The top
  priority is a launchable PC build plus live replays.

## Focus and branch policy

Approved by the owner at 05:46Z.

Focus, in order:

1. One test-range DLL that does fast start, vtable restore, give-weapon and
   the SP_02 cutscene skip.
2. Live XM8 replay with the gun-event log, then SPAS.
3. Reload parity P0 items as small PRs off `main`.

Research order: cutscene skip, then pump/dry-fire, then native sounds, then
dropped mag.

Branch policy:

- After PR #3 merges, `main` is the base. Each change gets a short-lived
  branch off `main` and a small PR, merged when green.
- One test-range line: `test/range`, `tools/test-range`,
  `feat/range-give-weapon` and `test/range-merged` collapse into one.
- Engine docs go only through the research PRs. The PC session pushes notes
  only to `re/pc-give-path-ghidra`.
- Delete fully merged branches; never touch Codex's.
- Check for a running `BFBC2Game` before launching. The planning PC trace
  `native-trace-20261008-055525-313` collided with a stage run and its
  `gun-events.jsonl` came out 0 bytes.
- Merged, to delete now: `ci/linux-sanitizers`, `feat/feel-wiring`,
  `feat/gun-event-emitter`, `feel/chamber-and-insert`,
  `integrate/feature-stack`, `tools/gun-event-log`, `tools/replay-harness`,
  `tools/typeinfo-dump`. After PR #5 merges: `test/range`,
  `feat/range-give-weapon`. Parked: `feat/spas-sim`. Later:
  `feat/reload-parity`.
- Research stays on `claude/fast-test-level-research-mij58q` via PR #4, then
  one follow-up PR that folds in `re/pc-give-path-ghidra`.

## Done (2026-10-08)

| Item | Where |
| --- | --- |
| FrostbiteVR cloned to `<repo>` on the owner's PC | PC |
| BC2 patched to build 795745, the build FrostbiteVR maps. The patched exe differs from FrostbiteVR's expected hash only in the signature block. The exe inspector finds all render hooks | PC; old exe at `<backup>` |
| BC2 settings set to DX11 with `Fullscreen=false` | PC; backup `<backup>\settings.ini.bak` |
| Investigation write-up, revised against KF2 VR and AS2 | artifact (private conversation, not linked) |
| Private repo created and seeded with FrostbiteVR history | `main` |
| x86 and x64 builds pass on the PC with 184-185 C++ suites passing | PC |
| Engine reflection dump: 1,991 types (1,423 classes, 231 enums, 7,048 fields with offsets), 15 checks against known facts pass, generated header compiles. Includes `profiles/typeinfo/795745`, the dumper and `docs/BC2-TYPEINFO-DUMP.md` | `tools/typeinfo-dump` @ `108a59b` |
| Gun-event log: JSONL schema `docs/GUN-EVENT-LOG.md` and `tools/analyze_gun_events.py` (58 tests). Checks the delay from seat to ammo credit, ammo conservation, double credits, fire leaks and freeze leaks | `tools/gun-event-log` @ `10dd524` |
| KF2 `MagazineFeedState` port (417 checks); AS2 magwell slider behind a profile flag (25% release, 40-250 ms auto-finish); KF2 150 ms motion guard; `docs/BC2_PHYSICAL_RELOAD.md` with per-weapon defaults. **Not yet wired to the BC2 adapter** | `feel/chamber-and-insert` @ `fadf199` |
| Replay harness: tape v1 reader/writer; offline sim and `replay_assert.py`; XM8 and SPAS scenarios; DLL tape source and recorder; `run.py` / `Run-Replay.ps1` runner; `cut.py`; `Start-BC2VRSession -RecordTape`; `docs/REPLAY_INPUT.md`. Linux ctest 167/167 (`-fshort-wchar`, as CI builds). The sim XM8 run is deterministic and passes `analyze_gun_events`. Windows-only code not yet compiled or run | `bc2/tools/replay-harness` @ `25d10a6` |
| Live gun-event emitter: lock-free ring plus a JSONL writer on the probe thread, fed only by existing hooks (no new native hooks). Writes `gun-events.jsonl` beside `native-trace.json`. Switch `--gun-events` / `-GunEvents`; `run.py` live mode passes it. The golden XM8 log passes `analyze_gun_events` and `replay_assert`. Linux ctest passes 168/169; gun-event tests are clean under ASan, UBSan and TSan. Windows wiring not yet built or run | `feat/gun-event-emitter` @ `fe88bde` |
| Feel wiring (cloud side): based on `feel/chamber-and-insert`, merges replay-harness, gun-event-log and gun-event-emitter. XM8 uses the AS2 slider; SPAS stays on Legacy. Hand latch through the auto-finish. `Bc2MagazineChamber`, with `blockFire` masking Fire only. Catalog of 25 weapon actions, all unverified. Seated haptic (0.7 for 45 ms) drained to the host. `ChargingHandleRack` recognizer, off by default. Engine `FiringFunctionData` reader with catalog fallback, not yet enabled in the DLL. Chamber, settle and rack rows in the gun-event log. Sim scenario `xm8-slider-release` passes `analyze_gun_events` and `replay_assert`. Linux ctest 174/174 (`-fshort-wchar`, as CI builds). Caveat: the experimental `AEK971_sp` geometry row also got the slider feel (a mislabel, kept as an extra test case). Not yet built on Windows or tried in game | `feat/feel-wiring` @ `75b7a03` |
| Reload parity: `docs/BC2_RELOAD_PARITY.md` with about 30 rows comparing AS2, KF2 VR and BC2-now (evidence, gap, engine mapping, priority), a ranked backlog and the firing-state map 0–15. AS2 `releaseItemOnInsert` on the slider, plus an opt-in SPAS As2 shell feel (live default stays Legacy). `slide_lock` gun-event row and labels on chamber rows. Linux 174/174 (`-fshort-wchar`, as CI builds). Based on `feat/feel-wiring` `75b7a03` | `feat/reload-parity` @ `32b209e` |
| `Bc2InputTapeSource` (replay harness) no longer aborts with stack smashing under `-fshort-wchar`. Root cause: `tests/Bc2InputTapeSourceTests.cpp` used `std::wmemset` and `std::wcsncpy`; glibc's wide functions assume a 4-byte `wchar_t`, so a 520-byte buffer got 1040 bytes written. Fix: plain copy loop. Test-only; production is unaffected because `wchar_t` is natively 2 bytes on Windows | `d56b673`, pushed in `feat/feel-wiring` |
| Test range spec and runner: format `FVR_BC2_RANGE 1` with C++/Python parity against golden files. Keys: level (validated against the 13 SP ids), route, weapons as `asset@Slot`, ammo, settings switches, `fixed_dt`, timeouts. Ranges `sp02-xm8` (manual), `xm8-spas`, `aek971-fixed-dt`. Table of 49 settings fields, all unverified. Capability-gated executor that restores state on every outcome and emits range rows. Runner writes the PC launcher's `BC2TestRange.cfg` and calls `Start-BC2TestRange.ps1 -InjectDelayMs 3000`; can stage and unstage the intro movies. `docs/BC2_TEST_RANGE.md` with a 10-item PC checklist. Linux ctest passes 168/168, including under ASan/UBSan; Windows CI pending | `tools/test-range` @ `5d74f6b` |
| SPAS live scenario fix: `spas-shell-reload.json` no longer requires a `capture` row in `fvr_order`, which live SPAS logging never wrote. `GunFvrTracker::Shell` emits only begin, `pouch_start`, complete/cancel and end; seat and credit come from the submit path. `replay_assert` tests pass. Going into `integrate/feature-stack` | `feat/feel-wiring` @ `0e8f838` |
| Linux CI with sanitizers: 164 portable suites and 824 Python tests pass. Follow-up fix for missing numpy/Pillow and short Windows temp paths | PR #1 (`ci/linux-sanitizers` @ `c104808`), merged as `3eca473` |
| `main` CI green after PR #1 merge. Every check green on head `c104808`: Linux gcc, asan-ubsan, Windows x86, Windows x64. Fixes: numpy/Pillow install and resolved temp paths (`55b3a35`), x64 `Launch-VsDevShell` arch (`2594731`), `ReloadFlowRecords` C1060 heap fix (`c104808`) | `main` @ `3eca473` |
| In-game Muzzle smoke test accepts any two guns and reads `expected_weapon` from the manifest (stopgap) | `test/any-loadout` @ `230a6e3` |
| Typeinfo dump surveyed for settings, level, spawn, kit, weapon, pickup, cutscene and menu types. Routes ranked and sent to the planning thread | PR #2 (merged) |
| KF2 VR harness shape summarized (launch, weapon forcing, replay format, log judging, robustness) | PR #2 (merged) |
| Fast test level and loadout research complete: launch args, level data, test levels, server data, mod tools, typeinfo dump, community sources, survey of this repo's launch, level and weapon routes | Research thread; PR #2 (merged) |
| PR #2 merged into `main` (test-level routes and research docs), all 8 checks green. The research thread will open a follow-up PR with its cloud disassembly findings in `docs/re` | `main` @ `dd37868` |
| PC build of PR #3 `6b53a5a` in `<old-worktree>`: x86 and x64 each pass CTest 199/199; Python 951 OK (4 skipped). Artifacts `build\x86\BC2NativeProbe.dll` (+ `BC2NativeTrace.exe`) and `build\x64\BC2XrHost.exe`. Launch: once in campaign gameplay, run `.\Start-BC2VRSession.ps1 -MagazineReload -GunEvents` from `<old-worktree>`. Handed to the owner at 05:30Z; no in-game or headset check yet | PC builds and engine mapping session |
| Native fast start, verified live on the PC: launch `BFBC2Game.exe` with no args, inject at about 3 s, set `Client.Level = Levels/SP_02`, hook `UIManager::update` (two stack args, `ret 8`; a one-arg detour crashes at the menu), and after 60 main-menu frames call `newSP` `0x7600f0`(difficulty 1) on the UI thread. Result: SP_02 with the XM8 at 30/210. `newSP` fires 4.3 s after launch; reaching player control takes about 2 min, mostly SP_02's opening cutscene. Write-up `docs/re/BC2_FAST_START_NEWSP.md`. Code pushed to `test/range` `1d19bac`; the vtable slot is still not restored on exit (see In progress) | planning PC session (`test/range` @ `1d19bac`); write-up on `re/pc-give-path-ghidra` @ `034f675` |
| `test/range-merged` `ceb23b5` live run (second PC session, 2026-10-08 ~00:54 local): inject at 3.1 s; `newSP` queued 0.4 s after the DLL load; player soldier found about 10 s after launch; first-person XM8 gameplay in the ~28 s screenshot (HUD 030/210); no crash in 123 s. Cutscene skip: `0x73fcd0` called 660 times over 99.6 s; `CS_11` never visible, so the skip likely works, but nothing in the log confirms it [inferred]. Give: data resolved (slot1 `SP_shg_SPAS12` → `0x40496e50` `SPAS12_sp`) but never ran, because the give is gated to the native give thread (48992, which only runs the spawn gives) while the input-update hook runs on 29148. The SPAS-12 was not given | second PC session |
| Push-everything audit (cloud + PC): all commits are on origin. The one uncommitted change is `tools/replay/run.py` in the PC `worktree-feel` worktree (foreground fallback, owned by the planning thread). Local cloud `feat/spas-sim` `75b7a03` is contained in `origin/feat/spas-sim` `081c5ed` | cloud and PC sessions |
| First Windows build of `feat/feel-wiring` `75b7a03` on the PC: 195/195 on x86 and x64, including the XM8 slider replay | `%USERPROFILE%\worktree-feel` |
| PC Ghidra cross-check of the give path: `docs/re/BC2_PC_GIVE_CROSSCHECK.md` confirms `0x6bcf80` (unique signature, ABI, soldier-owned item) and the weapon-by-name lookup. Per-shot ammo decrement not found statically | `re/pc-give-path-ghidra` (`54ca94c`) |
| First headless in-game XM8 fire and physical reload (run 072001): burst, begin 2→10→11, unseat, pouch, seat, 11→12 transfer, credit 30/209. Mode 7 one-launch scenario (fire to empty, 3 dry pulls, reload) passed in run 090531 and 6+ consecutive runs (085404–110023) | `proto/reload` |
| Seat = ammo: on the seat, one Update with delta + the remaining state-11 timer reaches the native Transfer on that frame (it used to arrive up to ~2.8 s late) | `proto/reload` |
| Reload clip sound withheld and replayed at the magazine pull inside the Ant listener dispatch `0xf4fb50`, under its lock; SPAS pump clip replayed at the rack (`f9df398`, `3029e0e`, `22cbc81`) | `proto/reload` |
| SPAS pump held at state 7 until a physical stroke, with fore-end motion; KF2/AS2 haptic set; pouch hover tick; dropped magazine with floor bounce; reload hints (per-part clocks, amber glow, magwell ghost) | `proto/reload` |
| F2000 and AEK-971 (with GP30) magazine profiles from shipped authored data; opt-in charging-handle rack for XM8/F2000 and bolt release on the eject press | `proto/reload` |
| Headless harness: test range, eye stream and receiver, flicker rule and pack audit, headset mirror window, magazine probes, hitch and input-gap injection | `proto/reload` (PC builds) |
| Fail-closed empty gate behind `FVR_BC2_EMPTY_GATE_FAILCLOSED=1` (`698e9ae`, `ef6b3b2`); review fixes: owner-keyed identity, support from the gun in hand, stock-reload fallback (`7dfe376`). Default flip waits on the gate matrix | `proto/reload` |
| KF2 VR vs BC2 reload-wiring assessment: hybrid (keep the native credit, use KF2-style fail-closed and event-driven control); closed-gate review; mailbox/server-authority/rack design | project files `kf2-vs-bc2.md`, `reload-closed-gate-review.md`, `reload-wiring-design.md` |
| Engine map for the firing, reload, sound and palette paths, with unique signatures on `c603f686`: [re/BC2_ENGINE_MAP_20261008.md](re/BC2_ENGINE_MAP_20261008.md) | `docs/engine-mapping-oct8` |

## In progress

Top priority (the owner, 05:15Z): a launchable PC build plus live replays; sim
work is stopped. Next PR: #3 (`integrate/feature-stack`); the owner approved
"Merge when green" (05:36Z).

| Item | Owner | State |
| --- | --- | --- |
| Spec `true`/`false` vs `0`/`1`: the range spec emits `true`/`false`, but the PC proved `0`/`1`. Confirm the setter accepts both | Planning thread | open |
| Range spec emits only `Client.Level`; `Server.Level` is available via `set.*` | Planning thread | open |
| PR #3 `integrate/feature-stack` @ `6b53a5a` into `main`: feel-wiring (with the SPAS rule fix), gun-event emitter, replay harness, gun-event-log, typeinfo-dump, `tools/test-range` and `test/range`, merged with `main`. Linux 178/178 passes in the default, ASan and `-fshort-wchar` builds; Fully green at `6b53a5a` (Linux gcc, ASan/UBSan, Windows x86 and x64). The owner chose "Merge when green" (05:36Z decision card) | Planning thread | merged at head `bd878fe` with all 8 checks green; merge commit `02905e3`. Branch deletion pending (see blockers) |
| `feat/spas-sim`: WIP at `081c5ed`, one commit on `feat/feel-wiring` `75b7a03`. Has the `ShellRowsFor` row builder and the `SimModel::Shell`/`ShellSim` stand-in. Not done: CLI, scenario, tape, tests, docs. Never built, no PR | Coordinator cloud worker | parked (sim work stopped) |
| Live gun-event log gaps: misses dry fire, has no rack rows, and only covers a 20 s window outside `-MagazineReload` | Planning thread | open |
| Record a real XM8 tape in the headset; the current XM8 tape is synthetic | Planning thread | open |
| Wire `anchor=weapon` and the `weapon=` start condition live | Planning thread | open |
| Compile and run the replay harness's Windows-only code (DLL tape source, recorder, `Run-Replay.ps1`, `-RecordTape`) | Planning thread | open |
| Runtime weapon forcing: route C1 harness method "give weapon X in slot Y" | Planning thread | open; see Findings |
| PC build and in-game checks of `feat/feel-wiring`, following "Adapter wiring" in `docs/BC2_PHYSICAL_RELOAD.md` | Planning thread (PC session) | build done (see Done). Live XM8 replay now works end to end: test range → gameplay → `Run-Replay -Mode live`. Blocked: the tape is held and never starts (see the reserve-lease row). The planning PC session acts only on the owner's typed lines |
| `run.py` foreground fix: `fix/replay-foreground` `d084313`, PR #6 | Planning thread (PC session) | PR open; CI blocked by billing |
| **Reserve lease never verifies (core blocker):** `Bc2InputTapeSource` releases the tape only when `reloadFlowRuntime::ReadReserve()` returns a verified lease (`NativeProbe.cpp` ~1772; `Bc2InputTapeSource.cpp:18`). In SP_02 with the XM8, over ~40 s and 9187 gathers, matched loaded/reserve/capacity stayed 0 and `weapon_checked_natively=false`. The same lease gates the physical mag reload. One run also logged an intermittent "Native gameplay binding unverified" | Planning thread (cloud worker) | in progress on `fix/reserve-lease-diagnostics`: reason rows, plus a fix if the cause is clear |
| Second PC session "PC builds and engine mapping": Ghidra mapping of the give path and reload/feel hooks (PR #3 PC build done) | PC builds and engine mapping session | in progress |
| Cross-check give function `0x6bcf80`, its callers and the selection setter `0x6d7c80` on the PC | PC builds and engine mapping session | static cross-check done (`docs/re/BC2_PC_GIVE_CROSSCHECK.md`); live check pending |
| Single test-range DLL: draft PR #5, `test/range-merged` @ `9d32297` (plus `main` and `tools/test-range` `bcb6c71`); `9d32297` stops `skipCutscene` once the give has run, bringing the restore forward, and sets the default skip window to 60 s. Earlier: `ceb23b5` (`feat/range-give-weapon` stays at `33d8716`). `f42a327` adds a key filter: only `Client.`/`Server.`/`Game.`/`Network.`/`Physics.` keys reach the registry; any other unknown key refuses the run and logs it (not clang-checked). The give matches `SoldierWeaponData.Name` (+0xc, e.g. `SPAS12_sp`), the full `WeaponAssetName` path (+0x40), or its last segment, so short cfg names work. `e9e9062` adds cutscene skip route A (`TestRange.SkipCutscene`). `ceb23b5`: `ranges/sp02-xm8-spas.cfg` uses `Give.Slot0 SP_rgl_XM8_Scoped` and `Give.Slot1 SP_shg_SPAS12`; the lookup takes the full path or its last segment (cpp:450). Fast start (`1d19bac` code) plus give (`9e3a566` code); restores vtable slot `0x1440860` after `newSP`, on give-up (no menu within about 10 min) and on detach. cfg split into `TestRange.*`, `Give.*` and engine settings. Example `ranges/sp02-xm8-spas.cfg`: `Client.Level Levels/SP_02`; `TestRange.Start newSP`; `TestRange.Difficulty 1`; `TestRange.WaitFrames 60`; `Give.Slot0 XM8_sp_s`; `Give.Slot1 SPAS12_sp`; `Give.Select 1`. Clang-clean, not yet run in game. After PR #3 it becomes the follow-up PR off `main`, carrying `bcb6c71` | Planning thread; second PC session runs it | ran live (see Done): fast start and gameplay work; give did not run (thread gate) |
| Fix the give thread gate: the give is gated to the native give thread (48992, spawn gives only) while the input-update hook runs on 29148, so the give never runs. The gate stays (a client-thread call to `0x6bcf80` would race the server); research is finding a per-frame hook on the server thread | Planning thread; research | open |
| Give-weapon code (`feat/range-give-weapon` `9e3a566`, now folded into `test/range-merged`): `Give.SlotN` / `Give.Select` keys in the test-range DLL. Two signature-verified MinHook detours (input update; pass-through on `0x6bcf80`), plus a check that pickup call `0x6c5f3e` targets `0x6bcf80`. Server soldier vtable checked against `0x1424850`. Lookup checks pickup entries first (`ServerPickupEntity` vtable `0x1424358`), then falls back to the full scan by type marker plus name. The give runs once on the game thread after the input update returns, refuses and logs if the thread isn't the engine's give thread, and the log records 12 return addresses. Ammo override matches the ground pickup: reserve `0x6cd850`, then loaded `0x6cd520`, both sites byte-checked. A slot that already holds the weapon is skipped ("already held"); only then does the selector `0x6d7c80` run, after a 23-byte check. Example cfg `ranges/sp02-xm8-spas.cfg`. Clean clang compile check, not run in game. Answers from static disassembly: (1) thread: run the give on the server-sim thread; which thread that is in SP is unproven, so confirm on the PC by breaking at `0x6c5cd0` during a pickup. Never call it from inside the replaced item's own Update hook: the give deletes that item (use-after-free). (2) `SPAS12_sp` in SP_02 is level data the exe cannot answer; found at runtime via live pickup entries, failing closed if absent; the offline package index on the PC can answer it. (3) Ammo and (4) selection: implemented as above (`selectNow=1` deploys and calls `0x6d7c80` itself) | Planning thread (cloud worker) | superseded by `test/range-merged` `33d8716` (staged copy of `9e3a566` in `%USERPROFILE%\fvr-testrange`, old files in `backup-before-9e3a566`) |
| Reload parity backlog (top items): P0 SPAS As2 consumer; pump via states 7/8; wider haptics; pouch hover; in-game charging handle; persistent dropped mags; sounds; dry-fire and abort rows. `feat/reload-parity` goes in after the first live run, deliberately kept out of PR #3 | Planning thread | open |
| Enable the engine `FiringFunctionData` reader in the DLL | Planning thread | open |
| Drive the `ChargingHandleRack` recognizer: needs an input and a state 7/8 driver | Planning thread | open |
| Feel-layer map | Planning thread | in progress |
| Engine mapping of the reload runtime path: `WeaponStateData`, `FiringFunctionData`, `ReloadBehavior`, `AnimationSoundMapData`, the live inventory component, an instant give-weapon write route, feel-layer hooks. BC2 doc to follow | Research thread | `docs/re/BC2_WEAPON_RUNTIME.md` on `main` via PR #2; cloud disassembly started (the owner OK 05:01Z) with the ground-pickup give path and inventory writers |
| Draft PR #4: give-path and firing-state findings in `docs/re/BC2_WEAPON_RUNTIME.md` with a new DIS evidence level; full reports `BC2_DISASM_GIVE_PATH.md` and `BC2_DISASM_FIRING_STATES.md`; PC asks rewritten as a verification order (Ghidra cross-check, then write-watch, then one supervised give); `tools/re/bc2re.py`, which reads the exe path from `BC2_EXE` (no binary committed) | Research thread | draft at `4446838`: soldier and weapon lookup (`6a49aa7`), SP start path (`6762285`), `newSP` fast start as LO and the correction that setter `0x4f4bd0` does not change EASTL string fields (`4446838`). Head `b1bed95`, which adds the CI-minutes change (push builds only on `main` plus `pull_request`; `docs/**` and `**/*.md` path-ignored; `workflow_dispatch` kept; a branch with no PR no longer runs CI). Waiting on CI, which is blocked by the repo-wide Actions failure (see blockers). The owner chose "Merge when green" (05:53Z decision card); the research thread merges it when green, then opens one follow-up docs PR off `main` with the rest of the findings plus `re/pc-give-path-ghidra` `307af24` |
| Research cloud workers: server soldier pointer and weapon-data lookup by name (feeds `Give.SlotN`), plus fast level-load | Research thread | in progress |
| Research parity mapping done: pump, dry fire, sounds, dropped mag, cutscene skip and checkpoint (see Findings). After PR #4 merges, one follow-up docs PR off `main` brings these in plus the PC's `docs/re` files | Research thread | mapping done; follow-up docs PR after PR #4 |
| Pin the per-shot ammo decrement (narrowed, see Findings): dump the shot listener vtable or write-watch firing+0x7c (needs a game slot); research is reading the listener implementations | PC builds and engine mapping session; research | open |
| Skip SP_02's opening cutscene `CS_11` so fast start reaches player control quickly (currently about 2 min). Candidate (static): call skipCutscene `0x73fcd0()` repeatedly from the UI hook after `newSP`; route B `0x886210` for non-skippable cutscenes (see Findings). Route A is pushed on `test/range-merged` `e9e9062`: `TestRange.SkipCutscene 1` keeps the UI detour after `newSP` and calls `0x73fcd0` every 30 UI updates for `TestRange.SkipSeconds` (default 180), after a 25-byte signature check (mismatch logs a refusal). It logs the first 3 calls and every 20th, then "skip: window over", and restores the slot. Live check: time-to-control. Backup: the native Continue path (newest profile save), since there is no checkpoint index | Planning thread; second PC session runs it | manual Esc skip verified live (the owner, 05:51Z). Route A ran live with `ceb23b5`: 660 calls over 99.6 s, `CS_11` never visible, likely works [inferred]. TODO: log the skip result and stop at gameplay start so the vtable restore runs early |
| Reverse the 12-byte `String` layout | Planning thread (PC session) | open |
| Live weapon catalog: list loaded `SoldierWeaponAsset` objects by `WeaponAssetName` | Planning thread (PC session) | open |
| Confirm the C1 kit slot swap on SP_02 | Planning thread (PC session) | open |
| Verify the test level and loadout routes in game, in the order listed at the end of `BC2_TEST_LEVEL_ROUTES.md`. All routes are unverified in game | Planning thread (PC session) | open |
| Pick the range level: a shipped SP level (AI off via `Server.IsAIEnabled` is a known crash), chosen by listing `Dist/win32/levels/*` on the PC | Planning thread (PC session) | open |

Research docs from PR #2, now on `main`:

- [BC2_TEST_LEVEL_ROUTES.md](BC2_TEST_LEVEL_ROUTES.md): ranked routes for fast
  level load, skipping movies, menus and cutscenes, and giving any weapon,
  with settings field offsets.
- [re/BC2_TYPEINFO_HARNESS_TYPES.md](re/BC2_TYPEINFO_HARNESS_TYPES.md): full
  type tables for the harness types.
- [re/BC2_TEST_LEVEL_COMMUNITY_SOURCES.md](re/BC2_TEST_LEVEL_COMMUNITY_SOURCES.md):
  community sources for BC2 settings, levels and movies.
- [re/BC2_WEAPON_RUNTIME.md](re/BC2_WEAPON_RUNTIME.md): ownership chain,
  firing object, state table, per-weapon reload data, feel hook table and
  gaps G1 to G10, with evidence levels.
- [re/BC2_GIVE_WEAPON_PC_ASKS.md](re/BC2_GIVE_WEAPON_PC_ASKS.md): PC
  disassembly asks for the native give-weapon path.

## Findings we need to use

Reload, compared with KF2 VR and AS2:

- FrostbiteVR has no chambered round and no slide lock.
- Releasing a magazine cancels the insert.
- No dropped magazines, no reload audio, no hints.
- Ammo is credited up to about 2.8 s after the magazine seats.
- The seat haptic is produced (`ReloadInsertion.h:346`) but never delivered
  on `main`; `feat/feel-wiring` drains it to the host.
- Only the XM8 and SPAS-12 have physical-reload profiles.
- FrostbiteVR handles the engine more safely than KF2 VR does.

Engine:

- October 8 engine map ([re/BC2_ENGINE_MAP_20261008.md](re/BC2_ENGINE_MAP_20261008.md)),
  extending #4's reports:
  - a unique-signature index on `c603f686`
  - live behaviour of the three firing copies: client B (+0x40) is re-synced
    by the engine, and the server copy can auto-reload alone
  - the empty auto-reload needs no input; ctx+0x28 (sprint) is the only
    per-call lever and it also blocks our own Reload pulse
  - `+0xa8` bit3 is non-finite ammo, not a reload switch
  - live hold and seat-advance delta behaviour
  - the Ant dispatch lock (`0xf4fb50`, CS `0x18a4bc0`, TLS `[0x1502e04]`)
  - palette packer rebase during reload animations
  - 64-bit magazine profile keys
- Research offline mapping: the engine has no chamber or slide-lock state.
  Each weapon has only two counts, rounds loaded and rounds in reserve, so
  chamber and slide lock are modelled VR-side, as the feel layer does.
- The engine broadcasts notifications for reload start/end and pump cycles.
  These are the cleanest hook points for the feel layer; none are wired yet.
  Unhooked native listener callbacks for reload begin/end, pump and shot:
  `0x8024a0`, `0x80f640`, `0x80f6b0`, `0x6ddb30`, `0x6ddb60`, `0x6dd960`.
  These and the `FiringFunctionData` fields went to the feel-wiring worker
  as hook candidates and data-driven per-weapon defaults.
  ABIs (PC): `0x8024a0`/`0x80f640`/`0x80f6b0` are thiscall void with
  once-per-arm bits at +0x55d, vtable-only; `0x6ddb30`/`0x6ddb60` are
  thiscall(bool notify) `ret 4`; `0x6dd960` is thiscall(float, ShotInput*)
  `ret 8`. `0x6e6994` is the jump table of Step `0x6e62d0`; states 3 and 4
  are no-ops.
- Nothing in the repo adds a weapon to the live inventory yet. The stock
  ground pickup, which swaps the gun in place, led to the native give path.
- Native give/replace-weapon function `0x6bcf80` (cloud static result,
  confirmed by a static PC cross-check; live check pending):
  - Signature: `83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84
    BE 00 00 00`.
  - ABI: thiscall on the server soldier, `(SoldierWeaponData*, uint slot,
    bool selectNow, bool forceSlot)`, `ret 0x10`. Returns the new server item.
    `data=NULL` removes the weapon. Ammo is filled to full natively via
    `0x6dd580`.
  - Callers: spawn loop `0x6bd350`, unlocks `0x6bd480`, ground pickup
    `0x6c5cd0` (which then sets ammo via `0x6cd850` reserve and `0x6cd520`
    loaded).
  - The client gets the gun by ghost replication: `0x8bb0c0` → `0x8bad10` →
    `0x8baab0`.
  - Selection setter `0x6d7c80`: thiscall on the inventory (soldier+0x2b4),
    `(int slot, bool validate)`, `ret 8`.
  - Gap: the server writer of the replicated weapon-state block.
  - Harness: the give is one call, `0x6bcf80(data, slot, true, true)`, on the
    game thread. It still needs a `SoldierWeaponData*` lookup by name.
  - PC static cross-check (capstone on exe `c603f686`): the signature is
    unique; `ret 0x10` and the args `(data, slot, selectNow, forceSlot)` are
    confirmed. The inventory vector is at soldier+0x2b8/+0x2bc. The new item
    is `operator new(0x24)` plus ctor `0x561b20(data, soldier)`, stored in the
    soldier vector; the soldier owns it and the returned pointer is borrowed.
    The replaced item is deleted via its `vtable0`. `0x6e6994` is a jump
    table, not code: 16 weapon states, a switch on weapon+0x3c in `0x6e62d0`.
    Full notes will land in `docs/re` from the PC session.
- Soldier and weapon lookup (research cloud static result; PR #4 `6a49aa7`,
  `docs/re/BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md`):
  - Local server soldier: `*(0x1566b18+0x2c)` is the `ServerPlayerManager`;
    players at +0x58..+0x5c. Take the non-AI player (+0xd84 == 0, INF); its
    soldier is at +0xc3c. Validate with soldier vtable `0x1424850` and
    soldier+0x220 == player.
  - Weapon by name: SP checkpoint restore `0x555530` already resolves
    `WeaponAssetName` and gives it via `0x6bcf80`. Pattern: resource manager
    `*(0x154c444)` → loaded check `0x465d10(*(RM+0xa4), name)` (no I/O) →
    typed request `0x449810` with `SoldierWeaponData` TI `0x1bfca60`. The
    harness can copy this; keep the resource ref alive while the item exists.
  - Correction: the pickup handler starts at `0x662f20`, not `0x662ef0`.
  - Still open (PC): the give thread; break at `0x6c5cd0` during a pickup.
- Give PC cross-check details (`54ca94c`): `selectNow` is forced on if the
  replaced item was selected; `forceSlot=false` reuses an existing slot that
  holds the same data; `data=NULL` frees the item and selects slot 0. Item
  vtable `0x14172f0`. Firing object: `new(0xac)`, ctor `0x6f1780`, vtable
  `0x1425814`. Request `0x449810` is thiscall(ref, name, 0, 0, TI
  `0x1bfca60`, false) `ret 0x14`; the native restore releases the ref after
  the give, while the harness keeps it.
- The server item-count getter is at `0x696bc0`.
- Firing-state machine fully decoded (research thread, cloud static, table
  spot-checked). States: 0 deploy, 1 post-reload wait, 2 ready, 3 pre-fire
  delay, 4 zoom-before-fire (likely unused), 5/6 shot, 7/8 bolt, 9 auto,
  10–12 reload, 13–15 hold-and-release fire. Feel events:
  - Reload begin: Commit 2→10. Reload end: Commit 12→1.
  - Mag seat / shell in: Transfer `0x6d6f40`.
  - Aborts: Commit 10/11→1 (sprint), or Deactivate `0x6e1eb0` (switch;
    bypasses Commit).
  - Bolt/pump: 6→7→8→1. Shot: `0x6dd960`.
  - No mag-out or charging-handle engine events; these are modelled VR-side.
  - Per-shot decrement, narrowed (PC, `re/pc-give-path-ghidra` `42d6249`):
    Step states 6/9 bracket firing+0x84 (shots pending) around notifier
    `0x6dd960`. The shot is consumed in a listener's vfunc +0xc (float, bool
    tracer, ShotInput*) for each entry of [firing+0x18, +0x1c), so the
    loaded decrement is probably in that listener. `0x6d6eb0` is CanFire;
    `0x6d6ee0` is the mag-not-full check.
- There is no physical pump input: native pump states 5/6 → 7 → 8 only
  follow a shot.
- Fixed in `0e8f838`: `spas-shell-reload.json` required a `capture` row that
  live SPAS logging never writes (closest is the `reload_capture` haptic
  row). Seat and credit come from the submit path, not `GunFvrTracker::Shell`.
- BC2 ships full engine class and field metadata. FrostbiteVR read it for only
  about 35 classes. Key classes: `WeaponStateData`, `FiringFunctionData`,
  `ReloadBehavior`, `AnimationSoundMapData`, plus sound and Havok physics.
- No `Cheat*`, `Console`, `Inventory`, `Loadout`, `SubWorld` or `MapList`
  types exist in this build. Every fast-start lever is a field on
  `ServerSettings`, `ClientSettings` or `GameSettings`.
- BC2 has no console, so forcing weapons means memory writes that are not yet
  proven.

Test level and loadout (PC, 2026-10-08):

- `-Client.Level Levels/SP_02 -Server.Level Levels/SP_02 -Client.LoadMenu false`
  on the command line is ignored. The game reaches the main menu after about
  8 s.
- WinMain (`0x401f00`) parses only `-runMultipleGameInstances`, `-dedicated`
  and `-server`. `-runMultipleGameInstances` skips the single-instance check.
- Static RE of 795745 (planning thread): command-line values are looked up
  only by exact name (`0x4f4cf0`, 41 callers, none dotted), so `-Client.Level`
  and `Win32Game.cfg`-style args do nothing. Settings registry: pointer
  `0x155cfdc`, dotted lookup `0x4f4600`, override queue applied as each group
  registers at `0x4f4a00`. Its feeders, the text loader `0x5017c0` and
  add-override `0x501ab0`, have no callers.
- `ClientSettings.Level` defaults to `"Levels/SP_01"`. SP starts only via
  the menu command `newSP` (UI cmd 132 → `0x7600f0`), which loads whatever
  `Client.Level` holds.
- Start kits: SP_01 has knives only. SP_02 onward has `XM8_sp_s`, XM320, laser
  designator, knife and grenade. The SPAS-12 only comes from the "SPAS12 and
  Grenade" pickup.
- Fastest proven state today: SP_02 with the XM8.
- Verified fast-launch baseline (planning thread, posted for the owner):
  `test/range` `84cb8d8` `tools/testrange/Start-BC2TestRange.ps1` with
  `-GamePath`, `-Bin` and `-InjectDelayMs 3000`. cfg: `Client.LoadMenu`
  false; `Client.Level` and `Server.Level` `Levels/SP_02`;
  `Server.IsAIEnabled` false (bools proven as 0/1). All four settings apply
  and read back live. The game still stops at the main menu, so New Campaign
  is a manual click.
  The verified cfg is `tools/testrange/ranges/sp02-menu-settings.cfg` on
  `test/range` `c885fdb` (bools as 0/1); `docs/BC2_TEST_RANGE.md` on
  `tools/test-range` `32b0126` has the section "Verified baseline: settings
  verified; level start still manual". Both are being merged into PR #3.
  The baseline doc was corrected on `tools/test-range` `bcb6c71`; both refs
  (`test/range` `1d19bac`, `tools/test-range` `bcb6c71`) are going into PR #3.
  Update: the baseline is now "level start native, no click" (see the fast
  start finding below), code on `test/range` `1d19bac`. Leave
  `Server.Level` and `Server.IsAIEnabled` out of the fast-start cfg: setting
  both (AI false) crashed the game about 7 s into the load (known crash). `docs/BC2_TEST_RANGE.md` on
  `tools/test-range` `5963b30` has the section "Verified baseline: fast start
  into gameplay (no clicks)", being merged into PR #3. The planning thread
  posted a FrostbiteVR-independent, paste-ready note for the mod author.
- Fast start (PC, `docs/re/BC2_FAST_START_NEWSP.md` on
  `re/pc-give-path-ghidra` `034f675`):
  - Inject with `CreateRemoteThread` + `LoadLibraryW` about 3 s after a
    no-args launch.
  - Settings: `SettingsManager::Set` `0x4f4bd0` on `*(0x155cfdc)`. `Set`
    ignores string fields, so `Client.Level` is written by swapping a fresh
    eastl string (`0x4014c0`) into the field returned by `Get` `0x4f4c40`.
  - Hook: `UIManager::update` is vtable slot 28 of vtable `0x14407f0`
    (entry `0x1440860`, holds `0x959900`), thiscall with two stack args
    (`ret 8`). A one-arg detour crashes at the menu.
  - Trigger: after 60 frames with uimgr+0x82b24 (main menu) non-null, call
    `newSP` `0x7600f0` thiscall(int difficulty=1) `ret 4`. `newSP` fires
    4.3 s after launch; player control takes about 2 min, mostly SP_02's
    opening cutscene.
  - Signatures verified unique on `c603f686`, except `Get`'s 8-byte stub.
  - Gap: the vtable hook is never restored (AGENTS.md "restore native
    state"); still missing in `1d19bac`; the planning thread owns the fix.
  - Known issue: the staged `9e3a566` give DLL made BC2 exit about 6 s after
    launch on the PC (00:44 local run, possibly also 00:42). Inferred cause:
    a cfg mismatch. `9e3a566` passed `TestRange.*` keys to `GetByName`
    `0x4f4c40` and `Set` as an unregistered engine group; its log has only
    the start line and the give never ran. `33d8716` strips `TestRange.*`
    before `ApplySettings` (cpp:643-650).
- SP_02 level data (PC, `docs/re/BC2_SP02_LEVEL_DATA.md` on
  `re/pc-give-path-ghidra` `307af24`):
  - SPAS asset `Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12`,
    `SoldierWeaponData` `bf721205-c6fa-431d-b3cb-ac3783cc243e`, in
    `sp_common`. SP_02 places `Pickup_Weapon_SPAS12` twice, so it is likely
    resident (INF).
  - Player kit (`marlowe.dbx`): slot0 `SP_rgl_XM8_Scoped`, slot2
    `SP_rgl_XM320_Scoped`, slot4 laser dummy, slot7 knife, slot8 grenade.
  - Other SP_02 pickups: AEK971+GP30, F2000, M60, M95, MG3, Grach, TOZ194,
    QBU88, QJY88, SCAR-L, AKS74u, USAS12, AN94, M16.
  - Correction: the short names in `ranges/sp02-xm8-spas.cfg` (`XM8_sp_s`,
    `SPAS12_sp`) are fine; the give matches `SoldierWeaponData.Name`, the
    full `WeaponAssetName` path, or its last segment.
  - Opening cutscene `CS_11` (`IsSkippable=1`): OnPlayerEnter → Run Once At
    Start → Save "Initial Save" → `CS_11.StartCutscene`. `CS_11.OnFinish` →
    Save "save post start cutscene" → controls on and the XM8 pickup.
  - Routes past it: (a) native skip of `CS_11`; (b) load the "save post
    start cutscene" checkpoint; (c) the authored `DebugMenuItemEntityData`
    warps "1 - First Action" … "5 - The Rail Sequence" (no retail consumer;
    dead data, question closed). There are 12 `SaveEntityData` checkpoints. AI-off does not
    stop `CS_11`.
- Cutscene-skip candidate (research, static only, bytes verified):
  - Manual: Esc (the menu concept) during a cutscene skips it natively when
    `IsSkippable` (`CutsceneEntityData`+0xcd) is set; `CS_11` has it set.
    **Verified live:** the owner confirmed at 05:51Z that Esc skips `CS_11`.
    Route A (`0x73fcd0`) is still pending its live run with `ceb23b5`.
  - DLL route A: call `0x73fcd0()` repeatedly from the UI hook after `newSP`.
    It takes no args; it is the skipCutscene UI command and queues message
    0x12/0x1b. It is ignored until a cutscene is playing.
  - DLL route B, for non-skippable cutscenes: capture the entity via
    onMessage `0x895a90` (entity = this-0xdc), then call requestSkip
    `0x886210(entity)`, which sets entity+0x100 and bypasses the flag.
  - The checkpoint-start worker is still running. Owner for wiring the skip
    in: the planning thread.
  - Known crash: `Server.Level` plus `Server.IsAIEnabled=false` crashed the
    game about 7 s into the load (null `this` at `0x612700`). AI-off is
    unsupported in SP (see below).
- Research static findings, not yet run in game (follow-up docs PR after
  PR #4):
  - `newSP`'s int is difficulty (0/1/2), not a checkpoint.
  - Checkpoint start: there is no checkpoint index. The native Continue path
    loads the newest profile save: `querySavedGames` `0x76ae90`, then
    `continueSP` `0x75ffa0`, or a replayed type-4 request. This is the backup
    to the `CS_11` skip.
  - Checkpoint fallback detail: no load-by-name. Each profile has one save
    slot that every save overwrites, and the checkpoint is the entity states
    in the save body. A start loads a save only via request type 4
    (`continueSP`) or type 7 (`doLastSavePoint`). Harness route: write "save
    post start cutscene" once, capture its type-4 request plus the blob, and
    replay it with `0x75df60`/`0x956e10`. The replay needs the same game
    version and `IsAiEnabled` value as the capture.
  - AI-off crash (INF): `IsAiEnabled=false` skips creating the AI system;
    `0x6908d0`, `0x70c120` and `0x72d560` then call through a null
    `[0x18903a8]`. Treat AI-off as unsupported in SP.
  - Pump: hold state 7 in the existing Step hook, mirroring native 7's own
    hold path on all three copies, and release at the rear stop.
    Corrections: Step writes timer=dt on entry, and the SPAS never holds 7
    natively.
  - Dry fire: the native click fires only when fully empty, so detect it in
    the Step hook. The native sound is FFD+0x194 `OutOfAmmoSoundEffect` via
    `0x6ebc30`.
  - Dropped magazine: render it as a mod prop, not a native entity. There is
    no magazine-only mesh asset; the magazine is a bone of the weapon's
    skinned mesh. The native runtime entity factory is mapped (server
    `0x6f67d0` → `0x71dd00`; client `0x874af0` → `0x8b8360`; destroy
    `0x62d810`). A client-only `ClientDynamicModelEntity` with physics is
    feasible but would show the wrong mesh. Recommendation: reuse the
    existing body-ammo prop renderer with a simple fall/rest, and keep the
    native route disabled.
  - Native weapon sounds: the empty click is FFD+0x194, played natively by
    `0x6ebc30(effects+0x104)`. Weapon data has no reload, mag or bolt sound
    fields; those come from the level's `AnimationSoundMap`, one sound per
    animation clip. So mag-out and mag-in are one baked sound per reload clip
    (INF), while the pump and shell-insert clips have their own sounds.
    Play/stop primitives: `0x637d50`, `0x637290`, `0x633cb0`; the manager is
    at `*(*(0x1571410)+0x70)`. To capture the GUID→sound map, or to mute
    native reload audio, hook the clip-start handler `0x881650`.
- Native SP start path (research static map; PR #4 `6762285`,
  `docs/re/BC2_DISASM_SP_START_PATH.md`; signatures unique; `0x7600f0` and
  `0x77bc10` checked):
  - `newSP` `0x7600f0` is thiscall with FE = `[[0x19a4ca8]+0x82b24]`,
    `ret 4`, and reads `Client.Level`. It issues UI request type 3, handled
    by `0x951e00`, which posts message 0x28/0x10 with "Levels/"+name.
    Client `0x56a890` queues state 5; `0x565450` calls `Main::startServer`
    `0x54b0b0`; the `ServerMain` ctor `0x756d40` calls `0x858900`, which sets
    Server+0x74.
  - Alternative: the front-end dispatcher `0x77bc10(FE, "startSingleplayer",
    "SP_02")`.
  - `Client.LoadMenu=false` skips the menu but starts nothing. Intro movies
    are driven by `0x760270` and the `legalDone`/`skipMovie` UI commands.
  - Likely dead (no readers): `DisableMovies`, `IsFastStartupEnabled`,
    `DisableMenuFlow`, `DisableCutscenes`.
  - The `newSP` step is confirmed live by the planning PC session's fast
    start (`0x7600f0(1)` after 60 menu frames).
- FrostbiteVR has no give-weapon route, and its ground pickup is hard-wired
  off.
- Chosen settings route: Route B (primary) writes `Client.Level` and turns AI
  off at the menu, then fires `newSP`. Route A (early inject/add-override
  through `0x501ab0`) is not viable. Synthetic menu clicks were dropped.
  Other levers (`DisableCutscenes`,
  `DisableMovies`, `UnrestrictedUnlocks`) are fields on `ServerSettings`,
  `ClientSettings` and `GameSettings`; registration stubs `0x534600`,
  `0x534680`, `0x534700`.
- No give-weapon command exists anywhere (no console, no RCON, no `Cheat*`
  types). Route: kit patch before spawn (`SoldierEntityData.Weapons@0x1b0` /
  `WeaponUnlocks@0x1a0`) or pickup patch
  (`WeaponPickupEntityData.Weapons@0xa4`), then the repo's native switch
  route.
- Give-weapon route C1 is the owner's preferred approach for testing. Owner:
  planning thread.
  1. Resolve the weapon by name from weapon data already in memory (for
     example `SPAS12_sp`, `AEK971_sp`).
  2. Before spawn, write its pointer into `SoldierEntityData.Weapons@0x1b0`
     for the chosen slot.
  3. Select it with the repo's existing weapon switch.
  4. Restore the original pointer when the test ends.
  5. Expose it as a harness method: "give weapon X in slot Y".

  Limits: 127 weapon definitions load with the shared SP data, and guns
  outside that set need their data loaded. A kit change applies only on
  spawn, so each new gun needs a respawn or checkpoint reload. An instant
  swap would need writes to the live inventory list, which so far is mapped
  for reading only.
- Reload tests must leave `ServerSettings.HasUnlimitedMags` off, because it
  hides the reserve accounting the test checks.
- Disproved: `Win32Game.cfg` as a way to pass settings. The R34 server reads
  launch args from `bin/Win32Game.cfg` (`Main.cpp:635`), but the client only
  looks up exact, undotted names, so dotted settings there do nothing.
- SP levels: `sp_01`..`sp_04`, `sp_003_b`, `sp_004_b`, `sp_005`, `sp_005_b`,
  `sp_006`..`sp_009`, `sp_008_b`. Naming mixes 2 and 3 digits, so exact names
  matter. No test or empty level ships, so the range will be a shipped level.
  Turning AI off via `Server.IsAIEnabled` is a known crash (see fast start).
- Intro movie skip: rename `Output\win32\movies\ea_logo_hd.res` and
  `dolbydigital.res`.

## Open questions and blockers

Blocker: 8 merged branches need deleting (`ci/linux-sanitizers`,
`feat/feel-wiring`, `feat/gun-event-emitter`, `feel/chamber-and-insert`,
`integrate/feature-stack`, `tools/gun-event-log`, `tools/replay-harness`,
`tools/typeinfo-dump`). The proxy refuses deletes (403), so the owner has to
delete them or enable "Automatically delete head branches".

Blocker: GitHub Actions has failed repo-wide since 05:52Z. Runs 118–122 on
`main` and PR #4 `db1ad05` fail in about 2 s with runner_id 0 and no logs.
The owner confirmed the cause is the billing/spending limit. PR #4 (`b1bed95`)
cuts CI minutes. PR #4 and PR #6 are waiting on it.

Blocker: the live XM8 replay tape is held and never starts because the
reserve lease never verifies (see In progress). The same lease gates the
physical mag reload.

The owner approved the UI focus fix for the live replay runner (05:44Z), which
unblocks `run.py` needing BC2 in the foreground.

Give-weapon test range: `test/range-merged` `ceb23b5` ran live; fast start
and cutscene skip work, but the give never ran because of its thread gate
(fix owned by the planning thread).

Open questions:

- Does the C1 kit slot swap spawn the soldier holding the target weapon, and
  does the original kit come back after restore?
- Which shipped level is the emptiest and fastest to reach player control? Answer by
  listing `Dist/win32/levels/*` on the PC.

## Key paths and facts

| Fact | Value |
| --- | --- |
| Repo | the private development repository, canonical branch `main` |
| Reference repos | `Kvasir94/Killing-Floor-2-VR` (KF2 VR mod, extracted AS2 data) |
| FrostbiteVR checkout on PC | `<repo>` |
| PC launch notes | `Start-NativeTrace` under PS 5.1 inherits pwsh 7's `PSModulePath` and loses `Get-FileHash`: reset `PSModulePath`. A failed tape load leaves the probe DLL resident, so each attempt needs a fresh BC2 |
| PC staging folders | planning PC session: `%USERPROFILE%\fvr-testrange-mine`; the second PC session stages only to its own folder |
| PC build worktrees | `<old-worktree>` (PR #3 build the owner launches); `%USERPROFILE%\worktree-feel` (`feat/feel-wiring` XM8 smoke test) |
| Default PC device (environment) | `env_015TMjHBvWMGzV3c7oKpkmpm` (set 2026-10-08) |
| BC2 build | 795745 (x86, single-player, DX11) |
| Pre-patch backup | `<backup>` (exe and `settings.ini.bak`) |
| Typeinfo dump | `profiles/typeinfo/795745/bc2_types.json` on `tools/typeinfo-dump` |
| Free disk on PC | C: about 14 GB, D: about 76 GB (corrected 2026-10-08; earlier note said about 6 GB) |
| Branches | `tools/typeinfo-dump` `108a59b`; `tools/gun-event-log` `10dd524`; `feel/chamber-and-insert` `fadf199`; `ci/linux-sanitizers` `c104808` (PR #1, merged as `3eca473`); `test/any-loadout` `230a6e3`; `claude/fast-test-level-research-mij58q` `5e775a6` (PR #2, merged as `dd37868`); `integrate/feature-stack` `bd878fe` (PR #3, frozen); research disassembly branch at `ab6390b` (draft PR #4); `bc2/tools/replay-harness` `25d10a6`; `feat/gun-event-emitter` `fe88bde`; `feat/feel-wiring` `0e8f838`; `test/range` `1d19bac` (PC experiments); `tools/test-range` `bcb6c71`; `feat/spas-sim` `081c5ed` (WIP, parked); `integrate/feature-stack` and `feat/reload-parity` `32b209e`; `feat/range-give-weapon` `33d8716`; `test/range-merged` `9d32297` (single test-range DLL, draft PR #5); `re/pc-give-path-ghidra` `42d6249` |
| Weapon slots (`WeaponSwitchingEnum`) | `wsPrimary1=0`, `wsPrimary2=1`, `wsGrenadeLauncher1=2`, `wsGrenadeLauncher2=3`, `wsStaticGadget=4`, `wsDynamicGadget1=5`, `wsDynamicGadget2=6`, `wsMeleeWeapon=7`, `wsHandGrenade=8` |
| In-gameplay menu state | `MenuIdEnum.MiePlaying=4`; wait out or fail on `MieMovie=14`, `MieLetterBox=13`, `MieSpawnMenu=2`, `MieCreateSoldier=18` |
