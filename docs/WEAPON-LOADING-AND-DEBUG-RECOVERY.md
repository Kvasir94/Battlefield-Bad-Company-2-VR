> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Weapon Loading & Debug/Dev Recovery in Frostbite 1.5 (BFBC2)

**Date**: 2026-10-10  
**Target Binary**: `BFBC2Game.exe` (PE timestamp 1314210909, build 795745, base `0x00400000`)

---

## 1. Weapon Loading on Non-Typical Maps (M9 on Campaign SP_02)

### 1.1 The Problem
When attempting to give or spawn weapons such as the **M9** (`US_hg_M9Beretta` / `US_hg_M9-3`), **M1911**, **MP412 REX**, **M1 Garand**, or **Thompson** in campaign levels (e.g., `Levels/SP_02`), `BC2TestRange.cpp` scans memory for `SoldierWeaponData` and fails with:
```
give: slot 1 US_hg_M9-3: not loaded (no SoldierWeaponData with that name in this level)
```

### 1.2 Root Cause Analysis
- **Partition Placement**: In Frostbite, DBX partitions containing `SoldierWeaponData` are bundled inside level `.fbrb` files.
  - Campaign maps like `sp_02` mount only `levels/sp_02` and `levels/sp_common`.
  - The M9 (`Objects/Weapons/Handheld/US_hg_M9Beretta/US_hg_M9-3.dbx` and `US_hg_M9Beretta.dbx`) exists exclusively in:
    - `Dist\win32\levels\mp_common\level-00.fbrb`
    - `Package\levels\mp2_common\level-00.fbrb`
  - Because `mp_common` is never mounted on `sp_02`, the data instances do not exist in RAM.
- **First-Person Assets Are Already Present**:
  - The 1P mesh (`US_hg_M9Beretta_Mesh.res`), LODs, and full 1P animation clips (deploy, fire, reload, idle, zoom) reside in `Dist\win32\async\weapon\us_hg_m9-3-00.fbrb` and `us_hg_m9beretta-00.fbrb`.
  - Both are registered in `Dist\win32\bundleManifest`.
  - When a weapon is given via `0x006bcf80` (`giveOriginal`), the native equip handler at `0x0080f19c` / `0x0080f1e4` parses the weapon asset name with `0x00513120`, extracts the name (`us_hg_m9-3`), and dynamically calls `IResourceManager::LoadBundle("async/weapon/us_hg_m9-3", 1)`.
  - Therefore, once the `SoldierWeaponData` DBX is loaded, 1P visuals and animations stream automatically.

### 1.3 How bundle redirects are resolved
Frostbite resolves level bundle dependencies through `Dist\win32\package.mft`,
parsed once at boot:
- The backend object lives in `main`'s frame (ctor `0x00bd2bd0`, vtable `0x01479698`)
  and is published at `[0x0155d000]` (setter `0x004e26c0`, getter `0x004e26d0`).
  Its `+0x324` byte is set to 1 when the file has been parsed (`0x00bd3575`).
- `0x00bd3380` reads the file; `0x00bd3040` (`ParseLine`, thiscall on a parser state,
  `(line, end, backend, mode)`, `ret 0x10`) handles one line:
  - `Signature <hex>`: parsed by `0x00bd2a40`. No digest is checked, so the
    file could be edited, but the mod does not need to.
  - `BundleSource <src>`: expands `game:` to `/data/dist/win32/` and `package:`
    to `/package/` (`0x00bd28c0`) and keeps it at parser `+0x68`.
  - `BundleTarget <dst>`: in mode 2, `AddTarget` (`0x00bd2fb0`, thiscall,
    `(src, dst)`, `ret 8`) appends `dst` to the source's node.
- Nodes sit in 32 hash buckets at backend `+0x32c` (hash `0x00bd27b0`: djb2 xor
  over lower-cased bytes, `& 31`), chained at node `+0x48`; `+4` count (max 16),
  `+8` interned targets. `GetTarget` (`0x00bd2d30`, vtable slot 4) hands targets
  out last-first, so an appended target is mounted first and has the lowest
  priority -- the slot the file already gives `*_common` bundles.

