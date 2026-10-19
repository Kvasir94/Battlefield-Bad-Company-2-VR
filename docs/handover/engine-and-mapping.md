# FrostbiteVR (BC2) — engine & architecture reference

Scope: `/home/claude/src/FrostbiteVR` at checkpoint 202 (2026-10-07). Read-only review.
Paths are relative to the repo root. Line numbers refer to this snapshot. Treat this as a guide, not as rules.
It was written to support the stated goal: polish the existing BC2 VR mod and ship a native, physical
reload system on the level of Arizona Sunshine 2 or the KF2 VR mod.

---

## 1. Architecture

### 1.1 Process topology

```
BFBC2Game.exe (x86, D3D11 renderer)                  BC2XrHost.exe (x64)
 └─ BC2NativeProbe.dll (injected)   ── shared mem ──►  OpenXR 1.0 (vendored loader 1.1.61)
     MinHook hooks: render/visibility/    FrameChannel v2  ├─ OpenXrInput (actions, grip/aim poses)
     camera/input/rig/fire/reload         Input lane v3    ├─ RetainedPresentation (≤250 ms reuse)
     2nd native RenderView = right eye    Feedback (64B)   ├─ BodyPropCompositor (host-drawn props)
     CopyResource → legacy shared tex  ── GPU textures ──► └─ D3D11 swapchains, projection layer
```

- **Injection is classic, not a proxy.** `BC2NativeTrace.exe` (`src/platform/windows/NativeTraceMain.cpp:100-117`)
  locates the single `BFBC2Game.exe` whose path matches `config/local.json`. It calls `CreateRemoteThread(LoadLibraryW)` on
  `BC2NativeProbe.dll`, writes a `NativeProbeConfig` with `VirtualAllocEx`/`WriteProcessMemory`, then runs a second remote
  thread on the single export `FvrRunNativeProbe` (`src/games/bc2/NativeProbe.def`, `NativeProbe.cpp:1632`).
  The game must already be running in campaign. Hooks are disabled at the end, but the DLL and trampolines stay resident until the game exits.
  `Start-BC2VRSession.ps1` (139 lines) starts the host, waits for `ready.json`, then calls `Start-NativeTrace.ps1`.
  It hardcodes `--eye-width 1920 --eye-height 1080` at line 64.
- **The "probe" is the product.** All gameplay and VR code runs inside `BC2NativeProbe.dll`. The intended adapter DLL `BC2Adapter`
  (`Bc2Module.cpp`, `CMakeLists.txt:122`) exports only versioned metadata and discovery (`FvrDiscoverBc2Profile`). It has no render or input callbacks.
- **Hooking uses MinHook 1.3.4** (vendored and unmodified; see `docs/NATIVE_TRACE.md`). Every hook target comes from a signature plus relationship proof.
  Before patching, the code compares the live bytes against the on-disk PE bytes (example: `Bc2Gameplay.cpp:1975-1981`).

### 1.2 Hooked native functions (build 795745)

| Area | Hook | Where installed |
|---|---|---|
| Render | world render (`ret 4`), view prepare `0xbb0ea0` (`ret 8`), view draw `0xbb1db0` (`ret 12`) | `NativeProbe.cpp:1786-1788` |
| Visibility | per-view visibility `0xbadff0` (6 args, `ret 24`); world update `0xbb4980` (lifecycle/stereo modes only) | `NativeProbe.cpp:1789-1790` |
| Work pool | native frame-arena/pool reset (grown for the extra eye view) | `:1791`, `ExpandWorkPool` `:407` |
| Pass evidence | indexed/non-indexed draw, context-camera and projection-override constant writers | `:1796-1808` |
| Present | DXGI Present wrapper `0x5e1390`, forced to SyncInterval 0 for the active VR chain (`-UncapMirror`) | `:1821`, `docs/PACING-20260928.md` |
| Input | player input update `playerInputUpdate`, input gather `inputGather` (49 actions; float setter `0x61c7e0`, bool `0x612a00`) | `Bc2Gameplay.cpp:2018-2019`, hooks `:859`, `:1752` |
| Fire | fire-origin builder, client shoot, matrix copy, server shoot, compose | `Bc2Gameplay.cpp:1984-1990` |
| Animation/rig | animation update, evaluate, post-evaluate, weapon world, 1P world-pose builder, root setter | `Bc2Gameplay.cpp:2000-2007` |
| Palette | skin palette getters A/B and native packer (`__cdecl Pack(src,dst,count)`) | `Bc2RigPublication.cpp:482-488`, `:201` |
| Reload | firing Update/Commit/Transfer/Restore/Step | `Bc2ReloadFlowRuntime.cpp:1597-1613`, `:884`, `:997`, `:1111`, `:1116` |
| Menu / optics / rig worker | menu mouse pump `0x909290`; optic filter; rig worker | `Bc2MenuRuntime.cpp:164`, `Bc2OpticFilterRuntime.cpp:64`, `Bc2RigWorkerRuntime.cpp:60` |

### 1.3 Stereo rendering (true stereo, not AFR)

