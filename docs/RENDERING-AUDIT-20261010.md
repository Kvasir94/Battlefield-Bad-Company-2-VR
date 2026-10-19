> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Rendering audit, October 10, 2026

Static audit only. Nothing was built, run, or checked in a headset. It has four
inputs:

- the known-issue inventory from the docs;
- a review of the game-side producer (`NativeProbe.cpp`, the bridge and the
  shared pair);
- a review of the host and transport (`OpenXrHost.cpp`, IPC, the swapchain);
- Ghidra reverse engineering of the Frostbite renderer, build 795745. Engine
  details are in [re/BC2_RENDER_PIPELINE_RE_20261010.md](re/BC2_RENDER_PIPELINE_RE_20261010.md).

Labels: **DIS** means read in decompile or disassembly. **INF** means inferred.
**SRC** means read in our source.

## Root cause behind the left-eye-only bugs

The left eye is the game's **main view**. The right eye is a fresh view created
by `createView("Fvr.SecondView.SingleFrame")` (`NativeProbe.cpp:1619-1624`).
WorldRenderer prepares and draws views in list order, so the main view goes first
(`0xbb4d80` → Prepare `0xbb0ea0`, Draw `0xbb1db0`, DIS).

Every frame, the world subsystem build `0x5de070` copies GameRenderer effect state
into **only the main view** (DIS). The fresh view keeps the constructor defaults
(`0xbab650`). Fields the eye view never receives:

| View field | Meaning | Visible bug it explains |
| --- | --- | --- |
| +0x1530 | world fade multiplier (`1 - GR+0x484`) | Fade in left eye only |
| +0x1650 / +0x15d0 | tint and full-screen overlay | Overlay or cover in left eye only |
| +0x154c / +0x1550 | foreground (ADS) blur filter | Gun blur on ADS |
| +0x1548 | ScopeFilter / HudFx object (ref-counted) | Scope presentation |
| +0x1560 / +0x1590 | colour correction and its matrix | Colour differs between eyes (INF) |
| +0x1630..+0x1648 | brightness/contrast, post amounts, focus (INF: radial/motion blur, DoF) | |
| +0x1600 / +0x1610 | DynamicPostProcess, **uninitialised** in the constructor | Possible garbage in the right eye |
| +0x1660 | LOD multiplier `1+f`; the eye view stays at 1.0 | Left/right LOD and edge detail differ |

**Explosion ghosting (INF):** one PostProcess and PostProcessView (WR+0x70) is
shared by both views, together with its exposure, luminance and blur history. Any
history read crosses eyes, and eye adaptation advances twice per frame. Only the
left view carries the blur amounts, so only the left eye blends in history
written from the right eye's position. The result is lines offset by one IPD.

**Worker contexts (SRC, medium):** the left eye's patch list is a whitelist: main
contexts, mesh decal, terrain effect and weapon. Any other worker context (for
example particles and destruction decals) can still draw the left eye with the
right eye's X/Y/W. This is the same class of bug as
[ARTIFACT-REPAIR](ARTIFACT-REPAIR-20260928.md).

## Ranked issue list

