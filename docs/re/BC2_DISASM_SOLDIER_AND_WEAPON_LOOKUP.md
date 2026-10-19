> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (build 795745, x86): server soldier lookup and weapon-data-by-name resolver (static RE)

> In this report **SV** means read in the static disassembly. It is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md), not that page's repo-code SV.

Source: `BFBC2Game.exe`, image base 0x400000. Cloud static disassembly with [`tools/re/bc2re.py`](../../tools/re/bc2re.py), 2026-10-08; nothing was run in the game.
Labels: **SV** = I read it in the disassembly. **INF** = inferred (name, purpose, or a link I did not trace end to end).
A signature check confirmed every signature against `.text`+`ctr`; each one matches only its own address. In the signatures, `??` replaces only rel32 call/jmp operands and absolute image addresses.

---------------------------------------------------------------------------
## L1: the local player's SERVER soldier

### Server game context (SV)
- **0x547bf0** is the lazy-init getter. It returns the ADDRESS of a static object at **0x1566b18**. The context is not a pointer. Its vtable is 0x1416904, the base ctor is 0x588a50, and the init flag is bit 0 of `0x1566b80`.
  `sig: B8 01 00 00 00 84 05 ?? ?? ?? ?? 0F 85 86 00 00 00 09 05 ?? ?? ?? ?? 53 33 DB`
- Members used here (SV from usage):
  - `ctx+0x18`: message/event manager. The give posts through `vf[0x1c/4]`.
  - **`ctx+0x2c`: ServerPlayerManager\***.
  - **`ctx+0x34`: server connection owner** (INF name). It holds `vector<ServerConnection*>` at +0x208/+0x20c.
  - `ctx+0x4`: must be non-null for the interaction handler to run.
  - `ctx+0xc`: an object with a time getter, `0x6ca490`.
- A harness can read the static directly. Read `*(u32*)0x1566b80 & 1` first: if it is 0, the context was never constructed. Then read `*(void**)(0x1566b18+0x2c)`, which is NULL when no server exists (INF; I did not trace the writers, which go through the ctx `this`).

### ServerPlayerManager (ctor 0x55b830, called from 0x853d19, primary vtable 0x1417680) (SV)
`sig: 51 53 55 8B 6C 24 10 56 57 55 8B F1 E8 ?? ?? ?? ?? 8D 4E 10 E8 ?? ?? ?? ?? C7 46 4C`
| off | field |
|---|---|
| +0x58 / +0x5c / +0x60 | `vector<ServerPlayer*>` begin/end/cap. 20+ loops iterate it, e.g. 0x549570, 0x548e88. |
| +0x68 | init -1 (meaning unknown) |
| +0x6c | `ServerPlayer*[maxPlayers]` indexed by player id. Use: `ebx = mgr->+0x6c[id]` in 0x662f20. |
| +0x70 | per-player 0x1c-byte records, indexed `id*7*4` with the id from player+0x154 (0x5612df). |

### ServerPlayer (ctor 0x572140, ret 0xc; vtable **0x14188d8**, 13 slots) (SV)
- **+0xc3c ServerSoldierEntity\* soldier** (raw pointer):
  - The ctor zeroes it (0x5721e0).
  - It is written only in `ServerPlayer::setSoldier` **0x5611c0** (thiscall, ret 4, arg = new soldier or 0), at 0x561283 `89 9E 3C 0C 00 00`.
  - That function then calls `0x6c0fd0(soldier, player)`, which writes **soldier+0x220 = player**. So the link is bidirectional and checkable.
  - Before overwriting, setSoldier keeps a weak ref to the old soldier at +0xca0.
