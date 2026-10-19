> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Reload guidance, audio and recoil continuation

## Current source and package

Integration source: `codex/bc2-resume-20261011` in
`%USERPROFILE%/Documents/Codex/2026-10-09/task-4/arsenal-compat-source-40dc698`.
Native DLL and XR host compiled from `0524f7c8bc1871db5266187af1decdd3035ba835`.
Main still has older native source; its handoff points here. No push occurred.

Launcher: `<repo>/build/kf2-recoil-0524f7c/BC2-VR/Launch-BC2VR.vbs`.
ZIP: `<repo>/dist/BC2-VR-20261011-kf2-recoil-0524f7c.zip`.
SHA256: `73e31e5638598e06a556dced3165ebed702e63258001d6847facd763702988b8`.
All 50 payload hashes verify. Assets and static helpers retain their separate
base/helper provenance. The installed/selected playtest build is unchanged.

## Implemented

- Finite visible pouch magazine and mapped charging-handle guidance. Actual
  magazine stays opaque with amber hint tint; small hollow cues cover missing
  meshes. Both eyes share the snapshot and original owner/space/lifetime.
- Button/hand/recovery origin survives delayed physical receipts. Only successful
  current-owner actions emit feedback. Audio diagnostics identify binding and
  dispatch failures; optional exact local audio audition remains in the launcher.
- Native GunSway rotation transfers once to the visual weapon and attached hands.
  Native hip/zoom response, runtime modifiers and angular recovery remain native.
- KF2-shaped backward kick uses the selected gun's original BC2 configured
  recoil amplitude: native half-to-full range gives mean `0.75 * amplitudeDegrees`.
  No invented per-gun strengths. Positive verified data maps to KF2's logarithmic
  0.8-4 cm impulse, 45 ms attack, 0.15-0.6 s exponential recovery, 5 cm stacking
  cap, and half impulse while actually braced. Native instantaneous modifiers are
  retained in angular recoil, not claimed as part of this configured strength.
- Authoritative accepted-shot receipts trigger kick once, retaining original QPC
  and owner/equipment identity. Native client copies cannot duplicate the kick.
- Shot haptics use the same native strength weight; the genuinely supporting hand
  receives 65% amplitude and 80% duration. A muzzle-support flag no longer causes
  an unbraced offhand pulse. Weighted feedback uses the paired new XR host.
- Raw contacts, tracked controllers, native shot rotation and HMD remain stable.
  Kick follows corrected controller aim; calibrated weapon axes match that bore.
  Generic native-attachment weapons still lack proven visual/ballistic alignment.
- Native recoil defaults on. Unified launcher exposes No Native Recoil and
  optional Native Recoil Trace; no command-line setup is required for playtesting.

## Verification and coverage

Incremental two-job native DLL and XR host builds passed. Focused recoil-envelope
and feedback wire/policy tests passed, including duplicate shots, owner changes,
expiry, stacking, bracing, recovery, weighting and invalid payload rejection.

Two normal gameplay firing replays completed with valid reports, probe/bootstrap
exit 0, no new crash report and responsive games during post-probe observation:

| Weapon | Native mean degrees | Confirmed kicks | Moving kick poses | Maximum backward displacement |
|---|---:|---:|---:|---:|
| XM8_sp_s | 0.5625 | 4 | 161 | 3.355 cm |
| M9-3 | 0.75 | 6 | 84 | 4.850 cm |

Reports are inside the candidate at `reports/native-trace-20261011-221358-100`
and `reports/native-trace-20261011-221654-033`. Both completed all 507 input-tape
ticks and read positive native strength for every accepted kick. Each recorded one
rejected kick pose; no rejection reason is recorded in that counter. No headset was
used, so haptic sensation and comfort remain playtest observations. The initial
M9 attachment was too early for the range's temporary give hook; waiting for that
existing setup to finish resolved attachment without a source change.

Owned test processes were closed and settings restored. All six protected
selected-install hashes matched. The private harness's `capture_incomplete`
label means no hand-capture metadata in normal gameplay mode; final reports,
not that legacy label, contain the firing evidence above.

Current coverage: 5 base guns / 6 exact assets are eligible for native-strength
feel; 4 base guns have complete physical-mechanism code (XM8, SPAS, F2000, M9).
AEK is magazine-only. Runtime kick is checked on XM8 and M9; complete headset
reload-flow acceptance is not established for all four. See
[weapon coverage](WEAPON-VR-COVERAGE-20261010.md) for the distinction between
catalog/loading, generic VR, exact profiles, mechanisms and live validation.

## Remaining

- Headset feel/appearance checks for guidance and the new recoil/haptic behavior.
  Guidance props depth-test against each other, not native scene depth.
- Rack audio is not audibly proved. Exact M9 graph exposes Start only, with no
  separate seat/button event or seek parameter. Do not borrow unrelated timing.
- XM8-FX-001 muzzle/ejection placement remains open; recoil does not fix the
  reported floating/offset effect. Broader aim/support/muzzle calibration remains.
- Broader gun mechanisms, complete reload-flow validation and recoil coverage.
  Holstered/unsupported weapons are not granted confirmed-shot coverage merely
  because they load. Native zoom policy was not changed to follow bracing.
- REA target closure is unconfirmed after prior request/close timeouts. Existing
  read-only saved-project evidence supplied the pinned native strength contract;
  no server or unrelated game was killed. See `re/BC2_NATIVE_RECOIL.md`.
