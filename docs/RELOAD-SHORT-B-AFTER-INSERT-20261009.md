> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Short B after a magazine insertion (October 9 2026)

Offline fix. Nothing here was run in BC2 or a headset; the evidence is preserved
trace analysis plus CPU tests.

## Report

From [the existing-IK playtest](PLAYTEST-20261009-EXISTING-IK.md), about 02:39 UTC:
after a visible XM8 magazine insertion, the player pressed B and the magazine was
ejected again instead of the reload completing.

## What the state was when the eject fired

Checked against `reports/native-trace-20261009-021010-472/gun-events.jsonl` (the same
session, `XM8_sp_s`, physical weapon authority active) and
`reports/live-reload-notes-20261009-023948/reload-events-timed.json`:

| Tick | UTC | Event | Authority view |
| --- | --- | --- | --- |
| 249219 | 02:36:25.36 | hand rack after an empty reload | chamber loaded, `needs_rack=false`, loaded 30 |
| 262588 | 02:38:41.75 | seat | loaded 31, chamber loaded, `needs_rack=false` |
| 262749 | 02:38:43.35 | B eject (`release` and `chamber eject` at WellEmpty, with no `unseat`/RemovedHeld rows, so not a hand pull) | loaded 1, chamber loaded |

There were no real shots after the 02:36:25 rack. `server_shot_sequence` stayed at
29. The later `fire ... consumed 29/30` rows coincide with unseats: native loaded
drops to the chambered 1, so they are unseat artefacts, not shots. Every insertion
after that rack was a tactical reload with a live round in the chamber. Earlier B
ejects in the same session show the same state (ticks 253193 and 253500, 3.4 s and
0.5 s after a seat).

The later session `native-trace-20261009-030348-709` repeats the pattern four times:

- ticks 9306 and 14253, 0.8 s and 0.6 s after a hand rack
- ticks 14503 and 14785, 0.7 s and 0.8 s after a seat with a live chamber

After that the reserve was exhausted (`reload_failed` haptics) and every magazine
had been dropped.

The seam is `Bc2PhysicalWeaponInteraction::Tick`, which decided short B by these
rules:

1. If a rack is owed and the weapon is loaded: `CycleAction` (rack).
2. Otherwise, if a magazine is seated: `UnseatMagazine` with eject-drop.

Nothing in that decision knew that a magazine had just been inserted. With a live
chamber after the insertion, rule 2 fired every time.

Checked from source (not observable in these traces):

- A press while the `SeatMagazine` receipt was pending was dropped by the
  `!pending_` guard, not ejected. That path did not cause the eject; it only lost
  the press.
- The empty-chamber case (insert, then B) already racked through rule 1.

Inferred, not established: the player pressed B to finish the reload, following the
voice suggestion of a required-rack fallback. The weapon was already ready to
fire, but nothing visible showed it, so B looked like the next step.

## Behaviour now

Short B (released before 600 ms; holding B for 600 ms is still the automatic native
reload) on a detachable-magazine weapon under the authority does the following:

| Situation | Short B |
| --- | --- |
| Rack owed and the magazine has rounds | Rack (`CycleAction`), unchanged |
| Within 5 s of our own applied seat, or of a rack applied inside that window, magazine still seated, no shot since | **Confirm**: one light tick on the gun hand. No eject, and no round cycled out. |
| Insertion settling, about to seat, or `SeatMagazine` receipt in flight | **Latched** for up to 1 s and resolved by the receipt: rack if owed, otherwise confirm. A rejected or expired seat drops it. It never ejects. |
| Magazine seated and the reload idle (window over, or a shot fired) | Eject, unchanged |
| Magazine in hand, pull in progress, other command pending | Nothing, unchanged |

A shot, an unseat or a weapon identity change closes the window. A cancel (tracking
loss and similar) drops a latched press but keeps the window, because the magazine
and chamber did not change.

Design choice: with a live chamber after a tactical reload, B confirms instead of
cycling the action. A cycle would discard a live round on every press with nothing
visible, while a confirmation costs nothing.

Accounting is unchanged. Every action is an existing authority command
(`CycleAction`, `UnseatMagazine` and `DropMagazine`). Dropped magazines stay
unavailable, and retained pocket ammunition stays poolable. No native address,
hook or call was added.

Code:

- `src/games/bc2/Bc2ShortReloadAction.h`: the pure decision, the window and latch
  constants, and the environment parser.
- `src/games/bc2/Bc2PhysicalWeaponInteraction.cpp`: the window opens on an applied
  seat and is refreshed by an applied rack; it also holds the latch.

The legacy consumer (`FVR_BC2_PHYSICAL_WEAPON_AUTHORITY=0`) is untouched.

## Environment

| Variable | Default | Effect |
| --- | --- | --- |
| `FVR_BC2_SHORT_B_RACK_AFTER_INSERT` | on | `0` restores the aca6ffd rule: rack if owed, else eject any seated magazine; a press during a pending seat is dropped |

The trace report's `physical_weapon_interaction` block gains `short_b`:
`rack_after_insert`, `racks`, `ejects`, `confirms`, `latched`, `latch_resolved` and
`latch_dropped`.

## Tests

`Bc2PhysicalWeaponInteractionTests`:

- decision table and environment parser
- tactical insert followed by B confirms; repeated presses neither eject nor bleed
  rounds; B after the window ejects
- a shot closes the window
- empty-chamber insert followed by B racks and the weapon can fire; a second B
  confirms
- B during the slider settle is latched and resolves into the rack
- B with the seat receipt held in flight waits and then racks
- a rejected seat drops the latch without an eject
- the kill switch restores the eject

Not verified: in-game feel, and whether 5 s is the right window. Observed B
presses came 0.5 to 3.9 s after a seat or rack.