| # | Issue | Cause | Fix | Conf. |
| --- | --- | --- | --- | --- |
| 1 | Left-only fade, cover, blur; LOD differs per eye | Effect state reaches the main view only (above) | Mirror main view → eye view through the native setters at WorldUpdate `0xbb4980` entry (vf slots 21–57, ref setters `0xb8f420`/`0xb8f470`, raw +0x4..+0x1c), or zero the effects | DIS |
| 2 | Left-only ghosting after explosions | Shared PostProcess history across eyes | Set GlobalPostProcess BlurBlendEnable 0x18c, PostTonemapBlurEnable 0x19e and HdrBlurEnable 0x192 to 0, and GameRender ForceBlurAmount 0x30 to 0. Check DirectExposureEnable 0x188. Long-term: a PostProcessView per eye | INF |
| 3 | Weapon draws in front of walls | `worldForegroundDepthClear` runs when WorldRender +0xb8 and +0xbd are set. Our first-person retarget also keeps the native clip Z (`ProjectionOverride.h:20-27`) | WorldRender +0xbd = 0, and use the full eye projection including Z for first-person contexts, with a smaller near plane | DIS |
| 4 | Blurry gun on trigger ADS | `worldForegroundBlur` (+0xbb) and ForegroundBlurFilterEnable (+0xbe) | Set both to 0 | DIS |
| 5 | 2D effects with no meaning in VR | DoF, vignette, film grain, scope filter | ForceDofEnable 0x17c = 0, ForceVignetteScale 0x20 = 0, FilmGrainEnable 0x19b = 0, ScopeFilterEnable WR+0xbf = 0 | DIS (offsets), INF (exact semantics) |
| 6 | Black dots near centre | Unattributed. Best suspect is HSAO | Test with HsaoEnable 0x197 = 0 | INF |
| 7 | Possible crash: `ScopedContextEye` is about 74 KB on the stack, zeroed on every Prepare/Draw, shadow views included (`NativeProbe.cpp:765`, `ExactWriteBatch<544>`) | Stack size | `std::optional` or a preallocated per-thread buffer | SRC |
| 8 | World drops to black on dashboard or brief tracking loss | `RetainedPresentation` requires focused && headValid, and resets on non-FOCUSED (`RetainedPresentation.h:23`, `OpenXrHost.cpp:373`) | Keep submitting the retained layer in VISIBLE and for about 100 ms of tracking loss | SRC |
| 9 | Any OpenXR error, or one 50 ms swapchain wait, ends the host | `Check()` throws; `Require(!TIMEOUT)` at `OpenXrHost.cpp:507` | Skip the layer, EndFrame with no layers, exit only on SESSION/INSTANCE_LOST | SRC |
| 10 | Hitching under GPU load | A 20 ms wall-clock copy budget marks a fault; any discard resets the pair and recreates both NT textures (`D3D11SharedPair.cpp:15,251`; `D3D11FrameBridge.cpp:119`) | No fault on timeout below about 200 ms; reclaim the mutex instead of `Reset()` | SRC |
| 11 | Black borders on fast turns | Zero overscan, plus a pipeline depth of 1 (`StagedFrameProducer.cpp:24-29`) and render-ahead from the current frame's pose | Render and submit tangents 5–10% wider; predict the request pose for its display time | SRC |
| 12 | Non-square pixels | 1920×1080 forced onto a FOV of about 0.93 aspect (`OpenXrHost.cpp:266`) | Game resolution at the headset aspect (e.g. 1600×1712) | SRC |
| 13 | No positional reprojection or ASW | Depth is not submitted | Share depth and chain `XrCompositionLayerDepthInfoKHR` | SRC |
| 14 | Menu judder | Legacy menu copy spins up to 20 ms inside the XR frame (`D3D11SharedPair.cpp:265-276`) | Use QueueCopy/PollCopy, as the world path does | SRC |
| 15 | Shadow shimmer between eyes | Cascades fitted per view (view+0x1680) | Fit with a shared culling camera | INF |
| 16 | Muzzle flash floats | Gameplay-side spawn basis, or an unretargeted effect context | Compare the effect telemetry fields (`Bc2WeaponEffects.h:249-255`) | low |

## Better way forward

1. **Make the eye view a full peer of the main view.** One mirror step at
   WorldUpdate entry fixes items 1 and 4, part of 5, and the LOD difference. It
   uses the native setters, and the engine already supports N views per request.
   This replaces the per-symptom gates.
2. **Own the VR post profile.** One settings pass writes the WorldRender,
   GameRender and GlobalPostProcess fields above. Leave an env kill switch for
   each group. This covers items 2–6.
3. **Generalise the left-eye retarget.** Change the whitelist in
   `ScopedContextEye` to "any binding matching the tracked right eye's clip
   X/Y/W", and stop allocating it on the stack.
4. **Late-latch the pose on the render thread.** Write render block +0x90 for
   both views at `0xbb4d80` entry and OR dirty bits 1|2. Keep the culling block
   from update time. Together with a deeper pipeline and depth submission, this
   addresses items 11 and 13.
5. **Make the transport robust.** Covers items 8–10 and 14. Split
   `OpenXrHost::Frame` (~190 lines) and `NativeProbe::DrawHook` (~200 lines) while
   doing this.
6. **Cut copies.** The left eye currently takes three full-resolution copies
   game-side and two host-side. Capture straight into the keyed shared texture,
   and mirror from `eyes_[0]`.

Open: the typeinfo names for `0x1bf8054` and `0x1c12254` are runtime-filled. The
PostProcess Force* field semantics need a live check.
