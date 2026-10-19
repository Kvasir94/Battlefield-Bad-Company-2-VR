> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (build 795745, x86) native weapon sound path: static RE report

> Cloud static disassembly of build 795745 ([`tools/re/bc2re.py`](../../tools/re/bc2re.py)), 2026-10-08.
> In this report **SV** means read in the disassembly; it is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md). Nothing here has run in the game unless marked LO. Script names refer to throwaway analysis scripts that are not in the repo.

The executable is read-only, and nothing was copied out of it. Scripts are `snd_*.py` in this folder. The typeinfo dump is `../bc2_types.json`, and the type extract is `snd_types.txt`.
Labels: **SV** = read in disassembly, **DUMP** = typeinfo dump, **INF** = inference.
Every signature was checked for uniqueness over .text+ctr (`snd_sig.py`), with `??` covering rel32 and absolute relocations. `@+N` means the signature starts N bytes into the function.

## 1. Reflected data (DUMP)

| Type | Field | Offset | Type |
|---|---|---|---|
| SoldierWeaponData (0x130) | WeaponFiring | +0x98 | WeaponFiringData |
| WeaponFiringData (0x4c) | PrimaryFire | +0x40 | FiringFunctionData |
| FiringFunctionData (0x1f0) | ReflectionSound | +0xf0 | SoundAsset |
| | **OutOfAmmoSoundEffect** (empty click) | **+0x194** | SoundAsset |
| | **ProjectileFireSound** | **+0x1cc** | SoundAsset |
| | **FireSoundEffect** | **+0x1d0** | EffectAsset (played through the sound system, see 0x6eba10) |
| | LoopingFireSound | +0x1e5 | Boolean |
| SoldierSoundData (0x88; SoldierEntityData+0x1f0) | WeaponDeploySound / BreathSound / WeaponZoomSound | +0x74 / +0x78 / +0x7c | SoundAsset |
| PowerToolWeaponEffectsData | DamageSound / RepairSound / RepairCompletedSound | +0x1c / +0x2c / +0x30 | SoundAsset |
| AnimationSoundMapData (0x28, Asset) | Categories | +0x18 | ArrayBase<AnimationSoundCategoryData> |
| AnimationSoundCategoryData (0x1c) | Name, AnimSound | +0x0 String, +0xc ArrayBase<AnimationSoundData> | |
| AnimationSoundData (0x8) | Sound, Animation | +0x0 SoundAsset, +0x4 BaseAnimationAsset | |
| SoundAsset (0x18, Editable) / SoundTemplate (0xbc) | Nodes, Outputs, PublicEvents, etc. | see `snd_types.txt` | |
| AnimationNodeData | CallbackId | +0x7c | Int32 |

- No reflected field has a reload, magazine or bolt sound (DUMP). WeaponEffectsData (0x1c) has no sound fields. In WeaponStateData, the only animation hooks are AnimTree1p (+0xb8) and AnimTree3p (+0xb4).
- ArrayBase in a struct is 0x10 bytes. Its element storage begins at field+8 and ends at field+0xc (SV 0x6f0aaf..0x6f0afb).

## 2. The play-sound primitives (SV)

The sound manager is a singleton. `0x7ff060()` returns the static object at **0x15713d8** (vtable 0x1431530).
- **SoundManager** = `*(*(0x15713d8+0x38)+0x70)`, which is `*(*(DWORD*)0x1571410 + 0x70)` (SV 0x6d42b5, 0x802094, 0x7cf2cd, 0x8f98af).
- **AnimationSoundMap** = `*(DWORD*)(0x15713d8+0x48)` = `*(DWORD*)0x1571420` (SV 0x82a4d3 write; 0x821222 clears it).

| Addr | ABI / pseudo-C | Signature | Label |
|---|---|---|---|
| **0x637d50** | `uint __thiscall SoundMgr_PlayAt(SoundMgr*, SoundAsset* snd, const LinearTransform* xf)`, `ret 8`. Returns a handle (0 = fail). Calls 0x637290 with an empty parameter list. | `81 EC D4 00 00 00 8D 44 24 14 8B D0 89 54 24 04` | SV |
| **0x637290** | `uint __thiscall SoundMgr_PlayAtParams(SoundMgr*, SoundAsset* snd, const LinearTransform* xf, SoundParamList* p)`, `ret 0xc`. If snd is NULL it returns 0. | `55 8B EC 83 E4 F0 8B 55 08 81 EC 28 01 00 00 85 D2` | SV |
| 0x633cb0 | `__thiscall SoundMgr_Stop(SoundMgr*, uint handle, int 0)`, `ret 8` | `8B 44 24 04 85 C0 53 56 57 8B F1 0F 84 B1 00 00 00` | SV |
| 0x629ae0 | `__thiscall SoundMgr_SetTransform(SoundMgr*, uint handle, const LinearTransform*)` (used at 0x802162) | `51 8B 44 24 08 56 8B F1 8B 8E 18 52 01 00 3B C1 72 1A 8B 54 24 10` | SV (name INF) |
| 0x6300f0 / 0x630030 | `ParamList::AddBool(const char* name, bool)` and `AddFloat(const char*, float)`, thiscall `ret 8`. Entries are 12 bytes. | | SV |
| 0x81b190 | `cdecl AddGlobalParams(ParamList*)`: adds 5 global bool parameters | | SV |

