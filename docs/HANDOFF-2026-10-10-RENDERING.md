> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Handoff: rendering wave, 2026-10-10

Orchestrated wave implementing [RENDERING-AUDIT-20261010.md](RENDERING-AUDIT-20261010.md).
The lane table and status are in [OPEN_WORK.md](OPEN_WORK.md).

## Decisions

- **Effects.** Mirror every native effect into both eyes and make
  post-processing correct per eye. Comfort tuning comes later, in the headset.
- **Approach.** Native-first: use the engine's own setters and per-view
  machinery, not after-the-fact patching. Engine facts come from Ghidra headless
  (`<kf2-repo>\third_party\ghidra\ghidra_12.1.3_PUBLIC`). Use a read-only copy of
  `D:\bc2-re\proj\BC2.gpr`.
- **Branches.** Each lane works on its own `render/*` branch, cut from local
  `main` at `3a89b55`. The orchestrator merges the branches into `main`.

## Paused for harness restart (2026-10-10, ~19:15)

All four lanes stopped at a safe point. Each one committed its work in progress
on its branch, with a `RESUME-Rn.md` note at the root of its worktree. None of
this is merged into `main` or pushed.

| Lane | Worktree (`<repo>\.claude\worktrees\…`) | Branch | WIP commit | State |
| --- | --- | --- | --- | --- |
| R1 | `agent-a3734f07b88f2759b` | `render/eye-parity` | `7e196dd` | RE findings and resume notes; code not started or partial (see RESUME-R1) |
| R2 | `agent-ac55e316b1144cd94` | `render/context-retarget` | `c3feabe` | ExactWriteBatch reuse API; first x86 build was in flight at the pause |
| R3 | `agent-a8b0f0abd05326a52` | `render/host-robust` | `6bef8cb` | Retention, pair reuse and async menu copy written, **uncompiled** |
| R4 | `agent-a61b500e91ea827f3` | `render/latency-quality` | `6788c02` | Late-latch pose lane, request prediction and FOV overscan, **uncompiled** |

**To resume:** start one agent per lane in that lane's existing worktree.
`isolation: worktree` would create a new tree, so do not use it. Each agent
reads its `RESUME-Rn.md`, checks out its branch, and builds before writing new
code. For reverse engineering, use the REA MCP; it is now installed and
registered (see the next section). Use the scratchpad Ghidra copy only as a
fallback.

**REA setup:** `rea-agents` 6.3.0 is installed globally with npm and registered
in Claude Code (`~/.claude.json`), Codex, Gemini CLI and Antigravity.
- Ghidra: `<kf2-repo>\third_party\ghidra\ghidra_12.1.3_PUBLIC`
- JDK 21: `<kf2-repo>\third_party\jdk21\jdk-21.0.12.1+1` (the JDK 17 on PATH is too old)
- `REA_GHIDRA_STARTUP_TIMEOUT_MS=3600000`, set because a cold CLI import of
  `D:\bc2-re\BFBC2Game_795745.exe` timed out at the default 330 s

It is not yet confirmed that REA can analyse x86 PE files with this release. Do
that first after the restart.

## Log

