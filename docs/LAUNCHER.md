# Unified BC2 VR Launcher

Open **BC2 VR Launcher** on the desktop, or double-click `Launch-BC2VR.vbs`.
This is the standard user entrypoint. PowerShell remains an internal backend;
there is no console to operate.

The portable ZIP includes the mod and launcher together. Extract it to a writable
folder, open Launch-BC2VR.vbs, and choose BFBC2Game.exe. Auto uses the registered
OpenXR runtime; a runtime JSON picker supports custom installations. The public
launcher shows Playtest, Test options and Profiles. Developer tabs are available
in the source checkout. Verify build works without the game or headset.

- **Playtest:** choose the prepared build, runtime, XM8, XM8 + SPAS,
  SPAS-12, M9 or Arsenal fixture,
  native/legacy hands, graphics policy, automatic/custom eye size, depth and
  pipeline mode. Save settings, verify the build, check the headset or play.
- **Test options:** optional controls, magazine/interaction, rendering and trace
  settings declared by the chosen build. Check Override to change a row.
  Defaults remain in effect for unchecked rows; numeric ranges and choices are
  validated before launch. Available options follow the chosen package.
- **Developer launch / Test range cfg / Developer switches:** existing custom
  range, mounts, weapon slots, flat-mode and lower-level diagnostic workflows.
  These are developer workflows, not overrides for the Playtest button.
- **Build:** existing incremental native/host/test-range build tools.
- **Profiles:** named profiles include both playtest settings/test options and
  developer configuration. Last-used settings and profiles are stored in
  `%LOCALAPPDATA%/BC2VR`; older local profiles are imported on first use.

Playtest uses `tools/Start-BC2Playtest.ps1`, which verifies the selected package's
manifest before calling its native launch wrapper. It preserves runtime choice,
headset readiness, graphics settings restoration and the
existing selected installation. Changing the build folder selects a launch
source in the GUI; it does not install that package.

New test features belong in this GUI. Keep automated CLI backends, but avoid new
one-off user launch commands. Add typed wrapper parameters for advanced test
options; the Test options tab discovers supported scalar parameters from their
AST metadata without evaluating the wrapper. Lifecycle/path arguments stay under
launcher control. New workflows that need more than a parameter get a dedicated
control or tab here.

Reload guidance is enabled normally. **No Reload Guidance** in Test options
disables the spare-magazine display and pouch/charging-handle cues.
**Native Recoil Trace** records native baseline, sway and shot-composition
ordering without changing recoil. **Rack Audio Profile File** optionally selects
an existing local exact audio profile for one physical-rack audition per game
process. Leave it empty for normal play; no private profiles are shipped.
The profile file contains up to 64 semicolon-separated rows, each with five
colon-separated, nonzero distinct 32-digit hexadecimal identities in the native
profile order: weapon, tree, animation, sound, map. This diagnostic does not
establish production reload audio or provide seat/button sounds.

Validation: `tests/Test-PlaytestLauncher.ps1 -PackageRoot <prepared-package>`
constructs the form, exercises argument and option validation, and launches no
game. `-SnapshotPath <png>` optionally renders an off-screen preview.
A historical candidate's file count does not describe the selected package.
