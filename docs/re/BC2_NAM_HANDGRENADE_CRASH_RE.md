> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 NAM_Handgrenade equip crash (fault 0x8ac396): static RE

Binary: BFBC2Game_795745.exe (sha256 c603f686...b0162), REA/Ghidra 12.1.3. Date 2026-10-10.
Evidence is static only. The game was not run. The original crash log was overwritten
(the current `BC2TestRange.log` has no `NAM_Handgrenade` give), so there is no stack for this fault.

## What the fault is
The fault is not in a grenade-specific handler. `0x8ac380` is the generic entity-data factory
lookup. It is `thiscall(registry, EntityCreationInfo*)`, ret 4.

```
0x8ac385  mov eax,[ebp+0x38]     ; ebp = creation info. +0x38 = cached factory
0x8ac388  test eax,eax ; jz 0x8ac393
0x8ac393  mov ecx,[ebp+0x2c]     ; +0x2c = the Data* (EntityData / Blueprint) to instantiate
0x8ac396  mov eax,[ecx]          ; <== FAULT, ecx == 0
0x8ac398  mov edx,[eax+8]        ; vtbl+8 = GetTypeInfo -> type id; looked up in the factory map (0x897fa0)
```

The only caller is `0x8b8360` (generic "create entity from Data" with child recursion).
Its decompile shows that `+0x2c` is the Data pointer of one node:
- Top-level callers (`0x82e4e0` and about 22 others) pass a valid Data.
- For a Blueprint with children, `0x8b8360` enumerates the child array (`FUN_013fc1f0`, `local_50[4]`),
  takes `piVar5 = list[i]` and copies the 0x40-byte info struct.
  It writes `piStack_14` (= info+0x2c) = `piVar5` and recurses (`FUN_008b8360(auStack_40)`).
- A null element is only guarded for the type check (`if (piVar5 != 0)`). It is not guarded
  against the recursion, so a null child reference becomes `[info+0x2c] = 0` and faults at `0x8ac396`.

## Root cause
The faulting object is a null child Data reference in the NAM_Handgrenade blueprint's object graph
(the weapon/throwable blueprint instantiated for the client weapon on equip).
The reference is null because it points to a partition instance that is not resident in SP_02.
Frostbite leaves an unresolved ref as null.
SP_02 never needs it, and the arsenal mounts only `nam_common`, `mp_common`, `mp2_common`
and the shader databases, so something the grenade blueprint references lives elsewhere.
The same class of failure produced the earlier `0xf691cf` (null `[asset+0x38]` from deleted
AnimTree1p resources) and `0xf68fc3` faults.

Evidence that the bundles themselves are present: `async/weapon/nam_handgrenade-00.fbrb` exists in
`Package/async/weapon` (127,779 B, the same size class as the SP `handgrenade` 127,889 B), and the
arsenal's `HasAsyncBundle` check passes. So this is not a missing async weapon bundle.
It is a dangling reference inside the data graph.

Not determined statically: which child is null (the grenade projectile, the 1P prop entity,
or an effect/sound blueprint). The bundles are compressed and I could not read partition contents.
The gun is currently excluded by prefix, so no live capture exists.

Confidence: high (about 90%) for "null Data* child reached through `0x8b8360` -> `0x8ac380`".
Low to medium (about 35%) for "it is a partition not mounted by the range" as the reason it is null.

## Proposed fix (smallest, no new bundle needed)
Guard `0x8ac380` against a null Data pointer. `0x8b8360` already handles a null return
(`if (piVar5 != 0)`, and the child result is only freed `if (iVar6 != 0)`).
The null-return path already exists at `0x8ac3d5`: `pop esi; xor eax,eax; pop ebp; ret 4`.
The stack at `0x8ac393` has exactly the same two pushes (ebp, esi).

Hook at `0x8ac393`. It displaces 5 bytes: `8b 4d 2c` (mov ecx,[ebp+0x2c]) and `8b 01` (mov eax,[ecx]).
Verify those bytes before patching. The cave is:

```
mov ecx,[ebp+0x2c]
test ecx,ecx
jz  0x8ac3d5          ; null Data -> return 0 (skip this child)
mov eax,[ecx]
jmp 0x8ac398
```

The child is skipped, the grenade is built without it, and the rest of the graph is fine.
The code is the same size as the gap-fill hook pattern already in the DLL (`0x4b59ea` site).

Where: `tools/testrange/BC2TestRange.cpp`, in the hook install section next to the gap-fill hook.
Add `PatchNullDataGuard()`, log "guard: 0x8ac393 installed", and give it an env kill switch
(e.g. `FVR_TESTRANGE_NO_NULLDATA_GUARD`).
Then drop `NAM_Handgrenade` from the default `arsenalExclude` (line ~921) and from
`ranges/sp02-arsenal.cfg` (line 30).

Risks: the grenade may show without its missing child (a missing throw prop or effect), and a
later dereference of a null result can fault elsewhere. If it does, the log's `exit:` stack and the new
fault address will show which. It also changes behaviour for every entity creation, but only where it
would have crashed. Cost is low.

## Fallbacks
1. Keep the exclusion (current behaviour). Safe.
2. To find the real missing partition, run once with the guard and log the parent Data's name and
   index when the guard fires (hook arg `[ebp+0x2c]` null, and read the parent from the
   caller frame at `0x8b8360`). Then mount the bundle that holds it.
3. The SP `HandGrenade` (slot 8, 0x1c6f5cf0) already works, so a Vietnam skin swap is not needed to test
   grenade throwing.

## Other notes
- The arsenal gives throwables into Primary1/Primary2 (log: `HandGrenade into slot 1`) and it worked
  for the SP grenade, so the slot is not the cause.
- Related VAs: `0x8b6b10` (pre-step, large), `0x897fa0` (factory-map find), `0x82e4e0` (blueprint instantiate).
