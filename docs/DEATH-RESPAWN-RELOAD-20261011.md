> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Death / checkpoint crash and persistent reload authority (2026-10-11)

- Branch: `fix/death-respawn-reload`, based on `codex/bc2-resume-20261011` `3be9c94`.
- This is static and source work only. No game, headset or GPU run backs any
  claim here.
- RE detail is in `docs/re/BC2_NULL_CLIENT_WEAPON_FOV.md`.

## 1. Crash attribution (high confidence)

**Session:** the installed `b74560c` package, session
`native-trace-20261012-020559-729`, local time 2026-10-11.

**Process:** `BFBC2Game.exe` (pid 25776), main thread 36020. The DLL was
resident.

**Fault:**

- 21:11:56.367, access violation at **`0x7efca0`**, reading `0x2c`.
- ECX=EAX=0, ESI=`1a3423e0` (the ClientSoldierEntity).
- Return chain: `0x89bc99` → `0x805f23` → `0x81ae2f` → ...
- Source: `stage/BC2TestRange.log`.

**Context:**

- 0.6 s earlier the game began the checkpoint reload (`bundle: load "levels/sp_02/loader"`).
- Just before that, `gun-events.jsonl` logged `weapon_switch` VSS → null.

**Not the fault:** EA's `crashreport.xml` names Eip `0x401090`, which is the
reporter's own frame.

**Code path (REA/Ghidra, build 795745):**

- The ClientPlayer view update `0x805e20` calls the ClientSoldier getter
  `0x89bc40`.
- That getter is thiscall with a float returned in ST0. Its fallback is
  `[soldier+0x400]`; the sibling `0x89bcb0` falls back to `+0x404`.
- It indexes the client weapon vector (soldier `+0x260/+0x264`) with the client
  inventory's slot (`+0x14c`).
- It passes the entry, with no null check, to `0x7efca0`
  (`mov eax,[ecx+0x2c]`).
- The return address shows the in-range branch, so the vector entry at the
  selected slot was NULL.

**Root cause, two layers:**

- The crash itself is the unguarded null deref. This attribution is certain
  from the log and disassembly.
- Why the slot was NULL during the checkpoint reload is not established. It
  could be native spawn ordering, or a mod-issued selection landing on a slot
  whose client wrapper did not exist yet.

## 2. Stale reload authority after death or checkpoint (medium-high confidence)

### What the authority keys on

`physicalWeaponRuntime` owns the per-weapon ammunition ledger: loaded,
reserve, chamber, magazine metadata and the native reload transaction. It is
keyed by:

- the `PlayerInventoryAnchor {player, soldier, weak, actorGeneration}`;
- server `serverPlayer/serverSoldier/inventory/inventoryData`;
- per item: `item/serial/data/firing/ammoAddress`.

### The gap

- `UpdateHook` advanced `physicalActorGeneration` only when the client
  player, soldier or weak *pointers* changed.
- The runtime retired weapons only when the server authority *pointers*
  changed.
- A checkpoint restore that rebuilds the pawn, inventory and items at recycled
  addresses therefore kept the previous life's ledger. The existing comment in
  `InventoryUpdate` already notes that serials are recycled.

### What a stale ledger does

- **The restore changes the native counts.** `BeforeUpdate` then sees
  `loaded != ledger.loaded`, marks the weapon `suspended` and counts
  `unexpected_ammo`.
  - A suspended weapon fails `PermitFire`, so it never fires or ejects.
  - It also fails `RequestNativeReload`, so the XM8 reload cannot finish.
  - Every weapon whose count the checkpoint changed is affected, so it still
    "doesn't fire after a switch".
- **A native reload was in flight at death.** `reload.Busy()` stays latched
  with no native idle to settle it. `PermitFire` then refuses and blocks the
  switch logic.
- **Client side.** `rigEpoch` (the ReloadStateOwner and hand claims) and the
  body inventory's automatic-stow actor are also pointer-keyed. They therefore
  carried the dead pawn's reload owner and hand state into the new life.

### What the trace does and does not show

- The `b74560c` trace does **not** show this failure directly: the game
  crashed during the reload, before any post-checkpoint frame.
- It does show `inventory_fresh=false` from the death (~342 s) onward. That is
  the server-side loss signal used below.
- Its whole session ran on `actor_generation 1`, so no death had been
  rebound before.
- The diagnosis rests on code reading plus the reported symptom.

## 3. Fix

### A. Null-weapon FOV guard (`Bc2Gameplay.cpp`, hooks[21]/[22])

- Detours `0x89bc40` and `0x89bcb0`.
- They are installed only if all 0x66 bytes of each body match the file image
  and the live image. Any mismatch leaves both getters native, and the failure
  is never fatal.
- Each hook repeats the native lookup with SEH-guarded direct reads:
  inventory, slot, bounded vector, wrapper, aim object.
- If any link is missing it returns the soldier's own fallback `+0x400` or
  `+0x404`. That is the same value the native getter returns when the view is
  ineligible.
- Otherwise it calls the original.
- It reads only and writes nothing.

### B. Client actor-loss rebind (`Bc2Gameplay.cpp`, `UpdateHook`)