Vanilla entry for `sp_02`:
```text
BundleSource game:levels/sp_02/level-00.fbrb
BundleTarget package:levels/sp_02/level-00.fbrb
BundleTarget game:levels/sp_02/level-00.fbrb
BundleTarget game:levels/sp_common/level-00.fbrb
```

Multiplayer levels list the common bundles as patch first, then base
(e.g. `mp_sp_002gr`):
```text
BundleTarget package:levels/mp2_common/level-00.fbrb
BundleTarget game:levels/mp_common/level-00.fbrb
```

### 1.4 Solution: in-memory mount from `BC2TestRange.dll`
`TestRange.Mount <source> <target>` lines in `BC2TestRange.cfg` are fed to the
engine's own `ParseLine` as a `BundleSource`/`BundleTarget` pair once `+0x324`
is set and before `newSP`, so paths expand exactly as the file's do and the
add goes through `AddTarget`. `package.mft` and the install are never modified.
The DLL logs the node's targets before and after each add. See
`tools/testrange/ranges/sp02-m9-pistol.cfg`:
```text
TestRange.Mount game:levels/sp_02/level-00.fbrb package:levels/mp2_common/level-00.fbrb
TestRange.Mount game:levels/sp_02/level-00.fbrb game:levels/mp_common/level-00.fbrb
```

### 1.5 Live result (2026-10-10): whole-bundle mounts do not hold a level
Tested with `sp02-m9-pistol.cfg` (in-game runs, windowed, no VR):
- The mount works: SP_02's `level-00` node goes 3 -> 5 targets, paths expand natively.
- MP data then references assets SP never loads, and the level load walks them
  unchecked. Three null guards in `BC2TestRange.dll` (only with `TestRange.Mount`)
  get past each crash in turn:
  - `0x5ac6d6`: `SoldierEntityData.WeaponUnlocks` entry with a null `Weapon`.
  - `0x5a5a5d`: `WeaponComponentData` with null firing data.
  - `0x8bbdea`: client re-equip of a weapon that has no client copy yet.
- With them, `US_hg_M9-3` resolves and the give succeeds (slot 1, 20/100 ammo).
- **But the level does not survive.** A few seconds after spawn the game drops
  back to the main menu ("RESUME GAME"), with or without selecting the M9. Seen
  live: menu -> load screen -> main menu, sometimes the level never comes up.
  None of the quit-flag stores (`0x57038e`, `0x5418c1`, `0x77c121`, `0x9638bf`)
  fire, and no access violation is logged, so the drop is a level-side abort,
  not a crash.
- Mounting `mp2_common` alone crashes during the load (`0x00ff2ac2`, read `0xc`):
  the patch bundle needs its `mp_common` base.

Conclusion: putting all of `mp_common` (15,861 entries, about 300 MB) into an SP
level pulls in MP game data that the SP level cannot host. The next approach is
to load only the M9's own partitions (`US_hg_M9-3.dbx`, `US_hg_M9Beretta*.dbx`,
their firing data and 1P assets) rather than whole level bundles.

### 1.6 Why the level drops to the menu (investigation log, 2026-10-10)
Tooling: headless Ghidra on the annotated project `D:\bc2-re\proj\BC2.gpr`
(`-readOnly -noanalysis`, a small `Decomp.java` post-script), `tools/re/bc2re.py`,
and live hooks in `BC2TestRange.dll` (`TestRange.TraceUi 1` traces without mounts).

- **Not a crash and not a quit.** No access violation, no quit-flag store, no
  `exit`/`PostQuitMessage`. The game keeps running; only the UI goes back to the menu.
- **UI trace** (hooks on `UIManager::gameEvent` `0x929a70`, thiscall `(const char*)`,
  and request push `0x956e10`, thiscall `(request*)`, 0x110 bytes, `+0` type; newSP = 3):
  the mount run raises the same in-level HUD events as the baseline
  (`settingsLoaded`, `health`, `changeWeapon`, `ammo`...). About 2 s after the
  give it adds `querySavedGamesEnd` then `exitIntroState`: the main-menu flow.
  No UI request is queued for it.