- `3a89b55`: audit and render pipeline RE committed.
- `3de7407`: AGENTS.md rule to use REA for RE.
- Restarted ~19:40: REA confirmed on the x86 exe (decompiles `0xbb4d80`). Lanes R1–R4 resumed in their existing worktrees, sharing one REA session with BC2 open. `GHIDRA_HEADLESS_MAXMEM=8G` added to the REA registrations; it applies from the next client start.
- `9f33bdf` R4 merged, `54a44fd` R2 merged, `0747589` R1 merged. Merged main builds clean on x86 and x64 (`build/orch`). Per-lane checks and report fields are in docs/re/BC2_LATE_LATCH_DEPTH_RE.md, BC2_CONTEXT_RETARGET_RE.md and BC2_EYE_PARITY_POST_RE.md. Frame protocol is now v7, so the DLL and the host must come from the same build.
- `d8f59b4` R3 merged. All four lanes are on main, and x86 and x64 build clean. Next: headset validation using the per-lane checklists, then the follow-ups in OPEN_WORK (depth submit, pipeline depth 2, shadow consistency, copy reduction, muzzle flash).
- R5 on branch `render/depth-submit` (cut from `1698da1`, not merged or pushed): depth submission and pipeline depth 2, both on by default. Frame channel is now **v8** (7056-byte mapping, two slots, per-slot props and depth attachment), so the DLL and the host must again come from the same build. Kill switches: `FVR_XR_DEPTH_SUBMIT=0` and `FVR_XR_PIPELINE_DEPTH=1`, honoured in either the game or the host environment. Built incrementally on x86 (`BC2NativeProbe`) and x64 (`BC2XrHost`) in `build/r5`. The original handoff claimed `FrameProtocolTests`, `AsyncFrameChannelTests` and `StagedFrameProducerTests` passed on both architectures; that historical execution is unconfirmed (see the recovery section below). Not run in BC2, and no GPU or headset test. Details: docs/re/BC2_LATE_LATCH_DEPTH_RE.md section 8, docs/FRAME_CHANNEL.md.
  - **Depth.** The game side copies the DSV bound at each eye's Draw return into a private typeless twin, converts it to R32F in a private context state, and publishes it as a second keyed pair, with near/far from view block `+0x1c/+0x20` in metres. The host enables `XR_KHR_composition_layer_depth` only if the runtime enumerates it, creates D32_FLOAT depth swapchains, draws the R32F depth into them as SV_Depth, and chains `XrCompositionLayerDepthInfoKHR` (0..1, nearZ/farZ in metres) only when the retained pair's own depth landed in both eyes. It falls back to color-only on setup failure, on a missing D32 format, or after 3 rejected EndFrames.
  - **Pipeline depth 2.** The host may issue a second request once the first is taken. The producer renders it into a second color (and depth) pair while the host still owns the first. Delivery is oldest first.
  - **Headset checklist (R5).**
    1. Host report: `depth_layers.extension/requested/active` are true, `setup_failure` is 0, and `pairs` and `frames` grow at about the presented rate with `missing` and `copy_failures` near 0. Native stream: `depth_submit.captured` ≈ 2×`published`, `capture_failures` is 0, `disabled` is false.
    2. Look around and strafe with the runtime's positional reprojection or motion smoothing forced on (SteamVR Motion Smoothing or the OpenXR toolkit). Near geometry (walls, the gun) should hold steady instead of warping. A uniform warp toward the face means bad depth: retest with `FVR_XR_DEPTH_SUBMIT=0`.
    3. Check the gun and hands at close range for tearing or halos under reprojection (first-person depth planes, see the RE doc).
    4. Pipeline: `pipeline.depth` is 2, `overlapped_requests` grows, the native `second_pair_captures` grows and `pairs_busy` stays near 0. Compare `fresh` per second in `frame_timing` against an `FVR_XR_PIPELINE_DEPTH=1` run, with the GPU state matched (both warm, card idle otherwise).
    5. There should be no new judder, no left/right eye mismatch and no black frames. Check `pair_reject_reasons` (Future and TooOld must not climb) and `relatch_rejected`.
    6. Toggle each kill switch on its own and confirm the old behaviour returns.

## R5 recovery and source review (2026-10-11)

The interrupted 24-path implementation was preserved in the integration owner's
verified recovery snapshot before this continuation. The earlier statement above
that all three CPU suites passed on both architectures was not backed by saved
test results; treat it as an unconfirmed historical claim. Saved x86/x64 compile
receipts were present.

Targeted review fixes:

- Preserve the copied pair's depth planes across a held color swapchain upload,
  and attach depth only after both color eyes and the matching depth eyes land.
- Refuse depth/body-prop reads once feedback returns the delivered lease, including
  acknowledgment deferred by mutex contention. A recycled producer pair must not
  remain accessible through an old ticket.
- Release runtime image DSV references before depth swapchain destruction. A depth
  release failure disables depth submission instead of retrying an unknown image
  state. Depth setup allocation exceptions can unwind to the host error handler.

Fresh checks: incremental x86 `BC2NativeProbe`, x64 `BC2XrHost` built using the
existing `build/r5` caches and two jobs. x64 `FrameProtocol`, `AsyncFrameChannel`,
and `StagedFrameProducer` passed 3/3 in 0.96 seconds. New cases cover optional and
invalid depth, late-latched depth identity, reads after feedback, two-slot
cancellation, slot reuse, and exactly-once GPU receipts deferred from visibility
to graphics. No fresh x86 CPU tests, GPU fixture, BC2, or headset run.

The first sandbox build failed with MSVC C1902; the same build outside the sandbox
passed. Actual `Local\KF2VR_NativeBuild` held through all build/test processes;
final release was `2026-10-11T04:21:38.2696767Z`. Logs and receipts are in
`build/r5/{x86,x64}`; the additional pre-edit source copy and lease log are under
`reports/renderer-resume-20261011` (local, ignored).

`BC2Observe` does not consume FrameChannel: it links BC2Profile/FrostbiteCamera
and reads native camera memory. No source change is needed there for ABI v8;
integration owns its coherent candidate build and provenance. DLL/host must be
paired. Both reviewed caches select checkpoint204 magazine geometry; x86 grip,
support, pistol-mechanism, reviewed-registry and native-operation headers remain
empty, with accepted grip baselines OFF. M9/recoil work remains separate.

No selected binaries, selection metadata, recovery snapshots, remote branches or
main source files were changed. Scene/first-person depth agreement and actual XR
depth swapchain behavior remain runtime validation items from the checklist above.
