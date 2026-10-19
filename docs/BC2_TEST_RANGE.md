> **Technical reference.** API details and examples below retain their stated checkpoint scope; check current source before reuse.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 test range

A test range puts the single-player game into a known level and a known loadout
fast: no menu clicking, no intro movies, no cutscene waits, the requested guns in
the soldier's hands. It is the "where and with what" half of a replay run; the
replay harness (`tools/replay/run.py`, `docs/REPLAY_INPUT.md` on
`bc2/tools/replay-harness`) stays the "what the hands do" half. There is one
runner. This branch adds the range spec, its C++ and Python parsers, the native
executor interface and its tests. It does not add a second runner.

Status: **harness layer only.** Every native operation is a stub behind a
capability record that ships empty, so a range can never touch the game on this
build until the PC proves each operation (section 9). The only range that runs
today is the manual fallback `sp02-xm8`, which needs no native operation at all.
The mechanism ranking in section 6 is **provisional**: it rests on repo evidence
and the PC static RE notes, not on a live run.

## Verified baseline: fast start into gameplay (no clicks)

Live on BC2 build 795745 (`BFBC2Game.exe` SHA-256 `c603f686…6b0162`),
2026-10-08: launch to SP_02 with no menu clicks. newSP fires about 4.3 s after
launch; control arrives about 2 minutes after launch, mostly SP_02's opening
cutscene (skipping it is the next item). XM8 at 30/210. Full native method, addresses and signatures:
`docs/re/BC2_FAST_START_NEWSP.md` (branch `re/pc-give-path-ghidra`). The DLL
source is in `tools/testrange/` on `test/range` (`1d19bac`).

```
.\tools\testrange\Start-BC2TestRange.ps1 -GamePath '<BC2 install folder>' -Bin <folder with BC2TestRange.dll, BC2TestRangeInject.exe, BC2TestRange.cfg> -InjectDelayMs 3000
```

```
Client.Level Levels/SP_02
TestRange.Start newSP
TestRange.Difficulty 1
TestRange.WaitFrames 60
```

| Step | Native route | Result |
| --- | --- | --- |
| Inject | `CreateRemoteThread(LoadLibraryW)` about 3 s after launch | Works; about 100 ms makes BC2 exit silently. The script default is 0, so always pass it |
| Set level | `Client.Level` field from `0x4f4c40`, value built with `0x4014c0`, begin/end/capacity swapped in | Read back `Levels/SP_02` |
| Other settings | `SettingsManager::Set` `0x4f4bd0` | Read back at the menu (`ranges/sp02-menu-settings.cfg`), but `Server.Level` plus `Server.IsAIEnabled 0` **crashed BC2 about 7 s into the SP_02 load** (null `this` at 0x612700). Leave both out; AI stays on for now |
| Start level | UIManager (`*0x19a4ca8`) `update` `0x959900` hooked via vtable entry `0x1440860`; after 60 menu frames, `newSP` `0x7600f0(difficulty 1)` on the UI thread | newSP at 4.3 s; gameplay after the opening cutscene (about 2 min). `update` takes two stack args (`ret 8`); a one-arg detour crashed at the menu |
| Loadout | SP_02 starts with XM8_sp_s | Other guns: `bc2/feat/range-give-weapon` (untested live) |

Open: the first version never restores the vtable entry; it must be restored
after newSP fires and on every failure path. `LoadMenu 0` alone does not skip
the menu; newSP is what starts the level.

## 1. Activation and isolation

- Nothing happens unless a range is active. The probe's executor
  (`fvr::bc2::TestRange`, `src/games/bc2/Bc2TestRange.h`) starts `Inactive`; its
  `Tick` does nothing and calls no hook until `Begin` accepts a valid spec. One
  range per process.
- `Begin` refuses (`unavailable`) unless every operation the chosen route needs
  has exactly one reviewed capability record for this exact executable (the
  `Bc2NativeOperationCapability.h` pattern: exe fingerprint, image base, four code
  proofs, operation digest, source and executable SHA-256, readback and restore
  receipts). The default record list is empty.
- Optional operations without a record are skipped with an explicit
  `skip`/`unavailable` row, never attempted. A required one fails the run.