- **UI state machine** `0x956650` (runs every UI update) pops states from a deque
  at UIManager `+0xc3ec0` (front `+0xc3ec8`, back `+0xc3ed8`):
  - 0: `0x9294e0` loads the `menu/main/mainmenu` bundle as pending op type 0.
  - 1/2: `0x9297b0` (in-game).
  - 3: drains pending ops, then `0x8fec10`.
  - 4: pushes 4 again.
  Pending ops live at `+0xc3fec` (count `+0xc3ffc`) and are serviced by `0x9540e0`.
  When one completes, `0x952630(type)` runs; type 0 builds the main menu
  (`0x951d30`, which sets `+0x82b24` = MainMenu).
- **States are pushed** with `0x9254b0` (deque `push_back`, thiscall on
  `UIManager+0xc3ec0`, `(int* state)`, `ret 4`). Callers: request drain `0x952487`
  (request 10 pushes 3), state machine `0x95696e`, and `0x964fdc`, `0x965028`,
  `0x9650c2`, `0x9650fa` (next to `showMenuActivateReasonPopup`, `0x964e15`).
  Next step: find which of these pushes state 0 in the mount run, and why.

- **Who pushes state 0 (live, hooks on `0x9254b0` and the UI message handler `0x9640d0`):**
  The UIManager message handler is `0x9640d0` (vtable `0x1440808`, thiscall
  `(category, type, data, extra)`, `ret 0x10`). Same as baseline until the give;
  within ~100 ms after it:
  1. `0x655cb0` (client connection update) sees the server peer closed
     (`peer+0x21c`), disconnects it (`0xc01b00`, reason at `+0x58`), and posts
     message 0x25/0x28.
  2. The client connection-state handler `0x55ce50` posts 0x2e/0x14 (state 15:
     disconnected), and the UI pushes state 4 (leave level).
  3. It then posts 0x2e/0x10, and the UI pushes state 0 (main menu).

  A `ClientCombatAreaTriggerEntity` (TypeInfo `0x1bf5b88`, remove handler
  `0x8ed9d0`) is removed on the way out and sends 0x1b/0x27; that is a symptom.
  Queued messages are posted through the message manager `[0x7ff060()+0x18]`,
  vtable `+0x24` `(category, message*, extra)`, type at `message+0x20`.
- **Mounts alone are fine.** With both MP bundles mounted and only the XM8 given,
  the level holds (60 s, no state 4/0). **Giving the M9 makes the in-process
  server drop the client connection.** The prime suspect is replication: the
  server creates the M9 item, and the client cannot rebuild it because the M9
  data is not in SP_02's network registry. Next: confirm the disconnect reason
  and find how replicated data references are resolved.

- **Disconnect reason (live, hardware write watchpoint on the client connection
  state):** the client connection object (`0x655cb0` this; `+0x54` state, `+0x58`
  reason) goes to state 3 with **reason 6** about 90 ms after the M9 give. The
  path is `0x655cb0` -> peer update `0xbfea20` -> `0xbf4420`. `0xbf4420` takes each
  incoming server packet and runs `0xbedf50(packet)`; a false return closes the
  peer, and `0xc06340` sets reason 6. **The client rejects a packet the server
  sends after creating the M9 item**, almost certainly the M9's replication data.

- **Network data ids.** The client rebuilds the soldier's weapons from u16 ids in
  the soldier ghost (`0x8bb0c0`, see `docs/re/BC2_WEAPON_RUNTIME.md` /
  `BC2_DISASM_GIVE_PATH.md`) via `0x5deb30(*0x156d500, id)`. The table is one
  process-wide object shared by the in-process server and client:
  - `[0x156d500]`, refcount `0x156d504`, mutex `0x156d530`.
  - Created by `0x5eb340` (callers `0x56803b`, `0x829be2`), released by `0x5eb410`.
  - `+0x00` map id -> data; lookup `0x5deb30` (thiscall, `(u16 id)`).
  - `+0x20` map data -> id; lookup `0x5debd0` (thiscall, `(data*)`, 0 = none).
  - `+0x40` next id (starts at 1).
  - `0x5e5d70` (thiscall, `(data*)`) registers data that has no id yet: assigns
    the next id and fills both maps (38 callers).
  Hypothesis: the M9 has no id here because nothing registered it, so its
  replication fails on the client.

