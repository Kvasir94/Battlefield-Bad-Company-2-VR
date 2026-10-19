# Reload hand retention and combat presentation

The headset report was support grips repeatedly releasing on all guns, SPAS
loading snapping the hand to support, a short/unreliable pump stroke, an XM8
handle that lost the hand before release, and chest ammo/hints increasingly
flickering, especially during combat. These observations supersede previous
claims of finished reload feel.

## Corrections in development source

- Support contact is an acquisition proof. An established grip renews from
  current real controller/owner evidence; renderer contact loss, leaving the
  acquisition radius, and a native render hitch no longer release it. Focus,
  tracking, owner/space changes and actual release still invalidate it.
- A squeeze consumed by shell loading or another hand action must go neutral
  before it can acquire support. The support claim renews before pump/reload
  consumers run, avoiding a transient claim loss within the same input step.
- SPAS maps physical hand travel to the original sampled pump animation using
  `FVR_BC2_PUMP_TRAVEL_SCALE`, default 1.35. This is a BC2 feel adjustment;
  it is not evidence that KF2 uses that numeric multiplier. Its original
  wrist, fingers and weapon part still come from the sampled animation.
- XM8 uses its receiver-compatible original acquire/prep/pull/return samples.
  The held rear stop remains owned until the actual squeeze releases. Missing
  geometry pauses physical progress without dropping the current hand claim.
  An expired lease may rebind to the current gun claim only with the same
  captured owner/equipment/space, fresh tracked input and no competing hand
  claim. The original action token and sampled progress survive; the next
  geometry sample rebases without inventing motion through the missing interval.
  Release frees the hand immediately; the original full return moves the part.
  Only a completed rear-stop/return cycle submits one physical CycleAction.
- Accepted shots advance the physical weapon revision. Chest reserve props
  previously required that entire revision to match, hiding unchanged reserve
  ammo during firing. Render-only material proof now compares exact reserve
  units, retained magazine IDs/rounds/capacity, pool, weapon incarnation and
  ownership against the current ledger. Original deadlines remain intact.
  Actual magazine/handle interaction continues to require its exact revision.
- Body and guidance consumers take payloads from one immutable tracking
  publication and compare current owner/equipment/space semantically. Pointer
  replacement during ordinary publication no longer causes a false rejection.
- Magazine glows use the independently verified current configured weapon
  frame and rig fingerprint, bounded by that frame's original deadline. Their
  geometry does not depend on per-shot physical contact validation. Handle
  hints independently recheck current rack readiness.

No evidence currently establishes unbounded prop/hint accumulation in the
reported session. Existing caches are bounded; the native source cache keeps
historical COM references, which merits measurement if deterioration persists.
The combat revision rejection and publication races are concrete source defects.

`native_skin_source_verification` now appears in the flushed one-second session
telemetry. It records pinned registrations/capacity, available/pending/rejected
hits, full-cache refusals, copy/read backlog and exact hash/create/map outcomes.
This distinguishes cache saturation from GPU readback delays without changing
cache lifetime, rendering decisions or resource ownership. Independent relaxed
counter loads can straddle an in-progress transition; `read_pending` is a gauge,
and the other counters are cumulative.

The successful `native-trace-20261014-060711-399` session contains 607 seconds of
telemetry. Published pair throughput was about 44–45/s in the middle intervals
and 38.3/s after 360 seconds. Its cumulative GPU export maximum reached
125,825 microseconds late in the session, exceeding the 100 ms hand lease;
body prop gate skips and tracked apply cancellations stayed zero. This supports
testing ownership recovery through a real-sized hitch, but does not establish
that GPU export caused every reported visual problem.

## Validation boundary

Focused checks cover support retention/rearming, SPAS travel scaling, original
XM8 phases and missing-geometry heartbeat, exactly one CycleAction, pose
presentation, unchanged reserve retention across accepted shots, and rejection
of changed material/identity or expired original evidence. They do not establish
headset smoothness or finished all-gun sampled-pose coverage. The next packaged
candidate must retain matching native/host IPC versions.
