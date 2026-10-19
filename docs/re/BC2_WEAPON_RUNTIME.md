> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 weapon runtime: reload path, live inventory and feel hooks

> Engine-mapping scope, build 795745 (x86), 2026-10-08. Offline pass: the
> typeinfo dump (`tools/typeinfo-dump`, `profiles/typeinfo/795745`) and what
> this repo already proves. Nothing here adds or enables a native operation.
> Addresses are preferred-image VAs (base 0x400000) on the pinned exe
> `3911fcc8...`; the patched PC exe `c603f686...` differs only in its signature
> blob. Re-derive every address by signature before use, as the repo's
> discovery code already does.

The physical reload needs three things from the engine: the per-weapon reload
data, a clean set of events for the feel layer (reload begin and end, pump or
bolt cycle, shot, ammo transfer), and, for the test harness, a way to put any
weapon into the soldier's hands without a respawn. This page records what is
known for each, how well it is proven, and what disassembly is still needed.

## Evidence and limits

| Label | Meaning |
| --- | --- |
| **SV** | Signature-verified: unique byte signature or whole-function fingerprint plus call-link checks in repo code, fails closed |
| **LO** | Live-observed in a native run; the reports are cited by the docs but are not in the repo |
| **HA** | Headset-accepted |
| **DUMP** | Field layout from the static typeinfo dump; no live values |
| **DIS** | Read in a static disassembly of the exe, with a unique byte signature; not yet in repo code or run in the game |
| **INF** | Inferred; nobody has observed it |

The dump has types only. All 158 `Server*` and about 100 `Client*` runtime
entity classes have sizes but no fields, so the weapon wrapper, firing object
and inventory layouts come from repo code and live observation, not the dump.
About 45 field offsets the repo reads live were cross-checked against the dump:
all agree.

## The engine has no chamber or slide lock

No chamber, slide-lock, bolt-catch, charging-handle, magazine-seated or
shell-eject field exists in any of the 1,991 reflected types (DUMP). Each
firing object holds exactly two counts: loaded (+0x7c) and reserve (+0x80).
Ammo transfer moves rounds between them and only shots remove rounds (SV, LO;
`docs/BC2-PUMP-RELOAD-20261001.md:76-80`, `Bc2ReloadState.h` `chamberKnown=false`).

Design consequence: chamber state and slide lock are VR-side models, which is
what `MagazineFeedState` on `feel/chamber-and-insert` already does. "Slide
locked back" is derived as loaded == 0 while the firing state is 2 (ready).
There is also no pump, bolt or charge input action: `EntryInputActionEnum`
has 50 values and only `EiaReload` (29) relates to reloading (DUMP). A
physical pump or bolt has to drive firing states 7 and 8, not an input.

## Ownership chain

| Link | Where | Evidence |
| --- | --- | --- |
| Context global | VA `0x015713d8`; `+8` `ClPlayerManager`; `+0xb4` local player | SV, HA |
| Soldier | weak handle at player+0xc54, soldier = `*weak - 4` (`ClientSoldierEntity`, 1600 B) | SV, HA |
| Client inventory | soldier+0x24c if `soldier+0x114` bit 0, else +0x248; `+4` `WeaponSwitchingData`; `+0x14c` selected slot | SV |
| Client weapon vector | soldier+0x260 begin / +0x264 end, 4-byte wrapper pointers, index = `WeaponSwitchingEnum` slot, entries may be null | SV, LO |
| Client wrapper | `+4` `SoldierWeaponData`; `+0x2c` aiming; `+0x38` `ClientWeaponFiringEffects`; `+0x3c` firing A (getter `0x7efce0`); `+0x40` firing B (getter `0x7efcf0`); `+0x44` unknown; `+0xd0` effects listener object | SV for +4/+0x3c/+0x40; rest LO or unknown |
| Server soldier | server context VA `0x1566b18`; manager `+0x6c[player+0x154]`; server player `+0xc3c` soldier (`ServerSoldierEntity`, 976 B) | SV, LO |
| Server inventory | soldier+0x2b4; items soldier+0x2b8 / +0x2bc; count getter `8B 81 BC 02 00 00 2B 81 B8 02 00 00 C1 F8 02 C3` | SV |
| Server item | `+4` `SoldierWeaponData`; `+0xc` `ServerWeaponFiringEffects`; `+0x10` firing object | SV, LO |

