> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 late latch, projection depth and render-target RE (lane R4, 2026-10-10)

Static only: Ghidra 12.1.3 headless on a private read-only copy of
`D:\bc2-re\proj` (build 795745, x86, preferred base 0x400000), plus a byte scan
of `D:\bc2-re\BFBC2Game_795745.exe`. The game was not run. The call chain
and the `0xbb4d80` extent were then rechecked in the shared REA session (Ghidra
provider, sha256 `c603f686…0162`): `procedure_callers(0xb92b40)` =
`0xb9c990, 0xbdfd80`; `procedure_callers(0xb9c990)` = `0xbadff0`;
`procedure_info(0xbb4d80)` body ends at `0xbb4f18`, consistent with the 3-byte
`RET 4` at `0xbb4f16`.

Labels: **DIS** read in decompile or disassembly. **INF** inferred. **SRC** read
in our source.

## 1. WorldRenderer::Render `0xbb4d80` (DIS)

- Prologue `SUB ESP,0x54; PUSH EDI; MOV EDI,ECX`; epilogue `ADD ESP,0x54; RET 4`
  at `0xbb4f16`. **`__thiscall`, `this`=WorldRenderer in ECX, one stack
  argument (the request), callee pops 4 bytes.** The only reference is the
  vtable slot at `0x14776a4`.
- The mod already hooks it: `WorldHook(void* self,void*,void* request)`
  (`__fastcall` with a dummy EDX), `NativeProbe.cpp` WorldHook. The signature
  matches the disassembly. No new hook is needed at this address.
- Body: gated on `WorldRenderSettings+0xd2` and `request+0xc4==1`; listeners
  `WR+0x9c` vf+0xc; `0x531370`; Prepare `0xbb0ea0(request,view)` for every
  active view (vf+0x48) in list order; then Draw `0xbb1db0(request,view,data)`
  in the same order; listeners vf+0x10; `request+0xc4=3`.

## 2. Why a camera write at `0xbb4d80` entry is not a late latch (DIS)

The audit proposed writing the render block (`view+0x90`) of both views at
Render entry and OR-ing dirty bits 1|2. The disassembly shows that this would
move almost nothing that is drawn:

- **`0xb92b40`** is the context camera binder that the mod already hooks as
  `ContextCameraHook` (`__thiscall(context, RenderView* primary, RenderView*
  secondary)`, `RET 8`). It **copies** the camera into per-frame shader-state
  storage:
  - `primary+0x80` (camera position) → `0x5c0e10("shaderState/cameraPos")`,
    pointer stored at `context+0x24`;
  - if `*primary & 1`, it rebuilds the view cache `0x96f260` first, then copies
    `primary+0x220` (view matrix) → `0x5c0ec0("shaderState/viewMatrix")`, at
    `context+0x1c`;
  - if `*primary & 2`, it rebuilds the projection `0x973800`, then copies
    `primary+0x2e0` through the projection setter `0x5c7f20`, at `context+0x20`;
  - if `*secondary & 8`, it rebuilds the frustum `0x965bf0`; `0x9a13e0(secondary+0x90)`.
- `0x5c0ec0` is a linear per-frame allocator (`LOCK XADD` on a bump pointer,
  then `memcpy` of `n*0x40` bytes). The context therefore holds a **value copy**,
  not a pointer into the RenderView.
- `0xb92b40` is called only from `0xb9c990` (5 sites) and `0xbdfd80`.
  `0xb9c990` is called only from `0xbadff0`, the per-view **visibility** pass
  that runs inside WorldUpdate `0xbb4980`, on the update side. `0xbdfd80` is
  reached from `0xbe0900` (INF: another gather path).
- So every mesh, decal and terrain draw consumes view/projection/position
  copies taken at **visibility time**. A pose written at Render entry would
  reach only what Draw `0xbb1db0` reads straight from the view (viewport, far
  plane via vf+0x18, post-process inputs, INF) and would desynchronise those
  from the geometry. The pose reported to the host would then not be the pose
  of most pixels.

**Conclusion.** The last native point where the camera is consumed for
geometry is the visibility pass. The late latch is implemented there (section 3).
A true render-thread latch would also have to rewrite the gathered
`shaderState/viewMatrix` and `shaderState/cameraPos` copies for every
role-0 context of both eyes, the first-person weapon contexts and the
left-eye retarget snapshots. Those structures are owned by `ScopedContextEye`
and the projection-override path (lane R2). It is left as a follow-up that
should be built on R2's generalised retarget. The rotation-only part would not
need the projection copies (`0x5c7f20`), because the projection does not change.

## 3. Implemented late latch (SRC)

