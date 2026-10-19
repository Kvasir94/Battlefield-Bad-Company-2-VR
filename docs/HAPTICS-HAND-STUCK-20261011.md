> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Haptics: left controller buzzing on right-hand fire, stuck across switches (2026-10-11)

Distinct from the 2026-10-09 host crash (null event dereference on delayed `ReloadFailed`,
fixed in 8b6da5e; see PLAYTEST-HOST-FEEDBACK-FIX-20261009.md). That fix is untouched: the
host still uses the returned pulse's `eventId`/`kind` for bookkeeping.

## Root cause
- Every owned shot (`Bc2Gameplay.cpp`, shot hook) sent a `GunFire` pulse to hand 1 (right, correct)
  **and**, whenever the shot frame was `supported`, a `PumpGunKnock` pulse to hand 0 (left).
  `supported` means the muzzle/shot frame is usable, not that the left hand holds the gun, so the
  left controller buzzed on every shot of a one-handed right-hand gun. In full auto the 50 ms and
  30 ms pulses overlap into a continuous buzz that reads as "stuck".
- Nothing ever stopped haptics except focus/presence loss. Pending pulses (and the runtime's
  running pulse) survived weapon switch, owner loss/death and gather stalls.
- Hand mapping (index 0 = left, 1 = right, `paths_`), durations (ns, all finite, 10-100 ms) and the
  shared-memory event ids (consumed monotonically) were checked and are correct.

## Fix
- Support-hand knock only while the support grip is actually held (`supportHoldingForHaptics`,
  updated from the support-grip result each gather). Fire recoil stays on the right (gun) hand.
- New control event `FeedbackKind::StopAll` (17): no pulse; the host cancels the pending second
  peak and calls `xrStopHapticFeedback` on both hands (`OpenXrInput::StopHaptics`) without raising
  the input barrier. The DLL sends it on weapon change and from `ClearReloadOwner` (owner loss,
  death, pause, focus/stale input), only if a pulse went out in the last second.
- Host clamps every pulse duration to 1..200 ms so XR_INFINITE_DURATION can never be requested.
- Kill switch: `FVR_BC2_GUN_HAPTICS=0` disables gun fire and support-knock haptics.
- An old host rejects kind 17 (invalid), so no effect there.

## Resume-branch note (codex/bc2-resume-20261011)
That branch (0524f7c) already removed the unconditional left-hand PumpGunKnock and sends the
braced left pulse only when `tracking.supportHolding&&supportToken`, so the wrong-hand cause is
already fixed there. This port keeps its KF2 recoil-weighted pulses unchanged and adds StopAll,
the duration clamp and the `FVR_BC2_GUN_HAPTICS=0` switch (now also covering the weighted pulses).
No FeedbackKind was added by that branch; StopAll=17 is free. FeedbackEvent's new recoil fields
default to the "no payload" value, so StopAll validates unchanged.

## Files
`include/fvr/interaction/Feedback.h`, `src/xr/OpenXrInput.{h,cpp}`,
`src/games/bc2/Bc2Gameplay.cpp`, `tests/FeedbackTests.cpp` (kind range check 17 -> 18).

## Test steps (in game, not run here)
1. Right-hand rifle, left hand off the gun, fire bursts: only the right controller pulses.
2. Grip the support hand on the gun, fire: left gets the light knock only while held.
3. Hold full auto, switch weapon mid-burst: buzz stops immediately on both controllers.
4. Die / open pause while firing: no residual vibration.
5. `FVR_BC2_GUN_HAPTICS=0`: no fire haptics at all.

Built (compile only): x86 `BC2NativeProbe`, x64 `BC2XrHost`. Tests not run.
