> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 engine map, October 8: signature index and live-verified additions

> Build 795745 (x86, single-player). This page extends the static reports
> merged in #4:
> [BC2_DISASM_FIRING_STATES.md](BC2_DISASM_FIRING_STATES.md),
> [BC2_DISASM_WEAPON_SOUNDS.md](BC2_DISASM_WEAPON_SOUNDS.md),
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md) and
> [BC2_PC_GIVE_CROSSCHECK.md](BC2_PC_GIVE_CROSSCHECK.md).
> It does not repeat their decodes. It adds:
>
> 1. one signature index, so the addresses can be found without this repo's
>    discovery code;
> 2. what the `proto/reload` runs observed live on 2026-10-07/08;
> 3. pieces #4 left open: the Ant dispatch lock and the skin-palette packer.
>
> - **Exe:** `BFBC2Game.exe`
>   - SHA-256 `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`
>   - PE timestamp 1314210909, SizeOfImage `0x1913000`, preferred base
>     `0x400000`
> - **Signatures** are the shortest instruction-aligned prefix that occurs once
>   in that file's executable sections, computed with pefile and capstone on
>   2026-10-08. Relative `call`/`jcc` operands are position-independent;
>   absolute operands are noted. Re-derive by signature and fail closed on any
>   mismatch.

