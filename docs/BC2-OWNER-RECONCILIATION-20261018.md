# BC2 owner reconciliation — October 18, 2026

Read-only reconciliation completed without a quota or permission denial. The
usage read reported 95% consumed. No worker retry, reset, compilation, game,
receiver, recording, runtime recovery, selection or deployment was performed.
KF2 retains sole actual-game coordination; no October 11 lease is assumed active.

## Current source and package identity

Main is `969543085ce437f141291fc987b20f3b2881f9e8`. Its pre-existing documentation
consolidation and export-policy changes are preserved. The historical cumulative
integration checkout has since advanced to clean `3be9c94bceabdc62f791317432080b8a286c05dd`;
it is no longer the October 11 morning `64f83dd` checkpoint.

The unified launcher's saved preferences choose
`build/playtest-reloads-20261014-split-audio/BC2-VR`, source `9695430`, host source
`5f018a1`, helper source `422960b`. Fresh independent rehash passed **76/76**
manifest payload files. Its manifest still says `selected=false`; this report
distinguishes the launcher's chosen package from that historical manifest field.
The old `reports/BC2-Selected.private.json` record and installed wrapper are absent.
Nothing was recreated or reselected.

The loose `build/x86/BC2NativeProbe.dll` and `build/x64/BC2XrHost.exe` retain the
older `4f94cac` package hashes. They are different from the binaries in the
launcher's chosen October 14 package. Do not infer which binary a future launch
loads from an old task pointer or the loose files alone.

Preserved historical packages also pass fresh independent rehash:

| Package | Payload hashes | Scope |
| --- | ---: | --- |
| `playtest-20261011-055537-4f94cac` | 22/22 | Original selected baseline and retained rollback payload |
| `playtest-20261011-191403-99610cd` | 24/24 | Later combined candidate; old task prepared pointer |
| `playtest-20261011-191013-5f8f1e6` | 24/24 | Isolated audio diagnostic, not the complete combined source |
| `native-hands-ready-a0fa4fe` | 34/34 | Native glove replacement candidate with recorded Assault proof |

The current launcher preferences were copied to the private reconciliation
directory before this work, and their before/after hashes match. Existing
packages, backups and settings were left intact. No rollback was executed.

## Preflight and evidence reconciliation

The private epoch-zero, feed-capacity-20 rack tape still matches SHA-256
`E149A24D62D475DAB2CDD4E52FB17A987D87838AC2A60C929087A08EB39C7E74`.
Its unchanged 216 rows describe 721 ticks. The retained production loader was
rerun offline: `loaded=true ticks=721 precondition=1`, exit zero. The original
audition driver's PowerShell parser reports zero errors. This validates saved
preflight, not suitability of that old driver for current source or runtime.

The later audio record supersedes the early pre-replay failure: one historical
audition consumed 649/721 ticks, rejected the cue, played zero, and rejected the
recording. The isolated audio link-access failure was subsequently cleared with
native compile and both focused audio checks passing. No accepted audible M9
cue follows. See [audio recovery](BC2_AUDIO_RESUME_20261011.md).

Later production XM8/SPAS event audio and native glove replacement are implemented;
the older morning claims that these have no implementation are superseded.
Saved evidence includes native Assault replacement in both eyes, ten-pack loading,
and silent/muted mixer checks. This turn did not repeat those runtime checks.
See [hands evidence](re/BC2_HANDS_READY_20261011.md),
[reload sampling](re/BC2_NATIVE_RELOAD_SAMPLING.md) and
[audio implementation](re/BC2_PHYSICAL_RELOAD_AUDIO_20261014.md).

Audible event attribution, headset endpoint/timing/feel, current-candidate human
acceptance and wider character/map/weapon coverage remain pending. An old M9
audition, a passing package hash, or muted mixer counters do not resolve them.

## Saved handoff and stopping state

Private evidence is saved under the existing task-4 workspace in
`reconciliation-20261018/receipt.private.json`, with preference and pre-edit
handoff backups. The receipt contains exact source status, package manifests,
binary hashes, tape/preflight results and unchanged-selection evidence.

BC2 is stopped after offline reconciliation, holds no lease acquired by this
turn, and has admitted no runtime work. No source behavior was changed. The
remaining blocker is coordinated runtime/audible/headset validation, outside
this turn's authorization. Resume from current launcher/source identity and
current KF2 coordination, rather than an October 11 scheduling assumption.