- Pose source: a new optional shared-memory lane `ipc::PoseChannel`
  (`Local\FrostbiteVR.Pose.v1.<token>.map`). The host writes one sample per XR
  frame, after locating the request views (section 4), using the `FrameRequest`
  wire format. Reads are lock-free and sequence-checked. Each sample expires
  100 ms after publication.
- Latch point: `ApplyTrackedView` for the main view, inside the visibility
  callback, before `AdjustViewBase` and `BuildTrackedViews`. Both eyes, the
  shared culling envelope and the right-eye camera come from the one latched
  sample.
- Admission (`runtime::CanRelatch`): the same space generation, the same
  reference head (byte-exact), the same world scale, a strictly newer
  generation, and `predictedNs` not earlier and at most 150 ms later.
- Report-back: `StagedFrameProducer::Relatch` updates the lease, so the ticket
  and the body-prop frame carry the latched generation. `FrameChannel::Publish`
  passes the latched sample, and `ipc::Publish` re-encodes the slot request
  (same id and deadline) only if `CanRelatch` holds and the ticket matches it.
  The host reads it back with `PollRequest(...,&rendered)`.
  `RemoteFrameProvider` accepts it only if `CanRelatch(request, rendered)`
  holds. **Submitted pose == rendered pose** follows. A pair whose label does
  not match its ticket is rejected by `RetainedPresentation::Commit`.
- Kill switch: `FVR_BC2_LATE_LATCH=0`, in either the game or the host
  environment. Evidence: `late_latch{latched,no_newer_sample,rejected,busy}` in
  the native stream report, and `render_views.relatched_pairs` in the host report.

## 4. Projection and depth convention (DIS)

- Symmetric builder `0x411a10(out, fov, aspect, near, far, flag)`, called from
  `0x973800` with `flag = (char)block[+8]`. Our camera copies set `+8 = 1`
  (`Bc2Camera.cpp`).
  - `flag=1`: `m[10]=far/(near-far)`, `m[14]=near*far/(near-far)`,
    `m[11]=DAT_014d9420`.
  - `flag=0`: `m[10]=far/(far-near)`, `m[14]=-near*far/(far-near)`, `m[11]=1`.
- Both are the conventional D3D [0,1] mapping: **depth 0 at the near plane, 1 at
  the far plane. BC2 does not use reversed-Z.** With `flag=1` the view looks
  down -Z, so `m[11]` is -1 (INF from the algebra; the constant itself was not
  dumped).
- The asymmetric builder `0x9736d0` (crop `+0x3c/+0x40`, scale `+0x44/+0x48`)
  feeds the same Z terms through `0x96ebf0` (off-centre). The native crop path is
  the asymmetric projection already used by `BuildRenderViewCopy`; overscan
  therefore needs no matrix patching.
- Depth layer parameters for `XrCompositionLayerDepthInfoKHR`: `minDepth=0`,
  `maxDepth=1`, `nearZ=block+0x1c / worldUnitsPerMeter`,
  `farZ=block+0x20 / worldUnitsPerMeter`. WorldUpdate clamps the far plane per
  view (`0x9659e0` writes `RenderView+0x20`), so read it after the update.
- The frustum builder `0x965bf0` ignores the crop and takes a symmetric half-angle
  (`0x4156e0`), clamped near 1 rad (SRC: `Bc2Camera.cpp` limit
  0.9999989867). Overscan is capped at tan(0.98) per tangent so the stereo
  culling cone stays valid.

## 5. Render target size (DIS/INF)

- The view viewport (`view+0xc58`, setter slot 15 `0xb862f0`) is copied each
  frame from the renderer's width/height getters (`renderer vf: MOV EAX,[ECX+0x18]`
  / `[ECX+0x1c]`, `Bc2Profile.cpp`), by the world subsystem build `0x5de070`
  (doc Q1). Draw sets the D3D viewport from `view vf+0x40` (DIS in `0xbb1db0`).
- The world colour and depth targets are owned by the renderer and sized from
  the swap chain/back buffer (INF: no per-view RT allocation was found in Prepare
  or Draw; the passes bind the renderer's targets via `DAT_01572a60` vf+0x68/0x6c
  in `0xbb1db0`).
- **Native per-view targets independent of the desktop back buffer: not found.**
  It would need either a renderer-level resize of the world targets (and every
  post-process target that depends on them) or our own RTs bound into each
  view's passes. Neither is a clean native route. The implemented item 4 sizes
  the game window to the headset aspect instead (`Start-BC2VRSession.ps1
  -HeadsetAspect`). The existing viewport equality check stays correct, because
  request size == window client size.

