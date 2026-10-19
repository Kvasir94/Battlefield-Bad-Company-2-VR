# FrostbiteVR (BC2 VR) — test tooling survey, Linux run results, critique, harness plan

Repo: `/home/claude/src/FrostbiteVR`. All runs were done on a copy at `/home/claude/scratch/fvr-copy`
(Linux, GCC 13.3, CMake 3.28, Python 3.13). The original repo was not modified.

---

## 0. Actual results (TL;DR)

| Layer | Command | Result on Linux |
|---|---|---|
| Python unittest (CI pattern) | `python3 -B -m unittest discover -s tests -p 'test_*.py'` | **785 run, 763 pass, 22 errors, 0 failures** (31 s). All 22 errors are `AttributeError: module 'ctypes' has no attribute 'WinDLL'`: Windows-only process-reading code (`tools/read_bc2.py`), not logic failures. |
| Python, not matched by CI pattern | `python3 -B -m unittest tests/ManualReloadCatalogTests.py` | **24/24 pass**. Note: CI's `test_*.py` glob **never runs this file**. |
| C++ configure | `cmake -S . -B build` (GCC) | **Configures cleanly**. `WIN32` blocks drop the D3D11, OpenXR, frame-channel and ModuleAbi targets. |
| C++ build | `make -k -j4` (~57 s) | **159/163 test executables build.** 4 fail one `static_assert(sizeof(NativeProbeConfig)==1184)`, because `wchar_t` is 4 bytes on Linux and 2 on Windows (`src/games/bc2/NativeProbeConfig.h:57-64`). |
| C++ ctest | `ctest -j4` | **159/159 built suites pass** (1.7 s total). |
| The 4 ABI-blocked suites | rebuilt with `-fshort-wchar` | `Bc2MagazineReloadProbe`, `Bc2PhysicalReloadProbe`, `Bc2PhysicalReload`, `Bc2ReloadHold`: **all pass**. So **163/163 portable suites pass on Linux.** |
| ASan + UBSan (GCC, `-fshort-wchar`) | full build + ctest (~1.5 min) | **161/163 pass, no UB reports.** 2 ASan hits (`Bc2EmptyFireProbe`, `SceneRecoveryProbe`) are `wcslen` global-buffer-overflow caused by my `-fshort-wchar` hack (glibc expects 4-byte wchar). They are not real bugs. |

Bottom line: the "Windows/MSVC" test suite is in fact almost entirely portable. The pure
interaction, math and BC2 policy layers (including all ~64 reload/magazine/hand/grip/ammo suites)
compile and pass on Linux. Only real D3D11, OpenXR, IPC and module-ABI code is Windows-bound.

---

## 1. Inventory: what each layer verifies and what it cannot

### 1.1 C++ deterministic unit/component tests (`tests/*.cpp`, `tests/Test.h`)
- **Framework:** `tests/Test.h` is 5 lines: a `CHECK(x)` macro that prints file/line and
  `return 1`, plus `Near(a,b,eps)`. Each suite is its own executable whose `main` calls
  `int Foo(){...; return 0;}` functions. There are no fixtures, parametrization, test
  filtering, per-case names in output, or JUnit XML.
- **Scale:** ~150 `.cpp` suites, about 24k lines, **9,780 `CHECK(` sites**. Shared headers:
  `*Fixture.h`, `*.inc` check bundles, and captured-data headers such as
  `CapturedSpasPumpMotion.h`, `CapturedAekEquip.h`, `CapturedPickupMatrices.h`,
  `SpasAxialMotion20261002.inc`, and the generated `Bc2MagazineRegistryFixture.generated.h`.
- **Registration (`CMakeLists.txt`, 561 lines):** `include(CTest)`. One big
  `foreach(name ...)` at line 167 registers about 130 suites linked against
  `FvrCore`/`BC2Camera`. Explicit `add_test` blocks add the rest. Windows-gated groups:
  D3D11 rigid prop/body-ammo renderer, frame/menu/feedback channels, OpenXR
  feedback, ModuleAbi, weapon-draw capture CPU, and the x64-only OpenXR fixtures. The docs
  say 186 suites run per architecture on Windows; Linux registers 163.
