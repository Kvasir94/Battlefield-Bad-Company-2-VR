> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Candidate status — 2026-10-11

## Glove appearance and ten mesh variants — October 11

The almost-black Assault glove was caused by cross-variant UVs under its native
material. Variant-native attributes now preserve its fabric shading. Short wrist
cuts and smooth recessed caps replace the long sleeve. All ten installed FP mesh
variants have generated packs, including separate Russian glove/finger materials,
Vietnam bandages and explicit NVA/Beach vertex layouts. The set loader preserves
exact GPU source hashes, owned-player admission and both-eye frame activation.

See [hands readiness and test sequence](re/BC2_HANDS_READY_20261011.md) for coverage,
build/package evidence and limits. Assault has saved both-eye appearance proof;
other variants have source/layout and asset checks pending map/class coverage.
Real headset feel is still untested. The selected build remains unchanged.
Final unselected candidate: `<repo>/build/native-hands-ready-a0fa4fe`;
34/34 hashes, two focused checks and the ten-pack in-game loader pass.

## Native replacement completed - October 11, 20:08 UTC

Native replacement is implemented and proved in the Assault campaign fixture.
Compiled source `a005db1`:8 exact GPU source hashes,4,752 replacement submissions,
both-eye saved frames with original arms removed, probe exit0 and no new crash
report. The native DLL and two focused checks pass. Unselected candidate:
`<repo>/build/native-skin-a005db1-assault` (25/25 payload hashes).
See [implementation and live proof](re/BC2_NATIVE_SKIN_IMPLEMENTATION_20261011.md).
The asset enables replacement; `FVR_BC2_NATIVE_SKIN=0` disables it. Headset
appearance/feel and other character variants remain unaccepted. Selected build
`playtest-20261011-055537-4f94cac` and installed files are unchanged.

Older checkpoint sections below are historical where they describe hands as
unimplemented or the replacement seam as unproved.

Read-only status check at 19:09 UTC, based on the local
`reports/resume-lead-20261011/baseline.private.json` (19:05:23 UTC), selected
manifest, current file hashes, integration source review and playtest ledger.
This note contains no private asset data. No build, game, selection or install
was performed by this review.

Parent closeout: source `99610cd9e45dfe5c3d56ded3e8eafefe52ab7672` compiled
the affected x86 DLL with two jobs; all seven focused controls/audio tests passed.
`build/playtest-20261011-191403-99610cd` passed VerifyOnly and independent
**24/24** manifest hash verification. The five protected installed/selection
files still match the pre-work baseline. No new game run, selection, install or
headset acceptance. These results supersede pending compile/package wording in
the individual review notes. Native DLL SHA256:
`93120BD9EF0C48D54A5FF6E95A8876EC048D87C39DB559E617D7F924AD1011DC`.

Tooling follow-up: REA's fresh import/full-analysis startup exceeded its
300-second MCP window and blocked close. Its exact owned Java/server processes
were identified, evidence preserved and stopped; the root server remains
responsive with no open target. REA target analysis itself is not repaired.
The permitted read-only saved-project fallback produced the
[native skin seam evidence](re/BC2_NATIVE_SKIN_SEAM_20261011.md). No unrelated
Java process was terminated or game launched. This documentation-only follow-up
does not change compiled candidate identity or claim new runtime validation.

| Layer | Identity / evidence | Status and limits |
| --- | --- | --- |
| Main source | `6449df3` | Includes renderer `57d7a24` via `66fc2bd`; not the cumulative integration tree. |
| Cumulative source | `codex/bc2-resume-20261011`, compiled source `99610cd`, base `3aa78af` | Includes later controls, quality, hands diagnostics and audio source. Continuation adds button-default receiver removal and diagnostic color-readback device guards. Subsequent documentation commits do not change the compiled code. |
| Selected package | `build/playtest-20261011-055537-4f94cac`, source `4f94cac`, renderer `57d7a24` | Manifest prepared 05:55:41 UTC; selected 05:55:41 UTC. All **22/22** package files match manifest and baseline hashes at 19:09:21 UTC. Manifest says `headset_accepted=false`. |
| Installed/selection state | Main `build/x86/BC2NativeProbe.dll`, `build/x64/BC2XrHost.exe`, selection JSON and both launch wrappers | **5/5** protected hashes match baseline. Installed DLL/host hashes also match their selected-package counterparts. Wrappers/selection were not changed. This does not establish every game-directory file or a currently running process's loaded modules. |
| New combined package | `playtest-20261011-191403-99610cd` | Prepared, unselected; **24/24** hashes and VerifyOnly pass. Fresh x86 DLL, inherited unchanged host/fixture provenance; no fresh x64 build claimed. Seven focused tests pass. Not recording-ready or headset-accepted. |
| Audio isolated build/package | Source `5f8f1e6`, exact clean-source gate | Native DLL and both focused audio tests passed at `gate-20261011-190827`. Unselected `playtest-20261011-191013-5f8f1e6` passed package VerifyOnly. This isolated diagnostic excludes the later shared hands checkpoint; it is not the combined candidate or a successful audition. |
| Headset observations | Current main `PLAYTEST-20261011-CONTROLS-AND-RELOAD.md` | Town building-edge parity and most observed explosion effects accepted in those conditions. Overall blur and snowy-cutscene eye mismatch remain. Depth extension active with zero reported depth pairs/frames does not prove successful depth submission. |
| Independent hands | Integration source and hands review | Diagnostics/backend work exist; independent replacement is not accepted. No hand appearance, wrist/finger motion, bilateral draw or replacement-readiness claim follows from these hashes. |

