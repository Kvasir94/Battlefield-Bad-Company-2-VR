# Reload wiring: KF2 VR vs BC2 (proto/reload)

Read-only assessment, 2026-10-08. Nothing was built, launched or changed.

Sources:
- **KF2:** `<kf2-repo>` @ `cd521600`, mainly `script/KF2VR/Classes/VRInteractiveReload.uc` and `VRWeaponRuntime.uc`.
- **BC2:** `origin-bc2/proto/reload` @ `50641dd` (PC builds worktree `<old-worktree>` @ `b51353e`; reload session `<worktree-reload>` on `wip/pump2` @ `7994322`).
  - `docs/2142-TRANSFER.md` §"October 8: headless verification harness" → "Reload features".
  - `src/games/bc2/Bc2ReloadFlowRuntime.cpp`, `Bc2MagazineEmptyControl.h`, `Bc2ReloadRequestCycle.h`.
  - Today's commit messages, which carry the run evidence.
- **Earlier write-ups:** `<kf2-repo>\KF2-reload-and-BC2-env-report.md` and `FrostbiteVR-vs-KF2VR-AS2-reload-assessment.md` (both Oct 7, against FrostbiteVR main `a820f76`).

## Verdict: hybrid

Keep the native reload as the only thing that credits ammo. Change *how* the native reload is controlled, following KF2's topology:
- fail-closed, not fail-open
- event-driven, not per-Update
- one authoritative copy

Do **not** move to owning ammo ourselves. The reasons are below.

The main correction to the question: **KF2 does not own ammo or replace the stock reload.** It starts the stock reload, pauses it, and calls the stock credit at the seat. BC2 already has the same shape:

| Step | KF2 | BC2 |
|---|---|---|
| Start | stock reload | native state 10 |
| Pause | `PauseTimer` | state-11 delta freeze |
| Credit | `PerformReload` | seat fast-forward into Transfer 0x6d6f40 |

What differs is where the control sits, which way it fails, and how many copies it must keep in step. Today's bugs come from those three differences, not from using the native reload.

## 1. How KF2 VR wires the physical reload

| Mechanism | KF2 VR | Where |
|---|---|---|
| Start | X/A or a pouch grip calls the **stock** reload. Preflight (catalog row, assets, rig sample, pose) fails → plain stock reload. | `VRInteractiveReload.uc` Begin; env report §1.2 |
| Blocking the native reload | Nothing is blocked. Just before the ammo notify it **pauses** the two stock timers and the anim: `PauseTimer(true,'ReloadAmmoTimer'/'ReloadStatusTimer')`, `bPauseAnims`. | `VRInteractiveReload.uc:2139-2156` |
| Ammo credit | At the physical seat: `ResumeStock`, `ClearTimer('ReloadAmmoTimer')`, then stock `PerformReload()`, so the notify cannot credit twice. The reload then ends early once the gun is "really loaded", and stock `ServerSyncReload` syncs the server on state exit. Stock `AmmoCount`/`SpareAmmoCount` own every round; there is no mod ammo. | `:2524-2540`, `:1700-1730`, `:2111` |
| Auto-reload at empty | Never reaches the engine. Fire goes through `VRWeaponRuntime.StartAction`, which **returns before `StartFire`** when the mag is out, a rack is owed, or there is no chamber. The engine never sees an empty fire press, so it never auto-reloads. | `VRWeaponRuntime.uc` `StartAction`, `CanEjectEmptyMagazine` |
| Chamber / slide lock | Entirely mod-side, per weapon: `MagazineFeedState` (native C++, 336 CTest checks) holds the flags `ChamberLoaded`/`NeedsRack`; `VRWeaponRuntime.bSlideLockPending` survives swap and stow. It is presentational plus a fire block. Release by X/A or by pulling the slide (`ReleaseSlideLock`), which clears the flags, blends `EmptyMagBlendNode` home and plays the gun's own return sound. | `VRInteractiveReload.uc:3640-3700`; `native/adapter/MagazineFeedState.h` |
| Copies / network | One client weapon actor. The owner runs its own reload timers; the server is synced by the stock state exit. No server change. | env report §1.2 |
| Failure polarity | **Fail-closed for fire** (input never forwarded). **Fail-to-stock for reload**: if anything is missing, the stock reload just plays. | |

## 2. BC2 vs KF2, mechanism by mechanism