- **Opt-in, not in ctest:** `RigidPropGpuTests` and `Bc2ReloadDrawCaptureGpuTests` (D3D11
  WARP) are build-only. `OpenXrPresentationTests`, `OpenXrRecoveryTests` and `OpenXrMenu*`
  run against the fake runtime with a real GPU through `Test-Offline.ps1 -Gpu`. The
  `D3D11HardwareBridge` probe runs only with `-DFVR_TEST_GPU=ON`.
- **Verifies:** the shared interaction state machines (`DetachableMagazine`,
  `ReloadInsertion`, `AmmoSupply`, `HandInteraction`, `SupportGrip`, `ManualReload`,
  `FeedMechanism`, `WeaponCycle`), rail/capture/seat geometry thresholds, claim/token
  ownership, deadlines and staleness, native-request/acknowledgement protocols against
  *modelled* native state, the probe state machines themselves (for example,
  `Bc2MagazinePhysicalProbeTests` drives the synthetic-player state machine offline), math
  (stereo, IK, pole vectors, optics), and IPC packet layout.
- **Cannot verify:** that the native BC2 offsets/hooks behave as modelled, real timing and
  threading inside the game, rendering, or anything about feel. Native state is represented
  by hand-built structs such as `s.native.acknowledgement = {...}`.

### 1.2 Python unittest suite (`tests/test_*.py`, ~776 `def test_` + 24 in `ManualReloadCatalogTests.py`)
Two families:
1. **Offline asset/pipeline tools** (`bc2_*`, `weapon_*_pipeline`, `build_*`,
   `package_preview`): mesh/Granny parsing, magazine descriptors, mechanism coverage,
   draw catalogs, and packaging determinism (same inputs give identical archive hashes,
   private data excluded). These run against synthetic byte fixtures, not real game files.
   One test can read the installed `BFBC2Game.exe` when `FVR_TEST_INSTALLED_BC2=1` is set.
2. **Auditor tests** (`test_audit_*`, `test_report_*`, `test_check_*`): each builds a
   synthetic trace dict (for example, `fixture()` in `test_audit_physical_reload.py`), then
   mutates it to confirm that the auditor rejects corruptions such as a missing ack,
   identity drift, reserve mismatch or fallback packs.
- **Verifies:** the evidence checkers are strict and fail closed, and the offline pipelines
  are deterministic.
- **Cannot verify:** the mod's runtime behaviour. These tests prove the *auditor* is
  correct, not the *mod*.

### 1.3 PowerShell launcher checks
- `tests/Test-PreviewLaunchers.ps1` (run in CI): parses the launcher scripts with the PS AST,
  then executes their parameter-prologue as a scriptblock to assert flag routing. Examples:
  experimental flags off by default, and fire-only boat mode rejected.
- `tests/Test-*Launch.ps1` (BodyHolster, BodyInventory, Optic, OwnedHostShutdown,
  PhysicalReloadRepeat, WeaponVisibility) and the root `Test-*.ps1` scripts (D3D11Bridge,
  FrameChannel, NativeMenu, NativeStream, NativeXrMuzzle, NativeXrRecovery, Offline) are
  **manual** wrappers. Most need a running BC2, a GPU, or both.
- `Test-Offline.ps1`: builds and runs ctest for x86 and x64 and writes a `summary.json` with
  explicit `native_rendering_tested=false` and `headset_tested=false`. `-Gpu` adds the
  fake-XR presentation/recovery fixtures and the cross-architecture GPU bridges.
- **Cannot verify:** game behaviour. These are wiring and smoke checks only.

