> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Rendering reconciliation — 2026-10-11

Scope: read-only source/history review of main `6449df3` and detached integration
`3aa78af`, plus the current main handoff, roadmap, open-work and playtest notes.
This review ran no build, test, game, headset, install or selection operation.
After review, the parent authorized the bounded ColorReadback guard fix below;
only this new document and Bc2StereoCaptureGpu.cpp were edited. Main's unrelated
untracked files were left intact. Line references below name integration `3aa78af`.

## Source disposition

R1–R5 are integrated in **both** compared trees. `57d7a24` (paired depth and
two-slot transport) is an ancestor of main through merge `66fc2bd`. The old
OPEN_WORK R5 row saying “not merged” and earlier R4 text saying the pipeline
switch does not exist are historical, superseded by the R5 recovery section and
commit history. They must not trigger a second implementation.

A direct diff is empty for `src/xr`, `src/graphics`, `src/ipc`, and the eye-parity,
per-eye-post, stereo-capture-GPU and desktop-eye-mirror implementations. Integration
changes in NativeProbe concern hand capture/observation and related gameplay;
they are not a second rendering-wave implementation.

| Lane | Integrated commits and files | Result and remaining limit |
| --- | --- | --- |
| R1 | `0747589`, including `69cc330`/`a1344df`; `Bc2EyeViewParity.cpp`, `Bc2PerEyePostProcess.cpp`, NativeProbe update/draw scopes | Main effect state mirrored with verified native setters; separate right-eye PostProcessView history. Default on, `FVR_BC2_EYE_PARITY=0` / `FVR_BC2_PER_EYE_POST=0` disable. Native blur/scope effects deliberately remain; independent exposure can still differ. |
| R2 | `54a44fd`, `91ea122`, `b72a4c5`, `b1f3260`; NativeProbe context write pool, projection helpers, `Bc2ForegroundDepth.h` | General matched worker retarget, pooled writes, first-person full eye Z and owned foreground-depth-clear override. Shares native world near plane. `FVR_BC2_GENERAL_RETARGET=0` / `FVR_BC2_FP_WORLD_DEPTH=0` disable. Close-gun clipping/occlusion remains a distinct validation item. |
| R3 | `d8f59b4`; `OpenXrHost.cpp`, `D3D11SharedPair.cpp`, `D3D11FrameBridge.cpp`, `RetainedPresentation.h` | Recoverable frame errors, VISIBLE/tracking-gap retention, keyed-pair reclaim and asynchronous menu copy. These paths exist; session-stop/loss and sustained-load correctness are not all accepted by the available playtest notes. |
| R4 | `9f33bdf`, `d814af9`, `af543e4`; NativeProbe late latch, request prediction, host overscan and headset-aspect launch | Render-thread pose lane, display prediction and default 8% FOV overscan. Overscan adds angles, not pixels. Shared shadow-cascade fitting remains unimplemented. |
| R5 | `57d7a24`, merged `66fc2bd`; frame protocol/transport, depth capture/writer, host | v8 paired color/depth ownership and pipeline depth 2. Recovery fixes preserve held depth planes, deny post-feedback reads and release DSV references before depth swapchain destruction. `FVR_XR_DEPTH_SUBMIT=0` / `FVR_XR_PIPELINE_DEPTH=1` disable respective features. |

Evidence details remain in `docs/re/BC2_EYE_PARITY_POST_RE.md`,
`BC2_CONTEXT_RETARGET_RE.md`, `BC2_LATE_LATCH_DEPTH_RE.md`, and
`docs/HANDOFF-2026-10-10-RENDERING.md`. This review uses existing recorded native
signature evidence; it performed no new reverse engineering.

## Eye differences and resolution

The current main-only untracked ledger
`docs/PLAYTEST-20261011-CONTROLS-AND-RELOAD.md` is evidence of user observations,
not code state. RENDER-ACCEPT-001 accepts matching damaged-building edges and
most observed explosion/eye effects in the town. It does not close
STEREO-CUTSCENE-002 (snowy cutscene, one eye scope-like blur; exact scene/eye
unknown) or CUTSCENE-001 (scripted scope/camera and fog presentation). Do not
restart the original broad per-eye-effects rewrite from those isolated symptoms.
XM8-FX-001 (visual muzzle/ejection origin) likewise remains distinct from
ballistic origin and renderer parity.

RENDER-BLUR-004 remains the principal reported visual priority. The ledger records
owner-measured runtime recommendations of 1832 x 1920 per eye versus selected
1920 x 1080 allocations: 58.95% of recommended pixels. Full resource copies/full
XR rectangles and default overscan support a resolution investigation; they do
not prove the whole cause. The referenced historical finalhost file was not at
its ledger path during this review, so its counters are owner-reported rather
than newly independently measured here.

Integration-only source commits `a632a5f`, `e97dbc1` and `332c89e` add
`tools/BC2EyeRenderProfile.ps1`, the HighCompatible graphics profile and corrections
for native enums (`MainQuality=custom`, `Aniso=4`, `MSAA=0`). The packaged wrapper
uses fresh runtime dimensions; the generic `Play-BC2VR.ps1` still has explicit
1920 x 1080 defaults and GraphicsProfile Preserve. Do not imply every launcher
now automatically selects headset resolution. `docs/BC2_RUNTIME_EYE_QUALITY.md`
and `docs/re/BC2_GRAPHICS_SETTINGS_CONTRACT.md` record the profile and offline
coverage. Actual normal-XR eye allocation, visual quality, sampler filtering and
performance after this profile remain unaccepted. Diagnostic 1920 x 1080 native
hand captures do not establish that normal-XR allocation.