Each weapon therefore has **three firing-object copies**: client wrapper +0x3c,
client wrapper +0x40 and server item +0x10. The server copy is authoritative.
A client-only write is overwritten by native server restoration
(`docs/WEAPON-SYSTEM-PIPELINE.md:232-237`), so any ammo or state write goes
server first.

### Firing object (same layout in all three copies)

| Offset | Field | Evidence |
| --- | --- | --- |
| +0x00 | vtable | SV |
| +0x08 | `WeaponFiringData*` | SV |
| +0x0c | `AmmoConfigData*` (into `FiringFunctionData`+0x170) | SV |
| +0x3c / +0x40 / +0x44 | current / previous / next state (0 to 15) | SV, LO |
| +0x50 | phase timer (float) | LO |
| +0x74 / +0x78 | capacity / reserve multipliers (float; SPAS capacity 4 x 2 = 8) | LO |
| +0x7c | loaded rounds (i32) | SV, HA |
| +0x80 | reserve rounds (i32, -1 = infinite) | SV, HA |
| +0x98 | capacity override (-1 = none) | SV |
| +0xa8 | flags (0x8 non-finite ammo) | SV |

Effective capacity is `0x6cd540`: the override at +0x98 if set, else
`MagazineCapacity` x the +0x74 multiplier (SV).

## Firing state machine

States are not a reflected enum. `FireState`, `ReadinessState` and
`AnimatedFireEnum` in the dump are AI and animation filters, not this machine.

| State | Meaning | Governing data | Evidence |
| --- | --- | --- | --- |
| 0 | deploy (`0x6e630e`): timer = `DeployTime`, then 2; entered from Activate `0x6e6d20` | `DeployTime` | DIS |
| 1 | return / release wait, then 2; leaving 12 sends reload end (`0x6ddb60`) once | `PostReloadSequenceTime` | SV, LO, DIS |
| 2 | ready; Reload edge or empty auto-reload (`0x6e6535..0x6e6579`) goes to 10 | | SV, HA |
| 5 / 6 | shot prep / step; 6 goes to 7 only for `FireLogicType` 1 | | LO |
| 7 | bolt / pump hold gate; on release timer = `BoltActionDelay`, next 8 | `BoltAction.Hold*` | LO (SPAS) |
| 8 | pump / bolt cycle; notifies listener +0x30 on start and +0x34 on leave; next 1 | `BoltActionTime` | LO (SPAS) |
| 9 | automatic repeat (`FireLogicType` 2) | `RateOfFire` | LO (boat, XM8) |
| 10 | reload begin; `ReloadDelay`, then 11 | `ReloadDelay` | SV, LO |
| 11 | reload wait, `ReloadTime` x multiplier x `ReloadThreshold` | `ReloadTime`, `ReloadThreshold` | SV, LO |
| 12 | ammo transfer once, wait the remainder; single-round weapons loop back to 11 | `ReloadType` | SV, LO |
| 3 | pre-fire delay; Step is a no-op, Tick `0x6e6a97` runs it; fire start `0x6e1fa0` enters it when `PreFireDelay` > 0; trigger release goes back to 2 | `PreFireDelay` | DIS |
| 4 | zoom-before-fire delay; Step no-op, Tick `0x6e6abd`; probably unused in shipped data | `ZoomBeforeFireDelay` | DIS, INF |
| 13 | hold-and-release charge (`FireLogicType` 3) | | DIS |
| 14 | release: sends listener +0x08, timer = `SpawnDelay`, then 15 | `SpawnDelay` | DIS |
| 15 | hold-and-release fire: listener +0x54, one shot with power from `0x6d6dc0`, auto-reload if empty | | DIS |

Update `0x6e9000` runs the Tick handler `0x6e69e0` (table `0x6e6ce4`) while
timer +0x50 > 0, then Commit when next differs from current, else Step through
`0x6e6994` (DIS). Listener notifications fire only when `ctx+0x24` is set
(DIS; "not a replay" is INF). Details: [BC2_DISASM_FIRING_STATES.md](BC2_DISASM_FIRING_STATES.md).

