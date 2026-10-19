> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 VR pump (state 7/8) and empty click: static RE

> Cloud static disassembly of build 795745 ([`tools/re/bc2re.py`](../../tools/re/bc2re.py)), 2026-10-08.
> In this report **SV** means read in the disassembly; it is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md). Nothing here has run in the game unless marked LO. Script names refer to throwaway analysis scripts that are not in the repo.

BFBC2Game.exe build 795745, x86, base 0x400000. Static disassembly only (capstone, `bc2re.py`).
**SV** = read in the disassembly. **INF** = inferred. Nothing here has run in the game or headset.
Scripts: `pump_dis.py` (range dump), `pump_sigs.py` (all signatures below; each is unique over
.text+ctr, 24/24 OK). FL = `FireLogicData` = FFD+0xc (`ebp`/`ebx` in Step/Tick).
FL+0x44 BoltActionTime = FFD+0x50, FL+0x48 BoltActionDelay = FFD+0x54, FL+0x4c HoldUntilZoom
= FFD+0x58, FL+0x4d HoldUntilFire = FFD+0x59, FL+0x88/+0x89 WaitForTrigger/ZoomRelease,
FL+0x8a/+0x8b HoldReloadUntilFire/Zoom, FL+0x18 FireLogicType.

## 0. Corrections to the existing docs (SV)