Labels: **DIS** is static disassembly (#4 calls this SV in its own reports). **SV** is checked by repo discovery code. **LO** is observed live, with the run ids cited. **INF** is inferred. Nothing here is headset-accepted.

## 1. Signature index

| VA | Role (names as in #4) | Unique signature | Notes |
| --- | --- | --- | --- |
| `0x6e9000` | Update, thiscall(ctx*, int) `ret 8` | `83 EC 08 56 8B F1 D9 46 48 57 8B 7C 24 14` | Direct callers `0x6b0014`, `0x6ec6ea` (thunk), `0x7d4dfa`; no pointer references |
| `0x6ec6c0` | Update forwarding thunk | `56 8B F1 8B 4E 14 85 C9 57 8B 7C 24 0C` | |
| `0x7efce0` | Wrapper getter +0x3c (client A) | `8B 41 3C C3` + `CC`×12 + `8B 41 40 C3` + `CC`×12 + `8B 41 44 C3` | 4-byte body; unique only together with its padding and neighbours |
| `0x7efcf0` | Wrapper getter +0x40 (client B) | `8B 41 40 C3` + `CC`×12 + `8B 41 44 C3` + `CC`×12 + `8B 41 28 C3` | |
| `0x6e62d0` | Step, thiscall(ctx*, float) `ret 8` | `F3 0F 10 44 24 08 53 8B 5C 24 08 55` | **Exactly one caller:** `call` at `0x6e9163`, return `0x6e9168` (Update+0x168). Jump table `0x6e6994` (data) |
| `0x6dd6a0` | Commit | `53 8B 5C 24 08 56 8B F1 8B 46 3C 83 E8 08` | |
| `0x6d6f40` | Transfer, thiscall `ret 4` | `51 55 56 8B F1 8B AE 80 00 00 00 85 ED` | |
| `0x6d71a0` | Restore | `56 57 8B 7C 24 0C 8A 47 39 8B F1 3A 86 A6 00 00 00` | |
| `0x6e1fa0` | BeginFire | `53 56 8B F1 8B 46 08 8B 58 40 83 C3 0C 80 7C 24 0C 00` | |
| `0x6e1eb0` | Deactivate | `55 8B EC 83 E4 F0 83 EC 38 56 8B F1 8B 46 08 57` | |
| `0x6d6eb0` | CanFire | `56 8B F1 8B 46 08 83 78 40 00 75 04` | |
| `0x6d6ee0` | Magazine-not-full | `51 56 8B F1 E8 F7 66 FF FF 84 C0 74 05` | |
| `0x6cd540` | Effective capacity | `51 8B 81 98 00 00 00 83 F8 FF 75 10` | |
| `0x6cd570` | Reserve getter (-1 if `+0xa8` bit3) | `51 F6 81 A8 00 00 00 08 74 05 83 C8 FF` | |
| `0x6cd5a0` | Non-finite reserve test | `51 F6 81 A8 00 00 00 08 74 0E 83 C8 FF` | |
| `0x6dd960` | ShotNotify | `51 56 8B F1 8B 86 98 00 00 00 83 F8 FF` | |
| `0x6ddb30` | Reload-begin notify (slot +0x38) | `80 7C 24 04 00 56 57 8B F9 74 05 8B 77 18 EB 03 8B 77 1C 3B 77 1C 74 11 8B 0E 8B 01 8B 50 38 FF D2` | |
| `0x6ddb60` | Reload-end notify (slot +0x3c) | as `0x6ddb30`, with byte 31 = `3C` | |
| `0x881650` | AnimSound OnAnimStart | `56 8B F1 8B 46 04 8A 88 75 04 00 00 80 E1 01 38 4E 50 75 1A` | Hooked live (LO) |
| `0x881690` | AnimSound OnAnimStop | same as OnAnimStart, last byte `15` | Hooked live (LO) |
| `0xf4fb50` | Ant listener dispatch, thiscall(events*, float dt) `ret 8` | `83 EC 0C 56 57 8B 7C 24 18 8B 07 3B 47 04` | The repo checks 54 bytes, including the lock address `0x18a4bc0` (absolute, `C7 44 24 14 C0 4B 8A 01`). See §4 |
| `0x50ece0` | TLS get (lock chain) | `A1 04 2E 50 01 50 FF 15 54 53 40 01 C3` | Absolute: TLS index `0x1502e04`, IAT `0x1405354` |
| `0x50ecc0` | TLS set | `8B 44 24 04 8B 0D 04 2E 50 01 50 51` | Absolute: `0x1502e04` |
| `0x5f2860` | Character preparation | `55 8B EC 83 E4 F0 81 EC 24 02 00 00 8D 84 24 A4 01 00 00` | Calls the packer twice per frame. See §5 |
| `0x4044d0` | Skin-palette packer, cdecl(src, dst, count) | **Not unique by prefix**: 53 bytes still match twice | Identify it by its two call sites in `0x5f2860` (returns `0x5f2b32`, `0x5f2b73`); repo discovery checks all `0xce` bytes |
| `0x6bcf80` | Give/replace weapon | `83 EC 38 83 7C 24 3C 00 53 8B 5C 24 44` | Matches the 24-byte signature in #4 |
| `0x6d7c80` | Selection setter | `80 7C 24 08 00 56 57 8B 7C 24 0C 8B F1 74 18 8B 8E 48 01 00 00` | |
| `0x6dd580` | Fill ammo to full | `51 0F 57 C0 33 D2 56 8B F1 8B 46 08` | |

## 2. The three firing copies, live

#4 lists the three constructor callers: client A `0x7f8c13`, client B `0x7f8c3a` and server `0x561b82`. The live runs added:

- **Client A** (wrapper +0x3c) owns the effects/sound listener. In the SPAS pump it is the copy that passes through state 8 (LO, `Bc2PumpHold.h`, 2142-TRANSFER "SPAS pump hold").
- **Client B** (wrapper +0x40) is **re-synced by the engine**.
  - At reload begin it was seen going 11 → 2 → 10 → 11 (run 134755, LO).
  - During a pump it skips its own state 8 and is re-synced (LO).
  - Restore `0x6d71a0` is applied to wrapper +0x40 at `0x8bb42c` (SV), so B is probably the predicted copy that rollback corrects (INF).
- **The server copy auto-reloads by itself.** In F2000 runs 131709/134305 it alone went 2 → 10 at empty (LO).
- **Practical rule** (proven for the pump, recommended for the magazine): drive server and A, and count B's re-sync as success. Any copy can auto-reload, so the empty gate must cover all three.

## 3. Auto-reload lever, hold and seat advance

**Lever.** #4's state-2 decode (`BC2_DISASM_FIRING_STATES.md` §1, row 2) is confirmed against this exe. Reload entry requires `ctx+0x24 && !ctx+0x28 (sprint) && next==2 && reserve>0 && (explicit || loaded==0 && !nonfinite)`. Consequences that the live work depends on:

- **The empty auto-reload needs no input.** Filtering input before the engine (KF2's approach) cannot stop it.
- **The mod's empty gate presents "sprinting" for one Step:** a compare-exchange write of ctx+0x28, restored after the call.
  - It is the only per-call lever, and it also blocks the explicit Reload term. A physical reload therefore needs a start token that lets one Step through.
  - `+0xa8` bit3 (non-finite ammo) would separate the two terms, but it changes reserve semantics at about 15 sites, so it is not a lever.
- **`HoldReloadUntilFireRelease` (FireLogic +0x8a) does not take part in this entry.** The `+0x8a` read at `0x6e645b` is on the firing object, in the fire branch.

**Hold, seat advance and hitches.** #4 shows that Update loops Tick/Commit/Step while dt remains. Live:

- **Hold.** Zeroing ctx+0x18 for one Update call, then restoring it exactly, freezes Tick in state 11 (SV, LO).
- **Seat advance.** Raising ctx+0x18 by the remaining state-11 timer for one call makes that Update run Tick 11 to zero, Commit, then Step 12 and Transfer, all in the same call. The credit lands on the seat frame instead of about 2 s later (XM8 runs 085404–110023, LO).
- **Hitch.** A 66 ms frame (about 15 fps) arrives as one Update with dt over 50 ms (run 145846, LO). A gate that admits only dt ≤ 50 ms lets that Step run ungated.

## 4. Ant dispatch and its lock (open in #4: "Ant dispatcher loop unpinned")

- `0xf4fb50` is the only caller of the AnimSound listeners. It iterates `ant+0xc0..+0xc4` and calls `vtable+4` (start, `0x881650`) and `vtable+8` (stop, `0x881690`).
- Listeners are registered by `0xf677b0` from the soldier's listener setup at `0x8ba3ae` (1P) and `0x8ba55b` (3P) (SV).
- The calls run inside a scoped lock:
  - `EnterCriticalSection(0x18a4bc0)`
  - a TLS chain node `{cs, previous}` at TLS index `[0x1502e04]`, read and written by `0x50ece0` and `0x50ecc0`
  - released in reverse order
- **To replay or stop a clip sound safely,** call the original OnAnimStart/Stop right after `0xf4fb50` returns, only for the ant that owns the listener, under the same lock taken the same way.
  - Replay landed 0.5–1.2 ms after the magazine pull (LO, runs 092142/092408, `f9df398`).
  - The listener thread differs from gameplay (runs 085753/085925).
- Waiting for the next clip callback instead put the replay at the pouch grab, not the pull (run 083229, LO).
- **Per-level mapping.** The SPAS pump clip GUID begins `1bf49c2d` and plays about 0.35 s after every shot (LO).

## 5. Skin-palette packer (where posed hands and guns go in)

- **Getters.** The soldier skin getters `0x875da0`/`0x875dd0` forward through the animation getter `vfunc +0x160(-1)` to `0x78b0a0`.
- **Packer.** Character preparation `0x5f2860` calls packer `0x4044d0` twice per drawn frame (returns `0x5f2b32`, `0x5f2b73`). The packer:
  - copies `count` 64-byte padded matrices into 48-byte 3×4 matrices in the native request arena
  - does not keep the source pointer
  - (SV, `docs/HANDS-20260930.md`)
- **Injecting a pose.** Copy into a private full palette and run the original packer once on it. Never overwrite the native palette.
- **During a reload animation the native palette changes every frame** (LO, pack audit `0199f8a`). A pose built against the previous frame's palette must be rebased: our edited bones keep their values, and every other bone takes the current native value (`cd00c1c`). Otherwise that pack falls back to the native pose, which shows as one-frame flicker.

## 6. Magazine profile keys (from shipped authored assets)

| Asset | Key | Descriptor | Timing words (offset, value) |
| --- | --- | --- | --- |
| `XM8_sp_s` (`SP_rgl_XM8_Scoped`) | `ScopedXm8 = 0` | `Xm8ReloadDescriptor` | completion deadline 3.5 s |
| `AEK971_sp` (`RU_rgl_AEK971/SP_rgl_AEK971`) | `AuthoredAek = 1` | fltAutomaticFire, rtMagazine, EiaFire 8, EiaReload 29, capacity 30, 4 mags, ReloadDelay 0, ReloadTime 3.2, threshold 0.75 | `{0x10,.75f}{0x14,0}{0x18,3.2f}{0x20,1}{0x24,2}{0x2c,0}` |
| `F2000_sp` (`BU_rif_F2000/SP_rif_F2000`) | `0xa83a3c5288dee2d4` (64-bit digest key) | Same as AEK, but ReloadTime 3.1 | `{0x10,.75f}{0x14,0}{0x18,3.1f}{0x20,1}{0x24,2}{0x2c,0}` |

- Source: `src/games/bc2/Bc2MagazineNativeProfile.h` on `proto/reload`. The F2000 values come from `SP_rif_F2000.dbx` (sha256 `62d1514a…`) and `BU_rif_F2000_Firing.dbx` (sha256 `84329145…`) in `sp_common/level-00.fbrb`.
- **Keys are 64-bit.** Packing them into 32 bits truncated the F2000 key and made it read as unsupported (run 145846, `ef6b3b2`).
- Charging-handle strokes from the shipped fire clips: XM8 `jntWpn_17` 0.12 m, F2000 `jntWpn_6` 0.10 m. The AEK's handle never moves in any clip.

## 7. Hook integration lessons (not engine facts)

- **Entry-gate window.** If a hook skips its control logic while a short exclusive section runs on another thread, its Update runs ungated (F2000 runs 131709/134305; `7687afe`, `698e9ae`).
- **Owner clears.** A gate keyed to an owner revision re-opens on every owner clear: a failed state read, focus or head loss, or stale input. Key it to owner identity and check the copy structurally (`7dfe376`).
- **Reserve reads during hooks.** 75–85% of gather-time reserve reads were deferred while a hook ran on a sim thread (`cf9d8ec`). Keep hooks short and read shared state outside them.

## 8. Still open

- Which copy the HUD ammo and the 1P reload animation read.
- Whether all three copies register the AnimSound/notify listeners (#4 open question).
- The per-shot loaded decrement (#4: inside the +0x0c listener chain, INF).
- Live confirmation of the give path.