### 1.4 GitHub Actions (`.github/workflows/build.yml`)
`windows-2022`, x86 and x64 matrix. The Python suite and launcher check run on x64 only,
then `Build.ps1` (Ninja RelWithDebInfo plus ctest), then a source-only package export.
Pinned action SHAs, 30-minute timeout. The workflow says outright: "No game, installed XR
runtime, headset, hardware-GPU probe … Passing CI is not native/headset proof."
Gaps: no Linux job, no sanitizers, no coverage, no artifact upload of test logs, and
`ManualReloadCatalogTests.py` is never run.

### 1.5 `tools/audit_*.py` live-trace auditors (~20) and `report_*`/`check_*`
Each one parses a native trace JSON plus manifest, completion, receiver and `pairs.jsonl`
from a real game run. They require exact chains: identity retained, native invocation
records with begin/end, server/client ammo conservation, a hold applied and restored,
zero palette fallbacks, all stereo pairs consumed, hooks drained, and binary SHA256s. The
output is a verdict string such as `bounded_simulated_sequence_verified`. This is the
project's real native-correctness oracle, and it is strong: it rejects "final ammo looks
right" as proof.

### 1.6 "Simulated player" in-game harness (docs `SIMULATED-PLAYER-197/200/201/202.md`)
- **Mechanism:** `BC2NativeIpcProbe` (`src/platform/windows/NativeIpcProbe.cpp`) connects to
  the injected adapter over the frame channel and feeds synthetic tracking/controller frames.
  The actual magazine choreography is a **closed-loop state machine compiled into the probe
  DLL**: `Bc2MagazinePhysicalProbe` (`src/games/bc2/Bc2MagazinePhysicalProbe.{h,cpp}`), with
  phases Warmup → ApproachMagazine → Grip → Pull → Release → Pouch → GrabReplacement →
  ApproachRail → Enter → Stroke → WaitAck → Done. It is enabled by config bit 0x4000000
  (`NativeProbeConfig.h:48`). It steers the *private synthetic input copy* toward targets
  that it derives from live contact geometry, and it fails on phase timeouts of 1.5–4.5 s.
  It never manufactures native acks.
- `tests/FakeOpenXrRuntime.cpp` (228 lines) is a test-only OpenXR loader with real D3D11
  swapchains. It is compiled into four variants (`FvrTestOpenXr` and
  `FvrNative{Campaign,Recovery,Support}TestXr`). Poses are scripted **by wall-clock ms**:
  for example, the right hand is offset +0.2 m during 9.5–11.5 s, and tracking is lost
  during 13.0–13.4 s.
- `Start-BC2VRTest.ps1`: starts `BC2XrHost.exe` and waits for `ready.json`, then
  `Start-NativeTrace.ps1` injects into an already-running BC2 in campaign gameplay.
- **Verifies:** one real-game XM8 magazine reload through real native consumers, with
  fast free-carry, wrist rotation and 0.60 partial grip. Evidence in run 202 includes
  7,218 retained native records and 22/191 → 30/183 ammo.
- **Cannot verify:** feel, haptics, visual quality in the HMD, arbitrary human gestures, or
  other weapons. The pass is per weapon and per scripted trajectory.
- **Important:** the run artifacts, launchers and even the `audit_simulated_reload.py`
  checker cited in docs 197/202 live in an **external `<local-recovery>/pipeline-runtime-20261005`
  root**. They are **not in this repo** (`tools/` has no `audit_simulated_reload.py`).
  The harness is therefore not reproducible from the repo alone.

### 1.7 Headset acceptance
Manual sessions are logged in `docs/HEADSET-*.md` and `RELOAD-*-HEADSET*.md`, with frozen
binaries identified by SHA256, a private evidence directory, and an explicit "tested with
failures" vs accepted status. AGENTS.md requires actual game and headset checks for any
headset/runtime claim. The 2026-10-05 session (`HEADSET-20261005-115245.md`) found
AEK/XM8 native reload continuing without physical insertion, plus SPAS support-hand
reacquisition failures. **No automated layer had caught either.**