- **vf[0x30/4] = 0x56d410 `getSoldier`**: `mov eax,[ecx+0xc3c]; ret` (`sig: 8B 81 3C 0C 00 00 C3`). The pickup handler calls exactly this (`player->vf[0x30/4]()` at 0x662f70 and 0x6632d4).
- +0xd84 byte: written only by the setter 0x532190, and that setter is called only from the AI-player creation 0x72d160 with value 1 (0x72d3ca). The ctor initialises it to 0 (0x5723c4). So **+0xd84 = isAIPlayer** (INF name, SV writes). It is also passed to the soldier in 0x6c0fd0.
- +0xa8: team id (compared in 0x662f89). +0x154: player id (INF). +0xc28: byte ctor arg. +0xc2c: eastl string (INF: name). +0xbc8: flags.
- setSoldier sig: `83 EC 38 53 8B 5C 24 40 85 DB 55 56 8B F1 75 14 E8 ?? ?? ?? ?? 8B 48 18`
- store-site sig (0x561281): `85 DB 89 9E 3C 0C 00 00 74 08 56 8B CB E8 ?? ?? ?? ?? 80 3D ?? ?? ?? ?? 00`
- ctor vtable-store sig (0x572184): `C7 06 ?? ?? ?? ?? C7 45 00 ?? ?? ?? ?? C7 07 ?? ?? ?? ?? 89 9E 18 0C 00 00`
- soldier setPlayer 0x6c0fd0: `56 57 8B 7C 24 0C 8B F1 3B BE 20 02 00 00 0F 84 ?? ?? ?? ?? 33 C0 3B F8`

### ServerConnection route (SV)
- **0x642a00** `findConnectionByPlayer` (thiscall on `ctx+0x34`, ret 4): it loops over `+0x208..+0x20c` and returns the connection whose `conn+0x1610 == player`.
  `sig: 56 8B 74 24 08 85 F6 74 28 8B 91 08 02 00 00 8B 89 0C 02 00 00 3B D1`
- `conn+0x1610` = the connection's ServerPlayer. The player is created by `ServerPlayerManager 0x575570(id, name)` at 0x63adcf/0x6539cc.
- **0x662f20** is the real start of the pickup interaction handler; the earlier report's 0x662ef0 is the tail of the previous function. It is thiscall on the connection and takes `[ebp+8]` = player id and `[ebp+0xc]` = target id. It checks `mgr->+0x6c[id] == conn->+0x1610`.
  `sig: 55 8B EC 83 E4 F0 81 EC F4 02 00 00 53 56 57 8B F9 89 7C 24 14 E8 ?? ?? ?? ?? 8B 70 2C`

### Recommended chain (pseudo-C)
```c
ServerSoldierEntity* LocalServerSoldier() {
  if (!(*(u32*)0x1566b80 & 1)) return 0;
  u8* mgr = *(u8**)(0x1566b18 + 0x2c);                        // SV
  if (!mgr) return 0;
  for (u8** p = *(u8***)(mgr+0x58); p != *(u8***)(mgr+0x5c); ++p) {   // SV
    u8* pl = *p;
    if (!pl || *(u32*)pl != 0x14188d8) continue;              // ServerPlayer vtable (SV)
    if (*(u8*)(pl+0xd84)) continue;                           // isAI (INF)
    u8* s = *(u8**)(pl+0xc3c);                                // SV
    if (s && *(u32*)s == 0x1424850 && *(u8**)(s+0x220) == pl) return s;   // SV vtable + back-link
  }
  return 0;
}
// Alternative (SV): conns = *(u8**)(0x1566b18+0x34); c0 = **(u8***)(conns+0x208);
//   pl = *(u8**)(c0+0x1610); soldier = *(u8**)(pl+0xc3c). In SP there is one local connection (INF).
```
- **Client to server link:** none found. No client-range code references the ServerSoldierEntity TypeInfo 0x1bf2390; all 93 references are below 0x780000 or in registration. The client learns about the server soldier only through ghosts (INF from absence). A cross-check is possible by comparing team (player+0xa8) and position; matching on the player id (server player+0x154 vs the client player id) was not traced.

---------------------------------------------------------------------------
## L2: SoldierWeaponData* by name. A native precedent exists (SV)

