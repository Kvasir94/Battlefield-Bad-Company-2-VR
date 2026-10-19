> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (build 795745, x86) Frostbite 1.5 renderer: static RE for VR

Static only (Ghidra 12.1.3 headless on a read-only copy of D:\bc2-re\proj, plus pefile/capstone on D:\bc2-re\BFBC2Game_795745.exe). Game not run. Repo not edited.
DIS = read in decompile/disassembly. INF = inferred. Preferred-base VAs.

## 0. Already known and used by the mod (not re-derived)
- GameRenderer singleton `[0x156d4f8]`; graphics/renderer `[0x1572a60]`; frame `0x5c9b40`; dispatch `0x5ca820`; Present `0x5eea30`/`0x5e1390`.
- WorldRenderView vtable `0x14776f8`, ctor `0xbab9f0`, factory `0xbadf80`, setActive (slot 17 `0xb86490`), viewport setter (slot 15 `0xb862f0`). Camera blocks `+0x90` (render), `+0x4f0` (culling), plus `+0xc70` and `+0x10d0`. View-anchor getter/setter `0xb86710`/`0xb86720` (slots 45/46, view+0x30).
- Cache builders `0x96f260` (view), `0x973800` (projection), `0x965bf0` (frustum). Projection setter `0x5c7f20`.
- WorldUpdate `0xbb4980`, per-view visibility `0xbadff0`, Prepare `0xbb0ea0`, Draw `0xbb1db0`, world render `0xbb4d80`.
- Mod design (NativeProbe.cpp, around lines 1040-1110): the main native view is the **left eye**. A second view made with createView is the **right eye**. ApplyTrackedView writes both camera blocks into each view, and SynchronizeTrackedViewport copies the viewport. **Nothing else is copied.**

## 1. Frame and threading pipeline (DIS)
1. Client frame `0x540d00` ("clientPreRendGrp") → GameRenderer vfunc+0xc = **GameRenderer::Update `0x5d1670`(dt,…)**, once per frame on the main thread:
   - `0x5ca690(dt)` ticks the timed screen-effect channels at GameRenderer+0x10a0 (16 bytes per channel; start/stop through vfuncs +0xc4/+0xc8/+0xcc, about 20 game call sites each).
   - Resolves fades from GameRenderSettings (GameRenderer+0x10):
     - `ForceBlurAmount`@0x30 → GR+0x48c
     - `ForceWorldFadeAmount`@0x3c → GR+0x484, otherwise channel 0
     - `ForceGlobalFadeAmount`@0x38 → GR+0x488, otherwise channel 3
     - `FadeEnable`@0x7c == 0 forces both fades to 0
   - Then each of 5 subsystems (GR+0x1088) calls vfunc+8 (frame build). Then "renderDispatch" either runs `0x5c9b40` inline (GR+0x9dc==0) or posts job `0x5ca820` to the render thread.
2. **World subsystem build `0x5de070`** (no direct callers, so it is a vtable entry). It fetches the "Render" settings (GameRenderSettings: reads LockView +0x81 and ResetLockedView +0x7d) and copies GameRenderer state **into the one main view at subsystem+0x34 only**:

| View setter (slot) | View field | Source (GameRenderer vfunc → field) | Meaning (confidence) |
|---|---|---|---|
| 0x3c (15) | +0xc58 viewport | renderer w/h | viewport (DIS) |
| 0x14/0x1c (5/7) | +0x90/+0x4f0 | GR cameras | render and culling camera (DIS) |
| 0x54 (21) | +0x1530 vec3 | `1 - GR+0x484` (world fade) | **fade multiplier**; ctor default 1,1,1 (DIS) |
| 0x5c/0x6c (23/27, ref-counted) | +0x1540/+0x1548 ptr | GR state | +0x1548 is a typed object: ScopeFilter (when ScopeFilterEnable) or HudFx (CRT) data (DIS use in Draw) |
| 0x64 (25) | +0x1544 | entity type `0x1c12254` vf+0x38 | list consumed in Draw with prepared[0x13] (INF: lens-flare/occlusion list) |
| 0x74/0x7c (29/31) | +0x154c/+0x1550 | GR | **foreground (gun) blur filter** params, used by PostProcess vf+0x1c when WorldRender.ForegroundBlurFilterEnable (DIS) |
| 0x84/0x8c (33/35) | +0x1600 v4 / +0x1610 | "DynamicPostProcess" settings +0x40/+0x30 | dynamic post params; **not initialised by ctor** (DIS) |
| 0x94 (37) | +0x1560 block (incl. 4x4 colour matrix at +0x1590) | GR+0x4d0, set by VisualEnvironment `0x90c640` | colour correction (DIS/INF) |
| 0xa4 (41) | +0x1648 bool | GR+0x1140>0 | post param flag (INF: DoF/zoom) |
| 0xb0 (44) | +0x1620 v4 | GR+0x1100 (≈20 writers 0x5b-0x60xxxx) | point the Draw measures distance to from the camera (INF: **DoF focus point**) |
| 0xc0/0xc8/0xcc (48/50/51) | +0x1640/+0x1630/+0x1644 | GR+0x1134+contrast, GR+0x10ec (Brightness), GR+0x1138 | user Brightness/Contrast (`0x8feff0`) and post amounts (INF: radial/motion blur) |
| 0xd4 (53) | +0x1650 v4 | GR+0x1160 | if any component > ε, Draw calls PostProcess vf+0x20: **colour overlay/tint** (DIS) |
| 0xdc (55) | +0x15d0 block (flag +0x15f5) | GR+0x540 | full-screen overlay drawn by renderer vf+0x8c at end of Draw (DIS) |
| 0xe4 (57) | +0x1660 float | `1+f`, f from entities of type `0x1bf8054` | **LOD multiplier**: WorldUpdate writes `vf0xe8()*k` to RenderView+0x30 (DIS); ctor default 1.0 |
| raw | view+0x4,+0x8,+0xc,+0x10..+0x1c | GR+0x4a4..+0x4bc | view+0xc byte gates part of the Draw post path (INF) |

   After the copies, `0x5de070` calls (subsystem+0x2c)->vf+0xc. Per the doc chain this is WorldUpdate `0xbb4980`, but the vf+0xc → `0xbb4980` link is INF.