---

## 2. Critique

### 2.1 Test quality
- **Mostly meaningful, not tautological.** The C++ interaction tests drive real policy
  objects step by step with explicit poses, timestamps and claims, and they assert
  intermediate phases, not only end state. `DetachableMagazineTests::RemovedMagazineCanMoveFreelyBeforeReplacement`
  is a good example: carry moves exceed rail step limits and the claim token must survive.
  Negative controls are common (stale ownership, NaN, sequence regressions, missing ack).
- **However, native behaviour is modelled by the test author.** Every C++ "native" ack and
  ammo transition is a hand-written struct. When the real game deviates, as with the native
  reload continuation seen in the headset run, the unit suite stays green. These are
  specification tests of the mod's own contract, not of BC2.
- **Fixture replay in some places:** `Captured*.h` and `SpasAxialMotion20261002.inc` freeze
  matrices from one capture. They are useful regression anchors, but they are single
  samples with no tolerance analysis across weapons.
- **Python auditor tests** are mutation tests of the checker against hand-built traces. They
  are good, but they test the oracle, not the system.
- **Readability and diagnosability are weak.** Dense one-line style, no test names in failure
  output (only `file:line: expr`), and the first failure aborts the whole suite function.
  Average file size is around 160 lines with 65 CHECKs per file.
- **Change-proofing narratives:** the docs repeatedly cite "baseline-failing shared tests". In
  other words, tests were written to reproduce field failures. This is good practice.

### 2.2 Coverage gaps
- No automated coverage measurement (gcov/llvm-cov) anywhere.
- No test of the *integration* of shared interaction + BC2 adapter + renderer over a long
  randomized session. Each suite is a short scripted sequence.
- No property-based or fuzz testing of the reload state machines, even though their
  robustness to arbitrary pose streams is the main quality bar for AS2-style feel. Examples:
  random grip flicker, tracking dropout mid-insertion, dual-hand contention.
- No visual oracle. The live probe checks frame copy, "distinct" and "changed" flags on
  sampled pairs (`NativeIpcProbe.cpp:369-376`), but not rendered content against
  expectations. There are no golden images and no RenderDoc captures; RenderDoc is not
  referenced anywhere in the repo.
- Only XM8 (and partly AEK/SPAS) have native simulated-player evidence. No per-weapon matrix.
- Belt-fed, pump, bolt, launcher and shell reloads have unit coverage but no simulated-player
  pass documented in the 197–202 series.

### 2.3 Flakiness risks
- The in-game harness is **wall-clock driven**: `GetTickCount64` windows throughout
  `NativeIpcProbe.cpp` (30 s/45 s budgets and pacing by `consumed*28000/targetPairs`),
  FakeOpenXr pose schedules in ms, and probe phase timeouts. Results depend on game frame
  rate, focus and load. Doc 197 records a failed `-01` attempt due to window focus.
- Cross-thread diagnostic contention: doc 202 had to add a 64-slot completion journal
  because Ends were dropped under gate contention, and 5 Begin-contention events remain
  unrecorded. Audit verdicts can flip to "inconclusive" depending on scheduling.
- Non-atomic late native reads (2 rejected in run 197) are tolerated by design, but they are
  nondeterministic.
- Process hygiene: a stopped mod leaves its DLL mapped, so a fresh BC2 is required for every
  run. That makes runs hard to batch.
- The offline C++ suites themselves look deterministic. They ran clean under ASan/UBSan, and
  most take ~0 s. The two slower ones are about 1–1.5 s.

### 2.4 Reproducibility of in-game automation
Partial. Inputs are synthesized from code, not recorded, so the trajectory is "the same
program" but not the same frames. Required external artifacts (recovery root, pinned
launchers, evidence JSON, `audit_simulated_reload.py`) are outside the repo. A campaign
checkpoint must be loaded manually ("Leave BC2 running in campaign gameplay"). There is
no single command that goes from a clean checkout to a native verdict.

