> **Technical reference.** API details and examples below retain their stated checkpoint scope; check current source before reuse.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Gun event log

A compact, append-only JSONL record of what the game's own weapon state machine
and the FrostbiteVR interaction layer did, tick by tick. It is the BC2 counterpart
of KF2 VR's `KF2VR_INTERACTIVE_RELOAD phase=...` rows and uses the same phase
vocabulary where the two games agree. `tools/analyze_gun_events.py` turns one or
more logs into per-reload timelines and a PASS/FAIL verdict.

The log is an observation channel. Nothing in it is read back by the game, and
no replay or test code may write consumer state to make a row appear. Ammunition
is judged only from rows the native hooks produced.

## Producers and files

| `src` | Producer | Typical file |
| --- | --- | --- |
| `native` | Hooks on the game's firing code: step `0x6e62d0`, update `0x6e9000`, transition commit `0x6dd6a0`, ammunition transfer `0x6d6f40`, the empty-ammo gate, shoot hooks | `gun-events.native.jsonl` |
| `fvr` | The interaction layer: hand claims, magazine/shell consumers, `ReloadInsertion`, `AmmoSupply`, feedback | `gun-events.fvr.jsonl` |

The live DLL producer (see Live producer below) writes both into one
`gun-events.jsonl`; any `gun-events*.jsonl` name is accepted by the runner.
Both producers may write to one file or to two. The analyzer merges any number of
files by `(qpc_us, tick)`, so both must read the same QPC clock. Hooks push fixed
structs into a lock-free ring. A drain thread formats and flushes lines, and it
never formats on the game thread. When the ring overflows, the drain emits a
`dropped` row with the count, and the analyzer fails the run (see Verification
boundary).

## Line format

One JSON object per line, UTF-8, no BOM, `\n` terminated, no pretty printing.
Unknown keys are ignored and unknown phases are counted and skipped, so new
fields and phases can be added without bumping `v`. Existing names and meanings
never change within a version.

```
{"v":1,"tick":48211,"qpc_us":812334100,"src":"native","weapon":{"asset":"XM8_sp_s","id":"0x1a2b4c00"},"copy":"server","phase":"transfer","loaded_before":3,"reserve_before":60,"loaded_after":30,"reserve_after":33}
```

| Key | Type | Meaning |
| --- | --- | --- |
| `v` | int | Schema version. This document is `1`. A reader rejects any other value. |
| `tick` | int | Game input tick: the count of `GatherHook` updates. This is the primary ordering key between events from different producers. `frame` is accepted as a synonym. |
| `qpc_us` | number | `QueryPerformanceCounter` in microseconds, one clock for the whole process. Used for every duration in ms. |
| `src` | `"native"` or `"fvr"` | Producer. |
| `weapon` | object | `asset` is the exact config name (`XM8_sp_s`, `SPAS12_sp`, `AEK971_sp`). `id` identifies the carried item (the server item pointer, hex string, or any stable scalar). A reload is tracked per `(asset, id)`. |
| `copy` | `"client0"`, `"client1"`, `"server"` or `null` | Which of the three native firing copies the row describes. `null` for `fvr` rows and for rows that are not tied to a copy. `branch` 0, 1, 2 is accepted as `client0`, `client1`, `server`. |
| `phase` | string | The event name. Lower-case with underscores. Hyphenated spellings (`pouch-start`, `freeze-start`, `auto-reload`) and the one-liner names `state`, `switch` and `pouch` are accepted as aliases. |

Only `v`, `tick`, `qpc_us`, `src` and `phase` are required. Rows are one event
each: the writer emits a row when something changes, not every tick. The one
periodic row is `sample`, every 30 ticks.

## BC2 native facts the rows depend on

- **Three firing copies.** Each weapon has two client branches (wrapper `+0x3c`
  and `+0x40`) and the server item. A native event is logged once per copy that
  runs it. The three copies must agree on state and ammunition shortly after
  every change.
- **Firing states.** `2` ready, `10` reload begin, `11` wait
  `ReloadTime x multiplier x ReloadThreshold`, `12` transfer plus tail, `1`
  return, then `2`. A single-round reload (SPAS12) goes `12 -> 11` repeatedly,
  one `transfer` per shell, and leaves through `1`. Bolt and pump cycles are
  `5/6 -> 7 -> 8 -> 1 -> 2` and are not reloads. Native dispatch can pass through
  several zero-duration states in one update, so consecutive `firing_state` rows
  need not be contiguous.