- **One simulation tick and two native views.** At startup the probe uses BC2's own view factory (`0xbadf80`, constructor `0xbab9f0`, 0x56b0 bytes)
  to create a second concrete RenderView and registers it in the request's main-view list.
  Each frame, `BeginTrackedFrame` (`NativeProbe.cpp:760`) takes a lease from the frame channel and activates the extra view (`setActive`).
  `ApplyTrackedView` (`:782ff`) then writes the left-eye camera into the main view and the right-eye camera into the extra view, at the
  post-clamp visibility boundary. It goes through native builders for view (`0x96f260`), projection (`0x973800`) and frustum (`0x965bf0`).
  Culling uses a symmetric cone that encloses both eye frusta (`math/StereoCulling`, `BuildCullingViewCopy`).
- **Stated invariant:** the top-level frame and world update are never replayed (`docs/ARCHITECTURE.md` "Required invariants" 1–3).
  World update `0xbb4980` advances time, serial and arenas exactly once.
- **Per-eye correction.** BC2 caches camera constants in projection-override and context bindings. These are corrected per eye
  during Prepare/Draw through `ExactWriteBatch`, which restores the exact bytes afterwards. This was the fix for the "wrong eye" and double-image problem (`docs/RENDERING-REPAIR-20260927.md`).
- **Capture and transport.** After each eye's draw the probe issues a `CopyResource` from the bound color target into a legacy shared texture.
  It polls an EVENT query on a later native draw, then publishes a 96-byte PairTicket.
  The NT handle + keyed-mutex path (v2) failed inside BC2 with `E_INVALIDARG`, so production uses legacy `GetSharedHandle`
  textures with fence feedback (`docs/D3D11_BRIDGE.md` "Version2 legacy compatibility"; `src/graphics/d3d11/D3D11LegacyRelay.cpp`).
- **Host side.** `src/xr/OpenXrHost.cpp` follows the full wait/begin/end discipline. It accepts sRGB and typeless swapchains and submits an image only with its original render pose and FOV.
  When no fresh pair is ready it reuses the last pair for up to 250 ms (`RetainedPresentation`). The eye size is overridden to 1920×1080 (`OpenXrHost.cpp:147-151`, `:374`).
  The viewport must equal the game's backbuffer (`NativeProbe.cpp` BeginTrackedFrame, `ViewportSizeChanged`).
- **IPC** (`docs/FRAME_CHANNEL.md`) uses a GUID-named `Local\FrostbiteVR.Control` mapping, a named mutex and an event.
  FrameRequest v2 is 248 B. Each request is a lease with its own QPC deadline (150 ms async lifetime, 50 ms sync cap).
  The host never relabels old images with newer poses. Input travels on a separate latest-value lane (InputPacket v2, 288 B). Haptics use FeedbackPacket (64 B).
- **Pacing.** The mirror Present is uncapped while VR is streaming. Measured: about 197 native FPS, mean handoff 18.7 ms, max 92 ms (`docs/PACING-20260928.md`).
  Before the uncap, 60 FPS native produced only about 15 fresh pairs per second.

### 1.4 Camera/projection

- BC2 uses right-handed padded-Vec3 transforms, RH projections with [0,1] depth and `P[2][3]=-1`.
  The core math is canonical LH: `camera_LH = C·camera_RH·C` with `C=diag(1,1,-1,1)` (`docs/BC2_NATIVE_DISCOVERY.md`; `src/engines/frostbite/FrostbiteCamera.cpp`).
- RenderView fields: type +4, RH flag +8, FOV scale +0xc, vertical FOV +0x10, near/far +0x1c/+0x20, aspect +0x24, crop +0x3c..+0x48, transform +0x50.
  Dirty bits 1/2/4/8. Asymmetric FOV is mapped through the crop fields (`Bc2Camera.cpp:25`).
  The native frustum ignores crop, so culling must use the separate envelope. The native clamp is about 114.6° vertical.
- `worldUnitsPerMeter` defaults to 1 and is passed through as a flag. No formal scale calibration is recorded.

### 1.5 Input path

The OpenXR action set lives in the host (`src/xr/OpenXrInput.cpp`: Touch, Index and simple profiles). Packets go over the input lane to `Bc2Gameplay`.
The gather hook *commits* semantic actions into the native 49-action cache for that tick. An earlier restore-at-end approach cancelled movement (`docs/CONTROLS-20260930.md`).
Aim goes through the angular aimer setters. Body-follow and roomscale go through a body-position getter.
Fire origin, muzzle and matrices are hooked so bullets leave the tracked muzzle.
Menus use a separate MenuChannel. The native menu cursor and events are injected through a manager at `0x19A4CA8` on a 1280×720 canvas (`docs/BC2-MENU-BINDING-20261001.md`).

### 1.6 Layering

| Layer | Path | Dependency rule |
|---|---|---|
| math | `src/math`, `include/fvr/math` (StereoMath, ProjectionOverride, StereoCulling, WeaponPoseMath, OpticAperture) | pure, no OS |
| interaction | `src/interaction`, `include/fvr/interaction` (48 headers: HandInteraction, DetachableMagazine, ReloadInsertion, AmmoSupply, ArmIk, SightFlip…) | portable policy, no native addresses |
| runtime/ipc | `include/fvr/runtime`, `src/ipc` (FrameCoordinator, StagedFrameProducer, RemoteFrameProvider, protocols) | fixed-width wire structs, static_asserted |
| engine | `src/engines/frostbite` (PeImage, BindingValidation, FrostbiteCamera) | Frostbite-generic helpers |
| game | `src/games/bc2` (236 files) | all offsets, signatures, hooks |
| graphics | `src/graphics` (D3D11 SharedPair/LegacyRelay/FrameBridge/MenuProducer, RigidProp, BodyPropCompositor) | no BC2 addresses |
| xr | `src/xr` (host, input, menu, diagnostic scene) | x64 only |