### The SP checkpoint weapon save/restore uses exactly this lookup
`ServerPlayer::setSoldier` 0x5611c0 runs this at 0x561293, only when `*(u8*)0x1566604` (an SP flag, INF) is set and `!player->isAI`:
- if there is an old soldier, it calls **0x555460 SaveWeapons(old)**;
- otherwise it calls **0x555530 RestoreWeapons(new)**.

Both are cdecl with one arg (the soldier) and a plain `ret`.

- **0x555460 SaveWeapons**:
  - For `i < min(itemCount, 4)`: `name += item->data(+4)->WeaponAssetName(+0x40) + '"'`.
  - It stores the result under the setting `CurrentSoldierWeaponList` and `CurrentSoldierWeaponIndex` in `[0x1571370]+0x150`.
  - **The key that is saved is `SoldierWeaponData.WeaponAssetName` (+0x40, an eastl string).**
  - `sig: 83 EC 0C 53 8B 1D ?? ?? ?? ?? 56 57 6A 00 81 C3 50 01 00 00 68`
- **0x555530 RestoreWeapons**: `sig: 83 EC 2C 56 8B 35 ?? ?? ?? ?? 81 C6 50 01 00 00 6A FF 68 ?? ?? ?? ?? 8B CE`
```c
for (slot = 0; token = next '"'-separated name; ++slot) {
  if (token.empty())                         { SetWeaponInSlot(s, 0, slot, 0, 0); continue; }   // 0x5555f4
  IResourceManager* rm = *(void**)0x154c444;
  if (!rm->vf0x14/4))        { SetWeaponInSlot(s, 0, slot, 0, 0); continue; }   // 0x5555fb..0x555616
  ResourceRef ref = { 0, (void*)0x14f267c };                                   // 0x42ad20
  ResourceRef_Load(&ref, token.c_str(), 0, 0, (TypeInfo*)0x1bfca60 /*SoldierWeaponData*/, 0);  // 0x459530 -> 0x449810
  if (ref.ptr) SetWeaponInSlot(s, ref.ptr, slot, 0, 0);                       // 0x55564f
  ResourceRef_Release(&ref, 0);                                                // 0x43d5a0
}
0x68e1c0(s, savedIndex);   // reselect (INF)
```
  - resolve-site sig (0x5555fb): `8B 0D ?? ?? ?? ?? 8B 11 50 8B 42 14 FF D0 84 C0 75 10 6A 00 6A 00 55 6A 00`
  - lookup-site sig (0x55561d): `8D 4C 24 1C E8 ?? ?? ?? ?? 6A 00 68 ?? ?? ?? ?? 6A 00 6A 00 8D 54 24 34 52`

### Pieces (SV unless marked)
- **ResourceManager global**:
  - `*(IResourceManager**)0x154c444`, stored at 0x557d6a after `new`+ctor 0x4b9ce0. Store sig at 0x557d5f: `8B CF E8 ?? ?? ?? ?? EB 02 33 C0 A3 ?? ?? ?? ?? 8B 10 8B C8 8B 02 FF D0`.
  - The ctor also writes `this` to **0x154c448** (0x4b9fe5), and the dtor 0x4b29a0 nulls it. 0x449810 uses 0x154c448.
  - vtable 0x140ff00. "ResourceManager_Partition", "ResourceManager_Async" and others are the names of its sub-objects.
- **`RM::vf5` (vtable+0x14) = 0x47a4e0 `bool Exists(const char* name)`** (thiscall, ret 4). It returns true if the name is in the resource-entry map (`RM+0x14`, via 0x449170). 0x449170 also retries `name+"!"`. Failing that, the **Win32 partition database** `RM+0xa4` (`0x4af9c0(dbManager RM+0xa0, "Win32")`) must report the partition through **0x465d10**.
  `sig: 56 57 8B 7C 24 0C 57 8B F1 E8 ?? ?? ?? ?? 85 C0 75 16 8B 8E A4 00 00 00`
