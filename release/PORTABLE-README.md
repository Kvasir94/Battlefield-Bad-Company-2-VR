# Bad Company 2 VR

1. Extract this entire ZIP to a writable folder (for example Documents\BC2-VR).
2. Double-click **Launch-BC2VR.vbs**.
3. Choose the folder containing your installed **BFBC2Game.exe**.
4. Start your VR software, connect the headset/controllers, and press **Play in VR**.

Runtime **Auto** uses your active OpenXR runtime. VDXR and SteamVR can also be
selected explicitly; the optional Runtime JSON field supports a custom install.
The launcher remembers settings. No terminal, compiler or Python installation is
required for normal launch. Windows PowerShell 5.1 and Windows Script Host must
be enabled (included with Windows). Python is optional for desktop window recovery.

This development build supports the tested BC2 executable. The launcher checks
its version and the included files before launching. It does not contain the game;
use your own installation. The current start location is the SP_02 test range,
with XM8, M9 and Arsenal choices. This is not a finished campaign-wide release.

**Test options** holds optional controls, interaction, rendering and diagnostics.
**Profiles** saves named settings. Logs are written to `reports` inside this
folder; local installation settings are generated in `config/local.json`.
Personal launcher settings are stored under `%LOCALAPPDATA%\BC2VR`.

Native hands include ten mesh variants. Assault has automated both-eye game
coverage; other variants and real headset feel still require playtesting.
Spare-magazine and charging-handle guidance is enabled by default; Test options
can disable it. Small amber cues indicate available pouch supply and a needed
charging action. Audio-profile and recoil-trace options are diagnostics;
recoil and event-specific reload audio coverage depends on the packaged build.
To update, extract the next release to a new folder and open its launcher.
No game files are overwritten. Source and third-party license notices accompany
the project; see LICENSE, THIRD_PARTY_NOTICES.md and licenses/.