The layering holds up well at the math, interaction and ipc levels. It breaks down inside `src/games/bc2`, where diagnostics, probes and production logic are mixed together (see §3).

---

## 2. Engine datamining catalog

Target build: `BFBC2Game.exe` 1.0.1.0, CurrentVersion **795745**, PE32 LAA, base 0x400000, SizeOfImage 0x01913000,
timestamp 1314210909, SHA-256 `3911FCC8…3D54258`. This is the only build observed. Discovery fails closed on any other build.

How each item was obtained: **S** = static RE (signatures, disassembly of the PE), **L** = live trace (read-only observer, or MinHook pass-through in campaign),
**A** = asset parse (FBRB/DBX/Granny/MeshSet), **H** = headset/user acceptance.

| # | Knowledge | Recorded in | How | Confidence |
|---|---|---|---|---|
| 1 | Exe identity/hash; PE layout | `docs/BC2_NATIVE_DISCOVERY.md`, `Inspect-BC2.ps1`, `InspectBc2.cpp` | S | High (single build) |
| 2 | ~150 unique byte signatures; each must be unique per executable section, with call-target / vtable / global-in-.data cross-checks | `Bc2Profile.cpp` (73), `Bc2MenuBinding.cpp` (23), `Bc2MenuState.cpp` (21), `Bc2RigWorkerBinding`, `Bc2ReloadFlow/State/Server`, `Bc2OpticDiscovery`, `Bc2WeaponMode`, `Bc2SelectedMeshes1p` | S | High for 795745; untested elsewhere |
| 3 | Renderer singleton RVA 0x1172a60, GameRenderer 0x116d4f8, frame 0x1c9b40, dispatch 0x1ca820, D3D11 renderer vtable 0x10497a0 (Present slot 5, w/h slots 10/11), device +0x7c, swapchain +0x88, dims +0x18/+0x1c, SyncInterval +0x168 | `BC2_NATIVE_DISCOVERY.md`, `Bc2Profile.cpp:25-58`, `PACING-20260928.md` | S+L | High |
| 4 | GameRenderer camera candidates +0x20..+0x340 (view/proj/inverse/transposes) | `BC2_NATIVE_DISCOVERY.md` | L | Medium ("observation candidates") |
| 5 | Render path: subsystems at GR+0x1088; stage vtable slots 4/5/6; world draw `0xbb4d80` (request+0xc4 1→3); prepare/draw ABI | same, `NATIVE_TRACE.md` | S+L | High |
| 6 | Concrete RenderView vtable 0x14776f8; 4 camera blocks of 0x460 at +0x90/+0x4f0/+0xc70/+0x10d0; active flag +0xc52; viewport +0xc58; owner +0x70 | `BC2_NATIVE_DISCOVERY.md` `DiscoverViewLayout` | S+L | High for blocks 1–2; blocks 3–4 unassigned |
| 7 | Camera cache builders (view/proj/frustum) and dirty bits; RenderView field map; outward planes `n·p+d≤0` | same, `Bc2Camera.cpp` | S+L (owned-copy calls) | High |
| 8 | View factory/lifecycle (0xbadf80, AddRef/Release, slot 59 dtor, request lists +0x14/+0x24) | same | S+L | Medium-High. One 11:35 crash is quarantined (`CRASH-20260925.md`) |
| 9 | Visibility path (world slot 3 → 0xbadff0, `ret 24`); visibility thread ≠ draw thread | `NATIVE_TRACE.md` | S+L | High |
| 10 | Work-pool sizing for the extra view | `NativeProbe.cpp:407`, `NativeWorkPool.h` | S+L | Medium |
| 11 | Projection-override / context-camera constant writers (mesh, terrain, decal) | `ARCHITECTURE.md` (Sep 28), `analyze_pass_projection.py` | S+L+GPU readback | Medium |
| 12 | Gameplay: player context global, input update/gather ABI, soldier getter +0xc54, input cache +0xc74, aimer setters, EntryInputActionEnum (49 actions) | `Bc2Profile.cpp:269-302`, `CONTROLS-20260930.md` | S+L | High |
| 13 | Fire chain: builder, client/server shoot, matrix copy, compose, spread seed | `Bc2Gameplay.cpp:1979`, `FIRING-ORIGIN-20260930.md` | S+L | High |
| 14 | 1P rig: 147 bones, arm/weapon chains, world/skin/evaluated palettes, 64 B matrices, SIMD padding, native IK palette, rig fingerprint `0xa7f219a1426216ab`, skeleton `ske01.res` | `Bc2Rig.h`, `HANDS-20260930.md`, `HAND-COUPLING-20260930.md` | S+L+A | High |
| 15 | Palette pack consumer (`Pack(src,dst,count)`, 48 B column-packed); 2 slots per frame that are **not** eyes | `RELOAD-PRODUCER-BINDING-20261001.md`, `Bc2RigPublication.cpp` | S+L | High |
| 16 | Reload/firing state machine: Update/Commit/Transfer/Restore/Step; 3 firing copies (client, server, …); 0x40-byte firing snapshot (current, next, phase timer, loaded, reserve); context +0x18 = delta; flags +0x24..0x28; transfer paths (state12, timed, special4, zero-duration) | `Bc2ReloadFlow.h`, `Bc2ReloadState.h`, `Bc2ReloadServer.h` | S+L | Medium-High (`authorityProven=false` in code) |
| 17 | Weapon firing config fields (capacity, magazines, reloadTime/Delay/Threshold, bolt*, reloadType, authored pump states) | `Bc2ReloadState.h:37-49` | S+L+A | High per observed asset |
| 18 | Local-server inventory chain (manager → players → soldier → inventory → item → firing) | `Bc2ReloadServer.h:12-20` | S+L | Medium-High |
| 19 | Selected 1P mesh identity and visibility, weapon-mode/underbarrel, ACOG/optic draw routes, reticle draws | `Bc2SelectedMeshes1p`, `Bc2WeaponMode`, `Bc2OpticDiscovery`, `OPTIC-*.md`, `RETICLE-*.md` | S+L | Medium |
| 20 | Menu: mouse pump, manager, cursor/event thunks, APT queue, 1280×720 canvas | `BC2-MENU-BINDING-20261001.md` | S (+L later) | Medium-High |
| 21 | Boat/vehicle routes, seat/commit, vehicle reticle | `Bc2VehicleRoutes`, `Bc2BoatProfile`, `BC2-VEHICLES-CAMPAIGN.md` | S+L+H | Medium (one boat) |
| 22 | FBRB archive reader (gzip-wrapped), MeshSet/MeshData skin decoding, bone FNV hashes | `tools/inspect_bc2_mesh_asset.py`, `bc2_mesh_geometry.py` | A | High |
| 23 | Binary DBX tree parser → weapon config/mesh GUID joins | `tools/bc2_weapon_config_pipeline.py`, `bc2_weapon_mesh_bindings.py`, `bc2_weapon_package_index.py` | A | Medium-High |
| 24 | GrannyAnimation resources (magic `29de6cc0…`), curve formats via LSLib, offline clip evaluation | `tools/bc2_granny_resource.py`, `bc2_granny_curves.py`, `bc2_weapon_animation_pipeline.py`, `inspect_reload_animations.py` | A | Medium (bounded subset of formats) |
| 25 | Authored grips (AEK971, XM8), supports, magazine geometry (AEK experimental), draw catalog, visibility descriptors | `profiles/checkpoint202/*.generated.h`, `tools/bc2_authored_*`, `bc2_visibility_descriptors.py` | A+L | Medium |
| 26 | Native operation capability bound to exe hash + source hash (`bc2-weighted-palette12-stride64-gather-actions-v1`) | `profiles/checkpoint202/Bc2NativeOperationCapabilities.generated.h`, `source-with-header.json` | S+review | High (deliberately narrow) |
| 27 | 44-weapon family plan (28 detachable magazine, 5 belt, 4 bolt, 3 tube, …); runtime scope covers only SPAS12, XM8 scoped and the 40 mm GL | `profiles/weapon-family-work-plan.json`, `runtime-weapon-scope-20261003.json` | A + planning | Plan only |
| 28 | Body ammo/equipment rigid-part cache (`.fvrprop`, 22 parts, ~10.5 MB, local only) | `config/body-ammo-assets.json`, `bc2_body_ammo_assets.py`, `BODY-DISPLAY-BATCH.md` | A | High |
| 29 | XM8 magazine: bone `jntWpn_6`, 762 vertices / 620 triangles, rigid; measured grasp (15 finger transforms) | `XM8-MAGAZINE-INTERACTION.md` | A+L | High |
| 30 | Live-process helpers: `tools/read_bc2.py` (read-only RPM), `inspect_native.py` (capstone disasm, xrefs), `Observe-BC2.ps1`/`ObserveBc2.cpp`, `Start-NativeTrace.ps1` (bounded MinHook runs), ~40 `audit_*/check_*` report validators | tools/, root | tooling | n/a |
| 31 | OpenXR runtime facts: SteamVR on Quest 3, 2064×2208 recommended, sRGB required | `OPENXR_HOST.md` | L | High |
| 32 | Desktop crash reports (EIP outside modules); keyed-mutex `E_INVALIDARG` in BC2 | `CRASH-20260925.md`, `D3D11_BRIDGE.md` | L | Low (cause unknown) |