- **Transfer.** `0x6d6f40` adds the missing capacity (`rtMagazine`) or one round
  (`rtSingleBullet`) to loaded (`+0x7c`) and subtracts it from reserve (`+0x80`).
  Loaded plus reserve is therefore conserved by every transfer and every
  prediction rollback. Only shots remove rounds.
- **No chamber.** The game has no separate chambered-round count. The log never
  records chamber as game truth. A mod-owned model may add `mod_chamber` to a row;
  the analyzer ignores it.
- **Freeze.** While a physical reload holds the native timer in state 11, the
  hook zeroes the frame delta at `context+0x18` for one original call and restores
  it. Each application per copy is a `freeze_start`/`freeze_stop` pair.

## Native phases (`src: "native"`)

| Phase | When | Fields |
| --- | --- | --- |
| `firing_state` | The current state of a copy changed | `from`, `to` (the one-liner `cur`/`prev` also work). Optional `next`, `timer`, `flags_a8`. |
| `transfer` | `0x6d6f40` ran for a copy. This is the native receipt. | `loaded_before`, `reserve_before`, `loaded_after`, `reserve_after`. Optional `invocation`, `path`. |
| `commit` | Transition commit `0x6dd6a0` ran | `from`, `to`. Optional `invocation`, `matched`. |
| `restore` | Client prediction rolled a copy back to server values | `loaded_before`, `reserve_before`, `loaded_after`, `reserve_after`. Optional `matched`. |
| `step` | The state step ran and a value changed | `state`, `loaded`, `reserve`. Optional `empty_override` (bool, the `context+0x28` byte was written). |
| `auto_reload` | The empty-ammo gate saw a reload that the game would start by itself | `suppressed` (bool: the override stopped it). Optional `loaded`, `reserve`, `requested`, `applied`, `restored`. |
| `freeze_start` | The delta override began for a copy | Optional `reason`, `timer`. |
| `freeze_stop` | The delta override ended for a copy | Optional `reason` (`release`, `cancel`, `abort`, `owner_lost`), `held_us`. |
| `fire` | A shot was attempted by a copy | `result` (`shot`, `dry`, `masked`), `loaded`, `reserve` (both after the attempt). Optional `consumed` (default 1 for `shot`, else 0), `shot_token`. |
| `weapon_switch` | A mode request was acknowledged or cancelled | `from`, `to` (assets), optional `action`, `cancelled`. |
| `sample` | Every 30 ticks | `loaded`, `reserve`, `cur`, `next`, `shots`, `transfers`, `mag_phase`, `stale`. |
| `dropped` | The ring overflowed | `n`, number of lost events. |
| `slide_lock` | Derived from the same copy reads (static RE, not live-verified): the action counts as held open while state 2 has loaded 0. It latches there and clears only when loaded rises or the copy enters reload states 10-12, so a dry trigger pull cannot toggle it. The engine has no lock state. | `locked`, `state`, `loaded`; `last_round: true` when a shot emptied the gun. |

## FVR phases (`src: "fvr"`)

These mirror KF2's `phase=` rows. The BC2 consumers emit them from the
`DetachableMagazine` and `ManualReload` phase and reason transitions. `copy` is
`null`.

| Phase | When | Fields |
| --- | --- | --- |
| `sample` | Every 30 ticks while a reload session is active | `hand`, `mag_phase`, optional distance and angle to the nearest socket |
| `begin` | A reload session starts (eject, 9 cm pull, shell request) | `hand`, `kind` (`magazine`, `shell`) |
| `pouch_start` | The free hand grabbed an ammunition object at the chest pouch | `hand`, optional `item` |
| `capture` | The object entered the insertion guide (`Free -> Guided`) | optional `approach`, `distance_mm` |
| `seat` | `ReloadInsertion` reached `Seated` and a transaction request was submitted | optional `cycle`, `credits` |
| `credit` | The consumer observed the native receipt for that seat | `loaded`, `reserve` (as read from the receipt), optional `cycle` |
| `rack` | A charging, pump or bolt gesture finished | optional `by_grip` |
| `cancel` | The operation was abandoned | `reason`, the consumer's cancel cause (`claim_lost`, `interaction_rejected`, `owner_changed`, `drop`, `deadline`, ...) |
| `unseat` | The magazine left the well. Magazine is out from here. | optional `hand` |
| `release` | The hand let go of a held object. `to: "well"` is a slider release-to-finish: the magazine settles in and a `seat` follows within 250 ms | `to` (`drop`, `pouch`, `well`, `holster`) |
| `complete` | The consumer saw the full plan finish | optional `cycle` |
| `end` | The session is over. Exactly one `end` follows every `begin`. | `reason` (`complete`, `cancel`, `swap`, ...) |
| `haptic` | A feedback pulse was sent | `kind` (`reload_capture`, `reload_seated`, `reload_applied`), `hand`, `sent`; optional `amp` (0 to 1), `ms` |
| `chamber` | The mod's VR chamber model changed (not game truth; the analyzer skips it) | `event` (`observe`, `eject`, `seat`, `rack`, `reset`), `loaded`, `display_ammo`, `chamber_loaded`, `needs_rack`, `mag_out` when out; `label` (`CHAMBERED`, `MAGAZINE OUT`, `RACK`, KF2's readout) when there is one |
| `claim` | A hand claim changed | `hand`, `kind`, `token`, `result` |
| `tape` | Replay or recording marker | `reason` (`start`, `precondition`, `end`, `stop`, `bookmark`), optional `name` |

