> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (build 795745, x86) weapon give/replace path: static RE report

> In this report **SV** means read in the static disassembly. It is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md), not that page's repo-code SV. Nothing here
> has run in the game.

Source: `BFBC2Game.exe`, image base 0x400000. Tools: capstone and pefile through [`tools/re/bc2re.py`](../../tools/re/bc2re.py). Static disassembly of the owner's staged exe (`c603f686...`), 2026-10-08; nothing was run in the game.
Labels: **SV** = I read the disassembly myself. **INF** = inferred (a name, a purpose, or a link I did not trace end to end).
Every signature below is unique in `.text`+`ctr` unless a note says otherwise. In the signatures, `??` replaces only call rel32 operands and absolute image addresses.
ABI notation: `thiscall` means `ecx` = this, the args are pushed right to left, and the callee pops them (`ret N`).

---------------------------------------------------------------------------
## G1: the native server give/replace function

### 0x6bcf80 `ServerSoldierEntity::SetWeaponInSlot` (name INF; behaviour SV)
```
sig: 83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84 BE 00 00 00
ABI: thiscall, ret 0x10 (4 dword args), returns the new server item* (eax) or 0
  ecx  = ServerSoldierEntity*  (has +0x2b4 inventory, +0x2b8/+0x2bc item vector, +0x220 player)
  arg1 = SoldierWeaponData*  (NOT the SoldierWeaponAsset; callers pass asset->[+0x50] after an
         IsA(SoldierWeaponData TI 0x1bfca60) check). NULL = remove the weapon in the slot.
  arg2 = uint slot
  arg3 = bool selectNow  (also forced to 1 internally if the replaced item was the selected one)
  arg4 = bool forceSlot  (0: if the soldier already holds the same data in any slot, reuse that
         slot index; 1: use arg2 unless that slot already holds the same data)
  There is NO ammo argument; ammo is applied by the caller afterwards (see below).
```
Pseudo-C (SV):
```c
Item* SetWeaponInSlot(Soldier* s, SoldierWeaponData* d, uint slot, bool select, bool force) {
  vector<Item*>& v = s->items;                       // +0x2b8/+0x2bc
  if (d) { /* slot de-dup by data, uses item->data (0x534080 = [item+4]) */ }
  if (slot >= v.size()) v.resize(slot+1, nullptr);    // 0x6b5e60 (insert n at end)  @0x6bd096
  if (!d && !v[slot]) return 0;
  if (Item* old = v[slot]) {                          // @0x6bd0e0
    old->firing(+0x10)->vf1(&s->+0x164);              // unregister
    old->firing->vf1(old->effects ? old->effects+0xd0 : 0);
    FiringStop(old->firing, 0) /*0x6e1eb0*/;  0x6cd370(old->firing);
    old->effects(+0xc)->vf0x30/4;        // undeploy (INF)
    old->vdtor(1);  v[slot] = 0;                      // @0x6bd150  C7 04 98 00 00 00 00
    if (s->inventory->selected(0x6ce960) == slot) { if (d) select = 1; else Inv_Select(s->inventory,0,0); }
  }
  if (!d) goto notify;
  Item* n = new(0x24) Item(d, s);                     // 0x561b20      @0x6bd18f
  v[slot] = n;                                        // OVERWRITE by index @0x6bd1b3 (89 3C 9A)
  n->firing->vf0(&s->+0x164);  n->firing->vf0(n->effects+0xd0);
  FiringReset(n->firing) /*0x6dd580: fill ammo*/;
  if (s->player) { SetMagMult(n->firing, 0x610680(d,player)) /*0x6d6ab0*/;
                   SetMagsMult(n->firing, 0x6106a0(d,player)) /*0x6d6bb0*/; }
  0x534350(n->effects, s->player);
  if (select) { 0x6e6d20(n->firing); n->effects->vf0x2c/4;   // deploy (INF)
    if (s->inventory->selected == slot) { if (player) player->0x55b800(d+0xc); }
    else Inv_Select(s->inventory, slot, 0) /*0x6d7c80*/; }
notify:
  post message {vtbl 0x1422378 (type id 0x17), soldier, *[s+0x90], slot} via
      ServerCtx(0x547bf0 -> 0x1566b18)->[+0x18]->vf[0x1c/4](0x14, &msg);   // @0x6bd2a9
  GhostDirty(&s->+0x16c, 0x10) /*0xc1d8d0*/;                               // @0x6bd2e3
  return n;
}
```
Dirty-bit call site: `6A 10 8D 8E 6C 01 00 00 E8 ?? ?? ?? ?? 8D 4C 24 10 E8 ?? ?? ?? ??` (0x6bd2e3) SV.

