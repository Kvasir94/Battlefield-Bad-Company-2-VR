> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 test level routes: empty level fast, any weapon in hand

> Research scope, build 795745 (x86, single-player, DX11), 2026-10-08. Nothing
> here is enabled in the mod. Every route is **unverified in game** unless its
> row says otherwise. Offsets are byte offsets inside the object, from the
> static typeinfo dump on `tools/typeinfo-dump`
> (`profiles/typeinfo/795745/bc2_types.json`). Full field tables:
> [re/BC2_TYPEINFO_HARNESS_TYPES.md](re/BC2_TYPEINFO_HARNESS_TYPES.md).
> Community sources with URLs:
> [re/BC2_TEST_LEVEL_COMMUNITY_SOURCES.md](re/BC2_TEST_LEVEL_COMMUNITY_SOURCES.md).

The replay/test harness needs three things from the engine, in this order:

1. **Level:** boot straight into a small, quiet level with no campaign flow.
2. **Gate skip:** no intro movies, menus, cutscene or "press a button".
3. **Weapon:** the player holds the weapon under test, chosen by name, for any
   weapon in the game.

It must never depend on playing to a point, a specific mission start, or
waiting through cinematics. This doc collects the routes for each, ranked, with
the evidence behind them and the exact strings, classes and offsets to confirm
on the PC. The harness itself (scenario spec, `Bc2TestRange` interface) is
owned by the planning thread on `tools/test-range` (`docs/BC2_TEST_RANGE.md`).
This doc is its research input and defines no harness code.


> **Update (2026-10-08, static disassembly):** the native single-player start
> chain from `newSP` (`0x7600f0`) to the server level setter, the settings
> API, the `Client.LoadMenu` menu skip and the intro-movie flow are in
> [re/BC2_DISASM_SP_START_PATH.md](re/BC2_DISASM_SP_START_PATH.md), with
> unique signatures. Not yet run in game.

## Recommended path

1. **Find the settings source** (Route A0). Every fast-start lever is a field
   on `ServerSettings`, `ClientSettings` or `GameSettings`. Test the
   `Win32Game.cfg` mechanism first; if nothing reaches the settings registry,
   write the settings objects in memory before the first level load.
2. **Pick the level from data, not memory.** List `Dist/win32/levels/*`,
   decode each `LevelData` for `HasAI`, start gates and size, and pick the
   smallest SP level. With AI off and the gates cleared, that is the range.
3. **Give weapons by kit patch before spawn** (Route C1), then select with the
   existing native switch route. Use the pickup patch (C2) as the fallback.
4. **Judge from logs**, the KF2 way: one process per scenario, startup and
   session budgets, per-weapon cases that fail independently, and
   `headset_accepted=false` on every result.

## Evidence and limits