### 2.5 Missing pieces for a reload-system dev loop
1. **Recorded controller input replay.** Real headset sessions are not captured as replayable
   `InputFrame` streams. There is `scene-input.jsonl` for neutral input only.
2. **A headless offline simulator** that composes `HandInteraction`, `DetachableMagazine`,
   `AmmoSupply`, `ReloadInsertion` and the BC2 adapter policies with a modelled native
   backend, so a recorded or perturbed session can be run in milliseconds on Linux/CI.
3. **Golden-image / visual regression** of the hand + magazine + weapon pose at key phases,
   either offline (rigid-prop CPU path) or as RenderDoc captures from the real game.
4. **Per-weapon parameterized matrices**, for example running the insertion policy against
   every `Bc2MagazineRegistry` profile.
5. **Feel metrics** as numbers: time-to-reload, capture tolerance envelopes, false-release
   rate under grip noise, and haptic event timing.
6. Linux/sanitizer CI and coverage reports.

---

## 3. Recommendations: harness for an AS2-quality physical reload

AS2-style reloads (free-moving magazine, magnetic-ish well capture, slide/bolt/pump
manipulation, per-weapon tuning) are dominated by **feel under noisy human input**. The
harness should make that measurable and fast to iterate, and keep the existing strict
native oracle as the final gate.

**R1. Make the portable core first-class (cheap, high value).**
- Fix the one portability blocker: static-assert `sizeof` per platform, or use `char16_t`
  for `NativeProbeConfig` paths. All 163 suites then build on Linux without hacks.
- Add a `ubuntu-latest` CI job: GCC + Clang, ASan/UBSan, `ctest`, Python suite with
  WinDLL tests skip-marked on non-Windows, and llvm-cov HTML uploaded as an artifact.
  Expected runtime is under 5 minutes.
- Fix the Python discover pattern (include `*Tests.py`) and the `test-temp/` written inside
  the repo root.
- Optionally move to doctest/Catch2 *in addition to* `CHECK`. Named cases, filtering,
  JUnit output and continuing after failure make CI triage much easier.

**R2. Input recording and replay (the key dev-loop tool).**
- Add an opt-in recorder in the adapter/host that writes every `InputFrame` (head, both hands,
  squeeze/trigger/buttons, tracking flags, sequence/observed/deadline ns) plus native
  reload-state samples to a compact binary `.fvrrec` with a JSON header that holds the
  weapon id, profile revisions and binary SHA.
- Write a `ReplayInputSource` for both (a) the headless simulator and (b) `BC2NativeIpcProbe`,
  so the same recording drives the live game in place of the scripted state machine.
- Commit a curated corpus of real headset reload recordings: fast reload, fumbled insert,
  dropped magazine, original-mag return, grip relaxation, tracking dropout, left-handed use.
  Every bug report from a headset session should yield a recording plus an expected verdict.

**R3. Headless reload simulator (`fvr_reload_sim`, Linux-portable).**
- Compose the real shared policy objects plus BC2 adapter policy with a *native model*: an
  ammo and animation-phase state machine that mirrors the audited native transitions. The
  model is fitted from the live trace JSON the auditors already parse. Run it deterministically
  on a virtual clock.
- Outputs: phase timeline, claims, requests and acks, prop roles, ammo, and the per-sample
  rail/position/angle errors that `Bc2MagazinePhysicalProbe` already rows. Write them in the
  same JSON schema as live traces, **so the existing `audit_*.py` tools run unchanged on sim
  output.** That gives one oracle for both sim and game.
- **Perturbation and fuzzing:** replay each corpus recording with jitter (pose noise, ±latency,
  dropped frames, grip noise around the 0.35/0.75 thresholds, hand swaps). Assert invariants:
  no reserve credit without seat, no native reload without physical insertion (the exact
  headset-0105 failure), claim exclusivity, and no stuck states. Add libFuzzer/RapidCheck on
  the `InputFrame` stream.
