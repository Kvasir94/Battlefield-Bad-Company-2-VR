> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 foreign weapon loading: verified VAs (BFBC2Game 795745)

These VAs are used by `tools/testrange/BC2TestRange.cpp` to load MP and Vietnam weapons into SP_02.
The narrative is in `docs/WEAPON-LOADING-AND-DEBUG-RECOVERY.md` 1.6 and `docs/WEAPON-VR-COVERAGE-20261010.md`.
Sources: REA (Ghidra 12.1.3 provider) and Ghidra headless decompiles, capstone listings, and live logs.
All call targets below were checked against their prologue bytes before being called or hooked.

## Bundle mounting
| VA | What | Signature / notes |
|---|---|---|
| `0xbd3040` | package.mft ParseLine | thiscall(parser, line, end, backend, mode 2), ret 0x10 |
| `0xbd2fb0` | AddTarget | adds a BundleTarget to a source node |
| `0x4a5c50` | target list for a bundle | elements 0x10 B (eastl path + idx); idx 0 = last appended |
| `0x4b56a0` | **level bundle loader** (runtime path) | reads first-listed target first; keeps a "seen" name set |
| `0x4b59ea` | per-entry gap-fill hook site | displaces `test byte [esp+0x104],1` (deleted flag); name `[esp+0xec]`, kind `[esp+0xf0]`, archive path `[esp+0xa0]`; resume `0x4b59f2`; skip `0x4b6370` (balances the `0x509340` scope); edx = 0 and ebp are live |
| `0x4de490` | sibling table builder | same first-wins pattern; does **not** run for the level bundle |

## Shader databases
| VA | What | Signature / notes |
|---|---|---|
| `[0x1572b18]` | Dx11ShaderSystem | vtable `0x14481f8`; database vector at `+0x220/+0x224/+0x228` |
| `0x9cb590` | RegisterDatabase (vtable +0x68) | thiscall(db, ctx), ret 8 |
| `0x9c8120` | re-merge all databases | fastcall(ss); sets `[ss+0x23c]+0xce = db+0xec` before each merge |
| `0x9c3f60` | MergeDatabase | thiscall(ss, db, &vector), ret 8. Maps 1 and 2: last merged wins. Map 3 permutation lists: rebuilt from every database in vector order, first wins |
| `db+0x8c` | database name (char*) | e.g. `Levels/SP_02/Shaders/Dx11_Single` |

## First-person anim tree crash (fixed by the gap-fill)
- **Crash site:** `0xf691cf`, `mov ecx,[eax+0xc]` with `eax = [asset+0x38] = 0`, in node ctor `0xf69030`.
- **Call chain:** `0xf6ac50` ← `0x828d50` (tree build) ← `0x7aa240` ← inventory switch `0x6ef5e4`.
- **Cause:** SP_02's archive deletes 25 `*_Scoped_AnimTree1p.res`.

## AI weapon ballistics cache (give fault `0xe93ed7`)
| VA | What | Signature / notes |
|---|---|---|
| `[0x1567180]` | cache map | SoldierWeaponData* → 64 bytes |
| `0x58ee90` | find | thiscall(cache, data), ret 4; returns node+0x14 or 0 (REA listing) |
| `0x5a5a00` | build entry | thiscall(cache, data), ret 4; reads `data+0x98` WeaponFiring → `+0x40` PrimaryFire → `+0x84/+0xa0..a8/+0xdc/+0xe4/+0x180/+0x184/+0x1d0/+0xf0` (REA listing) |
| `0x5ac927` | loader registration | `if ([sw+0xf0]) if (!find(cache, sw)) build(cache, sw)`; only for weapons that loaded entity data reaches |
| `0x5afb72`, `0x58f5b0` | weapon-selected handlers | `find([0x1567180], sw)` result goes unchecked into ctor `0xe93ec0`, whose `rep movsd` at `0xe93ed7` faults on null |

## Give path
| VA | What | Signature / notes |
|---|---|---|
| `0x6bcf80` | give/replace | thiscall(ServerSoldierEntity, SoldierWeaponData*, slot, selectNow, forceSlot), ret 0x10 |
| `0x5debd0` | net id lookup | on table `[0x156d500]` |
| `0x5e5d70` | net id register | on table `[0x156d500]` |
| `0x6d7c80` | Inventory::SetSelected | thiscall(soldier+0x2b4, slot, validate), ret 8 |

Weapon switching slots: `WeaponSwitchingEnum` 0..8. Switching uses `SwitchMap`. Inventory input tables cover slots 0..9 only.

## Other client faults seen in the arsenal sweep
- **`0xf68fc3`:** client 1P anim build for data without an async weapon bundle (MEC_shg_TOZ194*, Binoculars).
  The arsenal skips such data.
- **`0x8ac396`:** in `0x8ac380` (thiscall, handler lookup by type), `[obj+0x2c]` is null for `NAM_Handgrenade`.
  Unresolved; the gun is excluded.
