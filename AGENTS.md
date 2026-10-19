# Frostbite VR development rules

This is the public export's policy template. Keep its shared development rules
consistent with root AGENTS.md; private machine paths and upstream remote
configuration are intentionally not exported.

- The mod may own identified player state: write fields, call native functions,
  patch code and keep persistent overrides for a better VR result. Universal
  restoration is not required.
- All guns follow the KF2 VR standard: sample or play the original game's poses
  and animations. Physical VR actions drive native animation progress, including
  wrist and finger poses for support, pumps, charging handles and reloads. Keep
  the primary VR hand controller-authoritative and fit the weapon to it. Rigid
  hand attachments and approximate fallback poses are not finished support.
- Verify signatures, calling conventions and player-object ownership before
  native calls, hooks or writes. Preserve coherent rendering and ammo accounting.
- Use Ghidra (headless on a read-only project copy, or any equivalent
  disassembler) for native reverse engineering. Record addresses, signatures
  and evidence in docs/re/.
- Enable a feature once it works in game; retain an environment kill switch
  instead of default-off evidence gates.
- Prefer direct work on main when safe and coordinated; use short-lived branches
  when risk or concurrent work warrants isolation. Preserve unrelated changes
  and active builds. Push only to your intended writable fork, never upstream.
- Default to incremental affected-target builds with two jobs. The native DLL is
  x86 and the XR host is x64. Tests are opt-in; choose the smallest useful check.
  Full CTest, both architectures and Python are not per-edit or per-commit gates.
  Batch edits and reuse passed checks while inputs remain unchanged. Docs-only
  changes need no compile or tests.
- Ordinary source work and local builds need no approval pause, checklist,
  historical receipt or headset acceptance. Broader validation belongs to its
  actual risk or requested release. Preserve selected packages and active work.
- Use Launch-BC2VR.vbs / DevLauncher.ps1 as the normal entrypoint. New player/test
  settings belong there; retain CLI backends for automation. Preserve package
  hash checks, runtime selection and coordinated builds. Do not add game-launch
  locks. Settings and profiles live under %LOCALAPPDATA%/BC2VR.
- Never commit game files, generated game-asset caches, saves, recordings,
  machine-local configuration or credentials. Retain licenses and provenance.
- Start with docs/README.md and docs/STATUS.md. Historical records preserve
  evidence, not standing instructions or current agent assignments.