Magazine-out is derived from `unseat` (out) and `seat`, `cancel`, `complete`,
`end` (in). Any fvr row may carry `mag_out: true|false` to set it explicitly.

## Expected order of a magazine reload

```
begin  firing_state 2->10 x3  firing_state 10->11 x3  freeze_start x3
unseat pouch_start capture seat
freeze_stop x3  firing_state 11->12 x3  transfer x3  credit
firing_state 12->1 x3  firing_state 1->2 x3  complete  end
```

A shell reload repeats `seat`, `transfer` x3, `credit` once per shell and
returns `12 -> 11` between them. A cancel is `cancel`, then `freeze_stop` and
`firing_state ... -> 2` for each copy, then `end`. Any `transfer` that adds
rounds after a `cancel` is a takeover (the 2026-10-01 SPAS12 case).

## Analyzer

```
python tools/analyze_gun_events.py gun-events.native.jsonl gun-events.fvr.jsonl
python tools/analyze_gun_events.py --json --rules rules.json run/*.jsonl
python -m unittest discover -s tests -p test_analyze_gun_events.py
```

Exit status is 0 for PASS, 1 for FAIL and 2 for unreadable input or bad rules.
The text report lists each reload (seat to credit, ammunition per copy, double
credits, cancels, freeze time), copy disagreements, fire attempts, then the
checks and `RESULT: PASS|FAIL`. `--json` prints the same report as
`fvr.gun_event_analysis.v1`; it always carries `headset_accepted: false`.

### Timelines

A reload for a weapon opens on a `begin`, a `firing_state` change to 10, 11 or
12, a `freeze_start`, or an unsuppressed `auto_reload`. It closes when every
producer that took part has finished: the fvr `end`, and every copy that entered
state 10 to 12 back at state 2. A new `begin` before the `end` closes the old
one as `superseded`. A seat, credit, transfer, restore, cancel or similar row
that arrives with no reload open attaches to that weapon's last reload if it is
within 10 s, and is listed under `late_events`. Otherwise it starts an orphan
reload. Outcomes are `completed`, `cancelled` and `incomplete`.

### Checks

| Check | Meaning | Default limit |
| --- | --- | --- |
| `invalid_lines` | Malformed JSON, wrong `v`, missing required keys | 0 |
| `dropped_events` | Sum of `dropped.n` | 0 |
| `reloads_found` | Reload timelines found | at least `min_reloads` (0) |
| `seat_credit_ms`, `seat_credit_ticks` | Longest `seat` to `credit` (or first gaining `transfer` when no `credit` was logged) | 500 ms, ticks off |
| `seat_transfer_ms` | Longest `seat` to the first gaining native `transfer` | off |
| `uncredited_seats` | Seats with no credit or transfer in a reload that was not cancelled | 0 |
| `conservation_violations` | Per copy and reload: a transfer or restore that changes loaded plus reserve; a snapshot (`step`, `sample`, `credit`, next `transfer`) that disagrees with the previous one; a `fire` that does not consume what it says. Shots are the only legal loss. | 0 |
| `double_credits` | `credit` rows beyond the seats, plus gaining transfers beyond the seats on any one copy (reloads with fvr rows only) | 0 |
| `auto_reloads_unsuppressed`, `auto_reloads` | `auto_reload` rows where the override did not stop the reload, and all of them | 0 and off |
| `fire_leaks` | A shot while the magazine is out, or a shot that consumed nothing from an empty magazine (de-duplicated per weapon and tick across copies) | 0 |
| `fire_attempts_unloaded` | Every attempt in that state, including `dry` and `masked` | off |
| `copy_disagreement_ticks`, `copy_disagreement_ms` | Longest span in which two or more copies disagree on state, loaded or reserve, measured at the ticks where the weapon logged | 2 ticks, ms off |
| `cancels`, `cancels[reason]`, `cancels_denied[reason]` | Counts of `cancel` by reason | off |
| `freeze_leaks` | A `freeze_start` with no `freeze_stop` by the end of the log | 0 |
| `incomplete_reloads`, `stale_samples` | Reloads that never closed, and `sample` rows with `stale` set | off |