Several zero-duration states can pass inside one Update, so polling misses
states. Use the hooks below, not polling.

Observed sequences (LO): XM8 reload 2, 10, 11, 12, 1, 2, filling 22 to 30 in
one transfer. SPAS reload 10 once, then 11 and 12 per shell, 0.72 s per shell.
SPAS shot 5/6, 7, 8, 1, 2.

## Per-weapon reload data (for the feel layer)

Reached as `SoldierWeaponData`+0x98, then `WeaponFiringData`+0x40, then
`FiringFunctionData` (FFD). All DUMP and checked live by `ReadConfig`
(`src/games/bc2/Bc2ReloadState.cpp:80-119`). The offline pipeline
`tools/bc2_weapon_config_pipeline.py` reads the same fields from the archives
without a running game.

| Purpose | Field | Offset |
| --- | --- | --- |
| Magazine capacity | `MagazineCapacity` | FFD+0x184 |
| Authored magazine count | `NumberOfMagazines` | FFD+0x180 |
| Reload kind (0 single round, 1 magazine) | `ReloadType` | FFD+0x20 |
| Fire / cycle kind (0 single, 1 bolt or pump, 2 auto, 3 hold-release, 4 detonated) | `FireLogicType` | FFD+0x24 |
| Reload cancel rule | `ReloadLogic` | FFD+0x2c |
| Reload timings | `PostReloadSequenceTime`, `ReloadThreshold`, `ReloadDelay`, `ReloadTime` | FFD+0x0c, +0x10, +0x14, +0x18 |
| Pump / bolt timings | `BoltActionTime`, `BoltActionDelay` | FFD+0x50, +0x54 |
| Pump / bolt hold flags | `HoldBoltActionUntilZoomRelease`, `...FireRelease` | FFD+0x58, +0x59 |
| Reload hold flags | `HoldReloadUntilFireRelease`, `...ZoomRelease` | FFD+0x96, +0x97 |
| Pellets per shell | `NumberOfBulletsPerShell` | FFD+0xd4 |
| Abort reload on sprint | `AbortReloadOnSprint` | `WeaponFiringData`+0x44 |
| Pump visual flag | `IsPumpAction` | `WeaponStateData`+0xc0 |
| Skip reload animation | `SkipReloadAnimation` | `WeaponStateData`+0xc5 |
| Out-of-ammo policy | `HideWhenOutOfAmmo`, `SwitchToPrimaryWhenOutOfAmmo`, `AllowSwitchingToWeaponOutOfAmmo` | `SoldierWeaponData`+0x121, +0x122, +0x124 |

Known values (LO): SPAS12_sp `FireLogicType` 1, single round, 0.72 s per shell,
`BoltActionDelay` 0.5 s, `BoltActionTime` 0, capacity 4 x 2. XM8_sp_s automatic,
magazine, 2.8 s, threshold 0.75, capacity 30. 40mmgl single, magazine, 2.45 s,
threshold 0.55, capacity 1. SPAS pump timing is therefore not data-driven: only
the 0.5 s delay is authored.

## Hook points for the feel layer

The five hooks the repo already installs (`Bc2ReloadFlowRuntime::Install`)
cover state changes, transfers and restores. The native listener callbacks
below give reload and pump events directly. Their ABIs are now read in the
disassembly (DIS), but none is hooked.