| Mechanism | BC2 proto/reload | KF2 | Consequence for BC2 |
|---|---|---|---|
| Start | The physical begin drives native state 10 with an explicit Reload pulse. The start must pass the empty byte (+0x28), which Step 2 also tests before it accepts a Reload. | A stock reload call | Same idea. BC2's start shares a byte with the auto-reload block, so the gate must know about our own pulse (`unsolicitedArming` in `Bc2MagazineEmptyControl.h`). |
| Pause / hold | `RunReloadDeltaOverride` zeroes the frame delta in the **firing Update hook** at state 11, on 3 copies (client0 +0x3c, client1 +0x40, server item), each needing a fresh (≤50 ms) context and owner lease. | `PauseTimer` ×2: one call, one actor | BC2 has no pause API, so the hook is unavoidable. Its cost is that it is per-Update and needs leases on every call. |
| Ammo credit | On the seat, each copy's Update runs once with delta + remaining state-11 timer, so native Transfer 0x6d6f40 credits on that frame. | `PerformReload` at the seat | **Equivalent and already done.** It closes the old ~2.8 s seat-to-credit lag. |
| Auto-reload at empty | **Empty gate**: in Step (state 2), the byte at context+0x28 is overridden for one call and restored. It applies only when `ManualEmptyStepBoundaryEligible` passes, a predicate with 14 failure groups: owner lease, identity, 200 ms evidence window, branch/address, state/timer, delta, input bits and so on. **If any predicate fails, the native auto-reload runs** (fail-open). | Fire is filtered upstream of the engine (fail-closed) | **This is the biggest structural difference** and the source of most of today's reload-flow bugs (see below). BC2's auto-reload is state-driven per copy: the F2000 server copy went 2→10 alone with no fire or rack activity (runs 131709/134305). An input filter alone therefore can't replace the gate. The gate's *default* can still flip. |
| Copies | Server plus two client copies. The engine re-syncs clients from the server (11→2→10→11 at begin, run 134755). The SPAS pump already advances client0 then the server and *counts* the other client's re-sync instead of forcing it. | One copy plus stock sync | The pump's "drive authority, observe the rest" rule is KF2's model translated, and it works. The magazine path still treats all three copies as peers that must each be gated and fast-forwarded. |
| Reserve | Pooled reserve, read at gather time. 75–85% of reads deferred because a hook was in flight (cf9d8ec, now retried at a quiet moment). | Read on the actor, no contention | A consequence of doing work inside hot hooks on sim threads. |
| Chamber / slide lock | `Bc2MagazineChamber` + `ChargingHandleRack`: NeedsRack blocks fire after an empty reload, and the eject press releases a locked bolt (de2648c). All mod-side, **opt-in** (`FVR_BC2_CHAMBER_RACK=1`). | Mod-side, on by default | Same design as KF2 already. Only the default and the scenario rules are missing. |
| Pump | `PumpHoldGate` holds state 7 with the delta override, authority first. | `VRManualPump` gate | Equivalent. |

### Today's six bug classes, attributed

| Bug | Commit | Caused by the wiring? | Would KF2-style control remove it? |
|---|---|---|---|
| Lease dropped on an input gap → empty gun auto-reloaded (F2000 114713) | 20c196e | Yes: the fail-open gate needs a fresh lease | **Yes**, a fail-closed gate needs no lease to block |
| Ungated Update during the entry-gate window → server-only 2→10 | 7687afe | Yes: hook passes through while an exclusive section is held | **Yes**, blocking becomes the default for the ungated path |
| Reserve reads colliding with reload hooks | cf9d8ec | Partly: hooks do work and hold gates on the hot path | **Mostly**, if hooks only consume a mailbox |
| Client/server copy re-sync at begin (probe failed, consumer cancelled) | b51353e | Yes: copies treated as peers | **Yes**, drive the server and observe the clients, as the pump does |
| Grab claim racing the support grip | c195f62, bbad169 | No (input arbitration) | No, needed in any design |
| One-frame palette fallbacks (flicker) | cd00c1c, 0199f8a | No (presentation) | No, needed in any design |

Four of the six come from fail-open, per-Update, three-peer control. None come from letting the native reload credit the ammo.

## 3. Would owning ammo and reload state (never running the native reload) be easier or less flaky?

**No.** BC2's engine facts:

1. **The empty gate does not go away.** BC2's auto-reload is entered from Step state 2 on each copy whenever the gun is empty with reserve. Even if we never *start* a native reload, we must still stop the engine starting one, on all three copies. That is the same gate, and the same hook, that is flaking today. Owning ammo only removes the hold and the fast-forward, which are the parts that work.
2. **It needs ammo writes on three copies.**
   - The project's native rules currently forbid ammo writes: `Bc2ReloadRequestCycle.h` says "no synthetic requests, native calls, input injection or ammo writes", and AGENTS.md requires specific evidence for every enabled native feature.
   - Loaded (+0x7c) lives per copy, and the engine re-syncs clients from the server.
   - Reserve is a pooled value with ~260 uses (ledger: "touches about 260 reserve uses"), including pickups, resupply crates, HUD and checkpoints.
   - Writing them safely needs new RE: the authoritative copy, the replication direction, and who else writes the pool. That is exactly the kind of work the transfer hook spared us.
