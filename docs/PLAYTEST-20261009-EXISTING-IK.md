> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Existing-IK worn playtest — October 9, 2026

Append-only observation record for follow-up fixes. See [current status](STATUS.md)
and the [VR quality review and acceptance plan](VR-QUALITY-REVIEW.md).
Documentation work does not modify the running build.

## Session identity and evidence limits

- Observation time: approximately **2026-10-09 02:36 UTC** (October 8, CDT), as relayed by the owner during the live worn session.
- Reported session: `reports/native-xr-session-20261009-021007-394/session.json`.
- Reported source: `97a5b71`, described as the frozen existing-IK build. This identity is **reported, pending confirmation from the native owner**; no binary hash or session-log contents were verified by this documentation task.
- Input/evidence mode: the owner wearing the headset and reporting live impressions. This is human observation, separate from synthetic recording/replay.
- These notes capture a session in progress. They do not establish completed reload, long-run stability, crash closure or full headset acceptance.

## Live observations — approximately 02:36 UTC

| Area | The owner's observation | Interpretation limit / follow-up |
| --- | --- | --- |
| Overall image | Looks pretty good. | Qualitative impression; no image-quality measurement or all-scene acceptance. |
| Arm ends | Hard to see on screen. | Ambiguous observation. The viewed surface and intended arm-end appearance are unspecified; not classified as a confirmed defect. |
| Firing | Guns fire. | Observed firing in this session; no all-weapon, ammunition-conservation or repeated-cycle pass. |
| Muzzle flash | Appears above the actual muzzle. | Visually estimated displacement; no measured offset, transform ownership or root cause established. |
| Empty-magazine cue | When empty, the magwell highlights as a grab cue. | Cue behavior observed; does not independently prove physical transaction completion. |
| Moving-gun magwell cue | Moving the gun in VR makes the highlight/image disconnect and stutter away from the actual magwell. | Record cue-to-magwell alignment and timing separately from ammunition authority; cause unproven. |
| Gun motion | Whole gun feels somewhat stuttery. | Human feel observation, with no quantified motion latency or sampling rate. |
| Locomotion | Feels pretty good. | Positive scoped impression; combined roomscale, collision and interruption coverage not established. |
| Frame-rate impression | Subjectively much better than the prior worn failure. | Not measured FPS, a controlled A/B comparison or full acceptance. |

The owner hypothesizes FPS or stability may explain the remaining motion/cue issues.
That is a **user hypothesis**, not a confirmed cause. The positive overall image,
firing and locomotion observations coexist with the reported flash/cue alignment
and gun-stutter issues.

## Follow-up record for the existing native owner

1. Confirm the exact session/source/binary identity from the frozen running build and preserved logs.
2. Correlate the reported gun/cue stutter with tracking sample, weapon pose, cue pose and rendered/submitted image timing before selecting a fix.
3. Measure the flash origin relative to the rendered muzzle; retain the visual observation even if later measurements differ.
4. Clarify which screen/surface and arm-end appearance the owner meant before classifying that observation.
5. Keep later magazine-removal, repeated reload, process stability and worn comfort results as separately dated receipts.

These are follow-up questions and validation goals, not actions performed by this
documentation task. Later observations should be appended with their time and
identity rather than overwriting this report.

## Live observations — approximately 02:38 UTC, same reported session

Weapon identity is **likely XM8, unconfirmed**. These observations describe
interaction and presentation; they do not independently establish insertion,
chamber readiness or ammunition-command success.