- **ROOT CAUSE CONFIRMED (live):** `netid: US_hg_M9-3 ... has NO net id`. With
  `TestRange.RegisterNetIds 1` the DLL calls `0x5e5d70` for the given data
  before the give (it got id 1203). The client connection then holds: no reason
  6, no state 4/0, and the level keeps running with the M9 selected (HUD 020/100).
  Remaining: the first-person M9 mesh does not show (hand visible, no pistol).

- **Invisible first-person M9: SOLVED (2026-10-10 17:30), missing shaders.**
  The cause was not animation. Shader databases are per level, and SP_02's has
  no entries for MP weapon materials.
  - **Not pistol-specific.** An MP rifle from `mp_common`, `US_rif_XM8c` (rifle
    anim set), given in SP_02 is just as invisible: the hand sits in a grip pose
    with no gun, HUD 030/210. Screenshot: stage `mp-xm8c.png`.
  - **The async bundles are fine.** `Dist/win32/async/weapon/us_rif_xm8c-00`
    and `us_hg_m9-3-00` sit beside `sp_rgl_xm8_scoped-00`. Each holds only the
    `SkinnedMeshSet`, the `lod0` `MeshData` and, for the M9, the 1P clips.
    Textures are in `mp_common`, which is mounted.
  - **The level's shader database lacks the materials.** Each gameplay level
    bundle carries `Levels/<L>/Shaders/Dx{9,10,11}_Single.res`
    (`Dx11ShaderDatabase` etc.). It also carries `Levels/<L>/shaders.dbx`, a
    `ShaderDatabaseCollection` whose `Groups` list holds one `{Path, Name}` per
    render path. `sp_common`, `mp_common` and `mp2_common` carry no shader
    database. The mesh sets name their materials as strings, e.g.
    `Objects/Weapons/Handheld/US_rif_XM8c/Shaders/{Rubber,Composite,...}` and
    `.../US_hg_M9Beretta/Shaders/US_hg_M9Beretta_Mesh_{BrushedMetal,Plastic}`.
    In the decompressed SP_02 databases (all three):

    | Material path | SP_02 hits | MP map hits |
    |---|---|---|
    | `US_rif_XM8c/Shaders` | 0 | 13 |
    | `US_hg_M9Beretta/Shaders` | 0 | 6 |
    | `US_rif_XM8/Shaders` (SP XM8) | 12 | 12 |

    The MP column holds for mp_001..008, bc1_oasis_cq, mp_sp_002gr and the
    Package mp_003 and mp_009sdm patch bundles.
  - **The engine merges several databases by design** (Ghidra plus a live read):
    - The Dx11ShaderDatabase factory is `0x99fc30` (vtable `0x1446524`). On
      resource add, `0x98c5d0` calls `ShaderSystem vf+0x68` and stores the ctx
      at `db+0x88`. The database name is the `char*` at `db+0x8c`.
    - The shader system is `[0x1572b18]` (Dx11ShaderSystem, vtable
      `0x14481f8`). `vf+0x68` is RegisterDatabase `0x9cb590` (thiscall
      `(db, ctx)`, `ret 8`, lock `ss+0x250`). It appends to the vector at
      `ss+0x220/+0x224/+0x228` and then runs `0x9c8120`.
    - `0x9c8120` clears the three merged maps (`ss+0x20/+0x38/+0x50`) and
      merges each database in vector order through `0x9c3f60`. **A later
      database overwrites same-named entries.**
    - The lookups are vtable slots `+0x1c/+0x20/+0x24` (`0x9b02b0`, `0x9b0340`,
      `0x9b03d0`), thiscall, taking a pointer to a string whose first dword is
      the name. They return 0 on a miss.
    - Live vector on SP_02 with the mount: `systems/ui/shaders/dx11_single`,
      `Scenes/UIBackground/shaders/dx11_single`, `Levels/SP_02/...`,
      `Levels/MP_009SDM/...`.
  - **Fix, step 1: mount an MP patch bundle.** Adding
    `TestRange.Mount game:levels/sp_02/level-00.fbrb package:levels/mp_009sdm/level-00.fbrb`
    (15 MB, 301 entries, carrying its own shader databases) loads and
    auto-registers `Levels/MP_009SDM/Shaders/Dx11_Single`. **The M9 draws in 1P**
    (stage `m9-shaders.png`). The bundle also holds 13 `*deleted*` markers
    (minimap overlay, knife throat-stab FX, G3/M1Garand meshes); no effect seen
    yet.
  - **Fix, step 2: SP shaders win; foreign databases only fill gaps.**
    - **How the merge works.** `0x9c8120` merges every database in vector
      order through MergeDatabase `0x9c3f60` (thiscall on the shader system,
      `(db, &vector)`, `ret 8`).
      - In the first two merged maps, the **last** database merged wins a
        shared name.
      - The third map's per-name permutation list is rebuilt from **every**
        database, in vector order, and the **first** one wins.
    - **First attempt (wrong).** Moving SP_02's database to the end of the
      vector (a RegisterDatabase hook) made the MP permutations win the third
      map. With all mounts loaded, SP fence posts rendered black and red, and
      the XM8's textures looked blurry.
    - **Bisect.** Each mount was tried alone, all with the old hook:
      - `mp_common` alone was clean;
      - `mp_009sdm` alone showed the post;
      - the MP and Vietnam mounts together showed both artifacts.
      Vietnam was not the cause.
    - **Current fix.** `BC2TestRange.dll` detours MergeDatabase. The vector
      keeps its native order: the level's database comes before the foreign
      ones, so it is first in the third map. After the last database merges,
      the level's database (`Client.Level` + `/`) is merged once more, so it
      also wins the first two maps.
      - Result: XM8 detail and the fence match the no-mount control (stage
        `fix-compare.png`).
      - It is on whenever mounts exist; `TestRange.ShaderOrder 0` turns it off.
    - **Banding.** The banded, posterised sky in flat captures is pre-existing.
      It shows in no-mount controls, and the user confirms SP_02 always looked
      this way.
  - **Bundle entries: SP first; mounted bundles only fill gaps.**
    - **The loader.** The level bundle loader `0x4b56a0` gets the bundle's
      targets from `0x4a5c50`. It reads them first-listed first and keeps a
      "seen" name set, so SP's own targets win every name they carry.
      - `0x4de490` has the same pattern, but it never runs for the level
        bundle.
    - **What went wrong.** SP's targets also carry deleted entries (flag bit
      0, kind `*deleted*`), and the loader adds their names to the seen set.
      - SP_02 deletes 25 `*_Scoped_AnimTree1p.res`, so the Kobra and Scoped
        variants of MG3, F2000, M95, QBY88, QJY88, AKS74u and SCAR built a 1P
        anim tree from a null resource.
      - The client faulted at `0xf691cf` (node ctor `0xf69030` via
        `0x828d50`, from the inventory switch `0x6ef5e4`).
      - The crash-finder hit exactly the predicted names, in order.
    - **The fix.** `BC2TestRange.dll` filters each entry at `0x4b59ea`.
      - Data there: name `[esp+0xec]`, kind `[esp+0xf0]`, archive path
        `[esp+0xa0]`, deleted flag at `[esp+0x104]` bit 0. The skip target
        is `0x4b6370`, the native next-entry path.
      - A name SP deleted stays hidden from SP's own later targets, as native.
        The first live copy from a mounted target fills it.
      - A mounted target's deletions are ignored.
      - On SP_02 with all mounts this fills about 2,300 names. The visuals
        still match the no-mount control (stage `gapfill-compare.png`).
      - `TestRange.GapFill 0` turns it off.
  - The animation theory is ruled out. SP and MP default 1P anim trees are
    equivalent, with a 4-entry `BaseSetAnimTrees` pointing at
    `SoldierAnimTree1p{Hgr,Shg,Rif,AT}`, which exist in both `sp_common` and
    `mp_common`. The M9 already posed correctly once its shaders existed.

