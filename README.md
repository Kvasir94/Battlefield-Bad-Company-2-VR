# Bad Company 2 VR (FrostbiteVR)

A native VR mod for **Battlefield: Bad Company 2 single-player** (Frostbite 1.5,
PC, x86). The game's own renderer draws both eyes; an injected DLL hooks the
engine and streams stereo views, tracking and input to a separate 64-bit OpenXR
host. The goal is native feel with modern VR controls: tracked weapons and hands,
physical magazine, pump and charging-handle reloads driven by the game's own
animations, body holsters and roomscale.

This repository is a **developer handover package**: the full source, tooling,
tests and every engine-mapping and design record produced so far. It is work in
progress, not a finished player release. Per-weapon coverage, campaign
transitions and headset acceptance are incomplete; see
[current status](docs/STATUS.md).

## Requirements

- A legally owned PC copy of Battlefield: Bad Company 2, **executable build
  795745** (32-bit, Direct3D 11 path). Hooks are located by byte signature and
  fail closed on other builds. The engine records list the researched exe
  hashes; no game files are included or distributed here.
- Windows 10/11 x64 and an OpenXR runtime (SteamVR, Oculus/Meta, WMR or other)
  with a PC VR headset and tracked controllers.
- To build: Visual Studio C++ tools (x86 and x64) with the Windows SDK,
  CMake 3.24+, Ninja, PowerShell 7 and Python 3.10+ on `PATH`.

## Build

```powershell
.\Build.ps1                      # x86 native DLL (BC2NativeProbe), incremental
.\Build.ps1 -Architecture x64    # x64 OpenXR host (BC2XrHost)
```

Tests are opt-in (`.\Build.ps1 -FullTests`, or a focused `-TestRegex`), and none
of them launch the game. The x64 host needs the Khronos OpenXR loader in
`runtime/openxr/win64/`. See [BUILDING.md](BUILDING.md) for targets, caches,
loader pinning and packaging.

## Run

Open `Launch-BC2VR.vbs` (or `DevLauncher.cmd`). The unified launcher selects a
prepared build and OpenXR runtime, points at your `BFBC2Game.exe`, exposes
player and test options, and saves named profiles under `%LOCALAPPDATA%\BC2VR`.
See the [launcher guide](docs/LAUNCHER.md) and [controls](docs/CONTROLS.md).
A plain source build is not a complete playtest package; the launcher's package
tools assemble one.

## Where to start reading

The [documentation index](docs/README.md) is ordered for a new developer:

1. **Engine map**: Frostbite 1.5 / BC2 reverse engineering, addresses,
   signatures, object layouts and type info ([docs/re/](docs/re/),
   [engine and architecture reference](docs/handover/engine-and-mapping.md)).
2. **Reload system**: the physical weapon and reload pipeline and why BC2 is
   harder than KF2 ([reload system](docs/handover/reload-system.md),
   [KF2 vs BC2 deep dive](docs/handover/kf2-vs-bc2-reload-deep-dive.md)).
3. **Harness and tests**: replay tapes, the test range, scripted in-game runs
   ([test tooling](docs/handover/test-tooling.md)).
4. **Open work and roadmap**: [status](docs/STATUS.md),
   [porting roadmap](docs/FROSTBITE-PORTING-ROADMAP.md).

[ARCHITECTURE.md](docs/ARCHITECTURE.md) maps the source tree, and
[AGENTS.md](AGENTS.md) holds the development rules.

## Credits and lineage

This project is a fork of **[justiceofrip/FrostbiteVR](https://github.com/justiceofrip/FrostbiteVR)**,
the original Frostbite VR framework this mod grew from: the BC2 adapter, native
stereo path, OpenXR host and first physical interaction layer. It also builds on shared math and policy code
from [BFVR](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod) and
[BF2142VR](https://github.com/justiceofrip/BF2142VR), and borrows interaction
design lessons from the Killing Floor 2 VR mod.

## License

MIT, as inherited from upstream FrostbiteVR (see [LICENSE](LICENSE)). Vendored
MinHook, OpenXR, JsonCpp and LSLib-derived code keep their own licenses; see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Independent community project, not affiliated with or endorsed by EA or DICE.
Battlefield and Frostbite are trademarks of Electronic Arts. The license covers
this mod's code only and grants no rights to the game or its assets.
