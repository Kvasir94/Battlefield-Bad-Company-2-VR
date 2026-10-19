> **Technical reference.** API details and examples below retain their stated checkpoint scope; check current source before reuse.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Developer input tape replay

The input tape is a developer source for the controller input that the BC2
native gather hook reads. When it is active, the tape stands in for
`frameBridge.ReadInput` at the gather (`gameplay::InputReader`, which is
installed by `NativeProbe.cpp` before `gameplay::Install`). Everything
downstream is the production path, unchanged and running in the game:

- the consumer;
- `HandInteraction` and `TrackedRig`;
- the reload flow and its native requests.

A replay must never write ammunition, firing state, reload requests or
consumer state itself. It only supplies the input that a player's hands would
have supplied. A recorded tape plus its gun-event log
(`docs/GUN-EVENT-LOG.md`) is a regression case for a headset bug.

The tape is off by default. The player launcher never activates replay. The
only thing a playtest session can opt into is recording.

## Activation and isolation

The DLL learns the mode from `NativeProbeConfig` (1708 bytes): an
`inputTape[260]` path plus `tapeMode` (0 off, 1 replay, 2 record).
`ValidInputTapeConfig` refuses the configuration, and therefore the
installation, unless all of these hold:

- the path is absolute;
- the probe runs a tracked controller stream (`(flags & 0xff) == 9` with
  `0x800`, i.e. `-FrameChannel` with `-Controllers`).

For replay, it additionally refuses:

- every synthetic probe and fixture flag (magazine, physical-reload,
  reload-hold, rig-pulse, equip and death probes);
- a magazine session other than 0 or 3. Replay runs the real
  `-MagazineReload` / `-PhysicalReload` features, never a scripted probe.

How to activate each mode:

| Entry point | Replay | Record |
| --- | --- | --- |
| `BC2NativeTrace.exe` | `--input-tape <tape>` | `--record-tape <new path>` |
| `Start-NativeTrace.ps1` | `-InputTape <tape>` | `-RecordTape <new path>` |
| `Start-BC2VRSession.ps1` | (none) | `-RecordTape` |
| `BC2XrHost.exe` | (none) | `--record-tape <new path>` (companion) |

`Start-NativeTrace -InputTape` copies the tape into the trace folder as
`input.fvrtape` and writes `input-tape.json` with its SHA-256. A record path
must not already exist.

The DLL loads the tape before it installs any hook. A missing or malformed
tape refuses installation. It never falls back to live input.

The DLL stays mapped in BC2 after the trace ends, and its state is
process-wide. A second replay therefore needs a **BC2 restart**: one tape per
game process.

The source binds to the first owner it sees. A changed owner stops the tape
permanently (the binding is not wired in the DLL yet; see Known gaps). At end of
input, the gather receives unavailable frames forever. That cancels held
grips and triggers through the production release path. The source never
returns to live XR.

## Version 1 text format

The file is ASCII, with no BOM. Numbers use the classic decimal locale and
are separated by whitespace.

```
FVR_BC2_INPUT 1 STEP_MICROSECONDS SEED
#! weapon=XM8_sp_s loaded<capacity reserve>=30 anchor=world world_units_per_meter=1
#! source=dll_gather_record checkpoint=...
#! expect transfers=1 no-auto-reload seat_to_transfer_ms<=1500
# comment
TICKS AGE_MS EPOCH FOCUSED HEADVALID FLOOR head(7) referenceHead(7) left right
```

Header fields:

- `STEP_MICROSECONDS` is 1000 through 100000. Recordings use 10000.
- `SEED` is unsigned 64-bit provenance. It does not reseed the game.

Each record repeats for its run-length `TICKS`. A tape may hold at most
360000 ticks in total.

A pose is written `x y z qx qy qz qw` in the IPC frame's tracking space.
Quaternions must be normalised. A hand is written:

```
FLAGS BUTTONS trigger squeeze stickX stickY grip(7) aim(7)
```

| FLAGS bit | Meaning |
| --- | --- |
| 1 | Grip pose tracked |
| 2 | Aim pose tracked |
| 4, 8, 16, 32, 64, 128, 256 | Action active: Stick, Trigger, Squeeze, Primary, Secondary, StickClick, MenuClick |
| 512, 1024 | Touch active: thumb, index |

`BUTTONS` holds two fields:

- the held bits, in the same action order (1..64);
- the touched bits shifted left by 7 (128 thumb, 256 index).

The maximum for `FLAGS` is 2047 and for `BUTTONS` it is 511. Unknown bits,
non-finite or out-of-range axes and trailing tokens reject the tape.

Each record must pass `interaction::ValidInput`. In particular, a value on an
inactive action is invalid. Inactive zero is not a physical release. A release
that rearms the trigger needs the action active with value 0.

### Start conditions (`#!`)

These are the KF2 header-condition rules, applied to BC2's coherent reserve
lease. Before the first tick, the gather returns *no input* until the live game
matches every condition:

- `loaded=N`, `reserve>=N`, and `loaded<capacity` (compared with the live
  capacity, not a recorded number);
- `weapon=` (an exact match, or a case-insensitive prefix of the native asset).

`reloadFlowRuntime::ReadReserve()` reads the live state while the replay is
holding. It is an existing observation and makes no native write. The weapon
asset is not observable at the gather, so the DLL skips only the `weapon=`
condition. The sim and the analyzer still check it. If the conditions are
still unmet after 20 s, the source closes and the report says
`precondition_timed_out`.

The replay never edits game state to satisfy a condition. Get the game into
that state (weapon, partial magazine, enough reserve) before you start.

`anchor=weapon` stores the left hand relative to the right grip, so a
two-hand interaction survives a small difference in where the gun is held. The
default is `anchor=world`. Unknown keys such as `source`, `checkpoint` and
notes are provenance only and never block a replay.

### Ticks, freshness and epochs

Each native gather consumes one tick. Render passes and wall-clock polls
consume none. For the tick numbered `tick`, the gather publishes:

- `generation = tick`;
- `predictedNs = tick * STEP_MICROSECONDS * 1000`;
- `spaceGeneration = EPOCH + 1`.

The deadline follows the live rule: publish time plus 100 ms. A record with
`AGE_MS` over 100 is stale and replays as stale.

An `EPOCH` change becomes a new reference-space generation. Unlike KF2, the
first sample of an epoch is **not** blanked. The recorder captures exactly
what the gather saw, so blanking it would make a replay diverge from its
recording.

## Recording in the headset

Start a normal headset session with recording:

```powershell
.\Start-BC2VRSession.ps1 -MagazineReload -BodyInventory -RecordTape
```

It writes two tapes into the session folder:

- `input.fvrtape` comes from the DLL. It is the gather-side truth: every
  gather, including busy, stale and empty reads.
- `host-input.fvrtape` comes from `BC2XrHost`. It is the companion copy,
  written after menu filtering and before `UpdateInput`.

Use the DLL tape for replay. The host tape explains a difference between the
two, for example a frame the IPC lane dropped.

The DLL tape is written through a 4096-entry lock-free ring that is drained
every 10 ms. If the ring overflows, the missing gathers are written as
unavailable ticks, so tick numbering stays aligned with the gun-event `tick`.
The report's `input_tape.record` section counts the gathers `observed`,
`written` and `dropped` to overflow.

With the gun-event log enabled as well, the same session also yields
`gun-events*.jsonl`, keyed by the same gather `tick`.

## Cutting a regression tape

```powershell
python tools/replay/cut.py <session>/input.fvrtape --events <session>/gun-events.jsonl `
  --bookmark 1 --before 20 --after 5 --expect "transfers=1 no-auto-reload" `
  --out tools/replay/tapes/xm8-20261008.fvrtape
python tools/replay/cut.py <tape> --from-tick 4000 --to-tick 6200 --out cut.fvrtape
```

`cut.py` splits run-length rows at the window edges. It rewrites
`#! weapon/loaded/reserve` from the last native `sample` or `transfer` row
before the cut. It keeps the remaining provenance and adds
`#! source=cut:<name>:<sha12>:<from>-<to>`.

Write `#! expect` from what *should* have happened, before you fix the bug.
The cut then fails until the fix lands.

Bookmark chords are not emitted yet (see Known gaps). Choose the window by
the tick of the gun event you care about.

## Replaying

Scenarios live in `tools/replay/scenarios/*.json`. Each scenario names:

- its tape and weapon;
- the simulator setup;
- the live trace switches and run length;
- the sibling analyzer's rules;
- its own `rules`.

```powershell
.\Run-Replay.ps1 -Scenario xm8-mag-reload -Mode sim
.\Run-Replay.ps1 -Scenario xm8-mag-reload -Mode live -Sim -Repeat 3
python tools/replay/run.py --scenario xm8-mag-reload --mode sim --repeat 2
```

Every invocation writes a fresh `reports/replay/<scenario>-<mode>-<utc>/`.
The runner never reuses or overwrites an old run. The folder contains:

- `run.json`, with SHA-256 hashes of the scenario, the tape and the binaries;
- one directory per attempt;
- `summary.json` (`fvr.bc2.replay_run.v1`), recording the pass rate,
  whether the sim was deterministic, and `headset_tested: false`.

**sim** runs `fvr_replay_sim`, the offline simulator, on Linux or Windows.
It uses the real consumer, `HandInteraction` and `TrackedRig`, against a mock
native side on a 10 ms virtual clock:

- the reserve lease arrives after 150 ms;
- the transfer lands 300 ms after the request.

The simulator emits schema-v1 gun events with `"sim": true`. Two sim runs of
the same tape must be line-identical. It models only the detachable-magazine
consumer, so `spas-shell-reload` reports `sim: unsupported`.

**live** (alias `ipc`) runs on Windows with the headset idle. The runner:

1. takes `Local\FVR_BC2_DevelopmentFixture`;
2. refuses to start while another BC2 test is running;
3. requires exactly one BC2 in gameplay and brings it to the foreground;
4. starts `BC2NativeIpcProbe --sink-seconds N` as a pure stereo sink (a static
   240-pair receiver that refuses any controls flag);
5. runs `Start-NativeTrace -InputTape` with the scenario's switches.

Between repeats, it waits for you to restart BC2. `-Sim` runs the simulator
first and does not start the game if the sim already fails.

A live attempt that produces no `gun-events*.jsonl` is judged
`no_gun_events`, never a pass.

### Judging

Each attempt is judged twice, by:

- `tools/replay/replay_assert.py <events> --scenario <json> [--tape <tape>]`;
- the sibling `tools/analyze_gun_events.py --json --rules`, when present.

Both exit 0 for pass, 1 for fail and 2 for unreadable input.

`replay_assert` rule types:

| Type | Checks |
| --- | --- |
| `sequence` | matcher steps occur in order |
| `count` | `eq`, `min` or `max` occurrences |
| `ammo_delta` | `+fill`/`-fill`/`N`/`>0` on loaded and reserve, plus `conserve` |
| `within` | `from` to `to`, using `max_ms`/`min_ms`/`max_ticks`, for the first or `every` occurrence |
| `never` | a match `before`/`after` another event, or `while` the gun is `mag_out`/`mag_in` |

Matchers take the form `phase[key=value,...]`, for example
`transfer[copy=server]` or `cancel[reason=interaction_rejected]`.

The tape's `#! expect` tokens become rules as well:

- `transfers=`, `credits=`, `loaded=`, `reserve=`;
- `no-auto-reload`, `no-fire-mag-out`, `seat_to_transfer_ms<=`.

## Determinism limits

- **Latest-value lane.** The live IPC lane keeps only the latest frame, but
  the tape replaces the gather-side read, so replay is exact per gather. What
  differs from run to run is how many gathers the game performs per
  wall-clock second. Native timers (reload animation, transfer state) run on
  the game clock, not on tape ticks.
- **100 ms expiry.** Freshness follows the live rule. A gather that stalls
  longer than 100 ms turns the replayed frame stale, as it would live.
  Recorded stale ticks stay stale.
- **Open-loop drift.** The tape replays recorded poses. It does not react to
  where the gun actually is. A small offset in the weapon pose, camera height
  or seated depth can move a hand outside a capture volume.
  - `anchor=weapon` covers the left hand in two-hand interactions.
  - The synthetic XM8 tape was authored against the simulator's mock geometry
    and is expected to need a headset recording before it passes live.
- **Foreground.** BC2 throttles and drops input when it is not in the
  foreground. The live runner brings it forward and must not be competing with
  another window.
- **Resident DLL.** Allow one replay per BC2 process. `-Repeat` asks for a
  restart between attempts.
- **Precondition hold.** The 20 s wait covers the reserve lease and a partial
  magazine. It cannot load a save or change weapons for you.

## Verification boundary

The CTests check:

- `InputTape`: parsing, rejection, run-length, stale and epoch handling, EOF,
  owner stop, the anchor, and writer round-trip.
- `Bc2InputTapeSource`: the gather freshness contract, the precondition hold
  and timeout, the recorder ring and overflow gaps.
- `Bc2ReplaySim`: the real consumer end to end.
- `ReplayScenarioXm8Sim`: the committed tape through `run.py`.

`test_replay_assert` and `test_replay_cut` cover the Python tools.

None of these checks establishes headset or live-game behaviour:

- The DLL replay, the recorder, the PowerShell plumbing and the live runner are
  Windows-only and need their own live run.
- A green sim run proves the consumer's logic against a mock, not the game's
  response.
- The headset remains the acceptance test.

## Lessons carried over from KF2

- Swap the input where the game reads it (the gather), and leave the rest of
  the production path alone. A scripted driver that also writes inventory or
  ammunition proves nothing about the real path.
- Make every run fresh and hashed. A replay that reuses a previous folder, or
  passes on a normal process exit, has produced no evidence.
- Treat end of input, an owner change and a malformed tape as "unavailable
  forever". Never fall back to live or fixture input mid-run.
- Gate start-of-tape state with header conditions and wait for them. Do not
  write game state to satisfy them.
- Write the expectation before the fix and keep it in the tape. A regression
  tape that never failed is not a regression test.

## Known gaps

- The live DLL gun-event emitter (`-GunEvents`, see `GUN-EVENT-LOG.md`, Live
  producer) is built and tested on Linux but has not run in the game yet.
- Owner binding and `anchor=weapon` with a live right grip are implemented in
  `InputTape` but not wired in the DLL. The DLL passes owner 0 and re-uses the
  tape's own right grip.
- There is no SPAS tape, and the XM8 tape is synthetic (mock geometry). Both
  need a headset recording.
- Not yet implemented:
  - bookmark chords (the stick-click hold);
  - tape rotation every 5 minutes;
  - the binary `.fvrrec` format.