A check with a limit of `null` is reported as `INFO` and never fails. Overall
FAIL is any single FAIL.

### Thresholds

Every limit is a flag (`--max-seat-credit-ms 1000`, `--max-copy-disagreement-ticks 4`,
`--max-cancels-by-reason claim_lost=2`, `--deny-cancel-reason interaction_rejected`,
`--min-reloads 1`) and a key in a JSON rules file with the flag's name and
underscores. `none` or `null` disables a limit. CLI flags override the rules
file, which overrides the defaults. `--dump-rules` prints the effective set.

```json
{"max_seat_credit_ms": 250, "max_cancels": 0, "deny_cancel_reasons": ["interaction_rejected"], "min_reloads": 1}
```

## Live producer

The BC2 DLL writes the log itself during a native trace. It adds no hooks: the
existing reload-flow hooks, the gameplay gather and the API wrappers push fixed
`GunEvent` structs (`src/games/bc2/Bc2GunEventLog.h`) into a bounded lock-free
MPSC ring. The NativeProbe probe thread drains the ring on its existing 10 ms
loop and is the only code that formats or writes.

### Activation and isolation

The log is off unless all of these hold:

- `NativeProbeConfig::gunEventLog` is `Enabled`. `ValidGunEventLogConfig`
  accepts that only with the frame channel (`flags & 0xff == 9`) and controllers
  (`0x800`); anything else refuses the trace (status 10).
- The trace was started with `BC2NativeTrace --gun-events`, or with
  `Start-NativeTrace.ps1 -GunEvents` (also forwarded by
  `Start-BC2VRSession.ps1 -GunEvents`). The script refuses `-GunEvents` without
  `-FrameChannel` and `-Controllers`.
- `tools/replay/run.py --mode live` always passes `-GunEvents`.

The file is `gun-events.jsonl` next to `native-trace.json`, in the new run folder.
It must not already exist. It is opened before hooks are installed and closed after
they are removed. Open freezes are then closed with `reason: "trace_end"`. When the
log is disabled every push is one relaxed load and a return, so a normal session
writes nothing. `native-trace.json` gains a `gun_events` object (`written`,
`pushed`, `dropped`, `dropped_rows`, `failed`, `chamber_logged: false`,
`headset_tested: false`) and `gun_events_native_busy`.

### Where each row comes from

