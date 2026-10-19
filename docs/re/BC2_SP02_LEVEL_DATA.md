> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# SP_02 level data: weapons and opening cutscene

> PC session, 2026-10-08. Offline read of the installed archives: no game run,
> nothing written to the install. Parsed with the repo's FBRB and DBX readers
> (`tools/inspect_bc2_mesh_asset.py`, `tools/bc2_weapon_config_pipeline.py`).
> Their node and size limits were raised in a scratch copy, because
> `Levels/SP_02.dbx` has more than 100k nodes.
>
> Sources:
> - `Package/levels/sp_02/level-00.fbrb`, the patch overlay. It is used where
>   it exists, and its `Levels/SP_02.dbx` matches the Dist one apart from one
>   instance.
> - `Dist/win32/levels/sp_02/{level,ingame,loader}-00.fbrb`.
> - `Dist/win32/levels/sp_common/level-00.fbrb`.
>
> Evidence level for everything here is **DATA** (authored level data). Whether
> a partition is resident at runtime is **INF** unless a native check confirms
> it.

## 1. Weapons available in SP_02

**The SPAS-12 is there.** `Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12`
(SoldierWeaponData `bf721205-c6fa-431d-b3cb-ac3783cc243e`) is defined in
`sp_common/level-00.fbrb`. SP_02 places `Objects/Weapons/Pickups/Pickup_Weapon_SPAS12`
twice ("SPAS12 and Grenade"), and the level description lists `cl_sp_spas12`
as a collectible. Because the level instantiates pickups that point at it, the
SPAS-12 data has to be resident in SP_02 (INF; confirm live with the resource
manager's loaded check before a give, as `0x555530` does).

The player's starting kit (`objects/soldiers/sp/marlowe.dbx`, the PRESTON
spawn) uses slots from `WeaponSwitchingEnum`:

| Slot | Weapon |
| --- | --- |
| 0 | `US_rgl_XM8/SP_rgl_XM8_Scoped` |
| 2 | `US_rgl_XM8/SP_rgl_XM320_Scoped` |
| 4 | `LaserDesignator/SP_LaserDesignator_Dummy` |
| 7 | `Knife/Knife` |
| 8 | `HandGrenade/HandGrenade` |

The level also fires `Pickup XM8.Pickup` once after the opening cutscene,
through the "pickup delay" splitter. That is a native, event-driven give of
`SP_rgl_XM8_Scoped` + `SP_rgl_XM320_Scoped` into slots 0 and 2.

**Weapon pickups placed in SP_02** (sp_common pickup templates referenced from
`Levels/SP_02.dbx`; slot 0 weapon unless noted):

| Pickup | Weapons |
| --- | --- |
| Pickup_Weapon_SPAS12 (x2) | SP_shg_SPAS12 |
| Pickup_Weapon_AEK971 (x2) | SP_rgl_AEK971 + SP_rgl_GP30 (slot 2) |
| Pickup_Weapon_F2000 (x2) | SP_rif_F2000 + SP_rgl_F2000 |
| Pickup_Weapon_M60 (x2) | SP_lmg_M60 |
| Pickup_Weapon_M95 (x2) | SP_sni_M95 |
| Pickup_Weapon_MG3 (x2) | SP_lmg_MG3 |
| Pickup_Weapon_MP443Grach (x2) | SP_hg_MP443Grach |
| Pickup_Weapon_NS2000 (x2) | SP_shg_TOZ194 |
| Pickup_Weapon_QBU88 (x2) | SP_sni_QBU88 |
| Pickup_Weapon_QJY88 (x2) | SP_lmg_QJY88 |
| Pickup_Weapon_UMP (x2) | SP_rif_FNSCARL |
| Pickup_Weapon_Uzi (x2) | SP_rif_AKS74u |
| Pikcup_Weapon_USAS12 (x2, sic) | SP_shg_USAS12 |
| Pickup_Weapon_AN94 | SP_rif_AN94_Kobra + SP_rgl_AN94Abakan_Kobra |
| Pickup_Weapon_M16 | SP_rgl_XM8 + SP_rgl_XM320 |
| Pickup XM8 (x2, level-local) | SP_rgl_XM8_Scoped + SP_rgl_XM320_Scoped |
| Gadgets | C4, AT mine, laser designator, M2 Carl Gustav, mortar strike |

Enemy drops add `SP_rif_AN94_Kobra`, `SP_rgl_AN94Abakan_Kobra`,
`SP_at_CarlGustaf`, `SP_shg_TOZ194` and `SP_lmg_QJY88` (pickups embedded in
the `sp_ru_*` soldier partitions). The level-local archive itself adds only
`RU_at_RPG7` (full asset).