Notes:
- **Hardcoded addresses.** Source code finds RVAs again at runtime with signatures. Concrete VAs appear only in docs and reports.
  The checkpoint headers bind capabilities to the exact exe hash and source hash. That is good practice, but it guarantees zero
  portability to other builds (Steam vs Origin/EA App vs retail/patched, and any community exe) until someone re-reviews them.
- **Missing raw evidence.** Most "how obtained" evidence points to `reports/…` folders that are **not in the repo** (they were deliberately excluded).
  The catalog can be reproduced from tools only with a local install and the same build.

### 2.1 Underused resource: BC2 ships Frostbite reflection TypeInfo

The executable carries full Frostbite reflection: class name, size, parent, field table and enums. The repo already relies on it, but only one feature at a time.

- **Static form** (`Bc2OpticDiscovery.cpp:30-49`). Registration sites follow the pattern `push fields; push parent; push meta; push …; mov ecx, info; call ctor`.
  `meta` contains name, flags, size at +6 and field count at +13. Field records are 24 bytes: name at +0, type at +12, offset at +16.
- **Live form** (`Bc2BoatAim.cpp:18-38`, `Bc2BoatProfile.cpp:20`). An object's vtable slot 2 is `mov eax, imm32; ret`, which gives its TypeInfo.
  From there the code walks parents (+20) and fields (+36), checking name, offset and type.