- Every effect is proved by a later observation. A dispatch whose outcome is
  unknown is never retried. Restore runs once on every terminal outcome (ready
  and stopped, failed, timed out, owner lost) and a failed restore overrides the
  reported reason.
- Game files are touched only by the optional, journaled movie rename (section
  5.3), which `unstage` reverses exactly.
- Single-player only. Levels are validated against the 13 shipped SP ids.

## 2. Range file format, version 1

ASCII text, at most 64 KiB, lines at most 512 bytes. CRLF or LF. `#` and `;`
start comment lines. The first non-blank, non-comment line is the header:

```text
FVR_BC2_RANGE 1
```

Then `key=value` lines (spaces around `=` allowed). Unknown and duplicate keys
are errors; errors name the line (`line 4: unknown key bad`). A UTF-8 BOM or any
non-ASCII byte is rejected.

| Key | Value | Default | Meaning |
| --- | --- | --- | --- |
| `name` | identifier | file stem | label in logs |
| `notes` | free text | | provenance only |
| `level` | `Levels/<id>` | **required** | `<id>` one of `sp_01 sp_02 sp_03 sp_04 sp_003_b sp_004_b sp_005 sp_005_b sp_006 sp_007 sp_008 sp_008_b sp_009`, case-insensitive |
| `route` | `menu` `override` `manual` | `menu` | how the level gets loaded (section 4) |
| `spawn` | identifier | | `Client.SpawnPoint`; not with `manual` |
| `savepoint` | identifier | | `Server.SavePoint` plus `Server.LoadSavePoint=true`; not with `manual` |
| `weapons` | `a,b` or `[a, b]`, each `asset` or `asset@Slot` | **required** | 1 to 8 weapon asset names (`XM8_sp_s`), no duplicates (case-insensitive), slots unique |
| `select` | asset in `weapons` | first weapon | the weapon that must be active at ready |
| `clear_inventory` | bool | `true` | drop weapons not in the list |
| `skip_cinematics` | bool | `false` | skip the intro cutscene once if the operation exists |
| `ammo` | `native` `full` | `native` | default ammo state for every listed weapon |
| `ammo.<asset>` | `full` or `L/R` (`native` allowed for one side) | | per-weapon loaded/reserve, each 0..100000 |
| `teleport` | `x,y,z[,yaw]` decimals | | yaw in -360..360 |
| `menu` `skip_intro` `fast_startup` `cutscenes` `ai` `god` `checkpoints` `unlocks` `instant_spawn` `unlimited_ammo` `unlimited_mags` `scripted_input` | `true`/`false` | unset | settings switches (section 3) |
| `kit` | 0..13 | | `Client.Kit` |
| `team` | 0..15 | | `Client.Team` |
| `fixed_dt` | decimal 0.001..0.1 | | `Game.ForceDeltaTime` |
| `timeout` | seconds 1..3600 | 180 | whole run |
| `ready_timeout` | seconds, at most `timeout` | 90 | until ready |
| `set.<Group>.<Field>` | value without shell or quote characters | | passthrough setting, at most 32 |

Slots are the `WeaponSwitchingEnum` names: `Primary1 Primary2 GrenadeLauncher1
GrenadeLauncher2 StaticGadget DynamicGadget1 DynamicGadget2 MeleeWeapon
HandGrenade` (0..8).

The canonical form (`FormatRange`, `format_range`) is what the runner writes to
`range.ini`; parsing it again gives the same spec. C++ and Python agree byte for
byte, enforced by the shared golden files in `tests/fixtures/test-range/`.

Shipped ranges (`tools/test-range/ranges/`):

| Range | Route | Weapons | Purpose |
| --- | --- | --- | --- |
| `sp02-xm8` | manual | `XM8_sp_s` (already in the SP_02 kit) | the fallback; works today with zero native operations |
| `xm8-spas` | menu | `XM8_sp_s@Primary1`, `SPAS12_sp@Primary2` | target route B plus a kit rewrite |
| `aek971-fixed-dt` | menu | `AEK971_sp@Primary1`, full ammo, fixed 60 Hz step | any-weapon template, deterministic timing |

## 3. Settings mapping