- **0x465d10 `Database::FindLoadedPartition(const char* name)`** (thiscall on `RM+0xa4`, ret 4). It takes lock 0x154c4b0 and does a hash lookup in `db+0x5c` (0x44bc90). It returns `Partition*` or 0, and it **never loads**. That it holds only loaded partitions is INF: it is the same map the `/db/partitions` web handler checks before calling `readPartition` 0x4afb00.
  `sig (at 0x465d3c, fn+0x2c): 8B 4C 24 1C 83 C4 04 8D 54 24 18 52 8D 44 24 10 89 4C 24 1C 83 C6 5C 50 8B CE E8`
- **0x449810 `ResourceRef::Load(const char* name, int mode, int flags, TypeInfo* type, bool async)`** (thiscall, ret 0x14). `ResourceRef = { +0 T* ptr; +4 ResourceEntry* entry (sentinel 0x14f267c) }`.
  - It releases the old entry, then calls `entry = (*0x154c448)->vf[0xac/4](type, name, mode, flags)` (= **0x4b85c0**) and `entry->vf0()` (AddRef).
  - If `!async`: `ptr = entry->vf[0x40/4]()`, which is 0x43d0f0 `return entry+4`. Otherwise it registers a callback through `entry->vf[0x2c/4]`.
  - `sig: 53 8A 5C 24 18 56 8B F1 8B 4E 04 81 F9 ?? ?? ?? ?? 74 25 84 DB 74 0A`
  - 0x459530 is the eastl-String* overload: `name = str->begin`, then a tail jump to 0x449810.
- **0x4b85c0** (RM vf 43): it resolves the name against the search prefixes (0x490a50 → 0x449170), then finds or creates the entry (0x449210).
  - The map key is `name`, plus `'!'` when `type` IsA DataContainer 0x1bf030c (SV, 0x44924d).
  - ResourceEntry: size 0x28, vtable 0x140c030; +4 object, +0x1c refcount, +0x24/+0x25 state flags, where bit 4 of +0x25 = loaded.
  - With **mode not in {2,3,4} (mode 0 = what the natives use) it spin-waits (0x509140) until the entry is loaded, so it can block on I/O** if the partition is not resident.
  - `sig: 81 EC 14 04 00 00 A1 ?? ?? ?? ?? 53 55 56 8B 35 ?? ?? ?? ?? 57 50 8B F9`
- **0x43d5a0 `ResourceRef::Release(bool)`** (ret 4): `entry->vf1()`. vf1 is 0x4905f0, which does `lock xadd entry+0x1c, -1`. **When the count reaches 0 it starts the unload** (0x459180 / 0x487b70).
  `sig: 56 8B F1 8B 4E 04 81 F9 ?? ?? ?? ?? 74 28 80 7C 24 08 00 74 0A`
- **Type check:**
  - DataContainer `vf[2]` returns `TypeInfo*` (0x4ae84b).
  - **0x4e6100 `TypeInfo::IsKindOf(TypeInfo* base)`** (thiscall, ret 4) compares the class-id range `+0x1c` against `base+0x1c..+0x1e`.
  - `sig: 8B 41 04 66 8B 50 04 66 81 E2 F0 01 66 83 FA 30 75 2A 8B 54`
  - SoldierWeaponData TI = 0x1bfca60. DataContainer TI = 0x1bf030c.

