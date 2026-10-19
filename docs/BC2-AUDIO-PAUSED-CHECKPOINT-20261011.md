> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 audio paused checkpoint — 2026-10-11

This checkpoint adds rejection diagnostics only. Existing cue admission, exact-owner/proof checks, one-attempt semantics and deadlines are retained. Report fields distinguish proof/owner/request/callback/dispatch rejection stages and ordered Request guards; they do not identify every internal resolver/Poll leaf. No cue behavior fix or engine acceptance is claimed.

Private recording and audition helpers were repaired separately: device-frame placement preserves timestamp/discontinuity evidence, and revised capture uses 20-second native sampling, 30-second recording and an exact identity-matched physical-rack marker with required baseline/tail. Acceptance also requires completed 721-tick replay. These helpers are preserved in the private audio checkpoint repository, with originals and raw evidence kept local.

Validation before pause: mapping checks7/7, marker checks13/13, replay gate checks6/6, script parsing/hash pins passed. Earlier mixed-source native compile and both focused audio CTests passed. The later clean isolated two-file rebuild failed at link with LNK1104 opening Bc2ReloadSoundGateTests.exe; its output is not accepted, and its compiler lease was released. No retry or package was started after the pause.

The one earlier audition rejected the cue and recording quality and consumed649/721 replay ticks. Cleanup and restoration verified; selected baseline remained unchanged. No further game test, installation, selection or push occurred. Diagnostics are untested in engine.

All work is paused for architecture reassessment. Remaining: investigate the isolated linker output-file access failure; complete isolated native/audio validation; prepare a matching unselected receipt-backed package; rebind exact driver pins; only then perform a parent-coordinated bounded audition after renewed authorization. Assets/import require no new work. Do not resume the hands lane from this note.