All 18 direct callers (SV): 0x5555f4/0x555616/0x55564f (in 0x555530), 0x6bd3f5/0x6bd5df/0x6bd897/0x6bd8e0 (spawn, G4),
0x6bd972/0x6bd9c1 (0x6bd940: slot 9, select=1, force=1), 0x6bda77 (0x6bd9e0, vtable-referenced), 0x6c04b8 (0x6c0240),
0x6c149b (soldier vfunc 0x6c0eb0), 0x6c5f3e (pickup, below), 0x6c629b/0x6c62b9/0x6c6301/0x6c631f, 0x706b55.

### Pickup interact path (SV unless marked)
1. The player interaction handler 0x662ef0 (corrected by the lookup pass: it starts at 0x662f20) is referenced by vtable 0x141dfc4. I took that start from the CC-padding walk-back and did not confirm it. Inside it (INF: the use key):
   `ecx = pickup(0x69fb20 cast); push player->vf[0x30/4]() /*soldier*/; call 0x6c5cd0` at 0x6632e7.
   Call-site sig: `8B 4C 24 0C 50 E8 ?? ?? ?? ?? 8B C8 E8 ?? ?? ?? ?? 8B 4C 24 0C E8 ?? ?? ?? ??` (0x6632db).
   Other callers of 0x6c5cd0: 0x6632e7 and 0x663436 (both in 0x662ef0), 0x572ba9 (in 0x5727a0), and 0x6c73fa. 0x6c73fa is ServerPickupEntity vfunc slot 6 (0x6c73b0), an event handler: on event hash 0xc98ed6f1 it gives to the soldier of WeaponPickupEntityData.Soldier (+0xa0) (INF: a scripted give).
2. **0x6c5cd0 `ServerPickupEntity::Pickup(ServerSoldierEntity*)`**: thiscall, ret 4.
   `sig: 81 EC C4 00 00 00 53 55 8B AC 24 D0 00 00 00 56 57 8B BD 20 02 00 00`
   - The pickup copies its own entry vector (`this+0x34..0x3c`, 0x20-byte entries) to a local.
   - If `!ContentIsStatic(+0x99)` it clears the vector. If `!UnspawnOnPickup(+0x95)` it pushes the soldier's existing items back into the pickup with 0x6b7300. This is how the dropped/swapped gun ends up in the pickup.
   - For each entry e it calls (call site 0x6c5f2d, sig `8B 4C 24 18 8B 54 24 30 51 8B 4F F4 52 51 50 8B CD E8 ?? ?? ?? ??`):
     ```c
     item = SetWeaponInSlot(soldier, e.data, e.slot,
               select = e.data && e.slot==min(entry slots) && !(e.loaded==0 && e.reserve==0),
               force  = PickupEntityData.ForceWeaponSlotSelection (+0x90));
     if (item) { F = item->firing (+0x10);
        if (e.loaded != -1) { 0x6cd850(F, e.reserve) /*+0x80*/; 0x6cd520(F, e.loaded) /*+0x7c*/;
           if (ammoEmpty && selected==e.slot && !SwitchToPrimaryWhenOutOfAmmo/Allow... && !s->+0x270)
               Inv_Select(s->inventory, 0, 0); }
        if (e.f18 > 0) 0x6cd8c0(F, e.f18); }
     ```
     Ammo-apply sig (0x6c5f68): `83 3F FF 8B 5B 10 74 59 8B 47 04 50 8B CB E8 ?? ?? ?? ?? 8B 0F 51`
3. Entry layout (0x20 bytes, SV from the builder, the serializer 0x699440 and the deserializer 0x6b70b0):
   `+0 SoldierWeaponData*, +4 WeaponSlot, +8 AltWeaponSlot, +0xc LinkedToWeaponSlot, +0x10 loadedAmmo (-1 = keep native full), +0x14 reserveAmmo, +0x18 float, +0x1c bool(=1)`.