`RangeSettings` / `range_settings` turn a spec into ordered `Group.Field value`
pairs. The order is fixed: `Client.Level`, `Client.SpawnPoint`,
`Server.SavePoint` + `Server.LoadSavePoint`, then `menu`→`Client.LoadMenu`,
`skip_intro`→`Client.DisableMovies`, `fast_startup`→`Client.IsFastStartupEnabled`,
`cutscenes`→`Server.DisableCutscenes` (inverted), `ai`→`Server.IsAiEnabled`,
`god`→`Server.IsGodMode`, `checkpoints`→`Server.DisableCheckpoints` (inverted),
`unlocks`→`Server.UnrestrictedUnlocks` and `Client.UnrestrictedUnlocks`,
`instant_spawn`, `unlimited_ammo`, `unlimited_mags`, `kit`, `team`, `fixed_dt`,
`scripted_input`, then `set.*` in file order. A key set twice (including through
`set.*`, case-insensitive) is an error.

Each typed key maps to a reflected field in `src/games/bc2/Bc2TestRangeFields.h`
(owner type, TypeInfo VA, offset, size, field type). The offsets come from the
build 795745 TypeInfo dump: see `docs/BC2-TYPEINFO-DUMP.md` on
`bc2/tools/typeinfo-dump` and `docs/re/BC2_TYPEINFO_HARNESS_TYPES.md` on
`bc2/claude/fast-test-level-research-mij58q`; this document does not repeat them.
The table is **unverified**: an entry is used only through an admitted
operation, and the writer re-checks the live object's TypeInfo before it writes.

`RangeSettingsWriter` (`Bc2TestRangeSettings.h`) is the registry-route writer:

- plans each pair (Boolean, Uint32, Float32 are encoded; String fields such as
  `Client.Level` are `deferred_string` until the 12-byte Frostbite `String`
  layout is reversed; `set.*` keys with no table entry are `unmapped`);
- refuses the whole batch before any write when a singleton is unresolved, a key
  unmapped, a value invalid, a live type wrong or two writes overlap;
- records the original bytes, writes, reads back, and rolls everything back if
  any write does not stick;
- restores only bytes still holding the value it wrote. A field the engine has
  changed since is left alone and counted as a conflict (exact-write restore).

## 4. Level routes

PC static RE (route decision recorded by the coordinator; details on the
research branch `docs/BC2_TEST_LEVEL_ROUTES.md`):

- **Command line and cfg: dead end.** Argv values are looked up only by exact,
  non-dotted name (0x4f4cf0), so `Client.Level=...` on the command line never
  reaches the settings. The text config loader (0x5017c0) and add-override
  (0x501ab0) have no callers. Kept here only as a recorded dead end; the spec has
  no command-line route.
- Settings registry: pointer at 0x155cfdc, dotted lookup 0x4f4600. Each settings
  group applies the override queue as it registers (0x4f4a00).
  `ClientSettings.Level` defaults to `Levels/SP_01`.
- SP starts only through the menu command `newSP` (UI command 132 → 0x7600f0),
  which loads `Client.Level`. `-runMultipleGameInstances` skips the
  single-instance check.

| Route | Spec | Native operations | Launch |
| --- | --- | --- | --- |
| **B, primary** | `route=menu` | settings through `SettingsManager::Set` 0x4f4bd0 (PC launcher, live-proven), then `LoadLevel` (fire `newSP`, in progress on the PC) | PC `Start-BC2TestRange.ps1`, inject at about 3 s |
| **A, not viable** | `route=override` | add-override 0x501ab0 before the groups register | needs a suspended or early inject; BC2 exits silently when injected about 100 ms after launch |
| **Manual, fallback** | `route=manual` | none required | operator loads the level; executor waits (`await_level`) |

**PC launcher (bc2/test/range, 84cb8d8, `tools/testrange/`).** An x86
injector plus `BC2TestRange.dll` reads `BC2TestRange.cfg` beside the DLL
("Key value" lines, `#` comments), applies each pair through
`SettingsManager::Set` (0x4f4bd0, thiscall on `[0x155cfdc]`), and reads it back
through the getter (0x4f4c40). String fields are rebuilt with the engine's
`eastl::string` constructor (0x4014c0), so `Client.Level` works there. Live: at
about 3 s, `LoadMenu=0`, `Client/Server.Level=Levels/SP_02` and
`IsAIEnabled=0` all apply, but `LoadMenu=false` still shows the main menu, so
`newSP` is required. The native `newSP` call from a main-thread per-frame hook
is still in progress on the PC. Window observation uses `PrintWindow` (no focus
or input).