- **Invisible first-person M9 (history, 2026-10-10 16:30).** With the net id
  registered, the M9 is given, selected and fires (user confirmed), but the 1P
  gun mesh does not draw; the 1P hand shows in a fist. Checked so far:
  - **Data resolves.** Live read: `WeaponStates[0].Meshes1p` =
    `US_hg_M9Beretta_Mesh` and `Meshes3p` = `US_hg_M9Beretta_Mesh3p`, both
    non-null. Same shape as the XM8.
  - **Geometry bundle is requested.** On equip the client asks for
    `async/weapon/US_hg_M9-3` through resource manager `[0x154c444]` vtable
    `+0x70` `loadBundle(out, name, flags)` and gets a handle (logged by the
    `bundle:` hook). The `.res` geometry (`US_hg_M9Beretta_Mesh.res`,
    `_lod0_data.res`) is in that async bundle, mirroring the XM8
    (`sp_rgl_xm8_scoped`).
  - **Leading suspect: animation.** M9 `AnimBaseSet` = 4 (`wabsHg`),
    `WeaponClass` = 2; XM8 = 0 (`wabsRif`) / 0. SP has no handguns, so the SP
    soldier's 1P animation tree probably has no `wabsHg` set, leaving the arms
    unposed and the gun unplaced. Next checks:
    1. Give an MP *rifle* from `mp_common` (e.g. `US_rif_XM8c`). If it draws,
       the problem is pistol-specific.
    2. Compare the SP soldier's `AnimTree1p` (`SoldierEntityData+0x214`) with
       the MP soldier's.
    3. Try overriding the M9 data's `AnimBaseSet` to 0 in memory (mod-owned
       write on our own given data).
  - The client re-equip guard (`0x8bbdea`) is no longer needed now that the net
    id exists; it is off by default (`TestRange.ReequipGuard 1` re-enables it).
