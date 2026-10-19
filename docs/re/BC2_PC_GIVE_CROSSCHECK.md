> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 give path and feel hooks: PC cross-check

> PC session, 2026-10-08. Static reads only, on a copy of the installed
> `BFBC2Game.exe` build 795745 (SHA-256 `c603f686...`, image base 0x400000).
> Disassembly with capstone over the mapped image; Ghidra 12.1.3 headless
> auto-analysis ran on the same copy. Nothing was written to the game or its
> install, and no exe bytes are in git. Evidence level for everything here is
> **DIS** (static disassembly, as defined in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md)) unless marked otherwise. No
> native operation is enabled by this page.

It checks the research thread's cloud results (PR #4) independently and
answers the coordinator's cross-check asks for `0x6bcf80`.

## Give / replace: `0x6bcf80`

**Signature is unique.** `83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84 BE 00 00 00`
matches once in the mapped image, at `0x6bcf80`.

**ABI confirmed.** `thiscall`, `ecx` = server soldier, four stack arguments,
callee cleans (`ret 0x10` at both exits `0x6bd0dd` and `0x6bd302`):

```c
ServerItem* __thiscall Give(ServerSoldier* s, SoldierWeaponData* data,
                            uint32_t slot, bool selectNow, bool forceSlot);
```

Pseudo-C from the disassembly:

```c
items = &s->items;                       // vector<ServerItem*> at +0x2b8/+0x2bc
if (data && !forceSlot) {                // forceSlot == arg4
    // reuse an existing slot that already holds this data
    for (i = 0; i < items.size(); ++i)
        if (items[i] && items[i]->data == data) { slot = i; break; }
} else if (data && forceSlot) {
    // keep `slot` unless it already holds this data; then search as above
}
if (slot >= items.size()) items.resize(slot + 1);   // 0x6b5e60 / memmove path
if (!data && !items[slot]) return NULL;
if (old = items[slot]) {
    old->firing->vfunc1(&s->+0x164); ...  // detach firing object from soldier
    old->vtbl[0](old, 1);                 // scalar deleting destructor: freed
    items[slot] = NULL;
    if (s->inventory(+0x2b4)->selected(+0x14c) == slot)
        data ? (selectNow = true) : SetSelected(inv, 0, false);   // 0x6d7c80
}
if (data) {
    it = operator new(0x24);              // 0x420db0
    ServerItem_ctor(it, data, s);         // 0x561b20
    items[slot] = it;
    it->firing->vfunc0(&s->+0x164);        // attach
    FillAmmo(it->firing);                  // 0x6dd580
    ... multipliers via 0x6d6ab0 / 0x6d6bb0 when s->player (+0x220) is set
    if (selectNow) { 0x6e6d20(it->firing); ...; select slot via 0x6d7c80(slot, false)
                     or 0x55b800 on the player when the slot is already current }
}
post soldier event (vtable 0x1422378, type 0x14); mark s+0x16c dirty bit 0x10;  // 0xc1d8d0
return it;                                // NULL when data == NULL
```

Argument meanings, corrected from the cloud note:

- `selectNow` (arg3) selects the new weapon. The function also forces it on
  when the replaced item was the selected one.
- `forceSlot` (arg4) controls whether the call reuses a slot that already
  holds the same data. With `forceSlot` false, a weapon already owned is
  refilled in its existing slot instead of being duplicated into `slot`.
- `data == NULL` removes and frees the item in `slot`; if it was selected,
  selection falls back to slot 0.

**Ownership.** The soldier owns the item. It is allocated with
`operator new(0x24)`, stored into the soldier vector, and later freed by the
next give into that slot through `vtbl0`. The returned pointer is
borrowed: never free it, and never keep it past another give, a death or a
respawn. A caller's pointer to the *previous* item in that slot dangles after
the call.

**Server item layout** (ctor `0x561b20`, vtable `0x14172f0`, size 0x24):

| Offset | Field |
| --- | --- |
| +0x00 | vtable `0x14172f0` |
| +0x04 | `SoldierWeaponData*` (stored raw; the first 40 ctor instructions take no reference) |
| +0x08 | owning server soldier |
| +0x0c | effects object (`ServerWeaponFiringEffects`) |
| +0x10 | firing object: `operator new(0xac)`, ctor `0x6f1780(data+0x98 WeaponFiringData, ...)`, vtable `0x1425814` |
| +0x20 | u16 serial from global `0x1566608`, incremented per item |

The firing object ctor (`0x6f1780`) zeroes loaded (+0x7c), reserve (+0x80)
and +0x84, sets both multipliers (+0x74, +0x78) to the 1.0 constant at
`0x14d94f0`, and `0x6dd580` then fills ammo. That answers ask E (G6).