This branch does not add an injector. The runner tool writes
`BC2TestRange.cfg` from the range (section 5.3) and the PC script launches.
Until `newSP` is proven, `menu` ranges reach the main menu with the settings
applied and a person starts the campaign; the in-probe executor's `menu` and
`override` routes stay refused at `Begin` (`unavailable`) and `sp02-xm8` remains
the zero-operation fallback.

## 5. Architecture

```text
run.py scenario ──range_for_scenario──▶ attempt dir: range.ini, BC2TestRange.cfg, range-run.json
        │                                        │
        │ (optional) bc2_test_range.py stage     ▼
        │   journaled intro-movie rename   PC Start-BC2TestRange.ps1 (settings at ~3 s, newSP)
        │                                        │
        │                                        ▼
        │                                  Start-NativeTrace.ps1 -InputTape …  (attach in level)
        ▼                                        │
 gun-events.jsonl ◀── range rows ◀── BC2NativeProbe: ParseRange → TestRange::Begin/Tick (later)
        │
        ▼
 judge(): bc2_test_range.analyze  /  replay_assert  range[result=ready]
```

### 5.1 Executor

`TestRange` is a per-frame state machine driven by `RangeObservation` snapshots
(front-end flag, level loaded and its name, cinematic active, player alive,
`MenuIdEnum` id, owner soldier, weapon list with loaded/reserve and capacities,
selected weapon, position). Steps, in order:

1. `write_settings` (menu route; fails `level_already_loaded` if a level is up)
2. `load_level` (waits for the front end, dispatches, then requires a loaded
   level whose name matches, else `wrong_level`); manual route: `await_level`
3. `prepare_loadout`: the kit `Weapons[]` rewrite, only before the soldier
   spawns (`player_already_spawned` skip otherwise), always paired with a
   restore hook
4. `await_player`: binds the owner; skips the intro cinematic once if admitted,
   otherwise waits it out
5. `clear_inventory`, `give_weapon` (one per tick), `select_weapon`, `set_ammo`
   (`unobservable` if capacities are unknown), `teleport` (within 0.5 m)
6. `ready`

Owner change or death after binding ends the run (`owner_lost`). Observations
older than 200 ms or from the future are ignored. Each dispatch waits at most
5 s for its effect (level load 120 s). At most one hook call per tick.

Every step emits a gun-event-log v1 row (`docs/GUN-EVENT-LOG.md` on
`bc2/tools/gun-event-log`) with `src="fvr"` and `phase="range"`:

```json
{"v":1,"tick":120,"qpc_us":99,"src":"fvr","phase":"range","serial":4,"step":"give_weapon","result":"ok","reason":"none","weapon":{"asset":"SPAS12_sp"}}
```

`result` is one of `dispatch ok wait skip fail ready finished`. The executor
keeps at most 256 rows.

### 5.2 Probe integration (not on this branch)

- `NativeProbeConfig` (1184 bytes, flags word full) needs a new path field for
  `range.ini`, next to the replay worker's `inputTape` field. Bump the config
  size once for both.
- MinHook is initialised once in `FvrRunNativeProbe` (NativeProbe.cpp:1740). Any
  range hook installs through that instance, never a second `MH_Initialize`.
- Route A (suspended launch, add-override before group registration) is **not
  viable**: injecting about 100 ms after launch makes the retail exe exit
  silently (DRM or startup checks). `RangeBootReceipt` and the override branch
  of `Begin` stay in the code only so an old `route=override` range is refused
  cleanly; no boot stage will be built.
- The PC DLL owns settings writes. `RangeSettingsWriter` (raw field writes with
  exact restore) is kept as the in-probe alternative for a later run that needs
  restore inside one process; it is not used by the launcher flow.

### 5.3 Runner tool

`tools/bc2_test_range.py` mirrors the parser and owns the file side:

