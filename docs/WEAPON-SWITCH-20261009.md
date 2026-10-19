> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Weapon switching and the right-stick equipment cycle, October 9

This change is built and CPU-tested only. Nobody has tried it in a headset or in
the game yet. The October 8 stick-switch seams it builds on (server inventory
update hook `0x6f1980`, setter `0x6d7c80`, the inventory validity callback) are
described in [2142-TRANSFER.md](2142-TRANSFER.md#stick-weapon-switching-done-2026-10-08-in-scripted-runs-headset-not-checked).
It adds no new native addresses, hooks or writes.

## Controls

| Input | Effect |
| --- | --- |
| Right stick click, released before 450 ms | Next item carried (forward cycle) |
| Right stick click held 450 ms | Previous item carried (backward cycle). Fires once at 450 ms; the release does nothing |
| Right stick flick up / down | Other gun (guns only, as on October 8); from equipment, back to the last gun |
| Left stick click alone | Sprint (unchanged) |
| Both stick clicks held 1 s | Recenter (unchanged). Touching the left click during a right click cancels the click |

The cycle order uses BC2 `WeaponSwitchingEnum` slots. Each gun is followed by
its own underbarrel launcher:

```text
Primary1 (0) -> GrenadeLauncher1 (2) -> Primary2 (1) -> GrenadeLauncher2 (3)
-> StaticGadget (4) -> DynamicGadget1 (5) -> DynamicGadget2 (6)
-> Knife (7) -> HandGrenade (8) -> wraps to Primary1
```

The cycle skips slots that hold nothing. It also skips slots that the game says
cannot be selected right now, such as a thrown-out gadget. In the SP_02 starting
kit (XM8, XM320, LZ-537 designator, knife, grenade), five forward clicks go
XM8 -> XM320 -> LZ-537 -> knife -> grenade -> XM8. A long click from the XM8
goes to the grenade.

Quick repeated clicks chain from the item the last click asked for. Two fast
clicks from the XM8 land on the LZ-537. Without chaining, the second click would
start from the XM8 again, because the game has not switched yet, and land on the
XM320 a second time.

Haptics on the right hand:

- one light tick (0.25 / 12 ms) when the server actually selects the item;
- two short buzzes (0.2 / 25 ms, 50 ms apart) when a request is refused.

These strengths are design defaults and have not been tuned in a headset. The
host must be rebuilt for them (new `FeedbackKind` 12/13). An older host drops
them, and nothing else changes.

## Environment switches

| Variable | Default | Effect |
| --- | --- | --- |
| `FVR_BC2_STICK_CYCLE=0` | on | Restores the October 8 click: launcher/gadgets 2..6, then back to the gun. No backward cycle |
| `FVR_BC2_STICK_CYCLE_MELEE_GRENADE=0` | on | Keeps the knife (7) and hand grenade (8) out of the cycle |
| `FVR_BC2_STICK_CYCLE_BACK_MS=N` | 450 | Long-click threshold, 250..1500 ms |

`FVR_BC2_PHYSICAL_WEAPON_AUTHORITY=0` still selects the older non-authority path.
That path uses the same cycle order, checks every slot with the native validity
callback, and selects with native validation on.

## Failures found in the worn evidence

1. **Flicks did nothing with one gun.** The user's note "Quest2 right-stick
   up/down weapon switching did not work despite attempts" (03:11 UTC,
   `reports/live-movement-notes-20261009-031100`) matches session
   `native-trace-20261009-030348-709`. Its final report has `weapon_switch`
   latched 14, selected 14, **no_target 54**, and 28 next plus 28 previous flick
   commands. All 14 selections were right-click equipment cycles
   (XM8 / 40mmgl / LZ-537 in gun events). The authority snapshot had a single gun
   in slot 0, and the SP_02 kit has nothing in slot 1. A guns-only flick had no
   target, and nothing told the player so.
   *Fixed:* every carried item is now on the click cycle. A flick with no other
   gun gives a reject buzz.
2. **Unsuccessful flicks cancelled the support hand.** A held flick kept
   `NextWeapon`/`PreviousWeapon` set for its whole deflection. Those bits feed the
   support-grip cancel flags, the hand-ownership cancel, the magazine/tube
   `sample.cancel` and the sight cancel, whether or not a switch happened. In
   the single-gun case above, every flick dropped the support grip and any
   magazine interaction for nothing. This is the handoff's "support cancellation
   on unsuccessful weapon flicks".
   *Fixed:* `ResolveStickSwitch` now runs once at the start of the gather, before
   any of those consumers. It latches the request and passes the flick bits on
   only when a switch was requested or deferred. A flick that fails is invisible
   to the consumers for its whole deflection.