| Phase | Producer | KF2 counterpart |
| --- | --- | --- |
| `firing_state`, `step`, `fire`, `slide_lock` | `Update`/`Commit` hook entry and exit reads of each copy (`client0`, `client1`, `server` from the branch index). `fire` is a drop in `loaded` with `reserve` unchanged; any other change is `step`. `slide_lock` is derived from the same reads. | weapon state lines |
| `transfer` | Transfer hook `0x6d6f40` exit, before/after per copy, with `path` | `phase=credit` receipt |
| `restore` | Restore hook, with `matched` | none |
| `commit` | Transition commit hook `0x6dd6a0` | none |
| `auto_reload` | Step hook when the `context+0x28` override was applied (`suppressed: true`) | none |
| `freeze_start` / `freeze_stop` | Edge of `hold.applied` (the `context+0x18` delta override) in the Update hook. Stop is `release` while the copy is still in state 10/11/12, else `cancel`; `held_us` is measured. | `phase=hold` |
| `weapon_switch` | Gather hook, when the `SoldierWeaponData` asset or weapon pointer changes | `phase=swap` |
| `sample` (native) | Every 30 gathers, from the last observed values; `stale` after 250 ms without a hook read | `phase=sample` |
| `begin`, `unseat`, `release`, `pouch_start`, `capture`, `complete`, `cancel`, `end` | `DetachableMagazine` phase changes read in the gather, and `ManualReload` shell state for SPAS | `phase=begin` ... `phase=end` |
| `seat` | Magazine/SPAS transaction submit in the API wrapper, with `cycle` and `credits` | `phase=seat` |
| `credit` | Transaction acknowledgement, with the receipt's `loaded`/`reserve` | `phase=credit` |
| `claim`, `haptic` | Hand claims and feedback pulses in the gather (`reload_seated` from the magazine consumer's `ReloadInsertionHaptic::Seated`) | `phase=claim`, haptics |
| `release` (`to: "well"`), `rack`, `chamber` | `MagazinePhysicalResult::settleStarted`/`racked` and `Bc2MagazinePhysicalReload::Chamber()` after the consumer Tick. The sim emits them through the same `GunFvrTracker` row builders | `VRReloadInsertGuide` release, rack lines |
| `tape` | Input tape replay reader: `start`, `precondition` (met or timeout), `end` | none |
| `sample` (fvr) | Every 30 gathers while a reload session is open, with `mag_phase` | `phase=sample` |
| `dropped` | The drain, when the ring overflowed or a busy copy skipped a row | none |

`tick` is the gather count. `qpc_us` is `QueryPerformanceCounter` in microseconds,
stamped when the row is pushed.

### Limits

- Native rows exist only while the reload-flow hooks observe. Outside request
  mode (`-MagazineReload`) observation stops 20 s after it starts, and the exit
  read happens only for a recorded or requested invocation.
- Dry fire is derived from the Update entry context's fire bit: a rising edge
  on a ready (2/2), empty copy that is still ready and empty after the Update
  writes `fire` with `result: "dry"` and `native_click` (true when reserve is
  0, so the game plays its own out-of-ammo click; false in the VR case where
  reserve remains and auto-reload is held off). Static RE, not live-verified.
- An auto-reload the override did not suppress shows only as `firing_state`.
- The SPAS `credit` row uses the last observed server counts, not a receipt.
- The freeze stop reason is a heuristic from the copy's state.
- Native samples repeat the last hook read. They are not fresh reads.
- Each copy's tracker state is guarded by a try-lock and never waits. A busy
  copy skips the row, counts it in `gun_events_native_busy` and reports it in a
  `dropped` row, so the run fails rather than losing it silently. A skipped
  periodic sample is counted in `gun_events_native_busy` only.
- `rack` rows need the charging-handle input, which is off by default (no
  handle pose is mapped), so a normal run writes none.

### Verification boundary for the live producer

Linux tests cover the formatter, the ring (including 4 producer threads under
ASan, UBSan and TSan), drop rows, the native and fvr trackers, and the config
gate. `tests/data/gun-events-live-xm8.golden.jsonl` is written byte for byte by
those trackers. `tests/test_gun_event_golden.py` checks that it passes both
`analyze_gun_events.py` and `replay_assert.py` for `xm8-mag-reload`. The hook
glue in `Bc2ReloadFlowRuntime.cpp`, `Bc2Gameplay.cpp`, `NativeProbe.cpp` and
`NativeTraceMain.cpp`, and the PowerShell switches, are Windows-only and have
not run in the game. A live pass needs:

```
.\Build.ps1 -Architecture x86
.\Build.ps1 -Architecture x64
# BC2 in a campaign level holding the XM8, headset idle
python tools/replay/run.py --scenario xm8-mag-reload --mode live
# or by hand:
.\Start-NativeTrace.ps1 -FrameChannel <token> -Controllers -MagazineReload -GunEvents -Seconds 60
python tools/analyze_gun_events.py <run>\gun-events.jsonl
python tools/replay/replay_assert.py <run>\gun-events.jsonl --scenario tools/replay/scenarios/xm8-mag-reload.json
```

Check that `native-trace.json` reports `gun_events.dropped` as 0.

## Verification boundary

A passing analysis shows that the logged sequence for this run was internally
consistent: ammunition came only from native transfers, timing was within the
limits, and the three copies agreed. It does not show that every event was
logged (only `dropped` rows prove loss), that the feel or visuals are right, that
other weapons behave the same, or that a headset test passed. Rows from the
offline simulator are labeled by the run that produced them and never stand in for
a live game pass. The `fvr` rows are claims by the mod; only `native` rows count
as evidence of ammunition.
