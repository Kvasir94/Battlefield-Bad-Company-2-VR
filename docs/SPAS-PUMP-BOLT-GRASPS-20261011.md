> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# SPAS pump stroke and bolt grasps with native samples (2026-10-11)

Tier-2 item from `docs/HANDOFF-2026-10-11-ORCHESTRATION.md`: "SPAS pump driven
by controller stroke; bolt pulls with native authored grasps". Branch
`fix/spas-pump-bolt-grasps`, from `main` `9d8e07c`. Asset evidence is in
`docs/re/BC2_PUMP_RACK_GRASP_EVIDENCE_20261011.md`.

**Status: compiled only, in a worktree without the private headers.** It has
not been run in game or in the headset.

## What already existed (and is kept)

- **SPAS pump authority.** `PumpHoldGate` (`Bc2PumpHold.h`) owes one pump per
  shot and holds every firing copy at native state 7. `PumpStroke` follows the
  KF2 VRManualPump model: a 6 cm support-hand stroke along the gun hand's aim
  axis, with a 95 % rear stop and a 5 % forward lock.
- **Exactly one native rack.** A completed stroke calls `RackPump()`. In the
  physical-tube runtime it submits a `CycleAction` instead.
  - `RackPump()` releases the hold once per shot. `rackedShot_` is keyed by the
    loaded/reserve count after the shot, so a second stroke on the same shot
    returns false.
  - The tube path's `pumpCommand` stays pending until its receipt arrives and
    blocks any new stroke meanwhile.
  - Death, respawn and owner loss go through the existing `OwnerCleared` and
    reload-authority paths (`DEATH-RESPAWN-RELOAD-20261011.md`). This change
    does not touch them.
- **Native pump sample.** The private `SPAS12_sp` `Pump` profile holds the
  original `1P_BoltAction` samples for the wrist, 15 fingers and three moving
  parts.
  - Rig publication selects the nearest original sample by stroke fraction and
    direction, and drives `jntWpn_4`/`_3`/`_5` from it.
  - When the support grip is held, the left wrist and fingers come from the
    same sample.
- **Rack hand.** For guns with an exact private `Rack` profile (VSS, MG36,
  XM8 LMG), a held charging handle already uses the original sampled wrist,
  fingers and parts, indexed by handle travel.

## What this change adds

### 1. The pump owns the hand without a support claim (`FVR_BC2_PUMP_NATIVE_HAND`)

Before this change, the native pump sample posed the hand only when the
support grip was also held. The pump zone (5–85 cm forward, 20 cm lateral) is
wider than the support contact, so a stroke could run with the support claim
rejected. The fore-end then moved under a free, controller-posed hand.

- **Gameplay.** `PumpVisual` gains `handOwned`. It is set when the grip
  closed in the pump zone (`pumpZoneHeld`), and stays set while that same grip
  stays closed and tracked. That includes the post-rack settle below.
- **Rig publication.** When `handOwned` is set and support is not attached,
  the left wrist goes to `NativeActionWrist(sample, placed)` and the
  `1P_BoltAction` fingers are applied. This is the same sample that drives the
  fore-end.
  - The right hand keeps its controller target.
  - The arm is solved with the existing `SolvePresentedArms`.
  - If the solve fails, the free hand is kept and `pump_hand_rejects` is
    counted.
  - A live reload preview always wins, and the charging-handle hand is skipped
    while the pump holds the hand.

### 2. No native pump replay after a physical stroke (`FVR_BC2_PUMP_SETTLE_MS`)

When the stroke racks, the native continuation goes through state 8, which
plays `1P_BoltAction` on the native rig. Before this change `pumpVisual` was
dropped as soon as the pump was no longer owed. The fore-end was then handed
back to the native rig and pumped a second time with no hand on it.

Now a successful rack starts a settle window: 1.9 s by default, or 0 to turn
it off.

- During the window `pumpVisual` stays alive at `fraction=0, returning=true`.
  That is the original closed, returned sample, so the fore-end and any hand on
  it stay closed while the native animation runs underneath.
- The window covers the 1.033 s clip plus the up to ~0.7 s wait in native
  state 6 on an early rack (see the evidence doc).