- **Classes already validated through it (about 35):** `ClientSoldierEntity`, `ServerSoldierEntity`, `CharacterEntity`, `ClientWeaponComponent`,
  `WeaponComponentData`, `WeaponFiringData`, `FiringFunctionData`, `FireLogicData`, `WeaponStateData` (IsPumpAction +0xc0, SkipReloadAnimation +0xc5),
  `SoldierWeaponData`, `WeaponSwitchingData`, `PersistentWeapon`, `Client/ServerWeaponFiringEffects`, `SoldierAimingSimulationData`, `ZoomLevelData`,
  `ScopeFilterData`, `SniperLensScopeFilterData`, `HaloAimerData`, `EntryInputActionMappingData`, `InputActionMappingsData`, `ClientPlayerEntryComponent`,
  `ClientVehicleEntity/Component`, `VehicleEntityData`, `ChildRotationBodyData`, `SkinnedMeshAsset`, `CompositeMeshAsset`, `GameObjectData`, and enums such as `EiaReload=29`.
- **What's missing:** a complete dump. No one has enumerated every TypeInfo into a class/field/enum database.
  That is the single highest-leverage RE step available; see §5. Caveat: reflection covers *data* classes (`*Data`, assets, and component/entity fields that are exposed).
  Runtime-only engine internals (RenderView, the firing state machine, arenas, the view factory) are not reflected. That is exactly where most of the hard-won mapping above sits.

### 2.2 Clearly unmapped, ranked by how much it blocks the goals

| Blocking | Unmapped item | Blocks | Current state |
|---|---|---|---|
| **Critical** | Write authority for loaded/reserve on the server/client firing copies, and how the server copy replicates to the client | A mod-owned (AS2-style) reload | Read-only snapshots; `authorityProven=false` everywhere |
| **Critical** | 1P animation-state selection and reload-anim notify events (Granny clip choice, blend tree) | Suppressing the native reload anim; hand/weapon sync | Evaluate/post-evaluate hooked as pass-through only; clips parsed offline only |
| **Critical** | Level load/unload, death/respawn, checkpoint lifecycle hooks | Stability (two unexplained exits); stereo view rebuild | Owner/generation checks detect changes, but no lifecycle hook |
| High | Per-view render targets and history (camera blocks 3/4, `view+0x1540/+0x1548`, `+0x1670` queue semantics) | Eye resolution independent of desktop; black-dot/optic artefacts | Unassigned |
| High | Sound event trigger API | Reload feel (mag in/out, bolt clicks) | Nothing |
| High | Physics/collision query (ray/shape cast) and debris spawn | Dropped mags, hand-vs-world, physical holsters | Nothing; BC2 uses Havok |
| High | Native draw submission for extra meshes (props/body) | Lit, depth-correct props instead of host-composited flat props | Only palette-edit reuse of the weapon's own bones |
| Medium | Weapon/actor spawn and pickup APIs | Ground pickup polish, mag give/take | Ground pickup observed only |
| Medium | HUD pass identification | HUD as a VR quad layer | Not mapped |
| Medium | Recoil/spread runtime fields | Hand-space recoil | Spread seed only |
| Medium | Device feature level and flags | Transport choice | Not recorded |
| Low | Other exe builds (Steam, EA App, retail) | Distribution | Only build 795745 |

---

## 3. Critique

### 3.1 Soundness of the approach (mostly good)

- **The separate x64 OpenXR host is the right call.** BC2 is a 32-bit large-address-aware process with no headroom.
  A host process isolates the OpenXR runtime (SteamVR/Meta 32-bit support is poor) and survives runtime hiccups.
  The cost is an extra GPU copy and an IPC hop, plus a dependency on legacy shared handles.
  A keyed-mutex failure inside BC2's device pushed the code onto the legacy path. A likely cause is the device's creation flags or feature level, but nobody recorded them (gap).
  Recommendation: log `GetFeatureLevel`/`GetCreationFlags` of BC2's device once. If it is FL10_x, the NT-handle path will never work, so drop it.
- **DX path.** BC2 ships DX9, DX10 and DX11 renderers. Discovery requires the D3D11 renderer vtable (`Bc2Profile.cpp:41-55`).
  The doc notes that `settings+0x6c` read 0 while D3D11 was active, so don't trust that field.
  Users on `DxVersion=9` (or old configs) fail closed with no explanation.
  The installer/launcher should force `DxVersion=11` or `auto` in `settings.ini` and say why.
- **True single-tick stereo via a native second view** is the best technique for this engine. It beats AFR (no judder, no temporal disagreement between eyes) and beats frame replay (which the docs correctly forbid).
  Using the engine's own factory, culling and work pool means post-processing, LOD and decals mostly behave. Remaining artefacts: black dots and a floating optic dot.
  These are almost certainly per-view history, occlusion or screen-space buffers (blocks 3/4 and `view+0x1540/+0x1548` are unassigned).