### Name format and layouts (bc2_types.json plus SV)
- `String` (12 bytes) is an **eastl::string `{char* begin; char* end; char* capEnd}`**: the empty string is `{0x154c450, 0x154c450, 0x154c451}`, as seen at 0x4b9d7a and 0x555484. A field's text is `*(char**)(obj+off)`.
- DataContainer header 0..0xb. `vf[2]` = GetType (SV); +4 is a refcount (INF).
- `Asset.Name` +0xc, so **`SoldierWeaponAsset.Name` is at +0xc**; `ObjectBlueprint.Object` is at +0x50, and it holds the SoldierWeaponData.
- `GameObjectData.Name` +0xc. **`SoldierWeaponData.WeaponAssetName` +0x40.**
- The hierarchy is SoldierWeaponData (0x130) → GameObjectData → DataContainer, and SoldierWeaponAsset (0x58) → ObjectBlueprint → EntityBusData → Editable → Asset → DataContainer.
- Names seen in other `ResourceRef::Load` calls are partition paths without `.dbx`, e.g. "Persistence/MP_Template" and "Awards/MatchmakerParams". So `"Weapons/AEK971/AEK971"`-style strings fit (INF: the exact strings live in game data, not in the exe). **Pass the exact `WeaponAssetName` string.** You can read it from any live item: `item->data(+4)+0x40`.
- Kits/unlocks: SoldierEntityData.WeaponUnlocks +0x1a0, with 12-byte entries `{slot, SoldierWeaponAsset*, IsBundled, IsSelectable}`. Weapons +0x1b0 holds asset pointers. In both, `asset->+0x50` is the SoldierWeaponData (see give-path-report G4). They hold pointers, not names, so they double as a no-RM fallback: compare `*(char**)(asset+0xc)` or `*(char**)(data+0x40)`.

### Recommended harness resolver (pseudo-C)
```c
SoldierWeaponData* ResolveWeapon(const char* weaponAssetName, ResourceRef* keep /* caller-owned, long-lived */) {
  u8* rm = *(u8**)0x154c444;                                   if (!rm) return 0;
  // Loaded-only gate (no I/O): partition must already be resident in the current level.
  if (!thiscall<void*>(0x465d10, *(void**)(rm+0xa4), weaponAssetName)) return 0;  // "not loaded in this level"
  keep->ptr = 0; keep->entry = (void*)0x14f267c;
  thiscall<void>(0x449810, keep, weaponAssetName, 0, 0, (void*)0x1bfca60, false);
  DataContainer* d = (DataContainer*)keep->ptr;
  if (!d || !thiscall<bool>(0x4e6100, d->vf2, (void*)0x1bfca60)) return 0;  // IsKindOf SoldierWeaponData
  return (SoldierWeaponData*)d;     // then: thiscall 0x6bcf80(serverSoldier, d, slot, select, force)
}
// Keep 'keep' alive while the item exists (or release with 0x43d5a0(keep,0) only after removing it):
// the native restore releases right away, which is safe only because the level itself references the partition.
```
**How to tell whether a weapon is loaded in this level:**
- `0x465d10(RM+0xa4, name) != 0` is the cheap test with no side effects.
- `RM->vf5(name)`, as the native code uses, is looser: it is also true for any name that already has a resource entry.
- If the partition is not resident, do not call 0x449810 with mode 0. It would try a blocking load, and if that load succeeded, releasing the ref could unload the partition under a live item. INF: whether loading a weapon partition mid-level even works is untested; LoadOnDemand +0x125 and StreamGroup1p +0x78 also matter.

---------------------------------------------------------------------------
## Gaps / cautions
- Threading:
  - The native resolver call (0x555530) runs inside `ServerPlayer::setSoldier` on the server simulation. vf 0x4b85c0 takes the RM lock 0x154c474 (TLS-based counter) and the DB lock 0x154c4b0.
  - Calling it from the game main thread outside the server tick is unverified, and so is calling 0x6bcf80 that way (see give-path-report).
- I did not trace who writes `ctx+0x2c` and `ctx+0x34`, or whether `+0x58` also holds AI players (that it does is INF from 0x72d160 → 0x573750). Filtering on isAI covers either case.
- I did not decode the Partition object layout (instance list, primary instance). The typed `ResourceRef::Load` makes that unnecessary. How the entry for a SoldierWeaponData request picks its instance (`name+"!"` key) is INF; the native restore depends on it returning a SoldierWeaponData without an IsA check.
- The name hash in `db+0x5c` (0x44bc90) and the RM map (0x4c89b0): I did not check whether they are case-sensitive.
- `0x1566604` (SP checkpoint flag, set in 0x5557c0) and `[0x1571370]+0x150` (a settings store) were not traced further.