`SoundParamList` is a fixed vector (SV 0x6ebc49..0x6ebc6e). The layout is `{Entry* begin; Entry* end; Entry* capEnd; ?; Entry* inlineBuf; Entry buf[16]}` with 12-byte entries. After the call, free `begin` with 0x41b8b0 if it is not `inlineBuf`. Weapon sounds pass these parameters: `IsFirstPerson` (0x1425bc4), `IsZooming` (0x1425c30), `OCCL` (0x1424bdc), `ReverbArea`/`ReverbOcclusion` (fire only), and the global parameters from 0x81b190.

## 3. Weapon notification chain down to audio (SV)

Objects:
- The client weapon effects object has vtable **0x1432300**. Its constructor is around 0x8327a5. Prior docs place it at wrapper+0x38.
  - Its listener subobject is at **effects+0xd0** (vtable 0x14322a0). The listener's this-adjust is -0xd0.
  - effects+0x10 holds WeaponFiringData*, and effects+0x30 holds the weapon LinearTransform.
- The firing sound object is at **effects+0x104**, which is **listener+0x34**.
  - It is allocated as `new 0x44` with ctor **0x6d4280**. The call is `ctor(FiringFunctionData* = effects->[+0x10]->PrimaryFire(+0x40), &effects+0x30)`, thiscall `ret 8`, at site 0x832938. Signature: `8B 56 10 8B 4A 40 8D 56 30 52 51 8B C8 E8 ?? ?? ?? ?? EB 02 33 C0 89 86 04 01 00 00`.
  - Layout:
    - vtable 0x142528c
    - +4 FiringFunctionData*
    - +8 const LinearTransform*
    - +0xc SoundMgr* (copied from the singleton)
    - +0x14 last fire handle
    - +0x18..+0x24 handle ring
    - +0x28 ring index
    - +0x40 flags: bit0 = fired this frame, bit1 → IsFirstPerson, bit2 → IsZooming, OCCL = !(bit1)

Listener slots (vtable 0x14322a0). The per-frame dedupe bits are at listener+0x55c/+0x55d.

| Slot | Fn | Sound action | Signature | Label |
|---|---|---|---|---|
| +0x04 fire | 0x80f590 | Sends stats event (0x1a,0x15). Then **0x6eba10(fs)** plays FireSoundEffect (+0x1d0) with parameters through 0x637290 into the handle ring, then ProjectileFireSound (+0x1cc) through 0x637d50 at the muzzle transform. Then it forwards to sub-listeners slot +4. | `53 8B D9 B0 02 84 83 5C 05 00 00 75 61 08 83 5C 05 00 00` | SV |
| **+0x1c dry fire** | **0x7f9080** | `if(!(f55c&0x80)){f55c|=0x80; if(fs) 0x6ebc30(fs);}` | `B0 80 84 81 5C 05 00 00 75 12 08 81 5C 05 00 00` | SV |
| +0x38 reload begin | 0x80f640 | Calls **0x6ca270(fs)**, which is a bare `ret` (an empty native reload-sound stub). Then it sends a stats event (0x1a,0x16) and forwards to sub-listeners slot +0x38. | `57 8B F9 B0 04 84 87 5D 05 00 00 75 60 08 87 5D 05 00 00` | SV |
| +0x3c reload end | 0x80f6b0 | No sound call. Sends stats event (0x1a,0x18), then forwards to sub-listeners slot +0x3c. | prior docs | SV |
| +0x30 bolt start | 0x8024a0 | No sound call. Only forwards to sub-listeners slot +0x30. | prior docs | SV |
| +0x48 | 0x7f90a0 | `0x6ca280(fs)`, a bare `ret` | | SV |