**Callers.** 18 direct calls: `0x5555f4`, `0x555616`, `0x55564f` (checkpoint
restore, below), `0x6bd3f5` (spawn loop), `0x6bd5df`, `0x6bd897`, `0x6bd8e0`,
`0x6bd972`, `0x6bd9c1`, `0x6bda77`, `0x6c04b8`, `0x6c149b`, `0x6c5f3e`
(ground pickup), `0x6c629b`, `0x6c62b9`, `0x6c6301`, `0x6c631f`, `0x706b55`.

- **Spawn** (`0x6bd3c0..0x6bd3ff`): per kit entry `e`, `data = e+0x50` after
  an `isKindOf(SoldierWeaponData TI 0x1bfca60)` check (`0x4e6100`), then
  `Give(data, index, false, false)`.
- **Ground pickup** (`0x6c5f3e`): pickup entry stride uses `[-0x10]` data,
  `[-0xc]` slot, `[0]` loaded override (-1 = keep native fill), `[+4]`
  reserve. Calls `Give(data, slot, selectNow, pickup+0x90)`, then
  `0x6cd850(reserve)` and `0x6cd520(loaded)` on `it->firing`. Matches the
  cloud result.

## Selection setter `0x6d7c80` (ask F)

`thiscall` on the server inventory (soldier+0x2b4), `(int slot, bool validate)`,
`ret 8`. If `validate`, it asks `inv+0x148`'s vfunc +4 whether `slot` can be
selected and keeps the current slot if not. If the slot changes, it writes
current at +0x14c and previous at +0x150, and notifies `inv+0x148` vfunc 0 with
`(previous, new)`. The deploy animation is presumably behind that vfunc; that
is not traced here.

Small getters: `0x6ce960` = `inv+0x14c` (selected slot); `0x534080` = `item+4`
(data).

## Weapon by name (G5)

Confirms the research thread's find. `0x555530` (called from `0x5612b4`) is the
single-player checkpoint restore. It reads the save attribute
`CurrentSoldierWeaponList` (string at `0x1417fd0`), splits it on `"`
(`0x140c104`), and for token *i*:

1. Empty token: `Give(NULL, i, false, false)` (remove).
2. `(*0x154c444)->vfunc0x14` returns false (not loaded): remove.
3. Otherwise a typed resource request through `0x459530`, which tail-jumps to
   `0x449810`:

   ```c
   // ref = {DataContainer* ptr; Handle* h = sentinel 0x14f267c}; ctor 0x42ad20
   void __thiscall Request(ResourceRef* ref, const char* name,
                           int a2, int a3, TypeInfo* type, bool keepHandle); // ret 0x14
   // h = (*0x154c448)->vfunc[0xac](type, name, a2, a3); h->vfunc0()  (addref)
   // keepHandle ? h->vfunc0x2c : ref->ptr = h->vfunc[0x40]()
   ```

   The restore calls it with `(name, 0, 0, 0x1bfca60, false)`, then
   `Give(ref.ptr, i, false, false)`.
4. It then releases the request at once: `0x43d5a0(ref, false)`, which calls
   `h->vfunc4()` and resets the handle to the sentinel.

The native path does **not** hold the resource ref for the item's lifetime. The
item stores `data` raw, so the loaded partition, not the request, keeps the
data alive. For a harness give, the safe choice remains to keep the ref until
the item is gone, as PR #4 recommends; it costs nothing. Do not give a weapon
whose bundle the level has not loaded: step 2 is the guard.

## Feel listeners (G8)

| Address | ABI | Behaviour |
| --- | --- | --- |
| `0x8024a0` | `thiscall`, no args, `ret` | Once per arm (sets bit 0x2 of `this+0x55d`); walks the intrusive list at `this+0xc` and calls each node's listener (`node+0x10`) vfunc +0x30 |
| `0x8024e0` | same shape | bit 0x20; next sibling of `0x8024a0` |
| `0x80f640` | `thiscall`, no args, `ret` | bit 0x4; outer object is `this-0xd0`; optional `0x6ca270(this+0x34)`; posts message (0x1a, 0x16) through `0x7ff060()->+0x18` with `0x8082b0(outer)`; then listener vfunc +0x38 |
| `0x80f6b0` | `thiscall`, no args, `ret` | bit 0x8; same, message (0x1a, 0x18); listener vfunc +0x3c |
| `0x6ddb30` | `thiscall(bool notify)`, `ret 4` | If `notify`, calls vfunc +0x38 on each listener in the firing object's vector `[+0x18, +0x1c)`; with `false` it does nothing |
| `0x6ddb60` | `thiscall(bool notify)`, `ret 4` | Same, vfunc +0x3c |
| `0x6dd960` | `thiscall(float t, ShotInput* in)`, `ret 8` | Per shot: clears +0x88; computes `tracer = (capacity - loaded) % ammo+0x18 == 0`; if `in+0x24`, calls each listener's vfunc +0xc `(t, tracer, in)`; then adds heat (`WeaponFiringData+0x40` +0x164) to +0x8c and on reaching 1.0 sets overheated (+0x8a) and cooldown (+0x90 from +0x160) |