- A new owed pump takes over at once. A weapon change or the deadline ends the
  window.
- A pump released without a rack (8 s timeout, reload press, state change)
  starts no settle. The native pump plays exactly as before.

### 3. XM8 bolt pull with the XM8 receiver's native grasp (`FVR_BC2_RACK_FAMILY_GRASP`)

`XM8_sp_s` has no native rack. Its only reload clip never moves `jntWpn_17`, and
the hand never comes within 132 mm of the handle. As a result, the XM8 bolt
(`Bc2Xm8BoltCycle`) handle hand fell back to the controller rotation with a
generated MechanismGrip hand. Under the KF2 VR standard that fallback is not
finished support.

The XM8 LMG uses the same receiver rig. Its handle, carrier and magazine rests
in HandsIkPose are identical to under 0.1 mm. Its `1P_Reload` contains a real
native hand rack of the same `jntWpn_17`: a 117 mm stroke and a 22 mm pinch.

- **Donor table.** `FindNativeRackGraspDonor` (`Bc2NativeActionPose.h`) is an
  explicit table. Its only entry is `XM8_sp_s` → `XM8 LMG`, handle
  `jntWpn_17`, rest (0, 0.069722, −0.496687), tolerance 2 mm.
- **Admission.** `NativeRackGraspDonorPart` admits the donor only when its
  profile is a valid `Rack` for that exact asset, mesh and rig. The profile must
  also carry the handle exactly once, and its closed outbound sample must lie
  at the recipient rest within the tolerance.
- **Rig publication.** When no exact `Rack` profile exists for the selected
  asset, the bound donor is used for **the hand only**.
  - The handle travel shown (`handleShown`, which the bolt's pull distance
    drives) selects the original donor sample.
  - `NativeActionWristOnPart` takes that sample's wrist relative to the
    donor's handle and places it on **this** gun's handle as currently drawn,
    including its travel.
  - The donor's 15 fingers are applied.
  - The donor's part transforms are never written. The XM8 keeps its own
    carrier and handle travel (the `1P_FireStart` stroke) and its bolt asset
    draw.
- **Exact profiles keep priority.** A gun with its own `Rack` profile never
  uses a donor.

## Kill switches (all default on)

| Variable | Effect of `0` |
|---|---|
| `FVR_BC2_PUMP_NATIVE_HAND` | The pump never takes the hand without a support claim (previous behaviour). |
| `FVR_BC2_PUMP_SETTLE_MS` | `0` turns off the post-rack settle, so the native pump replays as before. Other values set the window in ms (1–10000). |
| `FVR_BC2_RACK_FAMILY_GRASP` | No donor grasps. The XM8 bolt hand returns to the generated fallback. |
| `FVR_BC2_NATIVE_ACTION_POSES` (existing) | No native action samples at all: pump, rack and donor. |
| `FVR_XM8_BOLT` (existing) | The XM8 bolt is off entirely. |

## Report fields

- **`gameplay` … `spas_pump`.** Adds `settles` and `settle_ms`.
- **Rig publication `native_grasps` object.**
  - `pump_hand_enabled`, `pump_hand_poses`, `pump_hand_rejects`
  - `rack_family_enabled`, `rack_family_bindings`, `rack_family_poses`,
    `rack_family_rejects`

## What remains approximate (not counted as finished)

- **XM8_sp_s grasp comes from a sister gun.** It is the original authored
  hand, but from the XM8 LMG's reload, on an identical receiver. No
  exact-asset clip exists in the game data, so this is the closest native
  evidence there is. The sample still has to be checked in the headset for
  hand/handle penetration.
- **Stroke ratio.** The pump stroke is 6 cm of hand travel, but the native
  fore-end travels 94.5 mm. The sampled hand therefore leads the controller by
  up to ~35 mm at full stroke. This mapping predates this change.
  - A 1:1 ratio means changing `PumpStrokeConfig::strokeMeters`, which also
    moves the rack thresholds.
  - That is a feel decision for the user and is left unchanged.
- **Donor travel ratio.** For the XM8 donor, handle travel maps to donor
  progress by fraction (117 mm donor against 120 mm drawn). The wrist is
  re-anchored on the drawn handle, so the hand stays on it. The finger shape
  is picked by fraction.
