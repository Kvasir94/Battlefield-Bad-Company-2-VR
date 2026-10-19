> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 feel-layer native hook points (static RE, build 795745, x86)

> In this report **SV** means read in the static disassembly. It is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md), not that page's repo-code SV. Nothing here
> has run in the game.

Scope: BFBC2Game.exe, image base 0x400000, read-only static disassembly
(capstone, [`tools/re/bc2re.py`](../../tools/re/bc2re.py)). No runtime or headset evidence; nothing here is
HA/LO. Labels: **SV** = I read the disassembly that shows it; **INF** =
inferred from behaviour, names or data.

Naming follows `docs/re/BC2_WEAPON_RUNTIME.md` (Update 0x6e9000, Step
0x6e62d0, Commit 0x6dd6a0, Transfer 0x6d6f40, Restore 0x6d71a0; firing object
+0x3c current / +0x40 previous / +0x44 next / +0x50 phase timer). New names
in this report: **Tick** 0x6e69e0, **BeginFire** 0x6e1fa0, **SprintInterrupt**
0x6d6d40, **Activate** 0x6e6d20, **Deactivate** 0x6e1eb0, **ResumeReload**
0x6cd8c0, **ReloadProgressPercent** 0x6ddb90, **ShotNotify** 0x6dd960,
firing-object constructor 0x6f1780.

Field mapping used below (SV from Step's `ebp = [[fo+8]+0x40] + 0xc`, typeinfo
offsets from bc2_types.json): `ebp` = `FiringFunctionData.FireLogic`
(`FireLogicData`). FL+0x00 PostReloadSequenceTime, +0x04 ReloadThreshold, +0x08
ReloadDelay, +0x0c ReloadTime, +0x10 PreFireDelay, +0x14 ReloadType, +0x18
FireLogicType, +0x1c ZoomBeforeFireDelay, +0x20 ReloadLogic, +0x24
HoldAndRelease (Delay +0x28, MaxHoldTime +0x30, KilledHoldingPowerModifier
+0x2c, ForceFireWhenKilledHolding +0x3c), +0x44 BoltAction (Time +0x44, Delay
+0x48, HoldUntilZoomRelease +0x4c, HoldUntilFireRelease +0x4d), +0x88/+0x89
WaitForTrigger/ZoomRelease, +0x8a/+0x8b HoldReloadUntilFire/ZoomRelease.
`WeaponFiringData` +0x38 DeployTime, +0x3c ReactivateCooldownTime, +0x44
AbortReloadOnSprint, +0x48 FireOnDeploy. FFD+0xd0 = Shot.SpawnDelay, FFD+0xd8
= Shot.NumberOfBulletsPerShot (burst counter seed, fo+0x84).

## October 8 correction pointers (static report evidence)

