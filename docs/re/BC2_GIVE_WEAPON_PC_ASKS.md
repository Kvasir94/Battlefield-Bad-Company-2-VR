> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 give-weapon: PC verification asks

> Work order for the PC session, build 795745. Run it only when the owner says
> "run the give-weapon asks in docs/re/BC2_GIVE_WEAPON_PC_ASKS.md". Background
> and gap numbers are in [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md); the
> addresses come from the cloud static disassembly in
> [BC2_DISASM_GIVE_PATH.md](BC2_DISASM_GIVE_PATH.md) and
> [BC2_DISASM_FIRING_STATES.md](BC2_DISASM_FIRING_STATES.md).

The cloud pass found the engine's own give function statically. These asks
confirm it in Ghidra and in one supervised live run, so the test harness can
call it instead of patching kits. Asks A to C write nothing to the game.
Commit the results into `BC2_WEAPON_RUNTIME.md`, raising each row from DIS to
SV or LO.

**Overlap:** the planning thread's `feat/range-give-weapon` branch is wiring
`Give.SlotN <asset>` onto `0x6bcf80`. Ask D is that branch's first live run.

## A. Static cross-check in Ghidra

Load `profiles/typeinfo/795745/ghidra_import_types.py` and confirm:

1. `0x6bcf80` signature
   `83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44 55 56 57 8B F1 0F 84 BE 00 00 00`.
   It is thiscall on `ServerSoldierEntity` (ctor `0x6c4040`, vtable
   `0x1424850`) with `(SoldierWeaponData*, uint slot, bool selectNow, bool
   forceSlot)` and `ret 0x10`, and returns the new server item.
2. The ground-pickup call at `0x6c5f3e` inside `0x6c5cd0`, and the
   spawn-loop call at `0x6bd3f5` inside `0x6bd350`.
3. The selection setter `0x6d7c80`, thiscall on the inventory at server
   soldier+0x2b4, `(int slot, bool validate)`, `ret 8`.
4. The client replace function `0x8baab0`, reached from `0x8bad10`.

## B. Server soldier pointer (G11)

Confirm the chain in
[BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md](BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md):
`*(0x1566b18+0x2c)` players, then the non-AI `ServerPlayer` (+0xd84 == 0),
then +0xc3c. Check soldier vtable `0x1424850` and soldier+0x220 == player.
Note the thread id when `0x6c5cd0` is hit during a pickup: the give must run
on that thread.

## C. Live write-watch (no writes)

Set hardware write breakpoints on the server soldier's item slot (+0x2b8
vector) and on the client wrapper +4. Then pick up the placed AEK-971 (the run
in `docs/HEADSET-20261003-182719.md`). Expected stacks:

- server: `0x6c5cd0` → `0x6bcf80`;
- client: `0x8bb0c0` → `0x8bad10` → `0x8baab0`.

While in game, also set a write-watch on firing +0x7c during one shot. The
hit is the per-shot ammo decrement site (G12).

## D. First supervised give call

Only after A to C hold, and on the range level: call
`0x6bcf80(data, slot, 1, 1)` on the game thread, with `data` taken from a
weapon the level has loaded: resolve it by `WeaponAssetName` as the checkpoint restore `0x555530` does, or take it from a pickup or kit entry. Check:

- the gun appears in hand, with full ammo;
- the old item is destroyed;
- calling again with the original data restores the original weapon.

Log everything with the gun-event emitter. Headset acceptance is a separate
step.
