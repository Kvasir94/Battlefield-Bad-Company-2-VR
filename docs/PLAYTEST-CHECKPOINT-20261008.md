> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Playtest checkpoint, 2026-10-08

Tag `checkpoint-2026-10-08` on `proto/reload`. A frozen copy lives in
`<checkpoint-backup>` (sources at the tag, the built binaries, its own
test-range stage), so later dev builds never overwrite it.

## Frozen tag versus current main

This page records `checkpoint-2026-10-08` (`e2f5a3968e4858ebf4190e39429b6aac7612cc38`), not current main.
The tag's Gameplay source lacks `InventoryUpdateHook`; the implemented server
selection hook below was added later. PR #9 merged at
`2b77bc1`, and pushed main `e1f86e9` contains the later server-inventory
stick switching hook
([source](../src/games/bc2/Bc2Gameplay.cpp)).
The [October 8 ledger](2142-TRANSFER.md#stick-weapon-switching-done-2026-10-08-in-scripted-runs-headset-not-checked)
records fix `87bc623` and scoped native runs. The no-stick-switch issue below
belongs to this frozen playtest record; it is not a blanket main limitation.
This correction does not upgrade any historical headset rejection to a pass,
and main's real-controller switch/reload acceptance remains open.
The October 9 gameplay snapshot adds independent gun/equipment selection and
release/rearming protections; see the [current controller card](CONTROLS-20260930.md#current-controller-card--october-9-2026).
It does not change this tag's historical result.

## Launch

Start Virtual Desktop (or SteamVR) first, then from the Start menu run
**BC2 VR Checkpoint 2026-10-08**, or:

```
powershell -NoProfile -ExecutionPolicy Bypass -File <checkpoint-backup>\Play-Checkpoint.ps1
```

Monitor only (no headset, no VR session; checks that BC2 fast-starts):

```
powershell -NoProfile -ExecutionPolicy Bypass -File <checkpoint-backup>\Play-Checkpoint.ps1 -Flat
```

Pick the gun with `-Gun XM8` (default), `-Gun F2000`, `-Gun AEK` or `-Gun SPAS`.
SP_02 starts with the XM8; the test range gives the others (the F2000 and AEK
replace the XM8 in slot 0; the SPAS goes to slot 1 and is selected).

The launcher fixes the playtest settings for its own process:

| Setting | Value |
| --- | --- |
| Physical + magazine reload | on (`Start-BC2VRSession -MagazineReload`) |
| Fail-closed empty gate | on (default; `FVR_BC2_EMPTY_GATE_FAILCLOSED` cleared) |
| `FVR_BC2_COPY_AUTHORITY` | `1` |
| `FVR_BC2_CHAMBER_RACK` | `1` |
| `FVR_BC2_RELOAD_MAILBOX` | off |
| Probe/test variables (`FVR_BC2_PROBE_*`, `FVR_BC2_TEST_*`, `FVR_BC2_CAPTURE_HIDE_LEFT_HAND`) | cleared |

## What to try

- XM8, F2000 and AEK magazine reloads: grab the magazine, pull it out, drop it,
  take a fresh one from the pouch, insert it until it seats.
- Chamber rack after an empty reload (pull the charging handle), and the bolt
  release: after an empty reload, press Reload once instead of racking.
- SPAS: load shells one at a time from the pouch, then pump (left grip on the
  fore-end, stroke back and forward). Shell loading is built but has not been
  checked in a headset yet.
- Magazine drop: the dropped magazine should land on the floor and stay a while.
- Empty gun: fire to empty and wait; it must never reload by itself.
- Reload button fallback: with the left hand not tracked (hand down or behind
  you), an empty gun plus a Reload press should reload the normal way.

## Known issues

- At the frozen playtest checkpoint, no weapon switching with the stick (flick
  up/down did nothing). Current main's implementation/evidence is noted above.
- Single player only.
- SPAS: cancelling while a shell is going in can load one extra shell.

## Logs

Each session writes `reports\native-xr-session-<utc>\` inside the checkpoint
folder (gun-events.jsonl, native trace, host logs); the test-range log is
`stage\BC2TestRange.log` there.