```text
python tools/bc2_test_range.py check [--catalog weapon-configurations.json] RANGE...
python tools/bc2_test_range.py canonical RANGE
python tools/bc2_test_range.py settings RANGE [--json]
python tools/bc2_test_range.py prepare [--range RANGE] [--tape TAPE] [--catalog C] --out DIR
python tools/bc2_test_range.py stage --game BC2_DIR --run DIR
python tools/bc2_test_range.py unstage --run DIR
python tools/bc2_test_range.py analyze --range RANGE LOG...
```

- `RANGE` is a name in `tools/test-range/ranges/` or a path.
- `--catalog` checks every weapon against `resolved_weapons[].native_name` from
  `tools/bc2_weapon_config_pipeline.py` output (`sp_common/level-00.fbrb`, 127
  chains), a JSON list or a text list.
- `prepare` refuses a non-empty output directory and writes the canonical
  `range.ini`, `BC2TestRange.cfg` (the settings pairs as "Key value" lines for
  the PC launcher) and `range-run.json` (`fvr.bc2.test_range_run.v1`: route,
  level, settings, weapons, select, timeouts, SHA-256 of range and tape, the
  `Start-BC2TestRange.ps1` argument template with `-InjectDelayMs 3000`, and
  `level_route_proven: false`). The runner copies the cfg into the launcher's
  `-Bin` directory beside the DLL before launching.
- `stage` renames `movies/ea_logo_hd.res` and `movies/dolbydigital.res` to
  `*.fvr-disabled` and writes `stage-journal.json`; it refuses leftovers from an
  earlier stage. `unstage` reverses exactly and reports a file that reappeared
  instead of overwriting it.
- `analyze` reads range rows from mixed gun-event logs and writes
  `fvr.bc2.test_range_analysis.v1`. Pass requires a `ready` row, no failure
  before it, no failed restore, and the ready weapon equal to `select`.
  `headset_accepted` is always `false`.

Exit codes: 0 ok, 1 a failed `check` or a failed `analyze`, 2 invalid input or usage.

## 6. Mechanisms, ranked (provisional)

**Level load**

1. Route B: `SettingsManager::Set` 0x4f4bd0 for `Client.Level` and switches
   (PC launcher, live-proven at about 3 s), then `newSP` (cmd 132 → 0x7600f0)
   from a main-thread hook (in progress).
2. Route A: add-override 0x501ab0 before group registration (queue 0x4f4a00).
   Not viable: needs an early inject, which makes BC2 exit.
3. Engine level-load call found through the `Levels/SP_01` xref or the
   "Recreating levels debug menu" code: an interface slot only, if newSP proves
   awkward.
4. Manual load (`sp02-xm8`): works now.
5. Command line / cfg: dead (0x4f4cf0 exact, non-dotted lookup).

**Weapons**

1. Kit `Weapons[]` rewrite before spawn: `SoldierSpawnEntityData.SoldierKit` →
   kit blueprint `Object@0x50` → `SoldierEntityData.Weapons@0x1b0` (and
   `WeaponUnlocks@0x1a0`), restored after. Only weapon assets already loaded in
   the level qualify.
2. Pickup re-point: `WeaponPickupEntityData.Weapons@0xa4`, e.g. the "SPAS12 and
   Grenade" pickup. The SPAS-12 exists in SP only as that pickup, and
   FrostbiteVR's ground pickup is hard-wired off, so this needs its own proof.
3. Settings `Client.Kit` and `UnrestrictedUnlocks`: auxiliary only.
4. Fallback: use the level's own kit. SP_02+ starts with `XM8_sp_s`, XM320,
   laser designator, knife and grenade.

Switching uses input actions 7/33/36 (Bc2BodyInventory.cpp:105-119) until the
selected weapon matches; no native select call is needed for kit weapons.

**Skipping waits**

1. Registry writes: `Client.DisableMovies`, `Server.DisableCutscenes`,
   `Client.IsFastStartupEnabled`, `Client.LoadMenu`.
2. Intro-movie rename (`stage`), reversible.
3. Cutscene skip hook (`CutsceneEntityData.IsSkippable@0xcd` or a skip call):
   interface slot `skipCinematic`.

## 7. Observation sources

- Player and inventory: the chain in `tools/capture_reload_state.py:171-211`
  (context 0x15713d8 → +8 → +0xb4 player → +0xc54 weak soldier; inventory
  +0x24c/+0x248 chosen by soldier+0x114 bit 0; selected inv+0x14c; weapons
  soldier+0x260/+0x264, data at wrapper+4). Server side soldier+0x2b4,
  +0x2b8/+0x2bc (Bc2ReloadServer.cpp:42-73).
