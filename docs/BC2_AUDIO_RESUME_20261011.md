> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Audio build recovery — October 11, 2026

The isolated audio source checkpoint `5f8f1e6db3ac2783cd0544ed37a03e966a9a9511`
now compiles, and Bc2ReloadAudioCue and Bc2ReloadSoundGate pass. No cue behavior
was changed and no new game run or recording was performed.

The retained first failure was LNK1104 opening Bc2ReloadSoundGateTests.exe at
the final manifest link. The first actual resumed incremental build linked that
target successfully, then failed with LNK1168 reopening Bc2ReloadAudioCueTests.exe
for writing. No matching test process or Defender detection was found; the cue
output subsequently opened exclusively for read/write. The next unchanged
incremental build passed. This establishes a cleared output-access failure,
not the identity of its historical lock owner. No process was terminated, no
security setting changed, and no source or admission guard was weakened.

The successful private receipt is task-6/audio-isolated-build-private/
gate-20261011-190827/status.json. It records two compiler jobs, clean source and
stable private header hashes, two focused passing tests, and compiler release
at 19:09:55 UTC. Native DLL SHA256:
`8C3971B5A23CF571006AD14180EE4D05590B54A42DB61D438EBD0BB55349C211`.
Earlier failed receipts remain intact.

Exact isolated package `build/playtest-20261011-191013-5f8f1e6` is prepared,
unselected, with VerifyOnly and all 24 manifest hashes passing. New private v3
supervisor/worker copies preserve v2 and pin that package, source and native
binary. Their PowerShell parse, 18 dependency hashes, and exact production
721-tick loader/precondition checks pass. The worker is byte-identical to v2;
no cue behavior changed. The future command and exact pins are in task-6/
BC2-AUDIO-V3-PREPARATION-HANDOFF.private.md. No audition was launched.

The shared integration audio diagnostics remain the two-file source change
checkpointed at `3aa78af`; the isolated audio source excludes the later shared
hands checkpoint and must not be described as the complete combined candidate.

Runtime evidence remains one cue rejected, zero played, incomplete replay and
rejected recording. A later explicitly authorized audition needs a verified
matching package, exact package/source/native pins, the epoch-zero 721-tick tape,
actual-rack identity/timing join, full replay completion and valid endpoint
coverage. No audible-success claim follows from compilation or offline tests.