The ledger's corrected depth receipt reports extension/requested/active true,
pipeline 2, but **zero depth pairs/frames**. Therefore depth reprojection is
implemented, not demonstrated successful in that session. Inspect producer
capture/publish and host copy/upload counters together before diagnosing why.
A shared scene/first-person depth convention and near-gun halos remain runtime
questions. Neither a successful compile nor extension enumeration closes them.

## Frame pacing, resize and recovery

Current source has nonblocking asynchronous queue/poll, two request slots and
pair reuse. `D3D11SharedPair.cpp:18` uses a 20 ms late marker and a 250 ms retirement
bound; the old audit's blanket “20 ms faults the pair” diagnosis is stale.
`RetainedPresentation.h` bounds focused images at 250 ms, tracking-loss grace at
100 ms and VISIBLE images at 30 seconds. Requirements/space changes invalidate
retention. `OpenXrHost.cpp:506` classifies lost, timeout and recoverable results;
its frame path separates locate, pair acquisition, upload and submission.

No current controlled FPS/latency result was established by this review. The
October 9 ledger's “better” frame rate is subjective; gun stutter is separately
reported. Compare fresh/retained/empty counts, pair rejects, copy waits/timeouts,
request overlap and frame_timing under matched dimensions/runtime/load before
changing pacing. More pixels and High quality can alter the budget substantially.

`D3D11FrameBridge.cpp:43–100` checks source device/dimensions/sample count and
reallocates on changed requirements, after draining graphics feedback. This is
not a promise of transparent whole-device recovery. Per-eye PostProcessView is
recreated for size/owner changes and retired before old-world cleanup.
`NativeProbe.cpp:1687` drains tracked work and worker callbacks before retiring
an owned stereo root. Preserve these ownership boundaries in any follow-up.

## Capture and mirror

There are separate paths: optional host mirror from composed scratch left eye
(`OpenXrHost.cpp:813`), game-window retained eye mirror
(`Bc2DesktopEyeMirror.cpp`), and bounded diagnostic stereo capture
(`Bc2StereoCapture.cpp` / `Bc2StereoCaptureGpu.cpp`). Their success is not XR
presentation acceptance. Desktop mirror already guards device identity and
recreates changed-size staging textures; it does not retain a backbuffer RTV
across ResizeBuffers. Diagnostic capture defaults off, supports bounded arming,
and uses reduced copies/nonblocking map. Its cost has not been measured here.
The audit's proposed full-resolution copy reduction remains an optimization
opportunity, not an established source defect or reason for a rewrite.

### Concrete bounded defect: diagnostic color readback device ownership

`Bc2StereoCaptureGpu.cpp:104` caches the mip texture by size/format/mip count only.
Queue gets the current context device at line 102 but never validates source or
cached-mip device identity. Poll at line 132 maps each pending staging resource
using whichever context is passed, without checking that resource's device.
Thus a device replacement with unchanged dimensions can reuse an old-device mip,
and a different polling context can map another device's staging resource.

Caller reachability is conditional but real in the source: `NativeProbe.cpp:1447`
gets the renderer's current device/context for CaptureTrackedEye, and line 1466
queues diagnostic capture even if the bridge rejected that eye. PumpFrameBridge
at line 1301 polls using `observedImmediateContext`, assigned once at line 2608.
Owner recovery does not abandon color readback; its only Abandon call is quiet
shutdown at line 2745. Therefore bridge source rejection does not itself protect
this diagnostic path. No actual device-replacement failure or causal connection
to a user symptom is claimed. A stale/destroyed observed context is a separate,
broader lifecycle concern; the small resource guards do not solve that concern.

Bounded fix implemented after parent review (uncompiled; parent owns batched build):

1. Queue: require source and context device identity before any copy; report
   Failed/E_INVALIDARG on mismatch. Include cached mip device identity in its
   recreation condition.
2. Poll: get the context device once, compare each staging resource's device,
   mark mismatches Failed/E_INVALIDARG and release that slot without Map.
3. Keep ordinary same-device dimension recreation and nonblocking behavior.
   Do not switch the process's global observed context or attempt whole-device
   recovery in this patch.

Existing `Bc2StereoCaptureTests` links only `Bc2StereoCapture.cpp`
(`CMakeLists.txt:573`), synthesizes ColorStatus data and does not compile/exercise
ColorReadback GPU glue. It does **not** cover the defect. Smallest meaningful
verification for a chosen fix: affected x86 NativeProbe compilation, and (if
requested/available) a focused two-device D3D11 fixture that verifies old mip
recreation and mismatched staging rejection without issuing invalid GPU calls.
No broad test run or headset acceptance is prerequisite to these local guards.
Source diff review confirms the guards precede the affected copy/map. No GPU or
compile success is claimed by this worker; parent owns the batched compile.
No existing focused two-device capture fixture was found. Header/API and context
ownership remain unchanged; this patch cannot recover a stale context pointer.