4. **0x6309e0** builds the entries from WeaponPickupEntityData (TI 0x1c00f9c check). It is thiscall on the pickup+0x2c subobject (pickup + 0x2c + 8 = +0x34 vector), ret 4, and is called from 0x633289. It walks Weapons (+0xa4, stride 0x18):
   `data = WPD.Weapon(+0xc)->[+0x50]` (IsA SoldierWeaponData), `slot=+0x10, alt=+0x14, linked=+0`.
   If `MaxAmmo(+8) > 0` and `FFD.Ammo.MagazineCapacity != -1`, then `total = 0x4e4590(MinAmmo,MaxAmmo)` (INF: random in range), `loaded=min(total,MagCap)`, `reserve=total-loaded`.
   `sig: 83 EC 38 53 57 8B 7C 24 44 85 FF 8B D9 89 5C 24 0C 0F 84 9C 01 00 00`
   0x6b7300 (`sig: 83 EC 20 55 56 8B 74 24 2C 57 33 FF 3B F7 8B E9 74 0D 8B CE E8 ?? ?? ?? ??`) builds an entry from a live server item (thiscall pickup, ret 0x14: item, slot, alt, linked, keepAmmo).

---------------------------------------------------------------------------
## G2: writers of the item vectors
Server soldier vector `+0x2b8 begin / +0x2bc end / +0x2c0 cap` (vector<Item*>) (SV):
| site | kind |
|---|---|
| 0x6c4207..0x6c4219 in the ServerSoldierEntity ctor 0x6c4040 | zero init. sig `89 9E B4 02 00 00 89 9E B8 02 00 00 89 9E BC 02 00 00 89 9E C0 02 00 00` |
| 0x6bd386 → 0x6bcbe0 (spawn) | resize to Weapons.count with nulls (append through 0x6b5e60, or shrink). Call-site sig `8D 8E B8 02 00 00 55 89 5C 24 18 E8 ?? ?? ?? ?? 68 5C 01 00 00 E8 ?? ?? ?? ??` |
| 0x6bd096 → 0x6b5e60 inside 0x6bcf80 | grow when slot >= size (append nulls up to slot+1). sig `8D 54 24 58 52 2B C8 51 57 8B CD E8 ?? ?? ?? ?? EB 27 8D 04 8A 8B CF` |
| 0x6bd150 | v[slot]=0 on removal. sig `8B 45 00 C7 04 98 00 00 00 00 8B 8E B4 02 00 00 E8 ?? ?? ?? ?? 3B C3` |
| 0x6bd1b3 | **overwrite by index** v[slot]=new. sig at 0x6bd18f `6A 24 E8 ?? ?? ?? ?? 83 C4 04 85 C0 74 11 8B 4C 24 4C 56 51 8B C8` |

0x6bcbe0 and 0x6b5e60 are shared vector template instances (95 and 70 identical copies), so match them by call site, not by body.
The other paired +0x2b8/+0x2bc writes (0x60761c in 0x6075e0, 0x7cc1b1 in 0x7cc190, 0x7d68d0 in 0x7d6890, 0x7e196b in 0x7e1910) are not in the server soldier ctor and not in any function that touches the soldier inventory/items. I conclude they belong to other classes (INF; I did not type them).

Client soldier vector `+0x260/+0x264/+0x268` (SV):
| site | kind |
|---|---|
| 0x8c2d34 in the ClientSoldierEntity ctor 0x8c2bb0 | zero init. sig `89 9E 60 02 00 00 89 9E 64 02 00 00 89 9E 68 02 00 00 83 CA FF 89 9E 70 02 00 00` |
| 0x8ba94e → 0x8b9a50 (spawn init) | resize to Weapons.count |
| 0x8ba9f3 | overwrite by index in spawn. sig (0x8ba9e9) `8B 86 60 02 00 00 8B 5C 24 18 89 3C A8 8B 7C 24 1C 83 C5 01 3B EF` |
| 0x8baae6 → 0x8b9a50 in 0x8baab0 | grow to slot+1 |
| 0x8bac00 / 0x8bac60 in 0x8baab0 | zero, then **overwrite by index**. sig (0x8bac59) `8B 4D 00 8B 54 24 24 89 3C 99 C6 02 01 80 BE B8 05 00 00 00 74 07` |
| 0x8baed6.. in 0x8bad10 | resize/truncate to the replicated count (0x8b1f60 append / erase tail). sig `8B B7 64 02 00 00 8B 8F 60 02 00 00 81 C7 60 02 00 00 8B C6 2B C1` |