| Event | Native source | Evidence | Status |
| --- | --- | --- | --- |
| Firing state change | Update `0x6e9000`, Step `0x6e62d0`, Commit `0x6dd6a0` | SV, LO | hooked |
| Ammo transfer (mag seated, shell in) | Transfer `0x6d6f40`, five classified call sites | SV, LO | hooked; never called by the mod |
| Client rollback | Restore `0x6d71a0` (64-byte snapshot) | SV, LO | hooked |
| Reload begin / end | Commit 2→10 / 12→1; firing notifiers `0x6ddb30` / `0x6ddb60` (thiscall `(bool emit)`, `ret 4`; begin fires twice per magazine reload); effects forwarders `0x80f640` / `0x80f6b0` (thiscall, no args) | DIS | **not hooked**; Commit already sees both |
| Reload abort | Commit 10/11→1 (sprint `0x6d6d40`); Deactivate `0x6e1eb0` writes state 1 without Commit on switch; no end notification on either | DIS | diff state across Update |
| Pump / bolt cycle | Commit 6→7 owed, 7→8 start, 8→1 end; forwarder `0x8024a0` (start); the client listener's +0x34 stop slot is a bare `ret` | DIS | **not hooked**; use Commit |
| Reload progress | `0x6ddb90` (tests states 10 to 12, returns 0..100; HUD getter) | DIS | no hook needed |
| Shot | `0x6dd960` (thiscall `(float power, ctx*)`, `ret 8`; states 5/6, 9); ClientShoot `0x81c170`, ServerShoot `0x579dc0` | DIS; shoot hooks SV | shoot hooks only in muzzle mode |
| Last round / slide lock | loaded +0x7c going 1→0 across Update or the shot call; lock = loaded 0 in state 2; per-shot decrement site not found | DIS | VR-side model |
| Dry fire | Step 2 calls listener +0x1c at `0x6e64e7` | DIS | not hooked |
| Reload hold | zero `ctx+0x18` for one original Update | SV, HA | in use |
| Empty no auto-reload | one-byte `ctx+0x28` Step override | SV, HA | in use |
| Animation events | `AnimationNodeData.CallbackId` (+0x7c); `AnimationSoundMapData` has no typed consumer | DUMP, INF | unmapped |

Recommendation: derive reload begin, end, abort, seat and pump from the
existing Commit, Transfer and Update hooks first; that needs no new detour.
Where a direct event is still wanted, fingerprint and hook the listener
forwarders in the same style as `Bc2ReloadFlow.cpp`. They give `reload_begin`, `reload_end`,
`pump_start`, `pump_stop` and `shot` from the engine's own notifications,
with no polling, which is what `docs/GUN-EVENT-LOG.md` needs from a producer.
True sub-phase events such as magazine out, magazine in, bolt back and bolt
forward have no native counterpart except possibly animation callbacks; until
those are mapped, the VR side owns them.

## Giving a weapon at runtime

What exists:

1. **Stock ground pickup** (Interact 27) replaces the held gun in place: the
   AEK-971 took the SPAS wrapper's address (HA,
   `docs/HEADSET-20261002-231419.md:28-33`,
   `docs/HEADSET-20261003-182719.md:12-16`). This is the engine's own
   give-or-replace path, and the best lead for an instant give.
2. **Kit patch before spawn** (route C1 in
   [../BC2_TEST_LEVEL_ROUTES.md](../BC2_TEST_LEVEL_ROUTES.md)):
   `SoldierEntityData.Weapons`@0x1b0 and `WeaponUnlocks`@0x1a0. Being tested
   live by the planning thread's PC session.
3. **Selection** of owned weapons through input actions 7, 33 and 36 and the
   native selector `0x6ef590` (SV).

Cloud static disassembly (DIS, details in
[BC2_DISASM_GIVE_PATH.md](BC2_DISASM_GIVE_PATH.md)) now names the native path:

| Piece | Address and ABI | Notes |
| --- | --- | --- |
| Give / replace | `0x6bcf80`, thiscall on the **server** soldier: `(SoldierWeaponData* data, uint slot, bool selectNow, bool forceSlot)`, `ret 0x10`, returns the new item; sig `83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84 BE 00 00 00` | NULL data removes. No ammo argument: full native ammo. Destroys the old item. 18 callers, among them spawn and ground pickup |
| Ground pickup | `0x6c5cd0` `Pickup(soldier)`, from the interaction handler at `0x6632e7`; calls the give at `0x6c5f3e`, then reserve `0x6cd850` and loaded `0x6cd520` | old weapon goes back into the pickup through `0x6b7300` |
| Spawn | `0x6bd350` (from `0x6c0f43`) walks `SoldierEntityData.Weapons`+0x1b0, builds the inventory (`new 0x15c` + `0x6ec700`), and calls the give per weapon; unlocks in `0x6bd480` | same creator as the pickup |
| Server vector +0x2b8/+0x2bc | grown with nulls to slot+1, overwritten by index | never inserted |
| Server item | `new 0x24`, ctor `0x561b20`, vtable `0x14172f0`; +4 data, +8 soldier, +0xc effects, +0x10 firing, +0x20 u16 id | no reflected type |
| Client | ghost replication: give marks the ghost dirty at `0x6bd2e3`; client slot 80 `0x8bb0c0` → `0x8bad10` → replace `0x8baab0` (`ret 0x14`) → wrapper `new 0x64`, ctor `0x837ec0`, vtable `0x1432020` | no client write needed |
| Firing object | `0xac` bytes, ctor `0x6f1780` seeds no ammo; `0x6dd580` fills loaded = override or round(+0x74 x `MagazineCapacity`), reserve = (round(+0x78 x `NumberOfMagazines`) − 1) x capacity | setters `0x6cd520` (loaded), `0x6cd850` (reserve) |
| Selection | `0x6d7c80`, thiscall on the **inventory** (server soldier+0x2b4): `(int slot, bool validate)`, `ret 8`; writes inventory+0x14c, previous at +0x150, calls listener vf0(prev, new) to deploy | the old "+0x14c selected slot" is this inventory field |

Name lookup and the server soldier (DIS, details in
[BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md](BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md)):

- **Server soldier:** the server context is the static object `0x1566b18`
  (valid when bit 0 of `0x1566b80` is set). `ServerPlayerManager` =
  `*(ctx+0x2c)`, with players at +0x58..+0x5c.
  - `ServerPlayer` (vtable `0x14188d8`) holds its soldier at +0xc3c (getter
    `0x56d410`). setSoldier `0x5611c0` also writes soldier+0x220 = player.
  - The local player has +0xd84 == 0, a flag set only for AI (INF).
- **Weapon by name:** the SP checkpoint restore `0x555530` already does it.
  - It takes the resource manager `*(0x154c444)`.
  - It requests a typed resource with `0x449810(&ref, name, 0, 0, SoldierWeaponData TI 0x1bfca60, 0)`.
  - It then calls the give `0x6bcf80`.
  - The name is `SoldierWeaponData.WeaponAssetName` (+0x40).
  - `0x465d10(*(RM+0xa4), name)` tests whether the partition is resident
    without I/O. Check it first, because `0x449810` spin-waits on a load.

Still missing: the server writer of the replicated weapon-state block (INF
link from item +0x20 to wrapper +0x54), and the thread the give must run on.
None of the above has run in the game.

Design rule for the eventual write: call the engine's own give or replace
function rather than storing pointers into the vectors. A raw store would skip
the three firing copies, the ammo seeding and replication, and the server
would restore over a client-only change.

## Open work, ranked

Static status (2026-10-08): G1 to G9 and G11 are answered at DIS level
above. Each still needs a live check before any write ships. G5 and G10 are open.

| # | Gap | Why | Where to start |
| --- | --- | --- | --- |
| G1 | Native give / replace function used by ground pickup | Instant give-weapon | Write-watch the SPAS wrapper +4 and server soldier+0x2b8 entries while picking up the AEK; statically, the `ServerPickupEntity` vtable interact slot and readers of `WeaponPickupData`+0xc next to +0x10 |
| G2 | Writers of the inventory vectors; fixed size or insert | Overwrite a null slot or append | Xrefs to `[reg+2B8h]`/`[reg+2BCh]` beside the known count getter; `[reg+260h]`/`[reg+264h]` in `ClientSoldierEntity` methods |
| G3 | Wrapper constructors, class names, vtables, size | Build and validate entries; settle +0x38 vs +0xd0 | Live wrapper vtable, its TypeInfo getter, `mov [reg], vtable` xrefs |
| G4 | Spawn path: callers of inventory constructor `0x6ec700`; consumer of `SoldierEntityData.Weapons` | Template for G1 and for C1 | Loops over `[SoldierEntityData+1B0h]` calling a creator per element |
| G5 | Name to loaded `SoldierWeaponAsset*` resolver | Give a weapon not in the kit | DIS: checkpoint restore `0x555530` pattern; confirm live, including case sensitivity |
| G6 | Firing-object constructor and ammo init | Correct ammo on a fresh weapon | Firing vtable xrefs; reads of `AmmoConfigData`+0x10/+0x14 near +0x74 |
| G7 | Selection setter `0x6d7c80` ABI and deploy chain | Select the new weapon with its deploy animation | Disassemble it and its callers from `0x6f1980` |
| G8 | Listener forwarders `0x8024a0`, `0x80f640`, `0x80f6b0`, `0x6ddb30`, `0x6ddb60`, `0x6dd960` | Native reload and pump events | ABI, args, per-copy behavior; add fingerprints |
| G9 | Firing states 0, 3, 4, 14, 15 | Complete the state table | Dispatch table at `0x6e6994` |
| G10 | Animation callbacks and `AnimationSoundMapData` consumer | Sub-phase events and reload audio | Readers of `AnimTree1p` (+0xb8); `CallbackId` |