3. **WorldUpdate `0xbb4980`** (DIS). It loops over request+0x24 views that are active (vf 0x48). For each view, before visibility:
   - clamps far: `0x9659e0` writes RenderView+0x20 = min(far, override)
   - writes LOD scale `+0x30 = view+0x1660 * k` (`0x965ad0`)
   - writes `+0x34/+0x38` (`0x965ae0`/`0x965af0`)

   It does this on both the render (+0x90) and culling (+0x4f0) blocks, then runs `0xbadff0`. **Each view culls independently.** No shared-visibility path was seen (INF from negative evidence).
4. Render thread: `0xbb4d80` runs **Prepare for every active view in list order, then Draw for every view in the same order** (DIS). The main view registered first, so **the left eye prepares and draws first**. WorldRenderer+0x9c listeners run vf+0xc once before and vf+0x10 once after. Draw also calls listener vf+0x14 for each view, which is a native per-view callback.
5. Draw `0xbb1db0` rebuilds the view cache if RenderView dirty bit 1 is set (`0x96f260`) and the projection cache if bit 2 is set (`0x973800`) (DIS). A late pose write with dirty bits |=3 on the render block is therefore honoured at Draw time.

## 2. Answers to the coordinator's audit
- **(a) CONFIRMED (DIS).** `0x5de070` is the only per-frame writer of the view effect block, and it writes the main view only. A fresh createView keeps the ctor defaults set by base ctor `0xbab650`:
  - +0x1530 = 1 (no fade)
  - pointers null
  - colour matrix identity
  - +0x1640/1644/1648/1650/15d0 = 0
  - +0x1660 = 1.0
  - +0x154c/+0x1550/+0x1600/+0x1610 uninitialised

  So fade, tint/overlay, ScopeFilter/HudFx, foreground blur, colour correction, user brightness/contrast, DynamicPostProcess and LOD multiplier exist **only in the left eye**. That matches the left-only fade/cover.
- **Left-eye-only effects are not double-ticked.** Effects and fades tick once in `0x5d1670`. The asymmetry comes from binding the values to the main view only.
- **Left-only ghosting (INF).** The PostProcess object (WorldRenderer+0x70) and its PostProcessView (factory `0x9ca800` → ctor `0x9c9e40`, holding "PostProcessView/ExposureTexture", "LuminanceBuffer" and "GPULuminanceBuffer") are per WorldRenderer, not per view. Post shader params include `oldExposureTexure`, `adaptLuminanceFactor`, `MotionBlurAmount`, `radialBlurCenter/Scales` and `dofParams` (binder `0x9b8450`). Any history-reading effect therefore runs twice per frame and sees the other eye's last output. Combined with the left-only effect values, the left eye blends with history that the right eye wrote, which would give horizontally offset ghost edges. Eye adaptation also advances twice per frame and crosses eyes.
- **(b) CONFIRMED (DIS, Prepare `0xbb0ea0`).** When WorldRenderSettings.ForegroundEnable(+0xb8) && ForegroundDepthClearEnable(+0xbd), a pass "worldForegroundDepthClear" (type 0xe, list WorldRenderer+0x254/+0x258) clears depth before the foreground (1P) list, so the gun draws over walls. With +0xbd=0 and +0xb8=1, the foreground list is merged into the first "worldMainOpaque" slice and depth-tests against the world. "worldForegroundBlur" (+0xbb, list +0x2d4) is the ADS gun blur, with the per-view filter at +0x154c/+0x1550 (Draw: PostProcess vf+0x1c when +0xbe).
- **(c) CONFIRMED (DIS).** The fields are far (+0x20, dirty |=0xa), LOD scale (+0x30 = view+0x1660*k), and +0x34/+0x38. They are computed per view in `0xbb4980` from that view's own camera blocks at update time. Main and second views can therefore differ in LOD by their +0x1660 values and their own cameras.
- **(d)** No native visibility sharing was found. The cheapest equivalent is to give both views the same culling block (+0x4f0), which the mod already does with a stereo envelope.