- On the input thread, a failed `Resolve` combined with `LocalSoldierAbsent()`
  sets `localActorLostAt`. Absence means any of:
  - the manager vtable is invalid;
  - there is no local player;
  - the weak soldier target is null;
  - the target is not a `ClientSoldierEntity`;
  - its `+0x220` is not the player.
- Ordinary `Resolve` failures do not count. Examples are weapon data mid-switch
  and a vehicle input router.
- The next successful local resolve forces all of the following, whether or
  not the pointers changed:
  - `actorChanged`, which bumps `rigEpoch` and `renderEquipmentEpoch`;
  - `++physicalActorGeneration`, which makes `PublishActor` clear the ledger
    and reject pending commands;
  - an input `epoch` bump;
  - a body-inventory actor re-entry.

### C. Server loss retirement (`Bc2PhysicalWeaponRuntime.cpp`, `InventoryUpdate`)

This covers the case where the client soldier object survives (ragdoll) but the
server pawn is rebuilt.

- While the anchored inventory cannot be read, the runtime records a loss only
  when the server player no longer owns the anchored server soldier
  (`serverPlayer+0xc3c != serverSoldier`).
- A vehicle seat changes only `+0xc6c`, so it never counts.
- If such a loss spans at least 500 ms, the next fresh read for our own
  inventory retires the ledger even when every address is unchanged
  (`loss_retirements`).

### Ammunition conservation

- The retired ledger is rebuilt by `Initialize` from the **native** counts
  that the checkpoint restored. The mod writes, refunds and discards nothing
  at retirement, so the fix cannot duplicate or lose ammunition.
- The only things dropped are previous-life metadata:
  - a held magazine;
  - an owed rack;
  - a pending reload token.
- That state belonged to a pawn that died.

## 4. Kill switches (all default on)

| Variable | Effect when `0` |
|---|---|
| `FVR_BC2_NULL_WEAPON_GUARD` | The FOV getters are not hooked (native crash path restored). |
| `FVR_BC2_ACTOR_LOSS_REBIND` | Restores pointer-only actor generation (B) and pointer-only server retirement (C). |
| `FVR_BC2_PHYSICAL_WEAPON_AUTHORITY` | Existing switch. Disables the whole ledger. |

## 5. Telemetry

- **`session-telemetry.jsonl` → `controls.death_recovery`:**
  - `null_weapon_guard_installed`, `null_weapon_guard_calls`, `null_weapon_guard_hits`
  - `actor_loss_rebind`, `actor_losses`, `actor_loss_rebinds`
  - `actor_loss_short` (rebinds whose loss was under 500 ms)
  - `last_actor_loss_ms`
- **`physical_weapon_authority`:** now also carries `authority_retirements`
  and `loss_retirements`. The final report adds `loss_retire_enabled`.

## 6. In-game test sequence (not yet run)

1. **Before launch.** Check `nvidia-smi` and what holds VRAM. Confirm the
   package DLL hash is not `efd3c0d0...` and not this worktree's
   non-installable hash.
2. **XM8.**
   1. Fire part of a magazine and note loaded/reserve in
      `physical_weapon_authority`.
   2. Start a reload: pull the magazine, then die with it out or mid-reload.
3. **Death and checkpoint.** Let the checkpoint reload.
   - Expect no crash.
   - If the null-slot case recurs, `null_weapon_guard_hits` is above 0.
4. **After spawn.**
   - `actor_loss_rebinds` or `loss_retirements` is at least 1.
   - The `physical_weapon_authority` weapons show a new `actor_generation`.
   - `suspended=false`.
   - Loaded/reserve equal the checkpoint's native counts.
   - `unexpected_ammo` has not increased.
5. **Reload the XM8 completely.** The phase reaches `completed`, and the bolt
   and rack behave normally.
6. **Switch weapons** at least twice, with the stick cycle and with the shoulder
   holster.
   - Each gun fires and ejects shells.
   - Shots are counted (`shots` rises).
   - `can_fire=true`.
7. **Death with no reload in flight.**
   - Repeat death → checkpoint once more.
   - Confirm that ammunition totals after spawn equal the native restored
     values (no duplicate magazine, no lost reserve).
8. **Vehicle ride over 1 s.** Ride a vehicle and leave.
   - `loss_retirements` and `actor_loss_rebinds` do **not** increase.
   - A held weapon state (e.g. owed rack) survives.

## 7. Compile

See the commit message and orchestration handoff for the worktree build
result and DLL hash. A worktree build is compile proof only, not an installable
candidate. The packaging recipe must supply the private authored inputs.

Worktree compile (2026-10-11): `.\Build.ps1` (x86, `BC2NativeProbe` only, two jobs) into `build/dev/x86`. It succeeded with no warnings in the changed files. DLL SHA-256 `38fd30f168261fc315770554644fd11c270e3170b23e304218e299480a218670`. The private authored headers (`BC2_NATIVE_ACTION_POSE_HEADER`, `BC2_AUTHORED_GRIP_HEADER`, `BC2_AUTHORED_SUPPORT_HEADER`, `BC2_DRAW_CATALOG_HEADER` and others) were empty, so **this DLL is not installable**. It is compile proof only.