- **Model-vs-game drift check:** for every live simulated-player run, replay its recorded
  input through the sim and diff the audited event sequence. Divergence flags that the native
  model is stale. This closes the "unit tests modelled native wrong" gap seen on 10-05.

**R4. Per-weapon tuning matrix.**
- Parameterize the insertion/removal suites over all registry profiles (magazine, shell,
  belt, pump, bolt). Emit a capture-tolerance envelope report (approach angle × offset ×
  speed → captured/seated/rejected heatmap) per weapon, and fail CI on regressions versus a
  committed baseline. This is the numeric stand-in for "feels magnetic but not sticky".

**R5. Visual regression.**
- Offline: render key phases (gripped, half-out, free-carry, rail-entry, seated) with the
  existing CPU rigid-prop path (`RigidPropTests`) or WARP (`Bc2ReloadDrawCaptureGpuTests`)
  into PNGs, and compare against goldens with a perceptual threshold (Windows CI with WARP).
- In-game: script RenderDoc in-application API captures (`RENDERDOC_API_1_x`
  `TriggerCapture`) at probe phase transitions during simulated-player runs. Store `.rdc`
  plus extracted eye PNGs. Use them for review, and for golden diffs once camera/pose are
  deterministic under replay.
- Blender (the user has it plus the MCP): render reference poses of the BC2 meshes from the
  asset pipeline to sanity-check grip/magazine anchor geometry against the profiles the
  tests assert.

**R6. Make the in-game run one command and less timing-sensitive.**
- Bring `audit_simulated_reload.py`, the launchers and a non-private evidence schema into the
  repo. Add `Run-SimulatedReload.ps1 -Weapon XM8 -Scenario fast-carry|replay:<file>` that
  starts a fresh BC2 at a checkpoint, injects, runs, audits, and writes a summary.json verdict.
- Drive phases by **frame/sequence count and observed state**, not `GetTickCount64`, with
  wall-clock as a generous outer guard only. Pin the frame rate (fps cap) during probes. Run
  each scenario N times and report the pass rate to quantify flakiness.
- Nightly on a self-hosted Windows runner with the game installed (no headset): weapons ×
  scenarios matrix with trend dashboards.

**R7. Keep headset acceptance, but narrow and structured.**
- Each headset session must record `.fvrrec` input, plus a short checklist of feel questions
  scored 1–5 per weapon and haptic timing notes. Failures become replayable corpus entries
  (R2) before any fix, following the project's existing "baseline-failing test" discipline.
- Compare with the KF2 VR mod workflow: there, a measured reference mechanism plus short
  headset checks worked well (doc 197 cites it). Replay makes that loop much tighter.

### Suggested order
1) R1 (≈1 day) → 2) R2 recorder plus a replay hook in the existing unit fixtures →
3) R3 sim emitting audit-compatible JSON, with invariant fuzzing → 4) R4 matrix →
5) R6 one-command live run and frame-driven pacing → 6) R5 visual goldens/RenderDoc →
7) R7 structured headset loop.

---

## Appendix: reproduction commands (Linux)
```
cp -r /home/claude/src/FrostbiteVR /home/claude/scratch/fvr-copy && cd /home/claude/scratch/fvr-copy
python3 -B -m unittest discover -s tests -p 'test_*.py'   # 785 run, 22 WinDLL errors
python3 -B -m unittest tests/ManualReloadCatalogTests.py   # 24 OK
cmake -S . -B ../build && make -C ../build -k -j4 && ctest --test-dir ../build -j4   # 159/163, 4 build-blocked
cmake -S . -B ../build-sw -DCMAKE_CXX_FLAGS=-fshort-wchar && make -C ../build-sw Bc2PhysicalReloadTests ...  # 4/4 pass
```
Logs: `/home/claude/scratch/{py.log,build.log,ctest.log}`, `/home/claude/scratch/build-asan/asan-ctest.log`.
