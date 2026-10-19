> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 null client weapon wrapper in the weapon-view FOV getters (build 795745)

Verified 2026-10-11 with REA (Ghidra 12.1.3 provider) on
`D:\bc2-re\BFBC2Game_795745.exe`, SHA-256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
Static evidence only; nothing was run.

## Crash record

- Playtest of the installed `b74560c` package. Session
  `native-trace-20261012-020559-729`, BC2 log
  `build/playtest-b74560c/BC2-VR/stage/BC2TestRange.log`.
- 21:11:55.734: `bundle: load "levels/sp_02/loader"`. This is the
  checkpoint reload after the death.
- 21:11:56.367, main thread 36020:
  `fault: access violation at 007efca0 (read 0000002c) ... eax 00000000 ecx 00000000 esi 1a3423e0`.
  Stack: `0089bc99 00805f23 0081ae2f 004edd8f ...`.
- At 21:11:56 the last `gun-events.jsonl` rows show `weapon_switch` from `VSS`
  to `null`, then the right-hand `gun_hold` claim was released.
- EA's `crashreport.xml` names Eip `0x401090`. That is the reporter's own
  frame, not the fault.
- No Windows Application Error event was written for BFBC2Game.exe.

Return address `0x89bc99` is inside `0x89bc40`, on its **in-range** branch.
The selected slot index was inside the client weapon vector, but the vector
entry itself was NULL. The out-of-range branch would also crash, through
`xor eax,eax; mov ecx,eax; call 0x7efca0`. ESI is the ClientSoldierEntity.

## Functions

| VA | Body | Meaning |
|---|---|---|
| `0x7efca0` | `mov eax,[ecx+0x2c]; ret` | Client weapon wrapper getter for the aiming object at `+0x2c`. The faulting instruction when ECX is 0. |
| `0x6ce960` | `mov eax,[ecx+0x14c]; ret` | Gets the selected slot index from the client inventory. |
| `0x89bbd0` | Calls a bool through the vtable at soldier `+0x220` (or `+0x224`, minus `0xc01`), else returns false. | Eligibility check for the weapon view. |
| `0x89bc40` | 0x66 bytes, `0x89bc40..0x89bca5` | Returns `aim+0xc` as a float. Fallback is `[soldier+0x400]`. |
| `0x89bcb0` | 0x66 bytes, `0x89bcb0..0x89bd15` | Returns `aim+0x10` as a float. Fallback is `[soldier+0x404]`. |
| `0x805e20` | ClientPlayer view update | The only caller of both getters: `0x805f1e` calls `0x89bc40` and `0x805f49` calls `0x89bcb0`. It reaches them only when player `+0xc68` (controlled) equals the weak-pointer soldier and `0x89bbd0` returns true. The results go to `0x612c30` and `0x612c80`. |

`0x89bc40` disassembly (`0x89bcb0` is the same except for the `+0x10` and
`+0x404` offsets):

```
56                push esi
8b f1             mov esi,ecx                 ; thiscall, ECX = ClientSoldierEntity
e8 88 ff ff ff    call 0x89bbd0
84 c0 / 74 52     test al,al / jz fallback
f6 86 14 01 00 00 01  test byte [esi+0x114],1
  -> ecx = [esi+0x24c] or [esi+0x248]         ; client inventory
e8 f8 2c e3 ff    call 0x6ce960               ; eax = inventory+0x14c (slot)
ecx = ([esi+0x264]-[esi+0x260])>>2            ; client weapon vector count
cmp eax,ecx / jb inrange
33 c0 8b c8 e8 .. call 0x7efca0 (ECX=0)       ; out-of-range: guaranteed AV
fld [eax+0xc] / pop esi / ret
inrange: eax=[[esi+0x260]+eax*4]; ecx=eax; call 0x7efca0   ; NULL entry -> AV (this crash)
fld [eax+0xc] / pop esi / ret
fallback: fld [esi+0x400] / pop esi / ret
```

### Calling convention

- Thiscall with no stack arguments. It ends in a plain `ret`, not `ret n`.
- It returns its float in ST0.
- ESI is saved and restored. No other callee-saved registers are touched.
- A `__fastcall(void* self, void* edx)` replacement returning `float` has an
  identical ABI.

### Signatures

The full bodies are pinned byte for byte in `Bc2Gameplay.cpp`, including the
rel32 call displacements. A Python check against the file image matched both
on 2026-10-11.

- `0x89bc40`:
  `56 8B F1 E8 88 FF FF FF 84 C0 74 52 F6 86 14 01 00 00 01 74 08 8B 8E 4C 02 00 00 EB 06 8B 8E 48 02 00 00 E8 F8 2C E3 FF ...`
- `0x89bcb0`:
  `56 8B F1 E8 18 FF FF FF 84 C0 74 52 F6 86 14 01 00 00 01 ...`

## Soldier fields used

| Field | Meaning |
|---|---|
| `+0x114` bit 0 | Selects the client inventory pointer: `+0x24c` when set, else `+0x248`. This matches `Resolve` and `ReadPlayerInventory`. |
| `+0x220` | Owning ClientPlayer. Also the eligibility object in `0x89bbd0`. |
| `+0x260` / `+0x264` | Client weapon wrapper vector, begin and end. |
| `+0x400` / `+0x404` | The soldier's own float fallbacks. The native getters return these when the weapon view is not eligible. |

## Mod response

`WeaponViewFovHook` and `WeaponViewFov2Hook` re-run the native lookup with
SEH-guarded reads:

1. If the inventory, slot, wrapper or aim object is missing or out of range,
   return `[soldier+0x400]` or `[soldier+0x404]`. This is the value the native
   getter already returns for an ineligible view.
2. Otherwise, call the original.

The hooks only read; they write nothing. They are installed only when both
bodies match exactly. The kill switch is `FVR_BC2_NULL_WEAPON_GUARD=0`.
Counters are in `controls.death_recovery` in `session-telemetry.jsonl`.

## Not established

- **Why the selected client slot held NULL.** Candidates:
  - a native spawn-order race;
  - a mod selection, such as the stick cycle, body-holster automatic stow on a
    new actor, or the VSS→null switch logged at 21:11:56, landing on a slot
    whose client wrapper did not exist yet.

  The guard makes either case non-fatal. It does not identify which one
  happened.
- Whether BC2 recycles the ClientPlayer, ClientSoldierEntity and server
  inventory addresses across a checkpoint reload. The reload-authority fix
  does not depend on this (see `docs/DEATH-RESPAWN-RELOAD-20261011.md`).