## 6. Shadow cascades (INF)

- Prepare `0xbb0ea0` iterates `view+0x1680..+0x1684` (8-byte entries; child
  shadow views, `ChildCount`) and builds the `worldMainOpaque`, `...Decal`,
  `...Transparent` and `...DepthWrite` slices per child (DIS).
- The child views are created and fitted per view during visibility (INF from the
  audit: cascades fitted per view+0x1680). Each child's camera is derived from
  its parent's **culling** block, through `0xb92b40`'s `secondary` argument
  (frustum `+0x90` → `0x9a13e0`).
- The mod already gives both eyes the **same** culling block (the stereo
  envelope from `BuildCullingViewCopy`, SRC). If the cascade fit is driven by the
  culling camera, both eyes already fit from one shared camera. Any remaining
  per-eye shimmer would then come from fitting against the eye-specific render
  block. No clean native route was confirmed to redirect it, so nothing was
  changed. The next step is a live capture that compares the child-view
  matrices of the two eyes in one frame (`trackedCameraEvidence` blocks 3/4).

## 7. Status of the remaining R4 items

- **Pipeline depth 2 (item 3, part): not implemented.** A second request in
  flight needs a second shared texture pair and a second keyed-mutex slot.
  Both live in `D3D11SharedPair`/`FrameSlot`, which lane R3 is reworking.
  `FVR_XR_PIPELINE_DEPTH` does not exist yet. Display-time prediction (the
  measured request-to-display lead) already removes most of the pose error
  that depth 2 would hide.
- **Depth submission (item 5): RE done, code blocked.** The convention is in
  section 4 (D3D [0,1], not reversed-Z; near/far from block `+0x1c/+0x20` in
  native units). The missing parts are:
  1. a game-side capture of the world depth target per eye (format discovery;
     a D24S8 or typeless target needs a shader copy to R32F/D32F, because
     a cross-process `CopyResource` of a depth-stencil format is not
     shareable);
  2. a second shared pair for depth (R3's `D3D11SharedPair`);
  3. host chaining of `XrCompositionLayerDepthInfoKHR` per projection view,
     enabled only when `XR_KHR_composition_layer_depth` is enumerated.
  Kill switch to add with it: `FVR_XR_DEPTH_SUBMIT=0`.
- **Shadow cascades (item 6): documented only** (section 6). No clean native
  route was found.

## 8. R5 follow-up (2026-10-10): depth submission and pipeline depth 2 implemented

Branch `render/depth-submit`. Static build only; not run in BC2 or a headset.

- **Depth source (SRC + existing evidence).** At each eye's native Draw return
  (`CaptureTrackedEye`) the DSV bound by the game is that eye's scene depth.
  The ground probe and `FVR_BC2_TEST_DEPTH_CAPTURE` read the same target at the
  same boundary; HEADSET-20261003 recorded 1920x1080 RGBA8/D24S8
  single-sample layouts there. Near/far are read from the live eye view's block
  (`view+primaryOffset+0x1c/+0x20`, section 4) at that boundary and divided by
  `worldUnitsPerMeter`. No native function is called or hooked, so no new
  signature needed verification.
- **Conversion.** `graphics::D3D11DepthCapture` copies the DSV texture into a
  private typeless twin (same family, whole resource), samples it in a private
  `ID3DDeviceContextState` (same pattern as the rigid-prop renderer) and writes
  R32_FLOAT per eye. A second named keyed pair (`depthProducers_`) carries it.
  Formats handled: D24S8, D32, D32S8X24, D16 (typed or typeless), single-sample.
- **Host.** `XR_KHR_composition_layer_depth` is enabled only if enumerated and
  `FVR_XR_DEPTH_SUBMIT` is not 0. Depth swapchains are D32_FLOAT
  (`DEPTH_STENCIL_ATTACHMENT`); the R32F scratch is drawn into them as SV_Depth
  (no format-cast copy). `XrCompositionLayerDepthInfoKHR` (minDepth 0,
  maxDepth 1, nearZ/farZ in metres) is chained onto each projection view only
  when the retained color pair's own depth landed in both eyes.
- **Pipeline depth 2.** Two request slots in the control mapping (frame channel
  v8), two color pairs (and two depth pairs) in the bridge, overlapped outcome
  tracking in `StagedFrameProducer`, and up to two in-flight requests in
  `RemoteFrameProvider`. `FVR_XR_PIPELINE_DEPTH=1` restores depth 1.
- **Not addressed.** If the first-person passes write depth with a different
  near/far than the world view (lane R2's first-person projection), the weapon's
  depth is decoded with the world planes here and is only approximate for
  reprojection. Not checked live.
