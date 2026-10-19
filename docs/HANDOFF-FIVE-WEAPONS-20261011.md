> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

## Latest installed playtest — 2026-10-12

Human requested immediate installation and no further replays. Selected package:
`<repo>\build\playtest-b74560c\BC2-VR`, native/host source
`b74560c983ff6fecd490403517cbbb9e0ab18401`, retained helper/launcher422960b.
All65 payload hashes verified. kva, default playtest settings, desktop shortcuts
and BC2-Selected.private.json point here; launcher reopened. Previous package
five-distinct-04d8da8 remains intact as rollback. No game currently owned by us.

Includes readiness input masking (shotgun premature firing audio), lifecycle and
held-mag cleanup recovery, support-grip retry, controller-authoritative wrist,
correct recoil basis,11 original-action profiles,31 native contact entries,
32-part pouch/holster cache and13 native material prop packs. Final native/host
builds passed; focused readiness/recovery/recoil/action-profile tests passed.
The prop branch mapping correction compiled after the first capture; its live
appearance is still unverified. Do not claim headset acceptance or fully sampled
M416/AKS/UZI/XM8C charging-handle coverage. Those effective native Rack samples
remain unfinished; existing gameplay remains available for human testing.

One earlier M416 capture was healthy but missed bound action rows because the
capture mode did not enable the mesh observer; that diagnostic is fixed. The
subsequent SPAS attempt failed before replay on prepared-game identity check,
restored settings, and is not test evidence. No more replays until requested.

# Five distinct weapons: implementation checkpoint

Native/helper source: `04d8da84ab8bf7aa8ab7f086c6900220505c7d97`.
Integration checkout remains `arsenal-compat-source-40dc698`; the main checkout
and selected package do not contain the same native source.

The counted additions are M416, MG36, VSS, AKS-74U and UZI. XM8 Compact and
XM8 LMG are bonus coverage, not two more distinct additions. All seven use
exact native configuration profiles, authored grips/fingers and measured
magazine geometry. Native mechanism geometry and the distinction between
sampled native strokes and explicit VR strokes are documented in
[the mechanism notes](re/BC2_FIVE_WEAPON_MECHANISMS.md).

Arsenal now prioritizes one base Give name per counted gun, in the order above.
The helper accepts exact Give names as well as legacy whole-family names;
scope/Kobra variants no longer occupy the first five places. The package
builder includes this source range configuration and the rebuilt helper.
Individual launcher choices include all seven additions.

## Package and selection

Candidate: `build/five-distinct-04d8da8/BC2-VR`.
Archive: `dist/BC2-VR-20261011-five-distinct-04d8da8.zip`.
SHA-256: `06faaa676562315403a45148b808080eac23814b14824329eba91bd1675644a8`.
Host source remains `0524f7c8bc1871db5266187af1decdd3035ba835`.
The user subsequently requested installation for personal testing. The
`five-distinct-04d8da8` package is now **selected**, both desktop shortcuts point
to its unified launcher, and the launcher was opened. User settings and named
profile `kva` retain Arsenal, VDXR and IK hands. The prior `kf2-recoil-0524f7c`
package remains available; selection/settings/shortcut backups are in
`reports/rollback-before-five-guns-20261011-191533`.
Both packages include the explicit host-local Utility-module import
that fixes launcher environments missing `Get-FileHash`.

## Verification completed

- Incremental x86 DLL and Arsenal helper builds passed with two jobs.
- Authored contacts, 14 grip groups, measured magazine partial/empty reloads
  and mechanism checks across the generated configurations, and effects checks
  passed. These magazine checks use a mocked native API.
- Registry exporter: 18 checks; grip exporter: 35 checks.
- Current-game discovery rejected all 121 signature/body mutations.
- Packaged Windows PowerShell verification passed all 50 payload files;
  packaged GUI self-test passed.
- Earlier `3d24bce` live M416 and MG36 firing runs admitted their exact profiles.
- VSS mounted `mp2_common` overrides the base magazine count from 3 to 4.
  Merged native extraction corrected the descriptor without relaxing matching.
- Live `04d8da8` VSS run `native-trace-20261011-232644-366`: replay admitted,
  eight server shots, all three firing copies ended at 12/140, healthy conserved
  ledger, zero ammunition write failures. Native recoil and visual kick both
  transferred; all retained hand evidence used authored grips.
- That VSS run admitted 18 effect spawns and 18 FOV bypasses. Retained flash
  offsets were within 0.2 mm of the native configured offset. See
  [the native FOV evidence](re/BC2_EFFECT_FOV_COMPENSATION_20261011.md).
  Eleven muzzle writes preserved native rotation/padding; three delayed server
  shots fell back after the original event pose deadline. Do not describe this
  as every shot using a tracked origin.

## Remaining live work

Full native physical reload/rack sequences are **not yet proven for these five**.
AKS-74U and UZI still need live firing/admission and reload checks. Check the
first five existing `arsenal:` enumeration lines in the helper log to confirm
actual mounted order. No headset visuals, felt haptics or audible cue results
are claimed by desktop replays.

Private input-only tapes and the existing harness are under
`reports/five-weapons-20261011`. M416, MG36 and VSS reload tapes are ready.
The generator requires an actual admitted runtime anchor before producing
AKS/UZI tapes. Use 240 receiver pairs with `--scene-controls-neutral`; this
spreads neutral transport over 55 seconds. `--sink-seconds` publishes no input
and did not bootstrap this native replay path. The failed empty-input run
`232448` is not a weapon failure or acceptance result. Earlier harness attempts
also failed before attachment due to an invalid pair count; neither establishes
reload coverage.

Further native checks stopped when an unrelated KF2 process started. The
harness correctly refused another game session. Do not terminate that game.
The user's subsequent request selected this candidate for their own playtest;
installation is not a claim that the pending live checks passed. Resume the
existing input-only tests when the shared game session is free and address
any actual failures. No new fixtures or separate launcher are needed.

## User-directed weapon update

Native checkpoint `278ba9ec8b4ee1afe6f4fa5359ab7222b8536a2a` built successfully
and is installed in the existing selected package. It adds explicit mechanism
hand ownership, atomic support-to-mechanism transfer, stale-grab cancellation,
and optional ammunition-neutral UZI cocking without forcing a reload rack.
The existing physical interaction checks passed. Firing feedback reports now
separate channel-write success/failure from headset haptic delivery.

AKS74u live report `20261012-003607-559`: seven confirmed server shots, all
three native copies 23/210, healthy/conserved. UZI `20261012-003921-997`:
eleven confirmed server shots, 21/224, healthy/conserved. Both admit exact native
profiles and transfer native recoil. A prior UZI attempt expired during trace
startup before native attachment and provides no weapon result.

All five input-only reload tapes are ready, but the user then explicitly
requested immediate installation for personal testing. Automated full reload
runs were stopped accordingly; do not mark them verified. The black-screen
repair owner separately owns the helper/normal-start rendering changes, which
must be preserved alongside this native DLL. No further automated game launch
is scheduled by this weapon task.