- **REST tool:** `Client.EnableRestTool true` applies (bytes change), but the
  game opens no TCP/UDP listener when it is set at launch-time injection. The
  claimed HTTP server is unverified.

This mounts the entire multiplayer weapon arsenal into `sp_02`, allowing `BC2TestRange.cfg` or mod code to resolve and give:
- `US_hg_M9-3` / `US_hg_M9Beretta` (M9 Pistol)
- `Beach/M1911A1/M1911A1` (M1911)
- `MEC_hg_MP412rex/MEC_hg_MP412rex` (MP412 REX)
- `Beach/M1Garand/M1Garand` (M1 Garand)
- `Beach/Thompson/Thompson` (Thompson SMG)
- `RU_lmg_PKM/RU_lmg_PKM` (PKM)
- `RU_shg_Saiga12/RU_shg_Saiga12` (Saiga 12)
- `UL_lmg_FNMinimi/UL_lmg_FNMinimi` (M249 Minimi)
- `UL_rif_HK416/UL_rif_HK416` (HK416)
- `US_rif_XM8c/US_rif_XM8c` (XM8 Compact)
- `US_shg_Remington870MCS/US_shg_Remington870MCS` (870 MCS)

---

## 2. Debug Fixtures, Dev Options, and Console Recovery

### 2.1 The Settings Registry (`SettingsManager`)
The retail executable contains an active, queryable settings registry:
- **Global Pointer**: `0x0155cfdc` (`void* registry = *reinterpret_cast<void**>(0x0155cfdc)`)
- **Setter**: `0x004f4bd0` (`void SetByName(void* this, EastlString* key, EastlString* value)`)
- **Getter**: `0x004f4c40` (`void* GetByName(void* this, const char* key, const void* fallback)`)