- **Step writes `timer(+0x50) = dt` and `+0x94 = ctx+0x1c` at entry for every state**
  (0x6e62e5, 0x6e62f2; Step is thiscall `(ctx*, float dtRemaining)`, `ret 8`, arg2 comes from
  Update's local dt at 0x6e9158). A handler that does not set a timer therefore leaves
  `timer = dt`, and the next loop pass runs Tick, which consumes all of dt and ends the Update.
  This is how waiting states (2 idle, 7 held, 1 held) consume time. It also means state 4 does
  **not** spin: Step(4) leaves timer = dt and Tick(4) runs BeginFire (fixes the "unused/spins"
  note in BC2_DISASM_FIRING_STATES.md section 1).
- **The state-7 gate does not read the ctx input bits for its hold.** It reads two latches on
  the firing object: `+0xa4` "fire latched" (set to 1 by Step 2 on an accepted fire press,
  0x6e6468) and `+0xa5` "zoom latched at fire" (= ctx+0x27 at that press, 0x6e6472). Update
  clears them before the loop: `if(!(ctx+0x2c&1)) +0xa4=0; if(!ctx+0x27) +0xa5=0` (0x6e911b).
  ctx bit0 is only read as a re-press in the zoom-hold case.
- **Native dry fire (+0x1c) fires only when the weapon has no ammo anywhere** (loaded<=0 and
  finite reserve<=0). With reserve>0 the empty press goes to auto-reload, or is silent when the
  mod's ctx+0x28 override blocks auto-reload. See section 2.

## 1. Bolt/pump cycle 6 -> 7 -> 8 -> 1 -> 2 (SV)

Update 0x6e9000 loop (SV, 0x6e9135..0x6e9183): `while(dt>0){ if(timer>0) Tick(ctx,&dt);
else if(next!=cur) Commit(ctx); else Step(ctx,dt); }`. Commit 0x6dd6a0: `cur==8 -> listeners
+0x34; cur==9 -> +0x14; next==9 -> +0x10; prev=cur; cur=next` (emit-gated by ctx+0x24).
Tick 0x6e69e0 `(ctx*, float* dt)`, `ret 8`: `timer-=*dt; if(timer<0){*dt=-timer; timer=0}
else *dt=0;` then dispatch `cur-2` through 0x6e6ce4. States 5..8 use the default no-op 0x6e6a8e.

```c
// Step entry (all states), 0x6e62d0                                         SV
fo->timer = dt; fo->seed94 = ctx->seed1c; FL = fo->wfd->ffd + 0xc;
// Step 6, 0x6e65b5
if (fo->burst84 > 0) {
  int b = fo->burst84; ShotNotify(fo, 1.0f, ctx);           // 0x6dd960
  bool shot = b > fo->burst84;
  fo->timer = SecondsPerShot(fo) /*0x6cd3d0*/ - ffd->SpawnDelay /*FFD+0xd0*/;
  if (shot) fo->a0 ^= 0x10000;                              // shot toggle (replicated INF)
  if (HasAmmo(fo) /*0x6d6eb0*/ && shot) {
    if (FL->FireLogicType == 1) { fo->next = 7; return; }   // 0x6e6625 bolt owed
    if (fo->burst84 > 0)        { fo->next = 9; return; }
  }
}
fo->next = 1;      // includes "last shell fired": NO pump owed natively (HasAmmo false)
// Step 7, 0x6e672d  (hold gate)
bool hold = (fo->a4 && FL->HoldUntilFireRelease) || (fo->a5 && FL->HoldUntilZoomRelease);
if (hold && (fo->a4 || !(ctx->input2c & 1))) return;       // timer stays = dt; 0x6e698a
fo->timer = FL->BoltActionDelay; fo->next = 8;              // 0x6e6771
// Step 8, 0x6e6785
for (l : listeners(ctx->emit24)) l->vt0x30/4;          // pump/bolt start
fo->timer = FL->BoltActionTime; fo->next = 1;               // 0x6e67a9 / 0x6e67af
// Commit leaving 8 -> listeners +0x34 (client: bare ret 0x533f90)
// Step 1, 0x6e6322
if (fo->prev == 12) { ReloadEndNotify(emit); if (FL->PRST > 0) { timer=PRST; prev=1; return; } }
if (FL->FireLogicType != 2) {
  bool h = HasAmmo(fo) ? (fo->a4 && FL->WaitForTrigger88) || (fo->a5 && FL->WaitForZoom89)
                       : (fo->a4 && FL->HoldReloadUntilFire8a) || (fo->a5 && FL->HoldReloadUntilZoom8b);
  if (h && (fo->a4 || !(ctx->input2c & 1))) return;         // 0x6e63cd
  FireEndNotify(emit);                                      // 0x6dd930, slot +0x08
}
fo->next = 2; fo->burst84 = 0; fo->timer = 0;
```

**What holds 7:** only `+0xa4 && HoldUntilFireRelease` or `+0xa5 && HoldUntilZoomRelease`
(SV). **What releases it:** fire release (Update clears +0xa4) for the fire flag; zoom release
(Update clears +0xa5) or fire re-press after release for the zoom flag (sniper "fire again to
cycle"). Then 7 -> 8 after BoltActionDelay; 8 -> 1 after **BoltActionTime** (SV, FL+0x44);
1 -> 2 at once unless WaitForTrigger/ZoomRelease (or the reload-hold pair when empty) holds.
SPAS12_sp (LO, docs/BC2-PUMP-RELOAD-20261001.md): both bolt hold flags **false**, Delay 0.5 s,
Time 0. So the SPAS gate never holds: on the frame of the shot it reaches cur=7/next=8 with
the 0.5 s delay running, then 8, 1, 2 inside one Update once the delay ends.

Other paths touching 7/8 (SV): SprintInterrupt 0x6d6d40 (only if WFD+0x44 AbortReloadOnSprint):
`sprint && (next==7||cur==7||cur==8)` -> `+0xa8|=0x10, next=1, timer=0, +0x54=0`; sprint end
with bit 0x10 -> `next=7, timer=0` (pump owed resumes). Deactivate 0x6e1eb0 (not killed):
`cur in {7,8} || next==7 || bit 0x10` -> `cur=next=7, timer=0` (pump owed survives a weapon
switch natively); killed -> cur=next=1. Restore 0x6d71a0 overwrites cur/next/timer/+0x48/
+0x54/+0x58/+0xa0/+0xa4/+0xa5/+0x7c/+0x80/+0x84 from a 0x40-byte snapshot (snapshot writer
around 0x6cd986 copies +0xa4/+0xa5 to snap+0x3b/+0x3c). Readers of +0xa4/+0xa5 outside the
machine: server-soldier code at 0x6c2d5d/0x6c2db4 (iterates server soldier+0x2b8 items; INF
"is firing/zooming" broadcast).

### Options for a VR pump hold

| Option | Verdict |
|---|---|
| A. Drive ctx input bits (fire/zoom) or the +0xa4/+0xa5 latches | **Rejected for SPAS (SV):** the gate also needs FL+0x4c/+0x4d, which are false; forcing ctx fire risks BeginFire if the chain reaches Step 2 in the same Update; +0xa4 leaks to the server reader 0x6c2d5d and to snapshots. |
| B. Write FL hold flags / swap fo+8 to a cloned WFD/FFD | Rejected: shared data (all copies/all SPAS) or a large pointer swap. |
| C. **Step-hook hold at state 7** (recommended) | In the existing Step hook (0x6e62d0 is already hooked): if `cur==7` (Step only runs when next==cur) and the VR pump is owed for this owner and weapon, do exactly what native Step does on its hold path: `fo->timer = dt; fo->seed94 = ctx->seed1c; return;` without calling the original. Tick(7) (no-op) consumes dt. This is byte-for-byte the native held-gate outcome (0x6e6761 -> 0x6e698a), so no restoration is needed. On rack, stop holding: native Step 7 runs (timer = BoltActionDelay, next 8), then +0x30, 8->1 (+0x34), 1->2. |
| D. Update-level dt override (existing `RunReloadDeltaOverride`) while `cur==7 && next==8` | Fallback with no new write type: SPAS rests in cur7/next8 for 0.5 s after each shot (SV path + LO data). dt=0 skips the whole loop but also freezes +0x48 cooldown, 0x6d6c60 heat and +0x5c/+0x60 timers for that copy (harmless for SPAS, INF). Holds mid-delay, not at the gate. |
| E. Write next/timer or call Commit 0x6dd6a0 | Not needed. Commit has no guard and would skip handlers; manual next writes bypass Step 7/8 notifications. |

Commit is *not* a hold point: after Commit 6->7 the same Update runs Step 7 immediately.

**Copies and authority (SV for the separate updates, INF for the replication direction):**
the server item+0x10, client A (wrapper+0x3c) and client B (wrapper+0x40) each run their own
Update/Step with their own ctx; all three reach Step 7. Apply the hold to **all three, server
first**, keyed by owner + weapon + shot generation (arm on Commit 6->7 for that copy). A
client-only hold would be overwritten by Restore (cur/next/timer are in the snapshot) and the
server would be ready to fire while the client is not. Release all three in the same frame.
Fire presses while held are ignored natively (Step 7/Tick 7 never call BeginFire), so no shot,
no dry click. Reload input is also ignored while held (reload is only handled in Step 2):
release the hold (or let the native pump run) when the player starts a reload, otherwise the
SPAS cannot reload with a pump owed. Everything runs inside the per-frame Update, off the shot
path (ShotNotify happens in Step 6, before 7). Server and client Updates may be on different
threads (INF): keep the VR "racked" flag atomic and per-owner.

Timing: after release, BoltActionDelay (0.5 s) still runs before Step 8 and the native +0x30
pump animation/sound (client forwarder 0x8024a0). Release at the **rear stop** so the delay
overlaps the forward stroke; shortening it means writing the native-set timer (not
recommended). Native skips the pump after the last shell (Step 6 `next=1`) and after reload;
any "chamber empty, rack first" rule there is VR-side (block the fire input), not state 7.

## 2. Dry fire / empty click (SV unless marked)

```c
// Step 2, 0x6e640d.., fire part
if ((ctx->input2c & 1) && !fo->overheated8a) {
  fo->a4 = 1; fo->a5 = ctx->zoom27;
  if (HasAmmo(fo))  { ...BeginFire / zoom-before-fire... }
  else if (!HasAnyAmmo(fo) /*0x6d7120*/ && !fo->fireHeld89)          // 0x6e64b4
    for (l : listeners(ctx->emit24)) l->vt0x1c/4;              // 0x6e64e7
}
fo->fireHeld89 = ctx->input2c & 1;                                   // 0x6e64f1
// HasAmmo 0x6d6eb0: ffd!=0 && (InfiniteMag(fo) /*0x6cd5e0*/ || fo->loaded7c > 0)
// HasAnyAmmo 0x6d7120: InfiniteMag || (a8&8) || NumberOfMagazines==-1 || scaled==-1
//                     || loaded>0 || reserve80>0
```

So +0x1c = rising fire edge (vs the previous Step-2 sample, +0x89), not overheated, loaded
<=0 and finite reserve <=0, emit flag set. ABI of slot +0x1c: thiscall, no args, `ret`, no
return value.

Listener implementations:
- Client wrapper+0xd0 listener: slot +0x1c = **0x7f9080** in all 11 wrapper vtables
  (0x14322a0 and siblings; refs 0x14322bc..0x1432b84). `if(!(l+0x55c & 0x80)){ l+0x55c |= 0x80;
  if(l->snd34) OutOfAmmoSound(l->snd34) /*tail jmp 0x6ebc30*/; }` (per-wrapper-update latch,
  cleared by 0x8382b4).
  `l+0x34` = wrapper+0x104 = a 0x44-byte client weapon sound helper (vtable 0x142528c, ctor
  0x6d4280(FFD*, wrapper+0x30), built at 0x832945; class name INF).
- **0x6ebc30** `OutOfAmmoSound(this)` thiscall, no args, `ret`: `if(!this->ffd4->OutOfAmmo
  SoundEffect /*FFD+0x194, SoundAsset, typeinfo*/) return;` builds params `IsFirstPerson`
  (this+0x40 bit1), `IsZooming` (bit2), `OCCL` (0 if first person else 1.0), then 0x637290
  (asset, this+8, params) (INF: play one-shot). Only caller 0x7f9097.
- Server: the 0x130-byte effects object built by 0x534290 (from 0x55c150, server item+0xc
  path) has secondary vtable **0x1416b10** at +0xd0 with slot +0x1c = 0x533f30 = bare `ret`
  (+0x30/+0x34 also `ret`). Registration on the server firing (addListener = firing vtable
  0x1425814 slot 0 = 0x6f1930) not traced: INF.

**Cheapest observation point (recommended):** the existing Step hook, client copy with
ctx+0x24 set, before calling the original, when `cur==2 && next==2`: read `ctx+0x2c&1`,
`fo+0x89`, `fo+0x8a`, `fo+0x7c`. Click when `fire && !fo->fireHeld89 && !fo->overheated8a &&
loaded<=0` (InfiniteMag false). This covers both the native +0x1c case and the far more common
VR case (loaded 0, reserve > 0, auto-reload suppressed), which natively makes no sound. It
needs no new hook and no write. Pick one copy (client A) to avoid duplicate clicks.
A detour on 0x7f9080 is unique but sees only the "totally empty" case and loses the firing
object. To play the native sound for the VR case, calling 0x6ebc30 on `[wrapper+0x104]` from
the client update thread is plausible (no args, no state), but unverified: INF, test first.

## 3. Signatures (all unique over .text+ctr, `pump_sigs.py`)

| Addr | What | Signature |
|---|---|---|
| 0x6e62d0 | Step entry | `F3 0F 10 44 24 08 53 8B 5C 24 08 55 56 8B F1 8B 46 08 8B 68 40 F3 0F 11` |
| 0x6e6322 | Step 1 | `39 56 40 75 2D 0F B6 53 24 52 8B CE E8 ?? ?? ?? ?? F3 0F 10 45 00` |
| 0x6e63cd | Step 1 hold test | `0A C1 84 C0 74 12 84 D2 0F 85 ?? ?? ?? ?? F6 43 2C 01 0F 84` |
| 0x6e64b4 | Step 2 dry gate | `8B CE E8 ?? ?? ?? ?? 84 C0 75 32 38 86 89 00 00 00 75 2A 0F B6 43 24 50` |
| 0x6e64e0 | Step 2 +0x1c call loop | `8B 0F 8B 11 8B 42 1C FF D0 83 C7 04 3B 7E 1C 75 EF 8A 4B 2C 80 E1 01 88` |
| 0x6e65b5 | Step 6 | `8B BE 84 00 00 00 85 FF 0F 8E EC 01 00 00 D9 E8 53 51 D9 1C 24 8B CE E8` |
| 0x6e6625 | Step 6 next=7 | `83 7D 18 01 75 0E 5F C7 46 44 07 00 00 00 5E 5D 5B C2 08 00` |
| 0x6e672d | Step 7 gate | `8A 96 A4 00 00 00 84 D2 74 0A 80 7D 4D 00 74 04 B1 01 EB 02 32 C9` |
| 0x6e6771 | Step 7 release | `D9 45 48 5F D9 5E 50 C7 46 44 08 00 00 00 5E 5D 5B C2 08 00` |
| 0x6e6785 | Step 8 | `80 7B 24 00 74 05 8B 7E 18 EB 03 8B 7E 1C 3B 7E 1C 74 11 8B 0F 8B 11 8B` |
| 0x6e6798 | Step 8 +0x30 / timer | `8B 0F 8B 11 8B 42 30 FF D0 83 C7 04 3B 7E 1C 75 EF D9 45 44 D9 5E 50 5F` |
| 0x6e69e0 | Tick | `51 53 56 8B F1 8B 4C 24 14 D9 46 50 F3 0F 10 01 8B 46 08 8B 58 40` |
| 0x6e911b | Update latch clear | `F6 47 2C 01 75 07 C6 86 A4 00 00 00 00 80 7F 27 00 75 07 C6 86 A5` |
| 0x6e9140 | Update loop | `0F 2F 46 50 72 24 8B 56 44 3B 56 3C 74 0A 57 8B CE E8 ?? ?? ?? ?? EB 1F` |
| 0x6dd6a0 | Commit | `53 8B 5C 24 08 56 8B F1 8B 46 3C 83 E8 08 57 74 2A 83 E8 01 75 4B` |
| 0x6d6eb0 | HasAmmo | `56 8B F1 8B 46 08 83 78 40 00 75 04 32 C0 5E C3 E8 ?? ?? ?? ?? 84 C0` |
| 0x6d7120 | HasAnyAmmo | `51 56 8B F1 E8 ?? ?? ?? ?? 84 C0 75 3D F6 86 A8 00 00 00 08 75 34` |
| 0x6d6d40 | SprintInterrupt | `32 D2 38 54 24 04 74 49 8B 41 08 8B 40 40 83 78 2C 00 75 1A` |
| 0x6e1ee3 | Deactivate keep-7 | `B9 07 00 00 00 3B C1 74 13 83 F8 08 74 0E 39 4E 44 74 09 F6 86 A8 00 00` |
| 0x6d71a0 | Restore | `56 57 8B 7C 24 0C 8A 47 39 8B F1 3A 86 A6 00 00 00 74 24 53 8B 5E 18` |
| 0x7f9080 | client dry fwd | `B0 80 84 81 5C 05 00 00 75 12 08 81 5C 05 00 00 8B 49 34 85 C9 74 05 E9` |
| 0x6ebc30 | OutOfAmmoSound | `81 EC D8 00 00 00 56 8B F1 8B 46 04 83 B8 94 01 00 00 00 0F 84` |
| 0x6d4280 | sound helper ctor | `8B 44 24 04 56 8B F1 8A 56 40 8B 4C 24 0C 80 E2 FA 57 33 FF 80 CA 02` |
| 0x6f1930 | firing addListener | `8B 51 1C 8B 41 18 83 C1 18 3B C2 56 8B 74 24 08 74 0F 39 30` |

## 4. Gaps

- SPAS WaitForTrigger/ZoomRelease (FL+0x88/+0x89) values not read; they decide whether 1->2
  waits for trigger release after the pump.
- Which copy's Restore source is server state vs client history: INF.
- Server listener registration (0x1416b10 object on server item+0x10) and the soldier+0x1c4
  listener's +0x1c slot: not traced.
- 0x637290 "play sound" meaning and calling 0x6ebc30 directly: INF, needs a runtime test.
- Option C must be checked live: all three copies held/released together, Restore during the
  hold, sprint and weapon switch while owed, reload request while owed, MP not tested.