---------------------------------------------------------------------------
## G3: item and wrapper constructors
**Server item**: `new(0x24)` → **0x561b20** (thiscall, ret 8: arg1 SoldierWeaponData*, arg2 ServerSoldierEntity*). SV.
`sig: 8B 44 24 08 53 55 56 8B F1 33 ED 57 8B 7C 24 14 89 46 08 C7 06 ?? ?? ?? ??`
- Layout: `vtbl 0x14172f0; +4 data; +8 owner soldier; +0xc effects; +0x10 firing; +0x14..+0x1c 3 dwords (passed to 0x55c150); +0x20 u16 instance id = ++g_u16[0x1566608]`.
- Firing: `new(0xac)` → 0x6f1780(WeaponFiringData* = data+0x98, data+0xc).
- Effects: `0x55c150(data->Effects(+0x7c), data+0x98, firing, &item+0x14)` (cdecl, 4 args).
- The vtable has no TypeInfo getter; there is no reflection type and no RTTI COL (vtbl[-1]=0). The class name is unknown. It is called "ServerSoldierWeapon" here (INF).
- Getter 0x534080 `8B 41 04 C3` returns item->data.

**Client wrapper**: `new(0x64)` → **0x837ec0** (thiscall, ret 0x10). SV.
`sig: 83 EC 20 8B 44 24 24 53 55 56 8B F1 C7 06 ?? ?? ?? ?? 89 46 04 57`
- Args: SoldierWeaponData*, [csoldier+0x250], [csoldier+0x180], 0x7f0bc0(csoldier->vf0x160/4).
- Layout: `vtbl 0x1432020; +4 data; +0x28 (new 0x80); +0x2c..+0x44 accessors 0x7efca0..0x7efd00; +0x38 effects; +0x54 u16 replicated instance id; +0x58=-1; +0x5c (new 0x40)`.
- The firing copies +0x3c/+0x40 are NOT made in the ctor. They are created lazily by 0x7f8be0 as two `new(0xac)`+0x6f1780(data+0x98, data+0x40), reached through 0x893470 → 0x7f8be0 (SV), for the local/owned soldier (INF).
- No TypeInfo or RTTI; name unknown (INF "ClientSoldierWeapon").
- Soldiers: ServerSoldierEntity ctor **0x6c4040** (size 0x3d0, primary vtable 0x1424850, TypeInfo getter 0x68d8c0 → TI 0x1bf2390). ClientSoldierEntity ctor **0x8c2bb0** (size 0x640, vtable 0x1437950). ServerPickupEntity ctor 0x6ad610 (vtable 0x1424358, TI 0x1bf2710, size 0x180). All SV.

---------------------------------------------------------------------------
## G4: spawn path
- **Server 0x6bd350** (thiscall, no args), called from 0x6c0f43 (soldier vfunc 0x6c0eb0 init). SV.
  `sig: 83 EC 0C 53 55 56 8B F1 57 8B 7E 0C 8D 44 24 18 50 8B CF E8 ?? ?? ?? ??`
  ```c
  SoldierEntityData* sd = this->data(+0xc);
  arr = sd->Weapons (+0x1b0, accessor 0x13b5080; header: count=[hdr+0xc], elems=[hdr+0x10]);
  items.resize(count, 0)                                  // 0x6bcbe0
  inventory(+0x2b4) = new(0x15c) Inventory(sd->WeaponSwitching(+0x180), &this->+0x168 /*listener*/); // 0x6ec700
  for i<count: d = IsA(arr[i]->[+0x50], SoldierWeaponData) ? .. : 0;
               SetWeaponInSlot(this, d, i, 0, 0);          // SAME creator 0x6bcf80 (@0x6bd3f5)
  for each item: FiringReset(item->firing);                // 0x6dd580
  sel = items[inventory->selected] ?: items[0]; 0x6e6d20(sel->firing); sel->effects->vf0x2c/4;
  ```
  Unlocks: **0x6bd480** (called from 0x6c1029) walks WeaponUnlocks (+0x1a0, accessor 0x13b50a0, 12-byte entries: slot +0, asset +4, IsBundled +8, IsSelectable +9). It filters with player 0x6c97d0 and also walks the +0x2a8 list. Then it calls `SetWeaponInSlot(d, slot, 0,0)` / `(d, slot, slot==0, 0)` (0x6bd5df/0x6bd897/0x6bd8e0). SV.