`0x8024a0`, `0x80f640` and `0x80f6b0` have no direct callers. They sit in the
same slots of about ten vtables from `0x14322d0` to `0x1432ba0` (`0x8024a0` at
slot *k*, `0x80f640` at *k*+2, `0x80f6b0` at *k*+3). The once-per-arm bit means
a hook sees the first event of each arm only; something else clears the bits.

Callers of the firing notifiers: `0x6ddb30` from `0x6e67c4` (Step) and
`0x6e6b1c`; `0x6ddb60` from `0x6e632e` (Step) and `0x6e6b64`; `0x6dd960` from
`0x6ddac4`, `0x6e1f33`, `0x6e65cc`, `0x6e669d` and `0x6e6933`.

**`0x6e6994` is not code.** It is the 16-entry jump table of the Step function
`0x6e62d0` (`thiscall(ShotInput*, float dt)`, `ret 8`), indexed by the state at
firing+0x3c (`jmp [ecx*4+0x6e6994]` at `0x6e6307`, guarded by `cmp ecx, 0xf`).
Targets for states 0 to 13: `0x6e630e`, `0x6e6322`, `0x6e640d`, `0x6e698a`
(3), `0x6e698a` (4), `0x6e658c`, `0x6e65b5`, `0x6e672d`, `0x6e6785`,
`0x6e6654`, `0x6e67bd`, `0x6e67fa`, `0x6e681e`, `0x6e689b`. States 3 and 4 go
straight to the epilogue, so they are no-ops in Step (G9, partial).

## Per-shot ammo decrement: not found statically

Every instruction in `.text` that writes a firing object's loaded count
(`[reg+0x7c]`) was enumerated:

| Site | Function | Role |
| --- | --- | --- |
| `0x6cd524` | `0x6cd520` | setter `SetLoaded(int)`, `ret 4`; 4 callers (pickup, give paths, `0x68ca24`, `0x707ddf`) |
| `0x6d6b0e` | `0x6d6ab0` | capacity-multiplier change, rescales loaded |
| `0x6d6ff6` | Transfer `0x6d6f40` | `loaded += n` (reload) |
| `0x6d7244` | client apply | copies loaded/reserve/+0x84 from a network state |
| `0x6dd5ed` | FillAmmo `0x6dd580` | `loaded = capacity` |
| `0x6f1815` | ctor `0x6f1780` | zero |
| `0x673e07` | `0x673dc0` (vtable `0x141fbd8`, a different class) | message handler: hash `0xea5c52ac` subtracts `data+0x88`, `0xd689f18a` adds `data+0x90`; **not** the soldier firing object |

None is a per-shot decrement of the soldier firing object. `0x6dd960` already
sees the decremented count (its tracer test uses `capacity - loaded`), so the
decrement happens before it, probably in a shot listener or through an
indirect pointer. The next step is one supervised write-watch on firing+0x7c
during a single shot, as planned for G1. It writes nothing to the game.

## Still open

- Which thread may call `Give`; static reads cannot prove it. Call it from the
  server update thread, as the planning thread's range DLL does (after the
  input update returns).
- Client replication of the new item (G8 of the asks): the cloud chain
  (vtable slot at `0x1437a90` → `0x8bb0c0` → `0x8bad10` → `0x8baab0`) is
  consistent with the xrefs here (`0x8bb0c0` appears only in that vtable;
  `0x8bad10` is called from `0x8bb2e5` and `0x8bfc1e`; `0x8baab0` from
  `0x8bae26`), but its logic was not traced.
- The per-shot decrement site, above.

### Narrowed further (second pass)

- `0x6d6eb0` is `CanFire()`, not a consumer. It returns true if `0x6cd5e0`
  (non-finite ammo) is set or loaded (+0x7c) > 0. `0x6d6ee0` is the "magazine
  not full" test used by the reload-on-empty path.
- In fire states 6 and 9 of Step (`0x6e65b5`, `0x6e6654`), the code saves
  firing+0x84 (shots pending), calls the per-shot notifier `0x6dd960`, and
  compares afterwards: `cmp edi, [esi+0x84]; setg bl`. So a shot is "consumed"
  inside `0x6dd960`, which means inside a listener's vfunc +0xc
  `(float, bool tracer, ShotInput*)`. The loaded-count decrement should sit in
  the same listener. It is a virtual call on another class, which is why no
  direct `[reg+0x7c]` write on the firing object turns up.
- Exhaustive negatives: every `mov/sub/add/dec [reg+0x7c]` and
  `[reg+0x84]` writer in `.text` was enumerated. No read, decrement, write
  sequence on +0x7c exists with a non-stack base register.
- Next step: list the firing object's listener vector (`[firing+0x18,
  firing+0x1c)`) on a live weapon and read each listener's vtable slot 3, or
  set one write breakpoint on firing+0x7c for a single shot. Either needs one
  game run.