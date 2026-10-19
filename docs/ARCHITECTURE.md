# Architecture

BC2 single-player is the first game adapter. Shared VR policy is separate from
Frostbite discovery and BC2's native rendering, input, animation and ammunition
bindings. [STATUS.md](STATUS.md) describes implementation scope;
[AGENTS.md](../AGENTS.md) defines development policy.

## Source map

| Work | Start here |
| --- | --- |
| Input and shared interactions | `src/interaction`, `include/fvr/interaction`; `ControllerInput.cpp` maps controller actions |
| Native BC2 integration | `src/games/bc2/Bc2Gameplay.cpp`; follow the relevant consumer from its gather/update call site |
| Physical weapon state and ammo | `Bc2PhysicalWeaponRuntime.*`, `Bc2PhysicalWeaponNative.*`, `Bc2PhysicalWeaponInteraction.*` in `src/games/bc2` |
| Hand/action presentation | `Bc2NativeActionPose.h`, `Bc2AuthoredSupport.h`, `Bc2NativeSkinRuntime.*` in `src/games/bc2`; authored profile data under `profiles` |
| Native recoil | `src/games/bc2/Bc2NativeRecoilTransfer.*` |
| Stereo production | `src/games/bc2/Bc2StereoCapture.*`, `Bc2StereoCaptureGpu.*` |
| Tracking, IPC and presentation | `src/runtime`, `src/ipc`, `src/graphics`, `src/xr`, `src/platform/windows` |
| Native discovery | `src/engines/frostbite`, `docs/re` |
| User entrypoint | `Launch-BC2VR.vbs`, `DevLauncher.ps1`, `tools/Start-BC2Playtest.ps1` |
| Asset tooling and diagnostics | `tools/bc2_*`, `tools/replay`, `tools/testrange`, `tests` |

Names above locate subsystems; inspect their callers and current build options
before assuming a particular path is enabled in a selected package.

## Runtime boundaries

The x86 native module runs with the 32-bit game. The separate x64 host handles
OpenXR. Tracking and stereo transport carry versioned identity, timing and
resource-generation information. Both eye images must belong to one coherent
native frame and device generation. Do not advance game simulation or animation
again to render the second eye.

Shared math and interaction policies do not establish native offsets or ABI.
OpenXR coordinates, native camera conventions, units and depth are converted at
the adapter boundary. Each future game needs its own verified bindings.
Across DLL/IPC boundaries use explicit layouts, versions and ownership; do not
pass STL containers, exceptions or borrowed game pointers as a portable API.

The mod may own persistent player weapon, ammunition and mechanical state.
Physical completion drives native progress through identified player objects and
verified functions. Keep ammo conserved and the owned firing copies coherent.
A visual pose or final ammo count alone does not prove a transaction succeeded.
Object replacement, respawn and stale tracking must not reuse an old ownership
claim. Scoped per-eye render/camera overrides still need appropriate cleanup so
later native rendering remains coherent; this is not universal state restoration.

Gun and hand poses come from original game poses/animations. Physical VR travel
drives their progress, including wrist and finger samples. Keep the primary
hand controller-authoritative and fit the weapon to it. Approximate poses and
rigid hand-to-part attachments do not complete native-animation coverage.
Use shared mechanism consumers plus exact configuration/profile data rather than
separate weapon-name state machines.

## Detailed references

See [frame transport](FRAME_CHANNEL.md), [D3D11 bridge](D3D11_BRIDGE.md),
[OpenXR host](OPENXR_HOST.md), [weapon profiles](WEAPON-PROFILE-PIPELINE.md)
and [native research](re/). Each reference has its own evidence scope.
The former architecture diary is preserved in
[ARCHIVE-ARCHITECTURE-20261013.md](ARCHIVE-ARCHITECTURE-20261013.md);
its old unconnected-producer and disabled-feature descriptions are historical.