- **Client 0x8ba910** (called from 0x8be722). It uses the same Weapons accessor, resizes +0x260, and does `new(0x64)`+0x837ec0 per weapon with an overwrite by index. It then creates the client listener object +0x21c (vtbl 0x143520c, size 8) and **two** inventories `+0x248` and `+0x24c` (each new(0x15c) 0x6ec700(sd+0x180, &csoldier+0x1c0)). SV. The client does not call 0x6bcf80.
- Inventory ctor 0x6ec700 (thiscall, ret 8): `+0 vtbl 0x1425b88, +4 WeaponSwitchingData*, +0x148 listener, +0x14c selected=0, +0x150 prev=0`. SV.

---------------------------------------------------------------------------
## G6: firing object (size 0xac, shared server/client)
**ctor 0x6f1780** (thiscall, ret 8; arg1 WeaponFiringData*, arg2 a ptr → +0x10). SV.
`sig: 0F 57 C0 8B 44 24 08 F3 0F 10 0D ?? ?? ?? ?? 53 33 DB 56 8B F1 C7 46 04 ?? ?? ?? ??`
- Sets: `+0 vtbl 0x1425814, +4 vtbl 0x1425804, +8 WeaponFiringData*`, `+0xc = &WFD.PrimaryFire(+0x40)->Ammo (FiringFunctionData+0x170 = AmmoConfigData*)`, `+0x74 = +0x78 = 1.0f`, `+0x7c = +0x80 = +0x84 = 0`, `+0x98 = -1` (capacity override), `+0x9c = 1.0`.
- The ctor seeds no ammo.

**Seed: 0x6dd580** (`FiringReset`, thiscall, no args). Called by the give right after creation, and by spawn for all items. SV.
`sig: 51 0F 57 C0 33 D2 56 8B F1 8B 46 08 89 56 3C 89 56 40 89 56 44 F3 0F 11 46 50`
```c
A = this->ammoCfg(+0xc);
cap = (this->+0x98 != -1) ? this->+0x98 : (A->MagazineCapacity(+0x14) == -1 ? -1 : round(this->+0x74 * A->MagazineCapacity));
this->+0x7c = cap;                                              // loaded
if ((this->+0xa8 & 8) || A->NumberOfMagazines(+0x10) == -1 || (m = round(this->+0x78 * A->NumberOfMagazines)) == -1)
     this->+0x80 = -1;
else this->+0x80 = (m - 1) * cap;                               // reserve
this->+0x84 = WFD.PrimaryFire->[+0xd8]; +0x88=1; +0x8a=0; +0x8c=+0x90=0;
```
- Multiplier setters, called by the give when there is a player. Values come from 0x610680 and 0x6106a0 (player specialization lookups through 0x6c9650, keyed by WeaponClass +0x84) (INF semantics).
  - **0x6d6ab0** (`sig 56 8B F1 D9 46 74 D9 44 24 08 D9 C0 D9 CA DF EA DD D9 9F F6 C4 44`): sets +0x74 and recomputes +0x7c and +0x80 with the formula above. It does nothing if the value is unchanged.
  - **0x6d6bb0** (`sig 56 8B F1 D9 46 78 D9 44 24 08 D9 C0 D9 CA DF EA DD D9 9F F6 C4 44`): sets +0x78 and recomputes +0x80. SV.
- Direct pickup setters:
  - 0x6cd520 `8B 44 24 04 89 41 7C C2 04 00` sets +0x7c = arg.
  - 0x6cd850 (`51 56 8B F1 F6 86 A8 00 00 00 08 75 2A 8B 46 0C 8B 40 10 83 F8 FF`) sets +0x80 = arg only when `!(+0xa8&8)`, NumMags != -1 and round(+0x78*NumMags) != -1. SV.