- Level ready: the rig epoch from `Bc2StereoRecovery.h` changes on level load.
- Front end: `MenuIdEnum` (`MiePlaying=4`), mapped in `Bc2TestRangeFields.h`.
- Front-end automation fallback: `game_window.py --activate --key`.

## 8. Replay harness integration

`tools/replay/run.py` stays the only runner. Points to add when the branches
merge:

1. Scenario JSON gains an optional `"range": "<name>"`; a tape may say
   `#! range=<name>` and `#! range.<key>=<value>` (unknown `#!` keys are already
   provenance, so old tapes stay valid). The two must agree.
2. Per attempt, call
   `bc2_test_range.range_for_scenario(scenario, tape, attempt_dir, catalog)`. It
   returns `None` when no range is named, otherwise writes `range.ini` and
   `range-run.json` and checks the scenario weapon is in the range.
3. Live mode copies `BC2TestRange.cfg` into the launcher `-Bin` directory and
   runs `Start-BC2TestRange.ps1` (with `-KeepRunning`) from `range-run.json`'s
   `launch` template, then attaches `Start-NativeTrace.ps1 -InputTape` once the
   level is up. `range.ini` goes to the probe only when the in-probe executor
   gets a config path. Optional `stage`/`unstage` brackets the launch.
4. `judge()` adds `bc2_test_range.analyze(range_rows(logs), range)` or a
   `replay_assert` rule matching `range[result=ready]`, and requires a pass
   before the tape's own expectations are judged.

## 9. Verification plan and PC checklist

Offline (this branch): `TestRangeSpec`, `Bc2TestRange`, `Bc2TestRangeSettings`
ctest suites and `tests/test_bc2_test_range.py`. They cover parity, about 50
rejections, the full menu-route hook order with one restore, unknown dispatch,
restore failure, timeouts, stale observations, wrong level, all-or-nothing
writes, rollback and exact restore.

On the PC, one item per capability record. Each needs a static proof, a live
readback receipt and, where it writes, a restore receipt:

1. **Settings singletons.** From the registration stubs Server 0x534600, Client
   0x534680, Game 0x534700 (Network 0x534780) find where each instance pointer is
   stored. Confirm each live object's TypeInfo is 0x01bf3b3c / 0x01bf3b74 /
   0x01bf3bc0.
2. **Registry.** Done on the PC for the setter 0x4f4bd0 and getter 0x4f4c40.
   Still to confirm: that `true`/`false` values (what this spec emits) convert
   like `0`/`1`, and that every key the spec emits resolves (log each `set`).
3. **String layout.** Solved on the PC for `Client.Level` (`eastl::string`,
   rebuilt via 0x4014c0). Confirm `SpawnPoint` and `SavePoint` the same way.
4. **newSP.** The UI command 132 → 0x7600f0 call path, its arguments and calling
   convention, and a safe thread to call it from at the main menu.
5. **Engine level load (slot).** The xref to `Levels/SP_01` and the "Recreating
   levels debug menu" code: the function that loads a level by name.
6. **Route A.** Closed: early injection exits the game.
7. **Kit chain.** `SoldierSpawnEntityData.SoldierKit@0x104` → `SoldierAsset`
   `Object@0x50` → `SoldierEntityData.Weapons@0x1b0`. Enumerate the loaded weapon
   assets by `SoldierWeaponData.WeaponAssetName@0x40` and confirm the rewrite is
   read at spawn and restorable after.
8. **Pickup.** Find the "SPAS12 and Grenade" `WeaponPickupEntityData` in SP_02
   and its `Weapons@0xa4` array.
9. **Cutscene skip.** The cutscene entity's skip path and whether
   `IsSkippable@0xcd` is honoured.
10. **Movies.** Confirm `movies/ea_logo_hd.res` and `movies/dolbydigital.res` are
    the intro files and the game starts cleanly without them.
11. **Catalog.** Run `tools/bc2_weapon_config_pipeline.py` on the PC and
    `bc2_test_range.py check --catalog` against every shipped range.
