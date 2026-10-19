# Building from source

Use 64-bit Windows, PowerShell 7, Visual Studio C++ tools with x86/x64 support and
the Windows SDK, CMake 3.24+, Ninja, and Python 3.10+. CMake, Ninja and Python must
be on PATH. `Build.ps1` imports the installed Visual Studio compiler environment.

## Incremental developer build

```powershell
.\Build.ps1                         # x86 native DLL only, two jobs, compile only
.\Build.ps1 -Architecture x64        # XR host only, when needed
.\Build.ps1 -Targets BC2NativeTrace  # optional diagnostic tool
.\Build.ps1 -Targets ControllerInputTests -TestRegex '^ControllerInput$'
.\Build.ps1 -FullTests               # explicitly run this cache's full CTest suite
```

The first invocation configures `build/dev/{architecture}`; later invocations
reuse its options and compile incrementally. `-Targets` accepts CMake target
names. By default x86 builds only `BC2NativeProbe`; x64 builds only `BC2XrHost`,
including their dependencies. Build `BC2NativeTrace`, `BC2Observe` or test targets
explicitly when needed. `-FullTests` without
`-Targets` builds all targets first. Explicit targets override these defaults. Tests never run implicitly. `-FullTests` runs offline CTest for this architecture
only; it does not run another architecture, Python or headset checks. A focused
`-TestRegex` invokes CTest once and fails if no tests match. Build the selected
test targets first (in the same invocation or earlier); CTest does not build them.
Do not combine `-TestRegex` and `-FullTests`.

Use `-BuildDirectory path` for another cache, matching its compiler architecture.
`-Configure` reruns configuration without clearing existing profile options.
Only an explicit `-MagazineGeometryHeader path` (or empty string to disable it)
changes that option in an existing cache, and requires `-Configure`. A fresh
build directory uses ordinary source defaults and checkpoint 204 geometry; it
does not inherit historical native-operation receipt requirements. These default
directories leave selected candidates and the old `build/x86`/`build/x64` caches
untouched.

Logs live in the chosen build directory's `logs/`; `build-receipt.json` records
compile targets and whether tests ran, including the focused regex. Skipped tests
are `tests_run=false`, `tests_passed=null`. A new attempt removes the previous
receipt before running; failed attempts cannot leave a success receipt.
A targeted build/focused test receipt describes that scope, not acceptance of
every target or the whole current repository.

Use the smallest useful check for a change. Both architectures, full CTest and
`python -B -m unittest discover -s tests -p 'test_*.py'` are explicit broader
checks chosen for a concrete risk or requested release/checkpoint, not automatic
requirements for shared changes, edits or commits. Batch edits before compiling
and reuse passed checks while their inputs remain unchanged. Documentation-only
edits need no compiler run. Source development and local builds need no routine
approval, headset acceptance or historical receipt. Ordinary CTest does
not launch BC2, select the installed XR runtime or run optional hardware GPU
probes. The x86 native module matches BC2's architecture; the x64 OpenXR host is a
separate process. A successful x64 build does not provide a native BF3/BF4 adapter.

## Checkpoint 203 configuration (historical)

```powershell
.\Build-Checkpoint.ps1 -Architecture x86 -Jobs 2
.\Build-Checkpoint.ps1 -Architecture x64 -Jobs 2
```

This explicit reproduction command configures `build/checkpoint203/{architecture}`
and runs full CTest. Its exact source receipt may reject newer source; this does
not block the ordinary developer build above.

Source snapshot `1764186` includes the later physical weapon authority and
inventory implementation as well as PR #9's pump/rack and switching work. The
checkpoint number describes the selected calibration/receipt, not current feature
coverage; it is not a receipt for the newer adapter. See [STATUS.md](docs/STATUS.md). No new build or receipt verification is
claimed by this documentation correction.

This configuration selects the shared retirement correction described in
[checkpoint 203](docs/RELOAD-TRANSITIONS-203.md), using calibration headers retained
from 202. The omitted registry fixtures affect two test targets only.
`profiles/checkpoint202` contains mod-authored contact/rail values and identity
hashes, not game meshes or animations. Some header comments retain their original
"private" wording from local development. Experimental geometry is labeled as
such; it is not automatically measured or accepted for every weapon.

The checkpoint 203 operation header and source receipt are in `profiles/checkpoint203`.
The older 202 source receipt remains historical; it intentionally will not verify
against changed source. To reproduce the initial publication, use commit
`a820f76b30a2451014c2bd1cb9a9460ab7d685d3` and its build script.

The operation header is pinned to an exact source dependency closure. Its receipt
is byte-sensitive, so `.gitattributes` disables automatic line-ending conversion.
Changing covered native code makes verification fail. Review and validate the
changed adapter before updating its operation receipt; changing a hash alone is
not new native evidence. Default builds remain available for policy development.

Build caches retain options. To return to defaults, use a fresh checkout/build
directory or explicitly clear the optional CMake variables listed in the script.

## Running development code

Use `Launch-BC2VR.vbs` / `DevLauncher.ps1` as the normal entrypoint. See
[the launcher guide](docs/LAUNCHER.md) for runtime choice, prepared packages,
player/test settings and profiles. The Build tab exposes development builds;
command-line backends remain available for automation.

The game needs its compatible 32-bit executable and Direct3D 11 path. The locally
researched executable SHA-256 is
`3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258`.
Other builds require compatibility investigation. No game files are supplied.
A compile-only DLL is not a complete playtest package: its generated profile
options, assets, host and helpers must match the intended package. Preserve
installed candidates and active runtime work when compiling locally.

The x64 host needs the Khronos OpenXR loader. Source builds do not supply the
binary; CMake can copy the loader from `runtime/openxr/win64/openxr_loader.dll`
beside the host. Preserve the packaging tool's loader and payload hash checks.
Generated body/hand assets remain local and are prepared from the user's game.
Do not commit them. Restart the game when replacing a mapped native DLL.

## Optional local tooling

For automation, `Setup-BC2VRPreview.ps1 -GamePath <installation>` creates ignored
`config/local.json`; native observers also accept `BC2_GAME_PATH`. The bounded
installed-executable test is opt-in through `FVR_TEST_INSTALLED_BC2=1` and reads
the file without launching or attaching to the game. Disassembly helpers need
`pefile`/`capstone`; screenshot helpers need Pillow. These are tool-specific
dependencies, not extra checks for each source edit.

`tools/Prepare-BodyAmmoAssets.ps1` prepares local body-display geometry from the
game installation when needed. Its generated `.fvrprop` output stays private.

## Automation and historical reproduction

`Play-BC2VR.ps1`, `Start-BC2VRSession.ps1`, native trace and replay helpers are
backend interfaces. Consult their current parameter help for automation; do not
make a new player workflow depend on memorizing their switches. Add player/test
settings to the unified launcher.

[Replay](docs/REPLAY_INPUT.md), [test range](docs/BC2_TEST_RANGE.md) and
[native trace](docs/NATIVE_TRACE.md) retain technical reproduction details.
Use their recorded checkpoint when reproducing an old result. Old test counts,
source receipts, absolute stage paths and selected-package hashes do not govern
ordinary current builds.

`tools/Build-SingleplayerPreview.ps1` is a legacy packaging backend: it still
expects the old `build/x86`, `build/x64` and `reports/*-build.json`/test-log layout,
and calls Build.ps1 without requesting tests. It is not compatible with the
current default developer build contract as written. Its historical guide is
not a validated current release recipe. This documentation cleanup does not
repair or execute that backend.