`sp_common`'s `SP_Flynncrate_PickUp` lists 38 SP weapon variants. That is the
widest set of SP `SoldierWeaponData` names to try with the name lookup. SP_02
does not place the Flynn crate, so only the variants above are certainly
referenced by this level.

Names for the `0x555530` / `0x449810` lookup are the `SoldierWeaponAsset.Name`
values, e.g. `Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12`.

## 2. What drives the opening cutscene

The level's event graph is the `EventConnections` array on `LevelData`
(1,568 connections). The start of the level is a chain of entity events:

```
Human Player.OnPlayerEnter
  -> SoldierSpawn "PRESTON".Spawn                       (player soldier)
  -> EventSplitter "Run Once At Start".Impulse
       -> Save "Initial Save".Save
            .OnSaveDone / .OnLoadDone
               -> Cutscene "CS_11".StartCutscene        (the opening cutscene)
               -> DelayTrigger ead59689 -> Sequence e346b5cf.Start
               -> "Run Once At Start".Disable
CS_11.OnFinish
  -> WeatherState "Initial Weather State".StartTransition
  -> Save "save post start cutscene".Save
       .OnSaveDone / .OnLoadDone
          -> EventSplitter "splitter ab1 setup".Impulse
               -> InputRestriction "restriction start".Deactivate   (controls on)
               -> CharacterStateTrigger "chartrigger start".Disable
               -> "AB1 IN", "Start AB01 After Cutscene", music, unspawn cutscene actors
          -> EventSplitter "pickup delay".Impulse
               -> WeaponPickup "Pickup XM8".Pickup                  (gives the XM8)
          -> ScreenFade "Fade start of level".FadeFromBlack         (load path only)
```

`CS_11` (`6be61f12-7cb8-482f-b24c-c400edb6279e`) is `IsSkippable = 1`, uses
menu state `MieLetterBox`, camera `CS_11_camera`, and the `cs11` VO wave. The
other three letterbox cutscenes (`CS_12_movie`, `CS_14`, `CS_15`) are also
skippable. The 14 in-world cutscenes, such as `crushing`, `repelling01` and
`DeadSoldier0x`, are not skippable but don't take control.

### Ways past it, best first

1. **Skip CS_11 natively.** It is authored as skippable, so the engine's own
   skip (the cutscene-skip input while `MieLetterBox` is up) fires `OnFinish`.
   Everything after that runs exactly as authored: save, controls on, XM8
   given. This is the cleanest route. The exe side, meaning which function
   handles the skip, is what the research thread is mapping.
2. **Start from the "save post start cutscene" checkpoint.** Its `OnLoadDone`
   runs the same setup (controls, AB1, XM8 pickup, fade-in) without CS_11.
   This needs an existing save at that checkpoint, which a first run produces,
   and a Continue or load instead of newSP.
3. **Debug menu entities**, authored but probably unreachable in retail
   (`DebugMenuItemEntityData`). Each one warps the squad, activates
   "Give back controls" and saves "Debug Save":
   - `1 - First Action` (also impulses `AB2 IN`)
   - `2 - Chopper Insertion` (impulses `Start Courtyard`)
   - `3 - Tank Sequence`
   - `4 - After Tank Sequence`
   - `5 - The Rail Sequence`
   - helpers such as `Debug - Corner guy`

   Firing `OnActivated` on one of them from code would be a scripted jump to
   that section. Whether retail still has a debug-menu consumer is not known.
4. **Other checkpoints** (`SaveEntityData`, 12 in all): `Save Before Tank`,
   `Save After MG`, `AB 2 child 1/2`, `save AB2 xing!`, `save AB2 trucks`,
   `save AB2 1st street`, `Save when enter village`, `Debug Save`. Each
   `OnLoadDone` fades in from black. They are later start points if a save
   exists.

Warp targets (`AlternateSpawnEntityData`, 20) include `New Player Position`
(x2), and the "First Action", "Tank", "Post-Tank" and "Chopper Insertions"
warp positions for Preston, Redford, Haggard and Sweetwater.

### Implications for the test range

- A test range that wants the XM8 already gets it from `Pickup XM8` after
  CS_11. Skipping CS_11 natively is enough, and no give is needed for the XM8.
- For a SPAS-12 range in SP_02, the data is resident, so the native give
  `0x6bcf80` with the name lookup can put it in slot 0 after `OnFinish`. The
  placed pickups are the stock alternative.
- AI-off (`Server.IsAIEnabled 0`) does not stop CS_11. It is driven by the
  save and cutscene events above, not by AI.

## Not verified

- Runtime residency of each sp_common partition in SP_02 (use the
  resource-manager loaded check).
- Which exe code consumes `IsSkippable` and the skip input (research thread).
- Whether `DebugMenuItemEntityData` has a consumer in the retail build.
- Contents of sequence `e346b5cf` started under CS_11.