24 settings groups are registered into `SettingsManager`:
1. `Client` (`ClientSettings`, size `0xfc`, vtable `0x0142daf0`)
2. `Server` (`ServerSettings`, size `0xa4`, vtable `0x0142db0c`)
3. `Game` (`GameSettings`, size `0xa4`)
4. `Render` (`RenderSettings`)
5. `WorldRender` (`WorldRenderSettings`)
6. `PostProcess` (`PostProcessSettings`)
7. `ResourceManager` (`ResourceManagerSettings`)
8. `MeshStreaming` (`MeshStreamingSettings`)
9. `Physics` (`PhysicsSettings`)
10. `Network` (`NetworkSettings`)
11. `Overgrowth`, `Decal`, `EmitterSystem`, `Monitoring`, `Soot`, `StaticCull`, `Occlusion`, `IrradianceVolume`, `ShaderSystem`, `VisualTerrain`, `DX9DisplaySettings`, `DX10DisplaySettings`, `Dx11DisplaySettings`, `Demo`

### 2.2 Recoverable Developer Switches

#### Client Settings (`Client.<Field>`)
| Field | Offset | Type | Description |
|---|---|---|---|
| `IsMenuDevOptsEnabled` | `0xf4` | Boolean | Enables developer options in UI manager (`0x00696140`). |
| `DebugMenuOnLThumb` | `0xe9` | Boolean | Checks left thumbstick click to open debug menu (`0x0108c480`). |
| `EnableRestTool` | `0xc6` | Boolean | Starts embedded HTTP/REST database server (`0x004af980`, routes `/db/domains/`, `/db/partitions/`). |
| `UnrestrictedUnlocks` | `0xc8` | Boolean | Grants all weapons, gadgets, and attachments. |
| `ShowBuildId` | `0xc1` | Boolean | Draws engine build info on the screen. |
| `DebugCollectibles` | `0xbc` | Boolean | Displays collectibles in map/HUD. |
| `HavokVisualDebugger` | `0xb6` | Boolean | Enables connection to Havok Visual Debugger (VDB). |
| `DisableMovies` | `0xf1` | Boolean | Skips intro and cutscene video playback. |
| `DisableHudMovies` | `0xf2` | Boolean | Disables HUD Flash/SWF layers. |

#### Server Settings (`Server.<Field>`)
| Field | Offset | Type | Description |
|---|---|---|---|
| `IsGodMode` | `0x95` | Boolean | Invulnerability / zero damage. |
| `IsJesusMode` | `0xaa` | Boolean | God mode variant. |
| `HasUnlimitedAmmo` | `0xc0` | Boolean | Infinite weapon ammo. |
| `HasUnlimitedMags` | `0xc9` | Boolean | Infinite reserve magazines. |
| `InstantSpawn` | `0x93` | Boolean | Skips spawn timer delays. |
| `UnrestrictedUnlocks` | `0xbe` | Boolean | Server-side unlocked inventory. |
| `DisableCutscenes` | `0xb5` | Boolean | Server skips cutscene triggers. |
| `DisableCheckpoints` | `0x8e` | Boolean | Disables autosave checkpoints. |
| `ShowTriggerDebugText`| `0xb6` | Boolean | Renders in-world trigger volume names. |
| `DrawActivePhysicsObjects`| `0xbb` | Boolean | Draws wireframe collision bounding boxes. |
| `RespawnOnDeathPosition` | `0xc6` | Boolean | Respawns player at exact point of death. |

### 2.3 Built-in HTTP REST Tool
Setting `Client.EnableRestTool true` activates the Frostbite database HTTP inspection server:
- Initialized via `0x004af8b0` / `0x004af980`.
- Routes:
  - `GET /db/domains/`: Lists all active memory domains.
  - `GET /db/partitions/`: Inspects loaded DBX partitions, types, and instances directly via web browser.

### 2.4 Console Commands & `ServerConsoleCommandEntity`
- While DICE stripped the visual tilde (`~`) drop-down console from retail, command execution is fully functional:
  - Any setting key-value pair can be pushed dynamically via `SettingsManager::SetByName` (`0x004f4bd0`).
  - Logic entity `ServerConsoleCommandEntity` (TypeInfo `0x01bf2438`, vtable `0x014230a0`, data class `ConsoleCommandEntityData` TypeInfo `0x01c05efc`) executes an array of console command strings (`Commands` at `+0x80`).
- A developer console in the mod can directly route input strings to `0x004f4bd0` to execute commands in real time.