Dry fire, **0x6ebc30** (`void __thiscall FiringSound_PlayOutOfAmmo(FiringSound*)`, plain `ret`). Signature: `81 EC D8 00 00 00 56 8B F1 8B 46 04 83 B8 94 01 00 00 00`.
```c
if (!fs->ffd->OutOfAmmoSoundEffect /*+0x194*/) return;
ParamList p; p.AddBool("IsFirstPerson", fs->flags>>1&1); p.AddBool("IsZooming", fs->flags>>2&1);
p.AddFloat("OCCL", (fs->flags&2)?0:1); AddGlobalParams(&p);
SoundMgr_PlayAtParams(fs->mgr, fs->ffd->OutOfAmmoSoundEffect, fs->xf, &p);   // fire-and-forget
```
Fire sound: **0x6eba10** (`__thiscall(FiringSound*)`, signature `55 8B EC 83 E4 F0 81 EC 74 01 00 00 53 56 8B F1 8A 46 40 A8 01`, SV). The other caller is 0x802520 → 0x6ebc20 (jmp thunk).

## 4. Reload, magazine and bolt sounds come from the animation sound map

1. The native reload-sound stub is empty (0x6ca270 = `ret`), and no reflected weapon field holds a reload, magazine or bolt sound. (SV, DUMP)
2. **Map build (SV).** This runs at client level load in the function around 0x82a399.
   - The code logs "Creating animation sound map" (0x1432238). It builds the asset name `<level path> + "/AnimationSoundMap"` (0x1432224).
   - It resolves the asset with `0x459530(name, 0, 0, TI_AnimationSoundMapData 0x1c0414c, 0)` at site 0x82a482. Signature: `53 68 ?? ?? ?? ?? 53 53 8D 44 24 30 50 8D 4C 24 50`. A typed wrapper at **0x7faa90** has signature `8B 44 24 08 8B 54 24 04 6A 00 68 4C 41 C0 01 50 6A 00 52 E8` (the TI immediate is left unwildcarded).
   - Next it calls `new 0x48` and **ctor 0x6f0a30**(AnimationSoundMapData*) (signature `F3 0F 10 05 ?? ?? ?? ?? F3 0F 10 0D ?? ?? ?? ?? 83 EC 30 53`). The result is stored at singleton+0x48.
   - The ctor walks Categories[i].AnimSound[j]. It skips entries where Sound or Animation is null. It inserts `key = 16 bytes at *(BaseAnimationAsset+4)` → `value {Guid, SoundAsset* @+0x10}` into the hash at map+0x04 (insert 0x6e3510).
   - Map layout:
     - +0x04 anim→sound hash
     - +0x20 active-instance hash keyed by owner (record +0x50 = Guid, +0x64 = handle, +0x70 = next)
     - +0x40 SoundMgr
     - +0x44 AnimationSoundMapData* (add-ref'd)
   - **Conclusion:** each animation asset maps to one SoundAsset. (SV)
3. **Play and stop (SV).**
   - Play is **0x6ee6a0**: `void __thiscall AnimSoundMap_OnAnimStart(Map*, const Guid* anim, Entity* owner, const LinearTransform* local)`, `ret 0xc`. Signature: `55 8B EC 83 E4 F0 81 EC 04 02 00 00 53 56 57 8B 7D 08`.
     - It looks up the anim GUID and returns if it is not mapped.
     - It returns if the same GUID is already active for this owner.
     - It computes `world = owner->0x610a20() (entity transform) * local` with 0x53db40.
     - It calls `h = SoundMgr_PlayAt(map->mgr, entry.sound, &world)` at site 0x6ee788 and records {owner weak ref, GUID, h}.
     - It passes **no parameters** (no IsFirstPerson).
   - Stop is **0x6eafb0**: `__thiscall(Map*, const Guid* anim, Entity* owner)`, `ret 8`. Signature: `83 EC 18 8B 44 24 20 53 55 56 8B D9 57 8D 4C 24 30`. It stops the matching handle and erases the record.
   - **0x6ee970** removes all records for an owner when the entity goes away. Signature: `83 EC 18 8B 44 24 1C 53 55 56 57 8D 54 24 2C 8B D9`.
4. **Animation-event dispatch (SV, with Ant internals INF).**
   - Ant animation listener base vtable: **0x1431540** (`[1]` OnAnimStart(const Guid*, int cbId), `[2]` OnAnimStop(...), both `ret 8`).
   - Registration: `0x7f0ef0(animObj, listener)` → `animObj+0x54 → 0xf677b0` pushes the listener onto the vector at animatable+0xc0.
   - The common animated-object ctor is 0x82dd10. It registers its own +8 listener (vtable 0x1431b58): **0x80fec0/0x80ff10**, which dispatch on arg2 (INF: CallbackId) through the hash at +0x90 to callback-object slot [0] or [2].
     - 0x80fec0: `@+0x23 8B 4E 08 8B 56 04 8B 44 24 04 3B 04 8A 5E 74 09 8B 48 04 8B 01 8B 10 FF D2`
     - 0x80ff10: `@+0x23 ... 74 0A 8B 48 04 8B 01 8B 50 08 FF D2`
   - **Soldier:** around 0x8ba3a3/0x8ba550, the soldier creates two animated objects (via 0x7b47d0 → 0x82dd10). For each one it allocates a 0x60 **AnimSound listener** (vtable 0x1435214; +4 soldier, +0x10 identity transform, +0x50 view flag = 1 at 0x8ba3f3 and 0 at 0x8ba5a0) and registers it with 0x7f0ef0.
     - **0x881650** OnAnimStart (`56 8B F1 8B 46 04 8A 88 75 04 00 00 80 E1 01 38 4E 50 75 1A`): `if ((soldier[0x475]&1)==this[0x50]) AnimSoundMap_OnAnimStart(map, anim, soldier, &this[0x10]);`
     - **0x881690** OnAnimStop (`... 75 15`) calls 0x6eafb0 in the same way.
     - Which view (1p or 3p) each flag value means is INF.
   - Other users of the same map are cutscene object entities (0x878110 at listener+0xd0 → 0x6ee6a0; signature `@+0x6c 8B 48 48 81 C6 30 FF FF FF 56 52 E8 ?? ?? ?? ?? 5E 8B E5 5D C2 08 00`) and ClientBangerEntity (0x8c5b00).
   - The Ant-internal loop that calls listener slot [1] was not pinned (INF).
5. **Answer to question 2 (INF).**
   - Reload sounds are per-animation-clip SoundAssets, not one sound per weapon and not CallbackId events.
   - A magazine reload clip carries one sound, with magazine-out and magazine-in baked into the template's timing. They are not separately addressable unless the asset splits the clip.
   - Separate clips (bolt/pump cycle, shotgun shell insert loop, reload-empty vs tactical) each get their own mapped sound, and each fires again whenever that clip starts.
   - Which clips exist per weapon must be read from the AnimationSoundMapData at runtime (category Name and the Animation asset Name at Asset+0xc).

## 5. Cheapest injected-DLL recipe (call on the game/client-update thread, INF)

- **Empty click (native, parameter-correct):**
  1. `fs = *(void**)(effects + 0x104)`, where `effects` is the effects object (vtable 0x1432300).
  2. Call `((void(__thiscall*)(void*))0x6ebc30)(fs)`.
  3. Alternatively call listener slot +0x1c (0x7f9080 on effects+0xd0). That path respects the per-frame dedupe bit.
- **Any SoundAsset at an arbitrary transform:** call `0x637d50(mgr, snd, &xf)`, then `0x633cb0(mgr, h, 0)` to cut it. Get `mgr` from `fs+0xc` or `*(*(0x1571410)+0x70)`. For 1p/3p-correct variants, use `0x637290` with a ParamList built through 0x6300f0/0x630030/0x81b190, mirroring 0x6ebc30.
- **Magazine-out/seat and rack sounds:**
  1. Read `map = *(void**)0x1571420`. If it is NULL, no level is loaded.
  2. Read `data = *(AnimationSoundMapData**)(map+0x44)`.
  3. Walk `Categories(+0x18; storage at +0x20..+0x24)` → `AnimSound(+0xc; storage at cat+0x14..+0x18)`, 8-byte {Sound, Animation} entries.
  4. Select entries by the Animation asset name. This needs String layout validation (INF).
  5. Simpler and safer: hook **0x881650** (or 0x6ee6a0) for one native reload per weapon, and cache `anim GUID → entry.sound` by looking it up in the map. Then replay through `0x637d50(map->mgr, sound, &weaponXf)`. Or call `0x6ee6a0(map, &guid, soldier, &identity)`, which dedupes per owner and must be paired with 0x6eafb0.
  6. Suppressing native reload audio during VR reloads means filtering 0x881650 on reload GUIDs.
- **From SoldierWeaponData** (`+0x98 → +0x40 → FiringFunctionData`): empty click is +0x194, shot is +0x1cc / +0x1d0. **No reload or bolt sound is reachable from weapon data.** It comes only from the level's AnimationSoundMap keyed by the 1p animation clips in `WeaponStates[i].AnimTree1p` (+0x88 array, +0xb8).

## Gaps

- The Ant dispatcher loop that calls listener slot [1] is unpinned.
- The meaning of the second argument (CallbackId) and of soldier+0x475 bit0 (1p or 3p) is INF.
- The String layout and how `*(anim+4)` derives the GUID are INF.
- The handle lifetime and the pruning of map+0x20 records after a sound finishes are unverified.
- The thread affinity of SoundMgr calls is INF. Native callers are client-update code.
- Nothing has been checked in game or with a headset.