| Source | What it gives | What it cannot give |
| --- | --- | --- |
| Typeinfo dump, 1,991 types (static parse of `BFBC2Game.exe` `c603f686...`) | Class layouts, field offsets and enum values for settings, level, spawn, kit, weapon and pickup data | Instances, values, names on the command line, members of runtime entity classes (all `Server*`/`Client*` entity classes have 0 fields) |
| PC session reads (planning thread, 2026-10-08) | Setting-name strings in the exe, WinMain argument parsing, level start kits, one failed launch | Not yet: what feeds the settings registry |
| This repo (`main` @ `98c7cf8`) | Attach-to-running-game tooling, read-only player/weapon readers, native weapon switch, FBRB and DBX readers | Any launch, level, menu-skip or give-weapon code (none exists) |
| Community (server trees, emulators, mod tools, wikis) | Server `Win32Game.cfg` mechanism, real level bundle names, intro movie files, data modding | Client argument syntax, client setting names (no public list) |
| KF2 VR harness ([Killing Floor 2 VR](https://github.com/Kvasir94/Killing-Floor-2-VR)) | Proven harness shape: launch, force weapons, log, judge, recover | Nothing Frostbite-specific |

Labels: **fact (dump)** means the field exists at that offset; **fact (PC)**
means observed on the development PC; **community** means a third-party source says
so; **inference** means expected behavior that nobody has observed yet.

## Findings from the PC so far (planning thread, 2026-10-08)

- `BFBC2Game.exe` contains the setting names `Client.Level`, `Server.Level`,
  `Client.LoadMenu`, `Client.LimitedMainMenu`, `Server.IsAIEnabled`,
  `Server.UnrestrictedUnlocks`, `Server.DisableCheckpoints`, `IsGodMode`,
  `Game.ForceDeltaTime`, `Client.UseScriptedInput` and the level string
  `Levels/SP_01`. **fact (PC)**
- `-Server.Level Levels/SP_02 -Client.Level Levels/SP_02 -Client.LoadMenu
  false` on the command line was **ignored**: the game reached the main menu
  after about 8 s. **fact (PC)**
- WinMain (`0x401f00`) parses only `-runMultipleGameInstances`, `-dedicated`
  and `-server`. Dotted settings are read at runtime through a registry lookup
  (`0x4f4600`, registry pointer at `0x155cfdc`). **fact (PC)**
- `SP_01` starts with knives only. `SP_02` onward start with `XM8_sp_s`,
  XM320, laser designator, knife and grenade. No mission starts with the
  SPAS-12; it comes only from the "SPAS12 and Grenade" pickup. **fact (PC)**

## Route A0: how settings reach the engine

| Rank | Lever | Evidence | Status |
| --- | --- | --- | --- |
| A0.1 | **`Win32Game.cfg`**: a text file of launch arguments that the engine appends to the command line. The R34 server's `bin/Win32Game.cfg` holds `-server -dedicated -feslhost ... -theaterhost ...`, and its RuntimeLog prints the merged line from `Engine/Game/Main/Main.cpp(635)`: `.bat` args first, cfg contents after. BF4 keeps the same file in `Scripts/` with lines like `-Core.DisplayAsserts false` | community (server tree, emulator readme) | Untested on the client |
| A0.2 | Dotted `-Group.Field value` arguments. A BC2 server panel passes `-Server.ThreadingEnable true -Core.JobProcessorCount 2`. Bare camelCase server args are confirmed (`-serverInstancePath`, `-port`, `-runtimeLog`, `-displayAsserts`, `-mapPack2Enabled`) | community | Client test failed with this syntax (PC); value syntax (`true` vs `1`) and level case not yet varied |
| A0.3 | **Direct write** of the settings objects before the first level load, restored on exit | fact (dump) layouts; see below | Needs the singleton addresses |
| A0.4 | Debug menu. The Final server logs `DebugMenu.cpp: Recreating levels debug menu`, so level-switch debug code ships in Final builds | community | xref that string in the client |

What the client test should try, in order: search the exe for `Game.cfg`,
`Win32Game`, `Commandline` and `runtimeLog`; launch with `-runtimeLog 1` and
look for a log that echoes the parsed arguments; drop a `Win32Game.cfg` next
to the exe and in `Scripts\`. `settings.ini` holds only `[Graphics]`,
`[WindowSettings]` and `[Sound]` (community, and
`docs/DESKTOP-WINDOW-OWNERSHIP-20261001.md`), so engine settings do not go
there. No developer console shipped (DICE: "No Console").

### The settings objects

| Class | Size | TypeInfo VA | Registration arg3 stub |
| --- | --- | --- | --- |
| `ServerSettings` | 0xcc | `0x01bf3b3c` | `0x534600` |
| `ClientSettings` | 0xfc | `0x01bf3b74` | `0x534680` |
| `GameSettings` | 0xa4 | `0x01bf3bc0` | `0x534700` |
| `NetworkSettings` | 0xa4 | `0x01bf3bf8` | `0x534780` |
| `DemoSettings` | 0x28 | `0x01bf3c30` | `0x534800` |

The arg3 stubs run 0x80 apart (then `PhysicsSettings 0x534880`,
`MonitoringSettings 0x534900`, render settings `0x534fb0..0x535560`).
**Inference:** they construct or register the singletons. Disassembling them
and the xrefs to the TypeInfo VAs should give the instance addresses and the
name mapping that `0x4f4600` uses. `String` fields are 12 bytes with an
undecoded layout; reverse it before writing `Level` or `SpawnPoint`.

## Route A: level load

| Rank | Lever | Evidence | Status |
| --- | --- | --- | --- |
| A1 | `ServerSettings.Level` String@0x7c and `ClientSettings.Level` String@0x5c, both set to the same path | fact (dump); names in exe fact (PC) | Command-line form ignored |
| A2 | Fast start: `ServerSettings.IsFastStartupEnabled`@0x9f, `ClientSettings.IsFastStartupEnabled`@0xf0, `GameSettings.FastStartupEnable`@0x98 | fact (dump) | inference |
| A3 | Empty the level: `ServerSettings.IsAiEnabled`@0xc5 false; per level `LevelData.HasAI`@0x3ae | fact (dump) | inference |
| A4 | Named save point: `ServerSettings.SavePoint` String@0x70 with `LoadSavePoint`@0xb8 | fact (dump) | inference |
| A5 | Level-graph jump: `MissionEntityData.NextLevelToRun` String@0x8c, `TransferWeaponsToNextLevel`@0xb1 | fact (dump) | inference |
| A6 | Entitlement and online gates: `NetworkSettings.SkipEntitlementCheck`@0x9c, `ClientSettings.CheckDeviceEntitlements`@0xbe | fact (dump) | inference |
| A7 | Trimmed bundle set: `GameSettings.UseBundles`@0x94, `BundleMountPoints`@0x20 (array of `{VfsPath S@0, Bundle S@0xc, IsResourcePath b@0x18}`) | fact (dump) | inference, last resort |
| A8 | Local R34 server plus the MASE emulator on a small SQDM map, with `instant_spawn`, `unlimited_ammo`, `all_stats_unlocked` | community | Last resort: MP map only, menu-driven, no bots, legal kits only, needs an emulator DLL in the game folder |

Mode type: `GameModeTypes` is `SinglePlayer=0, Multiplayer_Day1=1,
Multiplayer_Day2=2`; `ClientSettings.IsMultiplayer`@0xd5. Layers are chosen by
string (`GameModeData.PrimaryLogicLayerName`@0x84,
`SecondaryLogicLayerName`@0x54, `WorldData.Modes`@0x6c).

### Shipped levels and the level pick

The vanilla `package.mft` lists these SP level bundles (community, read from
the Mod Loader's original manifest): `sp_01`, `sp_02`, `sp_03`, `sp_04`,
`sp_003_b`, `sp_004_b`, `sp_005`, `sp_005_b`, `sp_006`, `sp_007`, `sp_008`,
`sp_008_b`, `sp_009`, and the shared `sp_common`. Note the mixed naming: the
first four use two digits and the rest three, so a guessed `Levels/SP_05` would
be wrong. Each level has `level-00`, `loader-00`, `ingame-00`, `terrain-00`
and voice archives under `Dist/win32/levels/<name>/`. MP paths are
`Levels/MP_001` to `MP_012*`, `MP_SP_002*`, `MP_SP_005*`, `BC1_*` and
`nam_mp_*`; the full list is in the sources doc.

**No test, debug or empty level shipped.** The range has to be a shipped
level with AI off and the start gates cleared. The pick should come from data:

1. List `Dist/win32/levels/*/level-00.fbrb` with sizes on the PC.
2. Read each with the repo's FBRB reader (`tools/inspect_bc2_mesh_asset.py`
   `Archive`, `:51-114`) and decode its `LevelData` with the typed DBX decoder
   in `tools/bc2_weapon_config_pipeline.py`.
3. Rank by archive size and entity count, and record `HasAI`@0x3ae,
   `LevelDescription.LoadingMovie` (LevelData+0x184), and
   `WaitForButtonBeforeStarting` (LevelData+0x1ec).

`SP_02` is the safe default until then, because it is already known to start
with `XM8_sp_s` and the XM320 in hand.

## Route B: intro, menu and cutscene skip

| Rank | Lever | Evidence | Status |
| --- | --- | --- | --- |
| B1 | `ClientSettings.DisableMovies`@0xf1, `DisableHudMovies`@0xf2 | fact (dump) | inference |
| B2 | Rename `Output\win32\movies\ea_logo_hd.res` and `dolbydigital.res` (restore after the run) | community (PCGamingWiki) | Saves seconds only |
| B3 | `ClientSettings.LoadMenu`@0xfa false, `DisableMenuFlow`@0xf9, `LimitedMainMenu`@0xba, `BypassMatchmaking`@0xea; keep `PauseGameOnStartUp`@0xd2 false | fact (dump) | `-Client.LoadMenu false` on the command line ignored (PC) |
| B4 | `ServerSettings.DisableCutscenes`@0xb5, `DisableCheckpoints`@0x8e | fact (dump) | inference |
| B5 | Level start gates in `LevelData`: `WaitForButtonBeforeStarting`@0x1ec, `StartDelayTime`@0xa8, `StartLevelFadedToBlack`@0x3b0, `StartLevelWithSoundFaded`@0x3af | fact (dump) | inference |
| B6 | Per cutscene: `CutsceneEntityData.IsSkippable`@0xcd; `MenuConfigAsset.IntroMovies`@0x54, `IntroMovie`@0x15c | fact (dump) | inference |

Ready detection: `MenuIdEnum.MiePlaying=4` means in gameplay. `MieMovie=14`,
`MieLetterBox=13`, `MieSpawnMenu=2` and `MieCreateSoldier=18` are states to
wait out or fail on. The repo already detects a new level or checkpoint
through new GameRenderer, world and request pointers plus a new local actor
generation (`src/games/bc2/Bc2StereoRecovery.h`; `rigEpoch` in
`Bc2Gameplay.cpp`). That is a detector, not a trigger.

## Route C: give any weapon by name

| Rank | Lever | Evidence | Status |
| --- | --- | --- | --- |
| C1 | **Kit patch before spawn.** `SoldierEntityData` (0x230): `Weapons`@0x1b0 is an array of `SoldierWeaponAsset*`; `WeaponUnlocks`@0x1a0 is an array of `{WeaponSlot u32@0, Weapon*@4, IsBundledWithSelectable@8, IsSelectable@9}`; `WeaponSwitching`@0x180. Find the target by name, swap it into the slot, restore on every outcome | fact (dump) | inference; needs live enumeration of loaded weapon assets |
| C2 | **Pickup patch.** `WeaponPickupEntityData.Weapons`@0xa4 is an array of `WeaponPickupData{LinkedToWeaponSlot@0, MinAmmo@4, MaxAmmo@8, Weapon*@0xc, WeaponSlot@0x10, AltWeaponSlot@0x14}`. Re-point the existing "SPAS12 and Grenade" pickup, or use `KitPickupEntityData.Kit`@0xa0 (`SoldierAsset*`), `KeepAmmoState`@0xa4, `KeepAdditionalWeapons`@0xa5 | fact (dump); pickup exists (PC); stock pickup of a placed AEK-971 via Interact(27) worked in the headset (`docs/HEADSET-20261002-231419.md:5`) | inference; FrostbiteVR's own pickup interface is off |
| C3 | Settings: `ServerSettings.UnrestrictedUnlocks`@0xbe, `ClientSettings.UnrestrictedUnlocks`@0xc8, `ClientSettings.Kit` u32@0x18, `Team` u32@0x1c, `SpawnPoint` String@0x50, `ServerSettings.InstantSpawn`@0x93, `GameSettings.UseCustomKit`@0x91 | fact (dump); names in exe fact (PC) | inference: `Kit` probably indexes `TeamData.Kits`@0x20 |
| C4 | Data mod through `package.mft` `BundleSource`/`BundleTarget` (MOD.SP adds MP weapons to the campaign this way) | community | Keep out of this repo (no game assets in source) |
| C5 | In-level console: `ConsoleCommandEntityData.Commands`@0x80 (TypeInfo `0x01c05efc`) | fact (dump) | Only if the level graph can be extended |

There are no `Cheat*`, `Console`, `Inventory`, `Loadout` or `Customization`
types in this build, no RCON give-weapon command, and no trainer that gives an
arbitrary weapon (community). A "give weapon" command is not expected to
exist; C1 or C2 has to be built.

### Weapon names and where they load from

- `sp_common/level-00.fbrb` holds 177 weapon DBX that resolve to 127
  `SoldierWeaponData` chains (definitions and variants, not 127 guns)
  (`release/WEAPON-PIPELINE.md:67-70`). `tools/bc2_weapon_config_pipeline.py`
  decodes them. This is the harness's offline weapon catalog.
- The live name is `SoldierWeaponData.Name` at +0xc (for example `SPAS12_sp`)
  and the asset path is `WeaponAssetName` at +0x40. `_sp` names with
  first-person descriptors are listed in
  `src/games/bc2/Bc2VisibilityDescriptorData.h:98-143`.
- Weapon packages are `Dist/win32/async/weapon/*.fbrb` (323 archives).
  **Inference:** a weapon outside the loaded level's bundles also needs its
  async package loaded, which MOD.SP's approach suggests. Start with weapons
  present in `sp_common`.

Slots (`WeaponSwitchingEnum`): `wsPrimary1=0, wsPrimary2=1,
wsGrenadeLauncher1=2, wsGrenadeLauncher2=3, wsStaticGadget=4,
wsDynamicGadget1=5, wsDynamicGadget2=6, wsMeleeWeapon=7, wsHandGrenade=8`.
Class (`WeaponClassEnum`, at `SoldierWeaponData`+0x84): `wcAssault=0,
wcShotgun=1, wcSmg=2, wcLmg=3, wcSniper=4, wcUgl=5, wcHgr=6, wcAt=7, ...
wcKnife=11, wcLaserDesignator=12`.

For long runs: `ServerSettings.HasUnlimitedAmmo`@0xc0, `HasUnlimitedMags`@0xc9,
`IsGodMode`@0x95. Reload tests must leave `HasUnlimitedMags` off, because it
hides the reserve accounting that a reload test checks.

## What this repo already has

The repo never launches BC2, picks a level, skips menus or gives a weapon.
Every script requires an already running `BFBC2Game.exe` in campaign gameplay
loaded by hand (for example `Start-BC2VRTest.ps1:12-15`). These pieces are
reusable:

| Piece | Where | Use in the harness |
| --- | --- | --- |
| Attach and inject | `src/platform/windows/NativeTraceMain.cpp`, `Start-NativeTrace.ps1` | Load the probe into the game the harness started |
| Simulated player (fake XR host, scripted 288-byte `InputPacket`) | `src/platform/windows/NativeIpcProbe.cpp`, `include/fvr/ipc/InputProtocol.h` | Drive grabs, reloads and fire without a headset |
| Weapon select among owned weapons, through native input actions 7, 33 and 36 | `src/games/bc2/Bc2BodyInventory.cpp:105-119`, `Bc2InputBinding.h`, `docs/LAUNCHER-BINDINGS-20261001.md:64-119` | After C1 puts the weapon in the kit, select it the native way |
| Read path player to weapon to ammo | `tools/capture_reload_state.py:171-228`, `tools/capture_reload_server.py:66-112` | Assert "holding X with N rounds" without writing |
| FBRB and DBX readers | `tools/inspect_bc2_mesh_asset.py:51-114`, `tools/bc2_weapon_config_pipeline.py` | Level pick, weapon catalog |
| Pause-menu binding | `src/games/bc2/Bc2MenuBinding.*`, `docs/BC2-MENU-BINDING-20261001.md` | Not a front-end route; in-game only |
| Window automation | `tools/game_window.py` (`--activate --key enter/escape`) | Only front-end automation today |
| Gun event log schema and analyzer | branch `tools/gun-event-log`, `docs/GUN-EVENT-LOG.md` | The judge; it has no emitter yet |

Read path, for reference (build 795745, re-derive by signature before use):
context global VA `0x15713d8`, then +8 `ClPlayerManager`, then +0xb4 local
player, then the weak soldier handle at player+0xc54 (soldier = `*weak - 4`).
Inventory is soldier+0x24c or +0x248 (by soldier+0x114 bit 0), selected index
at inventory+0x14c, weapon vector at soldier+0x260/+0x264, wrapper+4 is the
`SoldierWeaponData`. Server side: soldier+0x2b4 inventory, items at
+0x2b8/+0x2bc.

## Harness shape, KF2 VR mapped to BC2

| KF2 VR | BC2 equivalent |
| --- | --- |
| Launch `KF-BurningParis?Game=...?Mutator=KF2VR.VRBootstrap` with `-nostartupmovies -unattended`, warm start 42-56 s | Route A0 settings (level, `LoadMenu=false`, `DisableMovies`, `IsAiEnabled=false`), timed to `MiePlaying` |
| `VRDemo` mutator presses the stock Ready | Route B: clear start gates, no menu flow |
| `CreateInventory(class)` over an 18-entry allowlist; `give-one`/`give-all` over a local test channel | Route C1 kit patch from the `sp_common` catalog, then the native switch route |
| Iterate by switching until the target is `Active`; per-case cleanup; a failed case continues to the next | Same, with the selected index and `SoldierWeaponData.Name` as the "active" check |
| Judge by regex over `game.log`; `headset_accepted=False` | Judge `gun-events.*.jsonl` with `tools/analyze_gun_events.py`; same honesty fields |
| Mutex, refuse if the game is already running, startup and session timeouts, shutdown ladder, journalled DLL deploy | Same; the repo's scripts already refuse a second injector and need a fresh process per session |

## What to confirm on the PC, in order

1. Search the exe for `Game.cfg`, `Win32Game`, `Commandline`, `runtimeLog`
   and `Recreating levels debug menu`; try `-runtimeLog 1`; try a
   `Win32Game.cfg` beside the exe and in `Scripts\`.
2. Disassemble the settings arg3 stubs (`0x534600..0x535560`) and the xrefs to
   the TypeInfo VAs. Find the singleton instances and how `0x4f4600` resolves
   names. This decides between a file, the command line and a memory write.
3. Reverse the 12-byte `String` layout; read the live `Level` fields at the
   main menu.
4. Set `Level`, `LoadMenu=false`, `DisableMovies`, `DisableCutscenes` and
   `IsAiEnabled=false` before the first level load, and time the boot to
   `MiePlaying`.
5. List `Dist/win32/levels/*` and rank levels as above.
6. Enumerate loaded `SoldierWeaponAsset` objects reachable from the kit's
   `WeaponUnlocks` and from pickups, logging `Name`, slot and `WeaponClass`.
7. Swap one slot pointer (C1) on SP_02, confirm the soldier spawns holding
   it, then restore and confirm the original kit returns.

## What this does not prove

The dump is static. It proves the fields exist at these offsets in this build.
It does not prove that writing them changes behavior, that the engine reads
them after startup, or that the level graph honors them. The community
evidence is for the dedicated server, not the client. No route here is
enabled, and none is headset-validated.