## 3. Projection
- Asymmetric projection is native through RenderView crop +0x3c/+0x40 (projection builder `0x973800`). Frustum `0x965bf0` is symmetric (ignores crop) and clamps the half-FOV (repo doc, consistent).
- ADS zoom is GameRenderer vf+0x24 → GR+0x117c, written by soldier zoom `0x7ff360` and `0x73f070` (DIS writes; the FOV-scale meaning is INF).
- Native injection point: after `0x5de070`'s slot-5/7 copies, i.e. at WorldUpdate entry. Or render-only at `0xbb4d80` entry with dirty bits.

## 4. Settings toggles (write fields; registry `[0x155cfdc]`, GetByName `0x4f4c40`, get-or-create `0x4f56b0`)
- **WorldRenderSettings** (WorldRenderer+0x94, DIS by offsets):
  - ForegroundEnable 0xb8
  - ForegroundZPassEnable 0xba
  - ForegroundBlurEnable 0xbb
  - ForegroundDepthClearEnable 0xbd
  - ForegroundBlurFilterEnable 0xbe
  - ScopeFilterEnable 0xbf
  - TonemapEnable 0xea
  - ShadowmapsEnable 0xed
  - MultisampleCount 0x3c
  - ViewMode 0x30
- **GameRenderSettings** ("Render", GameRenderer+0x10):
  - FadeEnable 0x7c
  - ForceWorldFadeAmount 0x3c / ForceGlobalFadeAmount 0x38 (use 0 to force none)
  - ForceBlurAmount 0x30
  - BlurEnable 0x7e
  - ForegroundBlurEnable 0x7f
  - ForceFov 0x18
  - NearPlane 0x10
  - EdgeModelLodScale 0x40
  - EdgeModelForceLod 0x44
  - occlusion culls 0x73/0x74
- **GlobalPostProcessSettings** (PostProcess vf+0x14; Hsao offsets 0x196-0x19d DIS-matched):
  - ForceDofEnable 0x17c (Int32)
  - ForceVignetteScale 0x20
  - FilmGrainEnable 0x19b
  - BloomEnable 0x1a1
  - HdrBlurEnable 0x192
  - BlurBlendEnable 0x18c
  - PostTonemapBlurEnable 0x19e
  - DownsampleLogAverageEnable 0x18d
  - DirectExposureEnable 0x188
  - ChromostereopsisEnable 0x19c
  - HsaoEnable 0x197
  - BloodEffectFrameSize 0x130
- **Others:** OcclusionSettings.Enable 0x38, DecalSettings.Enable 0x34, EmitterSystemSettings.QuadEnableZOcclusion 0x73.
- Semantics of the Force* fields are INF from names, except the Fade/Blur ones, which are read DIS in `0x5d1670`.

## 5. Prioritised hook points and toggles
1. **Mirror main-view effect state to the eye view** at WorldUpdate `0xbb4980` entry, which runs right after `0x5de070`'s copies.
   - Use the native setters on the eye view: slots 21, 23, 25, 27, 29, 31, 33, 35, 37, 41, 44, 48, 50, 51, 53, 55, 57. Call the ref-counted setters `0xb8f420`/`0xb8f470` for the pointers. Copy view+0x4..+0x1c raw.
   - This fixes left-only fade, overlay and colour, and equalises LOD (+0x1660).
2. **Kill history-coupled post** until per-eye PostProcessViews exist. Candidates:
   - BlurBlendEnable / PostTonemapBlurEnable / HdrBlurEnable = 0
   - ForceBlurAmount = 0
   - eye adaptation: DirectExposureEnable (verify)
   - or zero view+0x1644/+0x1640 per eye

   This targets the ghosting.
3. **Gun over walls:** WorldRender.ForegroundDepthClearEnable(+0xbd)=0 (keep ForegroundEnable). **ADS gun blur:** +0xbb=0 and +0xbe=0.
4. **Screen-space/2D:**
   - ForceDofEnable=0
   - ForceVignetteScale=0
   - FilmGrainEnable=0
   - ScopeFilterEnable=0 (scope blackout)
   - HUD CRT FX via view+0x1548 (set null on the eye view, or accept)
   - optional HsaoEnable=0 (black-dot suspect, INF)
5. **Late latch:** at `0xbb4d80` entry (render thread) write the render block +0x90 of both views and OR dirty bits 1|2. Draw rebuilds the caches (DIS). Keep the culling block from update time.
6. **Native multi-view:** the request view list is the supported N-view path (DIS). Per-view shadows exist (view+0x1680 slice list), so cascades fit per eye (INF: eye-different shadow shimmer). Consider WorldRender.ShadowmapSliceCount or the culling-block cascades.
