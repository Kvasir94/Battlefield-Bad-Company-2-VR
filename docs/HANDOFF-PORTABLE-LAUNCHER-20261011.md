> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Portable mod + launcher checkpoint — 2026-10-11

## Finished after continuation

Portable ZIP: `<repo>/dist/BC2-VR-20261011-portable.zip`.
SHA256: `253ce802fc5cf63771ad453bae1300378fec8341a99d31ac9f281360b0f1c23f`.
51 files total; 50 hashed payloads. Native source remains a0fa4fe; launcher
source is 16a494c; static helper source is 252a790. Full commits are recorded in
package-manifest.json. Candidate and installed/selected files remain unchanged.

The extracted ZIP passed the existing GUI argument/options/profile tests and
GUI Verify Build from a path containing spaces, without game/headset launch.
All shipped PE imports were inspected; no separate MSVC runtime is required.
The existing Bc2Xm8BoltTests target was built incrementally with two jobs under
the shared compiler mutex: all 12 groups passed, including loading the actual
sanitized packaged asset (100 triangles). No new game fixture was created.
Launcher preference was restored to build/native-hands-ready-a0fa4fe.

Portable changes are reconciled onto codex/bc2-resume-20261011 in the integration
checkout at 49ac1b2. This branch contains both native source and launcher;
use it for source publication, not main's older native tree. No push or release
upload occurred. Real headset feel and the other nine variants in game retain
the coverage limits below. The archive is ready to attach to a development
release; it is not a claim of finished campaign or headset validation.

The remainder preserves the earlier checkpoint and its now-completed steps.

User requested: package mod and unified GUI together for GitHub; KISS, no
unnecessary fixtures. User then explicitly requested this checkpoint NOW.
Work is paused at that request. Do not call the current ZIP release-ready yet.

## Completed

- Main already contains the unified GUI commit c245584. Desktop shortcut is
  `%USERPROFILE%/Desktop/BC2 VR Launcher.lnk`, opening <repo>/DevLauncher.ps1.
- Portable GUI chooses its own extracted folder instead of a stale saved build.
  Game executable picker, Auto OpenXR runtime discovery (registry, Steam library
  paths, VDXR), and optional runtime JSON picker added. Release GUI hides developer
  and build tabs; playtest/options/profiles remain. Settings stay per user.
- Public package manifest support with file hashes and path-containment checks.
  Verify does not require the game or a headset. On Play, game selection is checked
  and local config is generated; private developer candidates retain their fixed
  config behavior. Existing shared session mutex/settings restoration preserved.
- tools/Start-BC2Game.ps1 retains the candidate feature environment but removes
  machine-specific paths and fixed private receipt/hash assumptions.
- tools/package_bc2vr.py creates ONE ZIP from an existing verified candidate and
  an explicit launcher/runtime allowlist. Excludes local config, private launch
  manifests, logs, development trees and original game archives. Native hand packs
  and required authored assets remain binary release payloads, never Git files.
  XM8 metadata paths are sanitized and its validation export hash updated.
- Existing GUI checks were extended, not new game fixtures. Extracted ZIP into a
  different path containing spaces; GUI constructed, settings/option validation
  passed and GUI Verify Build validated 50 payloads. No game/headset launch.

## Current artifacts and provenance

Native candidate unchanged:
`<repo>/build/native-hands-ready-a0fa4fe`
Native source a0fa4fef10ab974ed937dfe219647b43f566285e; DLL unchanged.
Selected/installed package remains unchanged; nothing published or pushed.

Intermediate packaging checks (NOT final release assets):
- dist/BC2-VR-portable-check.zip
- dist/BC2-VR-portable-verify.zip (51 files including manifest)
- reports/portable package check/BC2-VR (extracted check)
- reports/native-hands-ready-20261011/portable-launcher.png

The packaging check ZIP predates the static helper rebuild below. Its launcher
commit field also predates this checkpoint; regenerate final ZIP after committing.

## Real dependency found and fixed in source/build

PE imports showed the pre-existing range DLL/injector required msvcp140.dll and
vcruntime140.dll. Changed tools/testrange/CMakeLists.txt to static MSVC runtime
in BOTH main and the existing integration checkout. Incremental rebuild under
Local\KF2VR_NativeBuild, two jobs, succeeded for BC2TestRange and BC2TestRangeInject.
Only existing /EH and shadowed-self warnings. No new gameplay fixture/run.

Rebuilt files are in:
`%USERPROFILE%/Documents/Codex/2026-10-09/task-4/arsenal-compat-build-20261011-055132/fixture-x86`
Source:
`%USERPROFILE%/Documents/Codex/2026-10-09/task-4/arsenal-compat-source-40dc698`
No candidate or installed file was overwritten.

## Next steps (small, concrete)

1. Recheck imports of the two rebuilt range binaries using existing
   tools/package_preview.py pe_info. Add a small explicit stage-binary override
   to package_bc2vr.py (or prepare a new candidate with those two binaries).
   The current packager still copies the OLD dynamic-runtime binaries from a0fa4fe.
   Record the helper source commit in the final package metadata; native DLL source
   stays a0fa4fe. Do not silently mislabel mixed build provenance.
2. Regenerate one final ZIP, extract/verify it once, and check the portable wrapper
   and all runtime dependencies. Reuse existing checks; no new gameplay fixture
   required just to validate packaging. Sanitized XM8 export SHA was updated;
   native parser acceptance of that sanitized pair has not yet been executed.
3. Minor GUI footer still says other tabs contain developer tools even when the
   public GUI hides those tabs; correct that wording.
4. Verify supported game hash and user config path flow; no clean-machine headset
   test has occurred. Native hands retain prior limits (Assault both-eye automated
   proof; other nine layouts/assets verified, headset feel still untested).
5. Restore LocalAppData/BC2VR/playtest-settings.json package selection to
   <repo>/build/native-hands-ready-a0fa4fe if it still points to the temporary
   extracted packaging check. VerifyBuild's normal save action changed that GUI
   preference; it did NOT change the installed/selected mod. Old GUI process12496
   may still be open with its pre-edit script; do not kill any game/session.
6. Finish docs/LAUNCHER.md portability notes (currently older wording). The new
   release/PORTABLE-README.md is drafted. No upload, GitHub release or push yet.

## Branch/source caution

Main has launcher work; the native hands/controls/audio source lives on
codex/bc2-resume-20261011 in the integration checkout. Main is NOT equivalent to
that compiled candidate. The integration branch has numerous native commits;
main has historical handoff-only commits plus the cherry-picked GUI. Do not
publish main as if it were the compiled a0fa4fe source. Reconcile or push the
correct source branch before release; never push upstream origin, only origin-bc2.

Unrelated main untracked files preserved: .claude/, a.txt, d.py,
docs/PLAYTEST-20261011-CONTROLS-AND-RELOAD.md,
docs/re/BC2_NAM_HANDGRENADE_CRASH_RE.md. Never stage these incidentally.