| G11 | Local player's server soldier pointer | Call the give on the right object | DIS: `*(*(0x1566b18+0x2c) players)+0xc3c`; confirm live, plus the server-tick thread (break at `0x6c5cd0`) |
| G12 | Per-shot ammo decrement site | Exact last-round event | Live write-watch on firing +0x7c during one shot |
| G13 | Server writer of the replicated weapon-state block | Prove the item +0x20 to wrapper +0x54 link | Writers of the 0x40-byte block read by `0x8bad10` |

Live confirmation of the DIS rows is in
[BC2_GIVE_WEAPON_PC_ASKS.md](BC2_GIVE_WEAPON_PC_ASKS.md).

## Related engine-mapping reports

| Report | Covers | Evidence |
| --- | --- | --- |
| [BC2_DISASM_GIVE_PATH.md](BC2_DISASM_GIVE_PATH.md) | give/replace `0x6bcf80`, vectors, ctors, spawn, ammo, replication | DIS |
| [BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md](BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md) | local server soldier, weapon by name | DIS |
| [BC2_DISASM_FIRING_STATES.md](BC2_DISASM_FIRING_STATES.md) | all 16 firing states, feel observation points | DIS |
| [BC2_DISASM_PUMP_AND_DRY_FIRE.md](BC2_DISASM_PUMP_AND_DRY_FIRE.md) | VR pump hold in Step (state 7), dry-fire detection | DIS |
| [BC2_DISASM_WEAPON_SOUNDS.md](BC2_DISASM_WEAPON_SOUNDS.md) | empty click `0x6ebc30`, PlayAt `0x637d50`, AnimationSoundMap | DIS |
| [BC2_DISASM_ENTITY_SPAWN.md](BC2_DISASM_ENTITY_SPAWN.md) | runtime entity factory; dropped magazine stays a mod prop | DIS |
| [BC2_DISASM_SP_START_PATH.md](BC2_DISASM_SP_START_PATH.md) | newSP fast start | DIS, step 1 LO |
| [BC2_DISASM_CUTSCENE_SKIP.md](BC2_DISASM_CUTSCENE_SKIP.md) | skipCutscene `0x73fcd0`, requestSkip `0x886210` | DIS |
| [BC2_DISASM_CHECKPOINT_AND_AI.md](BC2_DISASM_CHECKPOINT_AND_AI.md) | save/Continue start, difficulty int, AI-off crash | DIS, INF |
| [BC2_FAST_START_NEWSP.md](BC2_FAST_START_NEWSP.md) | PC live fast start | LO |
| [BC2_PC_GIVE_CROSSCHECK.md](BC2_PC_GIVE_CROSSCHECK.md) | PC Ghidra cross-check of the give path | PC static |
| [BC2_SP02_LEVEL_DATA.md](BC2_SP02_LEVEL_DATA.md) | SP_02 level flow, CS_11 (IsSkippable=1) | PC data |

## What this does not prove

This page records layouts, addresses and observed behavior; it does not by
itself validate a new native operation. Follow the current [AGENTS.md](../../AGENTS.md)
for signature/ABI verification, player-object ownership and validation scope.
Owned gameplay state may persist. Enable working features with a kill switch;
ordinary development does not require both architectures, full tests or headset
approval. Keep scoped render cleanup and the actual limits of each native claim.