3. **Knife and hand grenade were unreachable.** SP_02 carries them in slots 7
   and 8. The live switch map targets them natively: `from X action 37 -> [7]`
   (EiaMeleeAttack) and `action 38 -> [8]` (EiaThrowGrenade), in test-range log
   `native-trace-20261008-233802-655/BC2TestRange.log`.
   *Added to the cycle:* they need the inventory validity callback to pass and are
   selected with native validation on. **Not verified in game.** The first
   in-game session must check two things. Does selecting slot 7/8 through the
   setter give a held knife or grenade that the trigger uses? Or does the game
   treat it as a one-shot melee/throw and return to the previous weapon on its
   own? If either goes wrong, set `FVR_BC2_STICK_CYCLE_MELEE_GRENADE=0`.
4. **Feedback ids collided across producers.** The feedback channel and the host
   accept only event ids above the last one seen. Each reload consumer counted
   ids from 1 on its own, so one producer's events could be dropped after
   another's. Every write now goes through one renumbering writer
   (`SendFeedback`). The proof is static (code reading). Trace evidence of
   dropped reload haptics has not been checked.

## Reload transactions and other owners

The rules follow the handoff's accounting rules for the physical weapon
authority:

| State of the held gun | Click or flick |
| --- | --- |
| Hold-B request not yet given to the authority | Cancelled; the switch proceeds |
| Native reload Queued or Ready (nothing native entered) | Cancelled through the authority. The switch is deferred until the reload is no longer busy, up to 1.5 s; after that it is dropped with a buzz |
| Native reload Running | Refused with a buzz. A running transaction is never stowed mid-reload. Live cancellation across switching is still not accepted |
| Body-holster draw in progress, or a sight mode change awaiting acknowledgement | Refused |
| Physical magazine or shell in hand, authority enabled | Allowed. The authority completes stowed-weapon cleanup on the server thread, as before |
| Same, authority disabled (legacy) | Refused (`blocked`), as before |

When the server confirms a selection, the support grip is reset with reason
`weapon_switch`, so the left hand does not stay latched to the old item. The
server callback re-checks every candidate before it selects:

- same item pointer, data and serial as the snapshot;
- the native validity callback, for slots 2..8;
- a read-back of the selected slot.

If a candidate fails, the callback tries the next one in cycle order. Gun slots
keep the authority's empty-weapon and cooldown waiver (`validate 0`).

## Telemetry for the next session

`controls.weapon_switch` is written every second in `session-telemetry.jsonl`.
The final report's `gameplay.weapon_switch` has the same fields. Before this
change these counters existed only in the final report. That report was empty
in both late sessions, so they were lost. Fields:

- `requests.{flick_next, flick_previous, cycle_forward, cycle_backward}`
- `latched`, `selected`, `no_target`, `no_inventory`, `expired`, `gated`, `blocked`
- `coalesced` (chained clicks) and `unchanged`
- `skipped_identity`, `skipped_invalid`, `select_rejected` (per-candidate server outcomes)
- `reload_blocked`, `reload_cancelled`, `deferred`, `deferred_expired`
- `body_blocked`, `mode_blocked`, `conflicts`
- `flick_switches` (flicks that kept hand cancellation), `outcome_selected`, `outcome_failed`
- `feedback_sent`, `feedback_dropped`
- `selected_slots[0..8]`: selections that landed on each slot. This shows whether the knife and grenade were ever really selected

Haptic rows `switch_selected` and `switch_rejected` also appear in
`gun-events.jsonl`, next to the existing native `weapon_switch` rows.

## Still open

- Headset feel: the 450 ms threshold, the haptic strengths, and whether a held
  click competes with the stick-turn thumb.
- Knife and grenade selection semantics (failure 3).
- Sprint while cycling: a right click during sprint (left click held) is treated
  as the recenter chord and does nothing. This is deliberate: the left-click plus
  right-click backward chord was rejected because it collides with recenter and
  sprint.
- Live reload cancellation across a switch (Running) remains refused, not
  implemented.
- The launcher's visual cant (handoff) is not addressed here.

Code: `src/games/bc2/Bc2EquipmentCycle.h` (pure plans),
`ControllerActions::StickClickEdge` (gesture),
`Bc2Gameplay.cpp` `ResolveStickSwitch` / `LatchWeaponSwitch` /
`InventoryUpdateHook`. Tests: `Bc2EquipmentCycleTests`, `ControllerInputTests`
`StickCycleTests`, and the `FeedbackTests` / `Bc2GunEventLogTests` kind checks.