- **XM8 bolt has no native chamber.** The XM8 bolt stroke is still
  presentation plus gating only (`XM8-BOLT-20261009.md`). No native charge or
  chamber boundary is identified for an automatic rifle.
- **No exact rack for other guns.** XM8C, M416, AKS74u and UZI have no native
  hand rack in any decoded clip. Guns without a mechanism profile have no
  charging handle at all.
- **Pump hand claim.** The pump hand is presentation that follows the existing
  stroke ownership (`pumpZoneHeld`). It does not take a separate left-hand
  arbiter claim. The support grip still owns the claim when it is held.

## In-game test steps (SPAS, then XM8)

Before any run, check the card and RAM (`nvidia-smi`, and what holds VRAM). Label
every timing cold or warm.

1. **Confirm the build.**
   - Build the integrated package with the private headers, following the
     `INTEGRATION-20261011.md` recipe.
   - Confirm `build.ninja` contains `FVR_BC2_NATIVE_ACTION_POSE_HEADER=`.
   - In the report, `native_grasps.rack_family_enabled` and
     `pump_hand_enabled` are `true`.
2. **SPAS, support pump.** Fire once and grab the fore-end with the support
   grip.
   - Pull back and push forward. The fore-end, wrist and fingers follow the
     stroke.
   - Exactly one rack happens: `spas_pump.racks` +1, one `rack` gun-event row,
     the next shot fires.
   - After the stroke the fore-end does **not** pump again by itself, and the
     hand stays closed on it.
   - `settles` +1.
3. **SPAS, pump-zone only.** Grip the fore-end slightly off the support contact
   (lower, or farther forward) so support does not engage, then pump.
   - The hand snaps to the native fore-end grip and follows the stroke.
   - `pump_hand_poses` grows and `pump_hand_rejects` stays near 0.
   - Release the grip: the hand returns to the controller.
4. **SPAS, double stroke.** Stroke twice after one shot. Only one rack is
   counted, and the second stroke does nothing.
5. **SPAS, settle off.** Run with `FVR_BC2_PUMP_SETTLE_MS=0`. The old native
   replay after the stroke is visible again, which confirms the switch.
6. **XM8 bolt.** With the left hand free and the magazine seated, squeeze the
   charging handle under the carry handle and pull back about 12 cm.
   - The hand shows the native XM8 rack grip, not the generic grip. It stays
     on the handle through the whole travel and on the return.
   - `rack_family_bindings` ≥ 1, `rack_family_poses` grows and
     `rack_family_rejects` ≈ 0.
   - Look for fingers clipping the carry handle or rail.
7. **XM8 bolt, switch off.** Run with `FVR_XM8_BOLT=1` and
   `FVR_BC2_RACK_FAMILY_GRASP=0`. The old generic hand returns.
8. **Regression, exact racks.** On VSS or MG36 after an empty reload, the rack
   still uses its own profile and `rack_family_poses` does not grow.
9. **Regression, death.** Die mid-pump on the SPAS and respawn. No stale
   settle or hand capture remains, and the next shot owes a fresh pump.

## Build and test record

- **Compile.** `.\Build.ps1 -Architecture x86 -Jobs 2` in the worktree, with a
  fresh cache, compiling `BC2NativeProbe` only.
  - TEMP/TMP had to be pointed at `build/tmp` on D:, because C: had 0 bytes
    free.
  - The build succeeded with no warnings in the changed files. DLL SHA-256
    `d5e1c837b38404a6e4b97e958df6862d8c5e3ecdd328249d0a5b4b1ad99eb3ef`.
  - The private headers were empty, so **the DLL is compile proof only and is
    not installable**.
- **Tests.** `Bc2NativeActionPoseTests` (x86) passes. It gains a `Donor`
  group: table lookup, admission and rejection, and wrist-on-part placement
  with unit scaling.
- **Real donor check.** The real private `XM8 LMG` `Rack` sample's closed
  handle lies 0.076 mm from the XM8_sp_s rest, so the donor binds. This was
  checked offline against `samples-racks.private.json`.