| Area | The owner's observation | Interpretation limit / follow-up |
| --- | --- | --- |
| Magazine extraction / held motion | The magazine can be removed and stays in hand as the hand moves. | Positive visible extraction/held-object continuity in this interval; not a repeated-cycle or all-weapon pass. |
| Released magazine | Releasing the magazine makes it disappear immediately, with no visible fall. | Visible held-to-world transition is missing from the user's view. No conclusion about object destruction, asset availability, ammunition loss or root cause. |
| Replacement from belt | A replacement can be grabbed from the belt, but there is no visible object/location cue; the user must know where it is. | Acquisition appears usable, but discoverability remains unresolved. No confirmed inventory/receipt accounting from these notes. |
| Replacement near magwell | The magwell lights up; bringing the replacement near it produces an authored snap-in that feels pretty good. | Positive cue/insertion feel. Does not prove a completed seating command or chamber-ready state. |
| Mechanism cue / reload completion | No visible slide or charging-handle cue; the user cannot locate the mechanism or finish reloading and resume shooting. | Mechanism discoverability and completion remain unresolved. Do not infer successful insertion/chambering from snap-in appearance. |
| Process continuity | No immediate magazine-removal crash reported in this interval. | Narrow observation only; the original crash is not declared fixed. |

Voice guidance suggested **one B press** using the verified required-rack
fallback. Its outcome is **pending**. B's supported physical-magazine behavior
is state-dependent; this suggestion is not proof of an automatic reload or a
completed rack. See the [current controller card](CONTROLS-20260930.md#current-controller-card--october-9-2026).

Follow-up: confirm weapon identity; record the result of the single B press;
trace the released magazine's last held pose and first world presentation;
clarify belt and mechanism hints. Keep source/session identity as reported until
owner confirmation, and append later outcomes rather than retroactively marking
this interval accepted.

## End-of-test observations — approximately 02:39 UTC

- After the visible magazine insertion, the owner pressed **B**. It ejected the magazine again instead of completing the reload. The suggested required-rack fallback **did not succeed in this observed state**.
- Soon afterward, the view spun or returned to a flat screen. The owner suspects a crash but is unsure whether the game or the VR adapter failed.
- **This full physical-reload human acceptance check failed.** Resumed firing is unconfirmed. Earlier positive firing, extraction and insertion-feel observations remain valid within their narrower scope.

The sequence does not establish a confirmed BC2 or VDXR crash, a causal B-button
bug, or whether the source consumer considered the weapon loaded/owing a rack.
The existing runtime/crash owner was urgently instructed to preserve the exact
active logs, input tape, binaries and process status before restart. This
paragraph records that instruction; this documentation task did not inspect
those receipts or restart anything. Append the owner's later evidence and
classification separately when available.

## Verified evidence follow-up — reviewed after the end-of-test observations

The crash lead verified the following from the same session and preserved
reload evidence. This is an evidence review relayed to the documentation owner;
this task did not run diagnostics or independently inspect local logs.

- Session `native-xr-session-20261009-021007-394` ended with **XR host exit `-1073741819` (`0xC0000005`, access violation)**. This establishes a host failure; it does not establish its cause or a VDXR failure.
- Native completion at **2026-10-09 02:39:17 UTC** reports **BC2 responding**, with no game exit or new game-crash report. Do not classify this as a confirmed BC2 crash.
- Preserved timed reload events identify the weapon as **`XM8_sp_s`**, resolving the earlier likely-XM8 qualification for these logged events.
- Several recorded insertions have **`loaded=31`, `chamber_loaded=true`, `needs_rack=false`**. The suggested rack instruction did not match those recorded states. This does not prove the exact state at every button press or establish a causal B-button bug.
- **Resumed firing remains unconfirmed.** The failed physical-reload human acceptance outcome is unchanged.

Private evidence retained by the crash lead, excluded from the repository:

- Session record — session identity and host exit; the crash lead reviewed native completion.
- Preserved timed reload events — `XM8_sp_s` identity and recorded ammunition/chamber/rack states.

At the time of this evidence review, the full host-crash/dump preservation receipt
was **not yet confirmed**. Native-owner follow-up was ongoing; exact source/binary
identity and the host access-violation root cause remained open. This dated entry
records no fix or restart.

Later proven cause/source-fix evidence and subsequent live observations are in
[the host feedback follow-up and prioritized queue](PLAYTEST-HOST-FEEDBACK-FIX-20261009.md#prioritized-open-work--resumed-after-the-october-9-playtest).
They do not retroactively establish the earlier worn failure's cause or acceptance.
