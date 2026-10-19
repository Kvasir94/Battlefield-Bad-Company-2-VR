# SPAS pump lifecycle and rear-stop repair — October 14, 2026

User headset feedback on the XM8/SPAS candidate `b54ad96`: SPAS pump/fire
readiness sometimes disagreed after swapping weapons, unnecessary pumping
was possible, and the support hand jittered off the pump at full rear.
The user also reported a game exit when grabbing a shell after firing blocked.

## Source findings and changes

- Pump stroke, hand latch and post-rack settle were process-wide presentation
  state. They now reset on exact physical weapon identity, equipment identity,
  hand ownership epoch or tracking-space changes. Resetting presentation does
  not clear the persistent weapon's pump debt or manufacture a completed cycle.
- Submitted pump commands retain their original owners until terminal receipts
  are consumed. A pending command blocks only its physical weapon; feedback
  and settle are emitted only for its originating presentation owner.
- Pump eligibility is now `pumpOwed || (needsRack && feed > 0)` for an initialized
  TubePump. The authority also rejects unnecessary TubePump `CycleAction`
  commands, so delayed or repeated gestures cannot eject a ready live shell.
  The last shot still permits one clearing stroke; insertion into a cleared
  empty tube permits one chambering stroke.
- The stroke's rear latch had immediately selected the returning animation
  branch at 95% travel. It now keeps the outbound branch until actual forward
  motion exceeds a small noise deadband, then latches returning for that stroke.
  Physical rear/forward thresholds map to the native animation endpoints.
- The original SPAS catalog assigns the progress-1 turnaround at `.316666667 s`
  to the outbound branch. Its first returning sample at `.333333333 s` is
  already at `.997522262` progress. Return evaluation previously failed above
  that value, allowing the sampled pump hand to fall back to ordinary support.
  Pump/rack return evaluation now includes that exact original turnaround as
  its upper endpoint, without extrapolation or an invented attachment pose.
- A post-rack closed-parts sample now respects `handOwned=false` in the support
  wrist, fingers and action-transition paths. It cannot reclaim a free/loading
  hand or replace a fresh ordinary support grip.

The private catalog comparison at physical travel `.93` (native `.977778`)
found an outbound/return wrist difference of `.78 mm / .14 degrees`, identical
15 wrist-local finger transforms and identical fore-end position. Both branches
match exactly at full rear. No additional approximate hand correction was used.

## Validation and evidence limits

Focused regressions cover presentation resets across ownership changes,
per-weapon pending receipts, owed-only cycles, the final shell and subsequent
empty reload, rear-hold noise, exact turnaround matrices and full return-path
coverage using the configured private original SPAS catalog. The incremental
x86 `BC2NativeProbe` build passed. `Bc2PumpHold`, `Bc2NativeActionPose`,
`PhysicalWeaponState` and `Bc2PhysicalWeaponInteraction` passed. Headset feel
remains to be checked in the replacement candidate.

The available playtest evidence is in the local candidate's reports:
`native-trace-20261014-215627-071` and
`native-xr-session-20261014-215624-014`. It contains no matching Windows
Application exception, fresh WER archive or BC2 crash dump. XR loses focus at
`154278953 ms`; native telemetry continues approximately 22 seconds afterward;
XR records `application_exit_requested` at `154301468 ms` and exits with code 0.
The last logged shell reload is approximately 110 seconds before focus loss;
later grabs are XM8. The native completion records game exit code 0 without a
final report. These records do not establish the reported shell-grab crash's
cause. A focused tube/preview/palette lifetime audit found no demonstrated
dangling reference; no speculative crash patch or new engine call was added.