3. **We would lose native side effects** that come free today:
   - `ReloadThreshold` and the fire block during reload
   - `rlWeaponSwitchCancelsUnfinishedReload`
   - sprint and switch interplay, the HUD reload state
   - the 3P reload for any observer
   - the reload sound timing that `Bc2ReloadSoundGate` relies on to learn the clip
4. **KF2 itself does not do this.** Its robustness comes from calling stock code at discrete moments on one actor, not from owning ammo.

## 4. Migration cost vs the current approach

**Full ammo ownership: high cost, little gain.**
- Needs new native evidence: ammo/reserve write ABI on three copies, replication, pickup interplay.
- Still needs the empty gate.
- Discards the verified hold, fast-forward and Transfer receipt path, which per the coordinator passes the XM8/F2000/AEK/SPAS tests. (The committed ledger still lists only XM8 runs and "no F2000/AEK run in game", so it lags behind those tests.)
- Every scenario and the headset check would restart from zero.

**Hybrid: contained cost.** It touches the gate and the hook entry in `Bc2ReloadFlowRuntime.cpp` / `Bc2MagazineEmptyControl.h`, plus the begin and seat copy handling. Credit, hold, presentation, pouch, haptics, sounds and probes are unchanged, and the existing scenarios and gun-event rules re-verify it.

**Hybrid changes, in order:**
1. **Fail-closed empty gate.** For the identified player weapon's three firing copies at state 2 with loaded==0, block auto-reload by default. Let it through only when our own start token is pending: the physical begin, or a stock Reload press routed as our token.
   - That needs 4 predicates, not 14: copy identity, state 2/2, loaded 0, no token.
   - It drops the lease, input-freshness and evidence-window requirements, which are what failed in 114713, 131709 and 134305.
   - Recovery is always possible: a Reload press issues a token. A failure now leaves an empty gun waiting for the player, not a surprise refill.
   - Restoring the byte after each call stays as it is today (exact write and restore).
2. **Event mailbox instead of per-Update policy.** The gather thread decides once per event (start, hold, seat advance, release) and publishes a per-copy action. The hook only reads the mailbox, applies the one override, restores it and records a receipt. No gates or leases are evaluated inside the hook, which also stops the reserve-read contention.
3. **Server copy is the authority.** Advance and hold the server copy, and observe the client copies' re-sync as the pump already does (`settled_copies`, `native_resyncs`). Hold or advance a client copy only when it is the effects/listener copy (client0 +0x3c). Re-syncs then count as success, not failure.
4. **Chamber/rack on by default** once the scenario rules include the rack. This matches KF2/AS2 Advanced mode. It is already implemented.

## Concrete first step

Write the fail-closed gate as a predicate change behind an env flag (for example `FVR_BC2_EMPTY_GATE_CLOSED=1`), with no other changes:
- In `Bc2MagazineEmptyControl.h`, add `ClosedEmptyStepEligible`: identity of the three firing copies, state 2/2, loaded 0, `!startTokenPending`.
- `Bc2ReloadFlowRuntime.cpp` uses it in place of `ManualEmptyFamilyStepEligible` when the flag is set.
- Keep the leak record. Its mask should then only ever read "token pending".

Validate with the existing XM8 and F2000 fire-to-empty scenarios (mode 7, 3 dry pulls) and the stale-input tape that reproduced 114713. The pass condition is `empty_gate_leaks` = 0 and no 2→10 without a start token.

This belongs to the reload session (it owns the reload logic). The PC builds session only needs the scenario run.

Before that, one static check is worth doing: confirm in Step 2's disassembly what the +0x28 byte gates, and whether `FireLogicData.HoldReloadUntilFireRelease` (+0x8a) also holds the auto-entry. If a config-side lever exists, the gate could become a one-time per-weapon setting instead of a per-Update override. This is unverified; I found no evidence either way in the docs.

## Not checked

- No game run and no headset. Run logs were not re-analysed; run results come from commit messages and the ledger.
- Whether the +0x28 byte affects anything besides Step-2 entry is taken from the code comment in `Bc2MagazineEmptyControl.h` (runs 075702/075836), not re-verified.
- KF2 auto-reload suppression is inferred from `StartAction` filtering. I did not trace KF2's stock auto-reload trigger.