Selected installed binary identities (SHA-256):

- Native DLL: `0CE8DCE862253CD0783C4E04AF3DE81DEE7571407D26AC86A4BB535B8B50D4CE`.
- XR host: `4232809F13E84C24DDAE9F095047C2526DC78FE6F9AC264AB604ED4EC5D2BC69`.

The selected manifest already describes short A release/chamber fallback, 600 ms
A automatic reload, right-stick UP jump and native-menu B back. It does not
contain today's no-grab default. Its floating-hands description is the older
native wrist/finger-preserving arm-collapse mode with visual acceptance pending,
not the proposed independent native-hand replacement.

## Diff review

The capture patch adds public D3D device identity checks before copy/map,
recreates the cached mip on device changes and retires mismatched pending
staging resources with Failed/E_INVALIDARG. ComPtr temporaries retain compared
objects, target status is written before slot reset and the mismatch branch
immediately continues. No new owning raw pointer, API change or evident compile
issue was found. This remains uncompiled by this reviewer and cannot recover a
stale context pointer. Existing CPU stereo-capture tests omit the GPU glue.

The controls diff passes the parsed option through both Gameplay construction
paths and both magazine backends. It gates receiver-grab admission and its hover
cue, leaving button eject, pouch pickup, insertion and action/slide input intact.
Existing pull tests opt in; new default-path cases cover no receiver claim,
button reload and ammo conservation. No blocking source defect was found in this
bounded review. Tests were read, not executed here.

Review caught a legacy neutral-gate interaction: an ignored receiver grip could
disarm button removal. The patch now omits that ignored grip only from the idle
legacy removal gesture, preserving actual grip for supply and active insertion.
Both frontends include a regression for A while that grip remains held. Player
feel remains part of the focused follow-up.

## Short real-player sequence for the next matching package

Run this only after separate authorization for the identified combined package;
record runtime, weapon/variant, source/package and actual eye dimensions once.
This is a focused follow-up sequence, not a prerequisite for ordinary source
work or a claim that today's source is installed.

1. **Default A / no receiver grab:** with grab option unset, touch and squeeze
   around the inserted magazine/support grip. The magazine must remain seated;
   no receiver-grab cue should advertise an unavailable action. Short A must
   release it. Also try A while maintaining a support/receiver grip and record
   whether a neutral release is needed. Pouch pickup must remain available.
2. **Manual and automatic recovery:** complete a partial manual reload, then an
   empty reload with the weapon's physical bolt/slide action. Verify refiring,
   ammo/reserve counts and short-A chamber fallback. Interrupt one manual cycle
   safely, then use the 600 ms A automatic route; confirm recovery/refire without
   duplicated or lost magazines. Record seat/bolt haptics separately from audio.
3. **Empty magwell support:** after ejection, touch/reacquire the front grip and
   rotate the weapon. The empty well must not gain a phantom inserted magazine;
   record chamber/ammo behavior independently from visible geometry.
4. **Stereo sharpness and cutscene:** compare town edges and explosion effects,
   then the previously reported snowy scripted scope/cutscene if identifiable.
   Record left/right separately, distance-dependent detail changes, blur and
   comfort. Confirm actual eye allocation against the runtime recommendation;
   do not assume the new quality profile is active from source alone.

Keep XM8 muzzle/ejection-origin symptoms, dropped-magazine material appearance,
frame-pacing measurements and independent hands as separate observations.
No proxy or newly accepted hands replacement is implied by this sequence.