- **Resolution is the biggest visual compromise.** Eyes are forced to the desktop backbuffer size, 1920×1080, while the headset recommends 2064×2208.
  The runtime FOV (Quest 3 is roughly square to tall) is squeezed into a 16:9 image through crop and aspect, so vertical pixel density is about half the horizontal.
  This hurts text, optics and reload close-ups badly. Fix options:
  (a) run BC2 windowed at a square/tall custom resolution such as 2048×2048 (simplest, also helps the mirror pacing), or
  (b) give the eye view its own render target. That needs ownership of per-view targets, which is unknown.
  (a) is a config change plus a launcher-side `settings.ini` write. It should be done before any optics or reload polish.
- **Latency.** Async lease plus legacy copy plus a fence polled on a *later* native draw plus host poll gives a mean of about 19 ms and a max of 92 ms of render-to-submit time.
  That is acceptable only because the runtime reprojects. Two improvements:
  submit the original render pose (already done), and request predicted poses for `now + measured pipeline latency` instead of the current display time.
  Do not chase lower latency by blocking the game thread.
- **Fragility to exe versions.** Signatures are strict and paired with full-function fingerprint proofs (`ReloadCodeProof` = rva/size/fingerprint), so *any* binary drift disables features.
  That is safe, but every distribution variant needs a profile pass. Collect hashes from Steam, EA App and retail plus the common no-DRM exes now.
  Also add a single "profile not supported" UX path.
- **Anti-cheat is irrelevant for SP.** The PunkBuster DLLs are loaded but inactive offline. CreateRemoteThread injection works.
  For an end-user product, still prefer a `dinput8.dll`/`dxgi.dll` proxy loader with the probe started on first Present.
  That removes the "start BC2, reach campaign, then run a PowerShell script" dance, which is the biggest usability blocker today.
- **Authority design (native operations need receipts).** Ammo moves only when the game's own reload logic transfers rounds.
  The mod freezes the native reload timer by overriding the context delta at `context+0x18` around the original Update (`Bc2ReloadHold.cpp:105`, `Bc2ReloadFlowRuntime.cpp:1046`).
  It then injects a Reload input pulse and waits for server "transfer" receipts. This is safe and conserves ammo, but it is the root of most reload bugs in the docs:
  native take-over after cancel, extra shells loaded, completion tails, contention losses, 3 firing copies to keep in sync, and 64-slot journals for missed diagnostics.

### 3.2 Opinion for the AS2/KF2-style reload goal

AS2 and the KF2 VR mod work because **the VR layer owns the weapon state**: magazine objects with their own round counts, a chamber, bolt/slide state and a physics-dropped empty.
The game is told the result (loaded and reserve counts), and the game's reload animation is suppressed.

The repo already knows everything needed for that: the loaded/reserve fields in the 0x40-byte firing snapshot, the server inventory → firing chain, Restore/abort helpers and the fire block.
So the recommended pivot is:

1. Keep the native reload state machine **idle**: block the Reload action and never pulse it.
2. Implement a mod-owned `WeaponAmmoModel` with per-magazine counts (BC2's pooled rounds become N physical mags), chamber state and bolt/slide state.
   Put it in `interaction/`; `DetachableMagazine` and `AmmoSupply` already exist.
3. On a seat or chamber event, write loaded and reserve to the *server* firing copy through one small reviewed write operation.
   Then let replication and the native snapshot sync the client copy, and verify by read-back.
   This replaces hold, pulse, receipt and journal with one write and one verification.
4. Keep the existing receipt machinery only as a regression oracle.

Expect roughly a 5× reduction in reload-runtime code. Weapons that need native-only behavior (shotgun tube, belt) can opt back into the native path family by family.

### 3.3 Code quality and maintainability

- **Dense code.** `Bc2Gameplay.cpp` is 2,317 lines and 212 KB. Its average line is 91 chars; 83 lines exceed 200 chars and one reaches 899.
  `NativeProbe.cpp` is 2,095 lines / 198 KB (111 lines >200 chars, max 1,730). `Bc2ReloadFlowRuntime.cpp` is 2,034 lines / 173 KB. `Bc2RigPublication.cpp` is 1,557 lines / 140 KB.
  Many lines pack 5–15 statements (for example `NativeTraceMain.cpp:111-116`). These files cannot be reviewed in diffs and resist clang-format and static analysis.
- **Global mutable state.** Each of the big files holds dozens of file-scope atomics and statics (35 to 48 `std::atomic` uses each) with implicit cross-thread contracts.
  The contracts span the visibility thread, the render thread, the game thread and the remote control thread.
- **Probes and production share code paths.** `Bc2Gameplay.cpp` includes about 15 probe/diagnostic headers and `#ifdef FVR_BC2_*` scenario switches.
  `Start-NativeTrace.ps1` has more than 40 mutually exclusive switches with hand-written exclusivity checks.
  Ship a single `Bc2Runtime` with diagnostics behind a runtime registry, not compile-time and PS-flag combinatorics.
- **Over-defensive layering.** Concepts like "lease", "receipt", "cohort", "quiet", "retirement", "prehold" and "journal" multiply.
  Many types encode "this is evidence, not authority" as `static constexpr bool authorityProven=false`. That is honest, but the result is a large state space with few headset-verified behaviors.
- **Strengths.** Layer boundaries (math, interaction, ipc, xr) are clean. Wire structs are fixed-width and `static_assert`ed. Discovery fails closed with exact restoration.
  There are 197 C++ and 64 Python test files, and portable code has real unit and GPU tests. Keep all of this.
- **Suggested refactor order.**
  (1) Split `NativeProbe.cpp` into `StereoRuntime`, `RenderHooks`, `CaptureBridge` and `DiagnosticsReport`.
  (2) Split `Bc2Gameplay.cpp` into input, fire, rig, body and vehicle modules.
  (3) Apply clang-format with one statement per line. Purely mechanical, with tests as the guard.
  (4) Replace the PS switch matrix with a JSON profile consumed by the DLL.

### 3.4 Documentation sprawl

There are 135 docs in `docs/`, about 104 of them dated or headset logs, totalling roughly 1 MB.
`ARCHITECTURE.md` is a reverse-chronological changelog rather than an architecture description. `2142-TRANSFER.md` is 109 KB.
Current truth is scattered. STATUS.md even says historical docs "do not override" it.
Recommendations:
- Move dated notes into `docs/journal/`.
- Keep about 8 living docs: Architecture, Native map (one table, per build), Stereo pipeline, IPC protocols, Input, Reload model, Weapon pipeline, Status.
- Generate the native map from discovery output (RVA, signature id, proof) so it cannot drift.

### 3.5 Risks

- The single exe build is a single point of failure, and there is one main developer machine and one headset.
- The view-factory crash (11:35) and the death-time exit are unexplained. Death/respawn and level transitions are the usual crash windows; test them first.
- The legacy shared-texture path depends on driver behavior. There is no device-lost or TDR recovery test.
- Retained 250 ms reuse during stalls risks discomfort. Prefer blanking or the runtime's last frame after about 100 ms.
- The automated "simulated player" acceptance is strong for logic but has repeatedly passed builds that then failed in the headset (`HEADSET-196-INTERACTION-FAILURES.md`, `RELOAD-193-HEADSET-CHECK.md`).

---

## 4. Gaps: engine knowledge still missing for a polished game

| Gap | Why it matters | Suggested route |
|---|---|---|
| Per-view render targets and history (blocks 3/4, `view+0x1540/+0x1548`, TAA/occlusion/SSAO buffers) | Black dots, floating optic dot, and the eye-resolution decoupling | L-trace D3D11 RTV/SRV bindings per view (RenderDoc on a 1-frame stereo capture); map `view+0xc70/+0x10d0` |
| Native device feature level and flags | Explains the keyed-mutex failure; decides the transport | One read in the probe |
| Eye resolution independent of the backbuffer | Visual quality | Custom windowed resolution first, per-view RT later |
| **Ammo write authority** (server/client firing copy write + replication) | Required for an AS2-style mod-owned reload | One reviewed write op, verified by read-back over a fire/reload/death/save cycle |
| Reload animation suppression / 1P anim state override | Native reload anim fights physical hands | Hook animation state selection (Granny/anim tree evaluate already hooked); or keep the state idle and drive the IK hands |
| Bolt/charging-handle/pump state | Manual actions (STATUS lists as unfinished) | Firing snapshot `next/current` + boltDelay/boltTime gating; mod-owned chamber flag |
| Physics for dropped magazines/shells | Polish, the AS2 feel | BC2 uses Havok. Spawning native physics debris is unknown and risky. Cheaper: host- or mod-side rigid simulation with a floor raycast (need a native raycast/collision query function) |
| Native raycast/collision query | Dropped props, hand-vs-world, holster occlusion, melee | Find the physics query entry (Havok `castRay` wrapper) used by the fire path; 1P fire hooks are a starting point |
| Native mesh instancing for props | Host-composited body props are flat-shaded with no native depth or occlusion (`PERSISTENT-BODY-EQUIPMENT.md:70`) | Either submit extra draws in native passes (reuse the weapon mesh/material, as the magazine already does through palette edits) or export native depth to the host |
| Third-person/full body mesh in 1P | Full-body roadmap exists, no visible body | Locate the 3P soldier mesh draw for the local player and its visibility flag; the rig is already understood |
| Sound | No audio hooks at all; magazine in/out and pump clicks are core to reload feel | Find the BC2 sound-event trigger (FMOD-based Frostbite 1.5 "SoundPatch" events) used by reload anim notifies; fire native events at physical seat/unseat |
| Haptics | Only 2 feedback kinds (`FeedbackKind` ReloadCapture/Applied); no fire, recoil, impact or holster haptics | Extend FeedbackProtocol; fire from the already hooked client-shoot |
| Recoil model | Recoil currently goes through native aim; VR needs hand-space recoil | Map the recoil/spread fields in weapon firing data (spread seed is already found) |
| Death/respawn/checkpoint/level-load lifecycle | Crash windows; stereo view must be torn down and rebuilt | Hook level unload/load and the player-soldier constructor/destructor (server player create/ctor already discovered) |
| Save/checkpoint ammo persistence | A mod-owned partial-mag model must survive checkpoints | Serialize the per-mag model keyed by weapon item, reconcile on load |
| Vehicles beyond one boat, turrets, UAV | Campaign completeness | Same route tables as `Bc2VehicleRoutes` |
| Other exe builds | Distribution | Hash matrix plus re-run of discovery tests |
| HUD/UI | Native HUD is in screen space per eye | Locate the HUD pass and render it to a separate quad layer |

---

## 5. RE tooling and process recommendations (to speed up mapping)

The project's main cost has been mapping a barely documented engine one behavior at a time.
Each finding becomes prose in a dated doc, plus a hand-written signature and a C++ "proof" function.
That process is safe but slow and hard to reuse. Suggested changes, in priority order:

1. **Full reflection dump → generated SDK (1–2 days, biggest win).**
   Write `tools/bc2_typeinfo_dump.py`. Statically scan every registration site using the pattern in `Bc2OpticDiscovery.cpp:Type`, and walk meta, fields, parents and enums.
   Emit:
   (a) `bc2_types.json` (class, size, parent, fields{name, offset, type}, enums{name, value});
   (b) a generated C++ header of `static_assert`-checked structs;
   (c) Ghidra/IDA struct definitions;
   (d) a ReClass.NET class file.
   Optionally cross-check live: vtable slot 2 → TypeInfo for every object reached from the soldier, weapon and inventory roots.
   This replaces most per-feature `Field(...)` proofs with a single generated, hash-pinned source.
2. **A single symbol/offset database as the source of truth.**
   Use SQLite or YAML in `profiles/native-map/`, keyed by `exe_sha256`. Each row holds: symbol, kind (function/global/vtable/field), RVA, signature,
   relationship proof, ABI (thiscall/ret N), how found (S/L/A/H), confidence, evidence pointer and doc link.
   - Generate the C++ discovery tables, the Ghidra/IDA label scripts and the human-readable native map from it.
   - Today the same facts are spread across `Bc2Profile.cpp` and roughly 10 other discovery files, `BC2_NATIVE_DISCOVERY.md`, ~40 dated docs and private reports.
   - A new exe build then becomes "re-run the matcher and diff the table".
3. **A shared Ghidra project (or IDA idb) checked into a private repo.**
   Script-import the DB labels, reflection structs and Frostbite class names. Use Ghidra's x86 decompiler for the firing state machine, the render path and the view factory.
   Keep `tools/inspect_native.py` (capstone) for CI-style checks, but stop doing primary RE through hex dumps and prose.
   Run BSim or function-ID matching to carry labels across exe builds.
4. **RTTI / vtable recovery.**
   Check whether `BFBC2Game.exe` retains MSVC RTTI (`.?AV` strings, CompleteObjectLocators); it is quick to verify.
   If it does, run a class-informer pass (e.g. Ghidra RecoverClassesFromRTTIScript) to name every vtable, including the non-reflected RenderView, WorldRenderer and firing classes.
   If it does not, name vtables through constructors and the reflection `getType` slot. Either way, record results in the DB.
5. **Live layout tooling.**
   Use ReClass.NET or Cheat Engine structure dissect with a reflection-aware plugin (from step 1), attached read-only. This replaces bespoke `read_bc2.py` and `Observe` scripts for exploration.
   For "who writes X" questions (ammo counts, reload phase timer, animation clip selection), use **WinDbg Time Travel Debugging** (it supports x86) or hardware write breakpoints.
   One TTD recording of a reload answers what took days of hook-and-observe.
6. **Fast hook prototyping.** Use Frida (works on 32-bit Windows) or a scriptable hook DLL (e.g. Lua inside the probe) to try hooks without the
   rebuild → copy → inject → 30 s bounded run → audit cycle. Promote a hook to C++ and MinHook only once it is understood.
7. **Trace replay.**
   - Record hook arguments and the native object snapshots they touch (the probe already writes JSON) in a compact binary format.
   - Replay them offline into the portable policy code.
   - Simulated-player fixtures already do half of this; make them input-driven from real headset traces so a headset failure becomes a reproducible regression.
8. **GPU-side mapping.** For render targets, per-view history and the HUD pass, use RenderDoc, launched with BC2 or injected at startup, and apitrace for D3D11 frame captures.
   One captured stereo frame maps which RTVs, SRVs and constant buffers belong to each view far faster than draw-hook inference.
9. **Carry-back from FB2 knowledge (BF3/BF4 community SDKs).**
   BF3 is FB2 and also 32-bit at launch. Its community SDKs (generated from the same TypeInfo system) share many class names and hierarchies with BC2:
   `ClientSoldierEntity`, `ClientWeaponComponent`, `WeaponFiringData`, `FiringFunctionData`, `GameRenderer`/`RenderView`, `ClientPlayerManager`, `EntryInputActionEnum`, `UpdatePoseResultData`.
   Use them for *names, hierarchy and semantics* (what a field means, which object owns what), never for offsets.
   FB1.5's TypeInfo record layout is already decoded here, so the BF3 SDK generators' logic should port with minor changes.
   Also check other FB1.x builds: BC1 (console only), the BC2 dedicated server (`frost.game.main` / R-builds) and BC2 Vietnam.
   Any of these may expose extra strings or symbols, but verify availability before relying on them.
   Granny formats: use the public `granny.h` (2.x) struct knowledge and LSLib (already credited) to finish the offline animation decoder, including clip ↔ notify events.
10. **Process changes.**
    - Every new native fact goes into the DB in the same PR as the code that uses it.
    - Dated docs become an append-only journal that links DB rows.
    - Set a time box for each unknown: TTD or Ghidra first, then a live hook.
    - Ask for a short headset check before any feature exceeds about 2 days of simulated-only iteration (the 193/196 regressions show why).

Expected payoff: steps 1–5 turn most "unmapped" items in §2.2 from multi-day investigations into lookups or single TTD sessions.
Step 2 makes the existing work portable to new exe builds and, structurally, to BF3.