[BC2_DISASM_PUMP_AND_DRY_FIRE.md §0](BC2_DISASM_PUMP_AND_DRY_FIRE.md#0-corrections-to-the-existing-docs-sv)
corrects this original decode: Step initializes `+0x50=dt` at `0x6e62e5`;
Tick consumes that time, so state 4 does not imply an Update spin or an unused
shipped path. State 7 uses firing-object hold latches `+0xa4/+0xa5`, not raw
current input as its hold state. Native dry-fire notification requires both loaded
and finite reserve empty. The corrected entries below propagate that report;
this documentation update performed no independent disassembly or runtime proof.

## 0. How the machine runs (SV, Update 0x6e9000..0x6e918a)

Update(ctx*, int) `ret 8`. Per call:
1. ctx+0x2c bit1 (OrderInputAction) -> listener slot +0x28.
2. If `WeaponFiringData.AbortReloadOnSprint`: `SprintInterrupt(ctx+0x28)`.
3. FireLogicType 4 (detonated) -> separate machine 0x6e1bb0, return.
4. Loop while remaining dt (`ctx+0x18`, local copy) > 0:
   - if timer `+0x50` > 0 -> **Tick**(ctx, &dt) (consumes dt; dispatch on current, table 0x6e6ce4)
   - else if next != current -> **Commit**(ctx) (prev = cur, cur = next; fires +0x34/+0x14/+0x10)
   - else -> **Step**(ctx, dt) (dispatch on current, table 0x6e6994; initializes timer=dt at entry, may replace timer/next, does not itself consume dt)

So Step is the "timer expired in state N" handler and Tick is the "waiting in
state N" handler. Several Commits/Steps can run in one Update. Step's entry timer
ensures a held/no-op handler is followed by Tick consuming the remaining time;
see the October 8 correction report.

Ctx fields (SV from Update/Step reads and client builder 0x7d4c79..0x7d4d73):
+0x18 dt; +0x1c seed (copied to fo+0x94); **+0x24 emit-events flag** (byte
passed by the caller; every listener notification iterates `[fo+0x18]` when
set and `[fo+0x1c]` (= empty range) when clear; INF: clear on prediction
replay); +0x27 zoom held; +0x28 sprint (blocks auto-reload, drives
SprintInterrupt); +0x2c bit0 fire (FireInputAction vs TriggerPullWeight), bit1
OrderInputAction, bit2 ReloadInputAction.

Firing flags `+0xa8` (SV): bit0 "reload begin already notified / reload
active" (set Step 12, Tick 10-12; cleared Step 2 and Tick 2); bit1 set by
BeginFire, cleared in Step 2 when +0x20 fires; bit3 non-finite ammo; bit4
"bolt pending after interrupt".

## 1. Step dispatch table 0x6e6994 (task 2)

`jmp [ecx*4+0x6e6994]` at 0x6e6307 after `cmp ecx,0xf; ja`. All 16 entries SV.

| St | Step handler | Tick (0x6e6ce4, states 2..13) | Behaviour (SV) | Name |
|---|---|---|---|---|
| 0 | 0x6e630e | none (default no-op) | timer = `WeaponFiringData.DeployTime`, next = 2. Entered via Activate 0x6e6d20 (`next=0`, 0x6e6db4), ctor/reset | **deploy** |
| 1 | 0x6e6322 | none | if prev==12: reload-end notify `0x6ddb60` (slot +0x3c); if PostReloadSequenceTime>0: timer=PRST, **prev:=1** (so the end notify fires once), return. Else (auto: next=2 immediately) / waits for fire/zoom release per BoltAction/WaitFor* flags, then `0x6dd930` (slot +0x08), next=2, +0x84=0 | **post-reload / release wait** |
| 2 | 0x6e640d | 0x6e6a87 (clear bit0) | first entry: slot +0x20 (ready). Fire edge with ammo: if !zoom && ZoomBeforeFireDelay>0 -> next=4, timer=ZBFD; else BeginFire. Fire edge with no ammo anywhere (loaded<=0 AND finite reserve<=0; 0x6d6eb0 false, 0x6d7120 false, fire not previously held): slot +0x1c (dry fire). Reload: `ctx+0x24 && !ctx+0x28(sprint) && next==2 && reserve>0 && (reload input && loaded!=capacity || loaded==0 && !nonfinite)` -> next=10, timer 0 (0x6e6535..0x6e6579) | **ready / idle** |
| 3 | 0x6e698a (no-op) | 0x6e6a97 | Entered only as *current* by BeginFire (`cur=3` at 0x6e1ffb / 0x6e6bca) when PreFireDelay>0, so Step(3) never runs. Tick: while fire held wait; on release -> slot +0x08, next=2 (cancel) | **pre-fire delay (cancellable)** |
| 4 | 0x6e698a (no-op) | 0x6e6abd | Step entry leaves timer=dt; the next Tick consumes time and runs BeginFire after updating +0x5c=60/RateOfFire. Step 2 supplies next=4 and ZoomBeforeFireDelay. The earlier timer-zero/spin/unused inference is corrected by the October 8 pump/dry-fire report §0 | **zoom-before-fire delay** |
| 5 | 0x6e658c | default | `0x6dd790` (slot +0x04, recoil/dispersion), timer = Shot.SpawnDelay, next=6 | **shot prep** |
| 6 | 0x6e65b5 | default | if burst left (+0x84>0): ShotNotify(1.0, ctx); timer = 60/RoF - SpawnDelay; then FireLogicType 1 -> next=7, burst left -> 9, else -> 1 | **shot release (single/burst/bolt)** |
| 7 | 0x6e672d | default | hold gate reads firing-object +0xa4 (fire latch) / +0xa5 (zoom latch at accepted fire), gated by BoltAction.HoldUntilFireRelease / HoldUntilZoomRelease. Update clears latches on release; raw fire re-press participates in the zoom-hold case. Held Step leaves timer=dt; on release timer=BoltActionDelay, next=8. See correction report §0/§1 | **bolt/pump hold** |
| 8 | 0x6e6785 | default | slot **+0x30** (bolt start), timer=BoltActionTime, next=1. Commit leaving 8 -> slot **+0x34** | **bolt/pump cycle** |
| 9 | 0x6e6654 | 0x6e6c45 | auto: 0x6dd790 + ShotNotify(1.0) per interval; Tick: on fire release with no burst left -> slot +0x08, next=2. Commit entering 9 -> +0x10, leaving 9 -> +0x14 | **automatic fire** |
| 10 | 0x6e67bd | 0x6e6ae0 | `0x6ddb30` (slot +0x38 reload begin); +0x54 = ReloadTime*mult+ReloadDelay; timer=ReloadDelay; next=11 | **reload begin** |
| 11 | 0x6e67fa | 0x6e6ae0 | timer=ReloadTime*mult*ReloadThreshold; next=12. Tick(11): if bit0 clear -> `0x6ddb30` again, set bit0 | **reload wait (to transfer point)** |
| 12 | 0x6e681e | 0x6e6ae0 | bit0 set; **Transfer(ctx+0x24)** at 0x6e682c; timer = ReloadTime*mult*(1-Threshold); ReloadType!=0, full (0x6d6ee0), or reserve<=0 -> next=1; else next=11 (per-shell loop) | **ammo transfer + tail** |
| 13 | 0x6e689b | 0x6e6c90 | 0x6dd790 (slot +0x04); +0x88=0; timer=+0x54=MaxHoldTime; +0x58=HoldAndRelease.Delay; next=14. Tick: release after Delay (or MaxHoldTime) -> next=14 | **hold-and-release charge** (FireLogicType 3) |
| 14 | 0x6e68d1 | none | `0x6dd930` (slot +0x08 release); timer=Shot.SpawnDelay; next=15 | **hold-and-release release** |
| 15 | 0x6e68fa | none | slot **+0x54**; power = 0x6d6dc0 (from hold time); ShotNotify(power, ctx); loaded==0 && reserve>0 -> next=10 (auto-reload) else cap==-1 -> 10 else 1 | **hold-and-release fire** |

Tick 10/11/12 (0x6e6ae0, SV) also contains the **single-round fire
interrupt** (ReloadType 0, fire pressed, has ammo, not overheated):
`0x6ddb60` reload-end notify at 0x6e6b64, then either (PRST>0) bit0, timer=PRST,
**cur=next=2 written directly** and **Transfer** at 0x6e6b94, or (PRST==0)
`0x6dd750` (slot +0x00) and straight to fire (cur=3 if PreFireDelay, next=5).
Fire pressed during reload otherwise -> slot +0x18.

Other state writers (SV): BeginFire 0x6e1fa0 (slot +0x00, cur=3 if
PreFireDelay>0, +0x84=burst, next 5 / 9 (auto) / 13 (hold-release));
SprintInterrupt 0x6d6d40; ResumeReload 0x6cd8c0 (ReloadLogic==1 only: cur=11,
next=1, timer=+0x54=t; caller 0x6c5fe7, INF network/switch-back); Activate
0x6e6d20 (instant Transfer if empty and zero reload time; keeps 7/10..12;
FireOnDeploy -> BeginFire; else next=0; slot +0x48); Deactivate 0x6e1eb0(bool
killed) (keeps 10..12 if ReloadLogic==1; bolt pending -> cur=next=7; else
prev=cur, cur=next=1 silently; killed+hold-release -> forced ShotNotify; slot
+0x4c; +0x48 = ReactivateCooldownTime); Restore 0x6d71a0; reset 0x6dd580;
ctor 0x6f1780 (size 0xac; callers 0x7f8c13 client A, 0x7f8c3a client B,
0x561b82 server).

## 2. Firing listener interface (vector at fo+0x18 begin / fo+0x1c end)

All slots are thiscall on the listener; every call site is gated by ctx+0x24
except Activate/Deactivate/0x6d6e80 (SV).

| Slot | Args / ret | Fired by (SV) | Meaning |
|---|---|---|---|
| +0x00 | () | BeginFire 0x6e1fc8, 0x6dd750 | fire accepted (trigger) |
| +0x04 | () | 0x6dd790 (states 5, 9, 13; 0x6e1d64) | shot prep |
| +0x08 | () | 0x6dd930 (Step 1/14, Tick 3/9) | fire released / fire end |
| +0x0c | (float power, bool tracer, ctx*) `ret 0xc` | ShotNotify 0x6dd9e3 | **shot** |
| +0x10 / +0x14 | () | Commit entering / leaving 9 | auto-fire start / stop |
| +0x18 | () | Tick 10-12 | fire pressed while reloading |
| +0x1c | () | Step 2 0x6e64e7 | **dry fire (empty click)** |
| +0x20 | () | Step 2 0x6e6439 | ready entered |
| +0x28 | () | Update 0x6e906d | order input |
| +0x30 | () | Step 8 0x6e679f | **bolt/pump start** |
| +0x34 | () | Commit 0x6dd6f7 (leaving 8) | **bolt/pump end** |
| +0x38 | () | 0x6ddb30 | **reload begin** |
| +0x3c | () | 0x6ddb60 | **reload end** |
| +0x40 | (bool) | 0x6ddae0 | detonator toggle (FLT 4) |
| +0x48 / +0x4c | () | Activate / Deactivate | weapon activated / deactivated |
| +0x50 | () | 0x6d6e80 | unknown |
| +0x54 | () | Step 15 | hold-and-release fire |

Client implementation: secondary vtable 0x14322a0 of the client wrapper
subobject at **wrapper+0xd0** (`lea reg,[this-0xd0]` in 0x80f640/0x80f6b0,
`[ecx-0xd0]` in 0x808220). 11 vtables contain 0x8024a0 (wrapper variants).
In 0x14322a0: +0x30 = 0x8024a0, **+0x34 = 0x533f90 (`ret`, no-op)**, +0x38 =
0x80f640, +0x3c = 0x80f6b0, +0x0c = 0x808220, +0x54 = 0x8024e0, +0x40 =
0x8025b0 (`ret 4`). A subclass vtable at 0x1432750 overrides +0x38/+0x3c with
0x8171d0 / 0x817250, which call the base forwarders directly (the only direct
callers) and then post event (0x1a, 0x17) / (0x1a, 0x19).

Dedupe latch (SV): each forwarder tests/sets a bit at listener+0x55c/+0x55d
(wrapper+0x62c/+0x62d): +0x30 bit 0x02, +0x38 0x04, +0x3c 0x08, +0x40 0x10,
+0x54 0x20 of +0x55d. Cleared by `mov word [esi+0x62c],0` at 0x8382b4 in
wrapper vmethod 0x838290 (once per wrapper update) and 0x7f00f0. So each
client forwarder emits at most once per wrapper update even though two client
firing copies (A/B) notify (INF that both A and B carry this listener).

## 3. Forwarder ABI and detour notes (task 1, compact)

All thiscall, `this` in ECX. Signatures are unique in .text (checked with
`find_bytes`), no relocations or rel32 inside the stolen bytes.

| Addr | this | Args / ret | Stolen bytes (instr boundary) | Signature |
|---|---|---|---|---|
| 0x8024a0 bolt start fwd | listener (wrapper+0xd0) | none, `ret` | 8: `mov al,2` (2) + `test [ecx+55Dh],al` (6) | `B0 02 84 81 5D 05 00 00 75 31 08 81 5D 05 00 00 56 8B 71 10` |
| 0x80f640 reload begin fwd | listener | none, `ret` | 5: `push edi; mov edi,ecx; mov al,4` | `57 8B F9 B0 04 84 87 5D 05 00 00 75 60 08 87 5D 05 00 00 8B 4F 34` |
| 0x80f6b0 reload end fwd | listener | none, `ret` | 5: `push ebx; mov ebx,ecx; mov al,8` | `53 8B D9 B0 08 84 83 5D 05 00 00 75 55 08 83 5D 05 00 00 55` |
| 0x6ddb30 reload begin notifier | firing object | (bool emit) `ret 4` | 5: `cmp byte [esp+4],0` | `80 7C 24 04 00 56 57 8B F9 74 05 8B 77 18 EB 03 8B 77 1C 3B 77 1C 74 11 8B 0E 8B 01 8B 50 38` (31 B; first 22 B also match 0x6dd750/0x6dd930/0x6ddb60) |
| 0x6ddb60 reload end notifier | firing object | (bool emit) `ret 4` | 5 | same, last byte `3C` |
| 0x6ddb90 ReloadProgressPercent | firing object | none, returns int 0..100 in EAX, `ret` | 6: `mov eax,[ecx+3Ch]; add eax,-0Ah` | `8B 41 3C 83 C0 F6 83 EC 0C 83 F8 02 76 06 33 C0 83 C4 0C C3` |
| 0x6dd960 ShotNotify | firing object | (float power, ctx*) `ret 8`; no return value | 10: `push ecx; push esi; mov esi,ecx; mov eax,[esi+98h]` | `51 56 8B F1 8B 86 98 00 00 00 83 F8 FF C6 86 88 00 00 00 00` |

Callers (SV):
- 0x8024a0: no direct callers; vtable slot +0x30 in 11 vtables (0x14322d0, 0x14324a0, 0x1432558, 0x1432610, 0x14326c8, 0x1432780, 0x1432838, 0x14328f0, 0x14329a8, 0x1432ae0, 0x1432b98). Reached from Step 8 (0x6e679f) after state 7 released, BoltActionDelay elapsed.
- 0x80f640: vtable +0x38 (9 vtables) + direct 0x8171d4 (override 0x8171d0). Reached from 0x6ddb30.
- 0x80f6b0: vtable +0x3c (8 vtables) + direct 0x817254 (override 0x817250). Reached from 0x6ddb60.
- 0x6ddb30: 0x6e67c4 (Step 10, entry from state 2 reload edge / auto-reload) and 0x6e6b1c (Tick 11 while flag bit0 clear). **Per static reading a magazine reload notifies twice** (Step 10, then first Tick in 11; Step 10 does not set bit0); single-round loops do not repeat (Step 12 sets bit0). The 0x6e6b1c path is the one used after ResumeReload (cur=11 directly).
- 0x6ddb60: 0x6e632e (Step 1 when prev==12: normal end) and 0x6e6b64 (Tick 10-12 single-round fire interrupt).
- 0x6ddb90: 0x5ddb7d, 0x732d44, 0x751970, 0x752c70, 0x77a407 (HUD/UI consumers, INF). Pure getter: `(1 - (+0x54)/(ReloadTime*mult+ReloadDelay))*100` if cur in 10..12 else 0. Not an event; call it or reproduce it, no hook needed.
- 0x6dd960: 0x6e65cc (Step 6), 0x6e669d (Step 9), 0x6e6933 (Step 15), 0x6e1f33 (Deactivate, killed while holding), 0x6ddac4 (in 0x6dda50, detonated machine). Sets +0x88=0, computes `tracer = (capacity-loaded) % AmmoConfig.TraceFrequency == 0`, notifies +0x0c, accumulates overheat (+0x8c, +0x8a). The burst counter +0x84 drops during the listener call (Step 6 compares before/after). **I did not find the per-shot `loaded` (+0x7c) decrement** in firing code 0x6c0000-0x6f8000 or the Client/ServerShoot bodies; the only direct writers are setters (0x6cd520, 0x6d6ab0 multiplier, 0x6dd580 reset, 0x6f1780 ctor, Restore, Transfer `add` at 0x6d6ff6). INF: it happens inside the +0x0c listener chain (wrapper vtable +0x4c from 0x808220).

Hooking guidance: prefer the firing-level notifiers 0x6ddb30/0x6ddb60/0x6dd960 over the client forwarders when the hook must know which firing copy (client A/B/server) and its ammo; filter on the emit byte (arg) != 0 and on the copy you treat as authoritative. The forwarders give one call per wrapper update but lose the firing object and get no pump-end (+0x34 is `ret`).

## 4. Recommended observation point per feel event (task 3)

Base rule: the five hooked functions already see every transition. Commit
0x6dd6a0 sees (cur -> next) before it writes, with ctx; Step sees state entries;
Transfer sees the ammo move. Use those first; add 0x6ddb30/0x6ddb60/0x6dd960
hooks only if the ctx+0x24 gating or call-site identity is needed.

| Feel event | Best native point | Field transition / condition | Evidence |
|---|---|---|---|
| **Reload begin** (enable mag release) | Commit with `cur==2 && next==10` (or Step entry with cur==10); optional hook 0x6ddb30 | First of the two 0x6ddb30 calls; dedupe per reload generation (bit0 of +0xa8 is set from the first state-11 Tick) | SV |
| **Mag out** | No native event. Model VR-side, gated by reload active (cur in 10..12) | Native window: state 10 (ReloadDelay) + 11 up to Threshold. The engine has no magazine-detached state | SV (absence), INF |
| **Mag seat** | Transfer 0x6d6f40 (hooked). Call sites: Step 12 0x6e682c (normal), Tick 0x6e6b94 (single-round fire interrupt), Activate 0x6e6d67 (instant reload on deploy) | `+0x7c` increases (`add [esi+0x7c],edi` at 0x6d6ff6); cur==12 for the normal case | SV |
| **Shell in** (single-round) | Same Transfer at 0x6e682c, repeated 12 -> 11 -> 12 | Commit `12 -> 11` = next shell; `12 -> 1` = last shell | SV |
| **Reload end** | Commit `cur==12 && next==1`, or hook 0x6ddb60 (fires once, in Step 1 with prev==12; prev forced to 1 when PostReloadSequenceTime>0) | Ready again at Commit `1 -> 2` | SV |
| **Reload abort** | Commit with `cur in {10,11}` and `next==1` (SprintInterrupt 0x6d6d40 when ReloadLogic==0, from Update when AbortReloadOnSprint) **and** Deactivate 0x6e1eb0 writing cur=1 directly (weapon switch, ReloadLogic==0; does not go through Commit: diff state across the Update hook or hook 0x6e1eb0). Neither path notifies +0x3c. Abort from cur==12 is not a feel abort (round already transferred; Step 1 notifies end). Also Restore 0x6d71a0 rolling state out of 10..12 | no Transfer since reload begin | SV (paths), INF (sprint meaning of ctx+0x28) |
| **Fire-interrupted shotgun reload** | Hooked Transfer at 0x6e6b94 + state written to 2 in Tick (not via Commit) | ReloadType 0, PRST>0: end notify, cur=next=2, one Transfer; PRST==0: no Transfer, straight to fire | SV |
| **Chamber / charge** (bolt/pump) | Commit `6 -> 7` (bolt owed, FireLogicType 1), Step 8 / Commit `7 -> 8` = cycle start (+0x30 / fwd 0x8024a0), Commit `8 -> 1` = cycle end (+0x34) | Chamber state itself is VR-side. Interrupted bolt: SprintInterrupt sets +0xa8 bit4 and next=1 (Commit `8 -> 1` still fires +0x34), resume writes next=7; Deactivate keeps cur=next=7 | SV |
| **Chamber after magazine reload** (charging handle) | None native. Engine goes 12 -> 1 -> 2 with PostReloadSequenceTime in state 1 | Model VR-side; state 1 timer is the native "charging" window | SV (absence), INF |
| **Shot** | Hook 0x6dd960 (power, ctx; emit byte ctx+0x24) or Commit `5 -> 6` / state-9 Step; ClientShoot/ServerShoot already SV | Shot count = burst counter +0x84 drop across the call | SV |
| **Last round / slide lock** | Post-call of ShotNotify 0x6dd960 or post-Update: `+0x7c` went from 1 to 0 while not reloading | Exact decrement site not found statically; observe +0x7c before/after the Update hook (already hooked). Slide lock = loaded==0 && cur==2 (doc rule) | SV (observation via existing hook), INF (decrement location) |
| **Dry fire on empty** | Step 2 slot +0x1c call (0x6e64e7): fire edge, previous fire not held, no ammo anywhere | cur==2, loaded<=0 AND finite reserve<=0; reserve>0 can auto-reload or be silent under the mod's reload block. See October 8 correction report §0/§2 | SV (cited static report) |
| **Auto-reload on empty** | Step 2 0x6e6572 (`next=10`) or Step 15 0x6e6969 | `+0x7c==0`, reserve>0, not sprinting; `ctx+0x28` override already used by the mod | SV |
| **Pump (shotgun)** | Same as chamber row: +0x30 at Step 8, +0x34 at Commit leaving 8 | Client listener +0x34 is a no-op, so use Commit for pump end | SV |

## Current source addendum — October 9, 2026

The earlier “decrement site not identified” statements below are historical.
Current [native adapter proof/targets](../../src/games/bc2/Bc2PhysicalWeaponNative.cpp)
identify `0x6d7040` as the accepted-shot function, with `ECX=firing+4`;
[callback](../../src/games/bc2/Bc2PhysicalWeaponNative.cpp) reports the
full firing object by subtracting 4. This is checked repository source. The
static-report corrections above retain their own evidence level, and this
refresh did no independent disassembly, game launch or runtime verification.

## 5. Historical gaps from the original report

- Per-shot loaded decrement site not identified (see section 3); verify with a
  write-watch on +0x7c during one shot.
- Whether both client copies (A/B) and the server copy register the
  wrapper+0xd0 listener is not proven; the latch implies at least two
  notifiers per wrapper (INF).
- State 4's earlier spin/unused inference is withdrawn: Step initializes timer=dt
  and Tick consumes it (October 8 correction report §0). Actual shipped-data
  reachability is not independently established here.
- Double reload-begin notification (Step 10 + Tick 11) is a static reading;
  confirm at runtime before relying on a single edge.
- ctx+0x24 meaning (emit vs replay) is INF; the gating itself is SV.
- No hook here is runtime- or headset-verified.