---------------------------------------------------------------------------
## G7: 0x6d7c80 `Inventory::SetSelected` (name INF)
`sig: 80 7C 24 08 00 56 57 8B 7C 24 0C 8B F1 74 18 8B 8E 48 01 00 00 8B 01`
ABI: thiscall, **ecx = inventory object** (server: `soldier+0x2b4`; client: `+0x248`/`+0x24c`), ret 8, args (int slot, bool validate). SV:
```c
if (validate && !listener(+0x148)->vf1(slot)) slot = this->selected;
if (slot != this->selected(+0x14c)) { this->+0x14c = slot;
   if (listener) { this->+0x150 = prev; listener->vf0(prev, slot); } }   // deploy/switch callback
```
- It writes **inventory+0x14c**, not soldier+0x14c. 0x6ce960 (`8B 81 4C 01 00 00 C3`) reads it. The "client soldier +0x14c selected slot" in the background notes is probably the inventory's +0x14c. The server soldier's own +0x14c is a different field: the pickup function saves and swaps it at 0x6c6090.
- Deploy callbacks (vf0 of the listener):
  - Server **0x6b1770**: the soldier+0x168 subobject, vtable 0x1424718. Item vector = this+0x150. It stops the old item (0x6e1eb0, effects vf 0x30), then deploys the new one (effects vf 0x2c, 0x5343b0, 0x6e6d20, 0x6d6e80, player->0x55b800(data+0xc)).
    sig `8B 54 24 04 53 56 8B F1 8B 86 50 01 00 00 8B 1C 90 85 DB 8B 4C 24 10`
  - Client **0x8a0e80**: the soldier+0x1c0 subobject, vtable 0x1437894. Wrapper vector = this+0xa0.
    sig `53 8B D9 8B 83 A0 00 00 00 8B 4C 24 0C 55 8B 2C 88 85 ED 0F 84 4C 01 00 00`
  Both SV.

---------------------------------------------------------------------------
## G8: how the client gets the new wrapper
**Ghost replication, not a direct call.** The server give marks the soldier ghost dirty (`0xc1d8d0(&soldier+0x16c, 0x10)`, SV) and posts a server message (vtbl 0x1422378, type 0x17, channel 0x14; INF purpose "weapon changed").

On the client, ClientSoldierEntity vfunc slot 80 **0x8bb0c0** reads the ghost state (`[stream+0x1004]+0x40`). If the weapon state changed (0x6ce700 compare) or flag bit 0 is set, it calls:
- **0x8bad10** (thiscall, ret 8; arg1 = 0x40-byte weapon state, copied to csoldier+0x5f4) SV:
  `sig: 83 EC 1C 53 8B 5C 24 24 8B 03 D1 E8 55 8B E9 83 E0 0F 89 6C 24 20`
  ```c
  n = (state->dw0 >> 1) & 0xf;
  for i<n: id = u16 state[4+2i]; d = id ? 0x5deb30(*0x156d500, id) : 0;  // net id -> SoldierWeaponData (INF)
           inst = u16 state[0x22+2i]; force = (w=vec[i]) && w->+0x54 && inst && w->+0x54 != inst;
           w = 0x8baab0(this, d, i, ?, force, &changed); if (w) w->+0x54 = inst;
  vec.resize(n);
  ```
- **0x8baab0** `ClientSoldier::SetWeaponInSlot` (thiscall, ret 0x14). Args: data, slot, (unused), force, bool* changed. SV. It is the client twin of 0x6bcf80:
  - It grows the vector and keeps the current wrapper if the data is the same and not forced.
  - Otherwise it unregisters, stops and deletes the old wrapper (vdtor(1)) and nulls the slot.
  - It then builds `new(0x64)`+0x837ec0, overwrites v[slot], and sets *changed=1.
  - For the local or owned soldier (0x89bbd0 or flag 0x114&2) it creates the firing copies (0x8933e0, 0x893470 → 0x7f8be0) and resets them with 0x6dd580.
  - It calls `0x817490(effects, player)` (INF).
  `sig: 8B 44 24 14 53 8B 5C 24 0C 55 56 8B F1 8D AE 60 02 00 00 C6 00 00`
- Link of the server item `+0x20` u16 instance counter to the replicated `state[0x22+2i]` and to the client `wrapper+0x54`: INF. I did not locate the server-side ghost writer of this 0x40-byte state. The client side and the server counter are SV.

---------------------------------------------------------------------------
## Gaps / cautions
- Calling 0x6bcf80 from a hook re-enters soldier and network state. It deletes the old item (vdtor) and posts a server message, and the client learns about the change only through ghost replication. Native callers are the server simulation/interaction paths. I have not verified which thread or tick 0x662ef0 runs on.
- Ammo: the give itself always produces a full magazine and full reserve (0x6dd580 plus the multipliers). Only the pickup path applies custom ammo afterwards (0x6cd850, 0x6cd520).
- Names of the item, wrapper and inventory classes are INF (no reflection or RTTI).
- The function start of 0x662ef0 comes from the padding heuristic and a vtable ref (0x141dfc4); its body is large, and the call sites inside it are SV.
- Not done: the server weapon-state serializer, the exact semantics of 0x55b800/0x534350/0x6e6d20/0x6cd370 (INF from usage), and the meaning of slot 9 in 0x6bd940.
