# Why BC2 reloads are so much harder than KF2's: a code-level comparison

Read-only study, 2026-10-08. Nothing was built, launched, committed or pushed.

**Sources**
- **KF2 VR:** `<kf2-repo>` main @ `cd521600`, plus `git log --all` (894 commits).
- **BC2 VR:** `origin-bc2/proto/reload` @ `7f1689a`, read in `<old-worktree>`, plus `git log main..origin-bc2/proto/reload` (326 commits).

**Method**
- The findings came from code and git first.
- The project's earlier write-ups (`kf2-vs-bc2.md`, `reload-wiring-design.md`, `reload-closed-gate-review.md`, `friend-ai-comparison.md`, the "KF2 reload wiring assessment" thread) were read only afterwards. Section 8 says where this report agrees with them and where it goes further.
- A separate agent then reviewed the same question blind, given only your question and the code paths. Section 9 has its view and where it disagrees.

**Tags:** [V] verified in code or git. [I] inference.

---

## 1. Short answer

**Mostly the engine, and partly how we chose to attach to it.** Decompiling is a minor factor. The state machine is decoded; what's missing is the pseudocode of three functions and one ownership fact.

- **[I] Rough blame split, revised after the blind review (§9):**
  - ~50% engine
  - ~30% our integration architecture and self-imposed rules
  - ~10–15% missing decompilation (no pseudocode of Update, Step or Tick)
  - ~10% process (headless-only iteration, no headset loop)
- **KF2 did not take more control than BC2 does.** Both gate the stock reload and let the game credit the rounds. KF2 pauses stock timers and calls `PerformReload()` at the seat. BC2 zeroes the frame delta at state 11 and fast-forwards into native Transfer on the seat frame. The shape is the same [V].
- **The difference is the kind of control.** KF2 acts:
  - on **one** object
  - from **inside the game's own thread**
  - through **named, public game functions** that were designed to be called
- **BC2 acts very differently:**
  - on **three** copies of the weapon's firing logic (two client, one authoritative server, even in single player)
  - from **hooks running on game sim threads**, coordinated with decisions made on **our VR input thread**
  - through **per-call memory overrides that must be restored exactly** because of the project rules
- Nearly all of BC2's churn is the cost of that bridge. Of 282 non-merge commits, at least 35–40 are explicitly fixes for timing, races, read misses, copy ordering or hitches [V]. Almost none are about the hand interaction itself.

**So "are we taking enough control?"** Yes in amount, no in shape. The win is not more decompiling or owning ammo. It is putting the reload decision **on the game thread, against one authoritative copy, with a gate that is closed by default**. That is what KF2 got for free.

---

## 2. Side by side

| Dimension | KF2 VR | BC2 VR (proto/reload) | Why it matters |
|---|---|---|---|
| **Language / layer** | ~10.7k lines of UnrealScript running *inside* the game VM [V] | ~17.7k lines of native C++ in an injected DLL, plus ~15k test lines [V] | KF2 code is game code. BC2 code is a foreign body. |
| **Engine source** | Full UnrealScript source and SDK; `KFWeapon.uc` is readable [V] | None. A state machine decoded from disassembly; 1,991 reflected types [V] | KF2 can read *intent*. BC2 can only read *behaviour*. |
| **How functions are reached** | By name: `PauseTimer`, `PerformReload`, `ReloadComplete`, `AbortReload`, `SetPosition` [V] | By address: 5 MinHook detours (Update `0x6e9000`, Step `0x6e62d0`, Commit, Transfer, Restore) plus a fingerprinted abort helper called directly (`Bc2ReloadFlow.cpp:102-109`) [V] | KF2's levers are designed APIs with known side effects. BC2's are internal functions with side effects inferred from runs. |
| **Native hooks for reload** | **Zero reload-specific.** One generic VM-dispatch hook filters `FireAmmunition` (`Adapter.cpp:985`) [V] | Five reload-specific detours, x86 only (x64 `Install` returns false, `Bc2ReloadFlowRuntime.cpp:2432`) [V] | |
| **Pause** | `PauseTimer(true,'ReloadAmmoTimer'/'ReloadStatusTimer')` plus `bPauseAnims` (`VRInteractiveReload.uc:2139-2157`). Set once; it stays paused. [V] | No pause exists. Each held Update zeroes `ctx+0x18` (dt) with a compare-exchange and restores it in `__finally` (`Bc2ReloadHold.cpp:85-121`). This must happen **every Update, on every copy**, for as long as the hand takes. [V] | **Biggest single source of fragility.** A missed Update, meaning a hook skipped, a lease lapsed or a lock contended, means the native reload runs on and refills for free. KF2's pause can't "miss". |
| **Credit** | Stock `PerformReload()` called at the seat (`:2527-2551`) [V] | `ApplyAdvance` raises dt by the remaining state-11 timer for one Update, so Transfer runs on the seat frame (`Bc2ReloadHold.cpp:92-98`, `dccc8bc`) [V] | Equivalent. This part works in both. |
| **Copies of weapon state** | One weapon actor. The owning client runs its own timers and `ServerSyncReload` reconciles (`USE_RELOAD_SYNC`) [V] | Three firing copies: client0 `+0x3c`, client1 `+0x40` (re-synced by Restore `0x6d71a0`), server item `+0x10`. The server is authoritative. [V/D] | Every hold, advance, abort and gate must be right on 3 copies that tick independently. Client1 is constantly overwritten (3,749 restore rows in one run, per the project's earlier report). |
| **Threads** | Everything runs in the game's script tick: one thread, sequential [I, standard UE3] | Hooks run on game sim threads. Policy and hand decisions run on our gather (VR input) thread, which reads native memory concurrently. The listener thread differs from gameplay (engine map §4) [V] | This gave rise to whole categories that don't exist in KF2: **leases** (≤200–250 ms ownership snapshots), **cohort/quiet reads** (75–85% deferred, `cf9d8ec`), **lock contention** (`cf21d15`), **entry-gate windows** (`7687afe`). |
| **Auto-reload at empty** | Never reaches the engine. Fire input is filtered upstream in `VRWeaponRuntime`, and the engine never sees an empty press. [V] | The engine's Step state 2 enters reload with **no input**, from `loaded==0` alone (Step decode at `0x6e651b`). It can fire on the server copy alone. The only lever is the `ctx+0x28` byte per call (`Bc2ReloadFlowRuntime.cpp:1018-1203`). [V/D] | KF2 is fail-closed by construction. BC2 had to build a gate, and it was fail-open until `ed749e1` flipped it. |
| **Failure polarity** | If anything is missing, the stock reload just plays. Fire is blocked by the mod. [V] | Holds need a current lease. When the lease lapses, the hold stops and the native reload continues to a free refill [V]. A cancel must call the native abort on all copies within a window (`986f0fe`, `5d415a9`, `73b4b65`). | **Two failure classes pull in opposite directions** (§4). |
| **Chamber / slide lock / rack** | Mod-side state (`MagazineFeedState.h`, `VRWeaponRuntime.bSlideLockPending`) plus a native `FireAmmunition` skip [V] | The same `MagazineFeedState` ported, but **opt-in** (`FVR_BC2_CHAMBER_RACK`). The engine has no chamber or bolt field and no pump input action [V/D] | Same design. Not a difficulty source. |
| **Animation** | The stock anim is paused; bones are driven by `SkelControlSingleBone` added to the gun's own AnimTree, plus `HideBoneByName` and `EmptyMagBlendNode` [V] | Poses are injected through a private palette copy before the native packer. The reload palette changes every frame, so it must be rebased (`cd00c1c`). Which copy drives the 1P anim is **unknown** (engine map §8) [V/D] | KF2 edits the animation through API. BC2 overwrites skinning output. |
| **Networking** | Inherited from stock `USE_RELOAD_SYNC` and comes "for free" [V] | The client/server split exists even in single player and must be handled by the mod [V/D] | |
| **Per-weapon data** | Sampled at runtime from stock clips (ammo-notify time, magazine track, 17-point path) on a hidden reference skeleton (`VRReloadRigSampler.uc`) [V] | Hand-reviewed native profiles with 64-bit keys (`Bc2MagazineNativeProfile.h`). Only XM8, F2000 and AEK, plus the SPAS [V] | KF2 scaled to 94 guns in ~4 days because data came from the engine. BC2's per-gun evidence rule makes each gun a review. |

---

## 3. How each was developed

### KF2 [V]
- **09-22:** a design doc ("pause the stock reload, never replace it") and the first implementation, a 619-line `VRInteractiveReload.uc` for 2 guns.
- **09-23:** a headset playtest that failed (backwards magazine, the shotgun took no shells, no rack registered).
- **09-27:** first headset pass that worked. About 25 reload commits and 5 days in.
- **09-28 to 10-01:** waves of guns. Up to 23 commits a day; one commit added 28 guns.
- **In total:** 115 reload commits over ~16 days. 94 guns are in the catalog; most have not been played in the headset.
- **The dead ends were all about feel,** not plumbing:
  - Insertion went through 4 approaches: magnet, scrubbed motion, auto-seat (reverted in 34 min), and the 85% slam that was replaced by AS2's slider.
  - Trigger-to-grip, cube props to rig-cut props, fixed tables to sampling.
  - The only plumbing rework was a network gate, added then removed.
- **The feedback loop was the headset,** with dictated playtest notes. The in-game commandlet harness was deleted on 10-01 inside an unrelated commit (`680c7d06`, 93 files) and was never central.

### BC2 [V]
- The git history starts at checkpoint 202 (`a820f76`, 10-07 22:38). Earlier checkpoints exist only in docs. `2142-TRANSFER.md:83-91` records an earlier headset session ("headset193") with broken rifle reloads.
- **10-08:**
  - 326 commits on `proto/reload` in ~18 hours: about 150 from live headless runs on this PC, plus the cloud ports.
  - At least 35–40 commit messages describe a timing, read-miss, race, copy-order or hitch failure from a specific run.
  - **Zero headset checks** of the new reload (`docs/HANDOFF.md:3-24`).
- **The dead ends are all plumbing,** not feel. Representative chain:
  - lease lapses → gate fails open → free refill (`20c196e`)
  - hook skipped during a gather-thread exclusive section → ungated Update (`7687afe`)
  - reserve reads collide with hooks (`cf9d8ec`) → quiet-moment retry
  - lock-wait failure cancels a held reload (`cf21d15`)
  - client1 re-sync misread as a failure (`96c9c3b`, `d02f334`)
  - a hitch expires the abort window mid-claim (`986f0fe`, `5d415a9`, `73b4b65`)
- **[I] Most fixes add a grace window rather than remove a cause.** Windows found in code and messages:
  - 300 ms seat retry
  - 500 and 750 ms read grace
  - 600 ms, 1.5 s and 2 s carries
  - 1 ms lock wait
  - 50 ms copy-order wait
  - 3 s orphan stop
  - 2 s stale-owner hold
  - 100/500 ms abort re-base
  - 8 s pump timeout

**The telling comparison:** KF2 spent its first week on *how the reload feels*. BC2 has spent its week on *whether the engine is still holding still*. The hand/insertion layer was ported from KF2 and AS2 in a few hours on 10-08 (`1319e13` and neighbours) and has barely churned since.

---

## 4. The core trap: two failure classes that fight each other [I, supported by the commit chain]

1. **A hold that misses an Update releases the reload.** Holds re-assert every Update and depend on a current lease and on coherent reads. Any read gap, lock contention, owner clear or hitch makes the native reload finish by itself, which gives a **free refill**.
2. **A cancel must actively stop the native reload.** It does this on all copies within a window. A **too-eager** cancel, triggered by a missed or deferred read, throws away a valid reload. A **too-late** cancel lets the server's shell loop or transfer complete.

Making (1) more tolerant means holding through more uncertainty, which needs longer graces. That makes (2) slower to notice a real cancel, and vice versa. The windows in §3 are the tug-of-war. **KF2 has neither problem.** Its pause is a latched state, not a per-frame assertion, and its cancel is `AbortReload()` on one object on the same thread.

---

## 5. The three hypotheses

### 5a. "We aren't taking enough control of the exe"

**Partly right, but not in the way the question implies.**
- **More ownership (writing ammo, replacing the reload) would not help.** The Step-2 auto-reload is input-independent and runs on every copy. Even if we never ran a native reload, we would still need the same gate on the same hook [V: decode at `0x6e651b`]. Ammo writes to client copies are undone by Restore [V/D].
- **The kind of control is wrong.** Three self-imposed choices make BC2's control weaker than KF2's:
  1. **Decisions live off the game thread.** The hand state machine and lease/owner logic run on the gather thread and re-check native state through concurrent reads. Hooks then consult that state under locks. KF2 decides and acts in the same tick on the same thread. This is the root of leases, cohort reads, quiet retries, lock waits and entry-gate windows: about a third of the run-failure commits.
  2. **Per-call, restore-everything overrides.** The `AGENTS.md` rule "restore native state on every outcome" forces a hold that is re-applied every Update, rather than a latched hold. A **persistent, owned hold** would remove failure class (1) at the source: our own flag on the player's server copy that the Update hook honours until we clear it, with no lease. Examples are code-patching the state-11 timer decrement for our flagged copy, or holding the timer value instead of dt.
  3. **Three peers instead of one authority, unless a flag is set.** By default the magazine hold still treats all three copies as peers [V: `Bc2MagazineReloadCycle` tracks `advanced_[3]`].
     - `d02f334` adds `FVR_BC2_COPY_AUTHORITY`. At `=1` the server and client0 decide and client1 is tolerated; at `=2` client1 is observe-only.
     - The playtest checkpoint runs `=1`, but the default is unset.
     - The empty gate and abort still cover all three copies.
     - This is KF2's topology, translated, but only half-adopted.
- **Verdict:** take control of the *timing and the decision point*, not of the ammo.

### 5b. "We haven't done enough decompiling"

**Mostly no.** What is decoded is enough for reloads:
- the full firing state machine 0–15
- the Step-2 entry condition
- Transfer, Restore and Commit
- the abort helper
- the begin/end notifiers `0x6ddb30`/`0x6ddb60`
- the pump decode
- the palette packer and give path

**The gaps that do matter are narrow and specific:**
- **Which copy drives the 1P reload animation and the HUD** (engine map §8). This decides whether a server-plus-client0 authority model shows the right thing. It is the one RE item blocking the architectural fix.
- **Where sim Updates run relative to each other.** Which threads, in what order per frame, and whether server and client copies can tick concurrently. This is partly in run logs but not written down as a fact. It decides whether in-hook decisions can be made race-free.
- **Optional:** the state-11 timer decrement site, if a latched hold by code patch is chosen.

More broad decompiling (G10 anim sub-events, G13 replication writer) would not change the reload design.

### 5c. "The engine is just that much harder"

**Yes, genuinely, and this is the biggest share.** Concretely:
- no script layer, no reflection by name, no source
- no pause primitive
- server-authoritative copies even in single player
- an input-independent auto-reload
- weapon logic on sim threads separate from where we read input
- no chamber, bolt or pump concepts in the data

Each of these converted a one-line UnrealScript call into a hooked, fingerprinted, restored, cross-thread transaction. KF2 also had separate, consistently named rig bones and an ammo notify in every clip, so it sampled per-gun data. BC2 needs reviewed per-gun profiles.

---

## 6. Other differences that make BC2 harder to *develop*, not just to run

1. **Build-test loop.** KF2 compiles script and tests in the headset. BC2 needs an x86 DLL build, a headless harness launch and run-log analysis. Run reports are private (`reports/native-trace-*`) and commits cite run IDs that aren't in the repo [V], so a new session can't re-check evidence.
2. **Tests verify the parts that don't fail.**
   - ~15k reload test lines cover the portable policy machines [V].
   - The offline replay sim models only the magazine consumer, with no three-copy concurrency, no threads and no hitches (`tools/sim/Bc2ReplaySim.cpp`). The SPAS sim is unbuilt (`feat/spas-sim`) [V].
   - The failures live in exactly the part that isn't simulated.
   - **[I]** A small "fake engine" in tests would catch the races in CI instead of in runs: three copies, Restore overwriting client1, and Updates interleaved on two threads with injected hitches.
3. **Code density.** `Bc2ReloadFlowRuntime.cpp` is 2,474 lines but 212 KB (~86 chars/line, many statements per line), and `Bc2Gameplay.cpp` is 277 KB [V]. That is hard to review and easy to break.
4. **Stale contracts.** The `Bc2ReloadRequestCycle.h:60-65` comment still says "no synthetic requests, native calls, input injection", but the code does all three (the Reload pulse at `Bc2Gameplay.cpp:1725-1728`, `nativeAbort` at `:775-777`) [V]. Agents reading contracts will reason from wrong premises.
5. **Feature flags multiply the state space.** These are defaults that differ between the playtest checkpoint and the code:
   - `FVR_BC2_COPY_AUTHORITY`, `_RELOAD_MAILBOX`, `_EMPTY_GATE_FAILCLOSED`, `_CHAMBER_RACK`
   - the compile define `FVR_BC2_PREHOLD_CLEANUP`
6. **The evidence rules are calibrated for a different engine.** The `AGENTS.md` rules came from the BF2142/Refractor work. They are good safety rules, but they push every lever toward a transient, restorable write, and that is the weakest shape of lever for a hold that must last seconds.
7. **Pace without a headset.** 326 commits in a day, each a fix to the last run, with no human playtest in between. KF2's first week had two headset passes, which reset priorities toward what mattered.

---

## 7. Recommendations, in order

1. **Make the hold latched and owned.** Keep a per-copy "held by VR" flag that only our hook reads and only our seat/cancel clears. Do not tie it to a lease. If the hook runs, it holds; release happens only by explicit command or by owner/weapon change. This removes the free-refill class.
   - This needs a decision from you on relaxing the "restore every outcome" rule for this one lever. Code-patching the state-11 decrement is cleaner still, but needs a small RE item.
2. **Move the decision onto the game thread.** The gather thread should only *publish hand events* (grab, seat, cancel) into a lock-free queue. The server copy's Update hook consumes them and decides, reading its own object with no concurrent reads.
   - This differs from the earlier mailbox design, which keeps decisions on the gather thread and sends commands down. Here the decision moves to where the data is, as in KF2.
3. **One authority, by default.** Make `FVR_BC2_COPY_AUTHORITY` the default, and move to `=2` (client1 observe-only) once the RE item below is settled. **First** resolve which copy drives the 1P anim and the HUD; that is the one RE item worth doing now.
4. **A fake three-copy engine in tests:** two threads, Restore on client1, hitch injection. Replay the 35–40 failure commits as regressions.
5. **Pseudocode-decompile Update `0x6e9000`, Step `0x6e62d0` and Tick `0x6e69e0`** (blind review, §9). Then drop the dt ≤ 50 ms abort (`ContextSafe`), which makes every hitch a reload failure.
6. **Admit guns by structure, not by exact timing floats** (§9). This is the scaling fix once the plumbing holds.
7. **A headset pass before more headless fixes, and KF2's `7040de34` process rule.** As KF2 shows, the feel issues will reorder priorities.
8. **Housekeeping:**
   - Fix the stale contract comments.
   - Collapse the flags to the checkpoint's defaults.
   - Commit the run summaries that commits cite (or a digest of them).

---

## 8. Against the project's earlier conclusions

I read these after forming the above: `kf2-vs-bc2.md`, `reload-wiring-design.md`, `reload-closed-gate-review.md`, `friend-ai-comparison.md` and the wiring-assessment thread.

**Agree:**
- **KF2 does not own ammo,** and owning ammo in BC2 wouldn't remove the gate (`kf2-vs-bc2.md` §3, `friend-ai-comparison.md` §1). Reached independently here.
- **The gate should be fail-closed by default.** Already done in `ed749e1`.
- **The server copy should be the authority,** observing client1, as the pump does (`reload-wiring-design.md` step 2). This is implemented behind a flag (`d02f334`) but is not the default.
- **Step state 2 has no config lever,** so the `+0x28` token approach is necessary (`reload-closed-gate-review.md` §1).

**Goes further:**
- **The hold, not just the gate, is the remaining fail-open lever.** The earlier docs flipped the gate's polarity but left the hold fail-open by design: "Hold expiry → release; native continues and credits. Benign" (`reload-wiring-design.md`, expiry table). It is not benign for a physical reload. A released hold *is* the free refill users see. A latched hold makes the whole system fail-closed, as KF2's `PauseTimer` is.
- **Decision placement.** The earlier mailbox design keeps decisions on the gather thread and makes hooks dumb. I'd invert that: decisions go in the server-copy hook, and the gather thread only reports hand events. This removes the latency risk that design flagged ("up to one gather period") and the receipt-replay logic.
- **The process and test gap:** headless-only iteration, a simulator that doesn't model the concurrency, and private run evidence. The earlier docs don't cover these.
- **The "restore every outcome" rule** as a contributor that is now a decision for you.

---

## 9. Blind review

A separate agent was given only your question and the code paths. It was not shown this report or any earlier project doc.

### Where it independently agreed
- KF2's day-one design (`de8bc152`) is the same as BC2's hold design. Only what "pause" costs is different.
- The engine is the largest single cause.
- Cross-thread policy is self-inflicted. It cites `3dc8545`, `cec96b2` and `cf9d8ec` as bugs the engine didn't force.
- Decisions belong inside the server-copy hook, with the VR thread posting intents to a single-producer queue. This matches my recommendation 2, and it calls the opt-in mailbox (`62c840e`) "half of this".
- Headset-first process.

### Where it disagreed, and what I folded in

1. **Blame split.**
   - Reviewer: engine ~45%, control ~35%, decompiling ~20%. Mine was ~55/35/10, with ~0–5% for RE.
   - Its case for more RE weight: the repo has **no pseudocode** of Update `0x6e9000`, Step `0x6e62d0` or Tick `0x6e69e0`, only state-table summaries. So edge cases like hitches and zero-duration states are still found by live runs.
   - I accept that. **Revised view: engine ~50%, our architecture and rules ~30%, decompiling ~10–15%, process ~10%.** A pseudocode pass on those three functions is now in the recommendations.
2. **Take the credit ourselves, like `PerformReload`.**
   - The reviewer would keep the copies in state 2 for the whole VR reload. On the seat, it would call Transfer `0x6d6f40` (or the pickup setters `0x6cd520`/`0x6cd850`) on the server copy, inside its own Update. Client copies would be treated as views that re-sync.
   - Transfer is already signature-verified and hooked, but never called by the mod (`BC2_WEAPON_RUNTIME.md` hook table). The fill function `0x6dd580` is also mapped [V].
   - **My position:** this is a real option I underweighted, and it removes the multi-second hold entirely, so class (1) disappears. It is not "owning ammo", since the native function still does the arithmetic.
   - **The trade-off:** the native 1P/3P reload animation and its sound timing would no longer play, which conflicts with "preserve native animation". It also needs evidence that calling Transfer outside its native call site is safe.
   - **I list it as Option B next to the latched hold (Option A). It is your call (§10).**
3. **Exact-float admission** (new point I missed) [V].
   - `MatchesReloadDescriptor` requires bit-exact timing floats per weapon, e.g. XM8 ReloadTime 2.8 (`Bc2ReloadConfigDescriptor.h:36,43`).
   - That makes every new gun a reviewed profile. KF2 admitted guns by class and pulled behaviour from the shared `KFWeapon` contract.
   - The reviewer suggests admitting by structure (ReloadType, FireLogicType, capacity read live) and keeping exact descriptors as diagnostics. **I agree.** This is the main scaling blocker after the plumbing is stable.
4. **The dt ≤ 50 ms safety rule creates the hitch bug class** [V: `ContextSafe`, `Bc2ReloadHold.cpp:16,74`]. A 66 ms frame aborts the hold as `UnsafeInput`. This is a self-imposed rule, not an engine fact. Folded into recommendation 1.
5. **KF2 explicitly retired its evidence tooling** [V].
   - `7040de34` (09-27) changed `AGENTS.md` to "headset → bug list → fix → build → headset". It limited tests to math, parsers and the launch path, and made replays, fixtures, probes and evidence tooling default-off.
   - **That was the same day KF2's first working headset reload landed, and the gun waves followed.** It is the strongest process signal in the history.
   - BC2 is on the opposite rule set. Folded into recommendation 5.

## 10. Decisions for you

1. **Hold design.**
   - **Option A, latched hold (my recommendation for now).** It keeps native animation, and the change to existing code is smaller.
   - **Option B, take the credit by calling Transfer on the server copy at the seat.** It is closest to KF2 and removes the hold entirely, but drops the native reload animation and needs new evidence.
2. **Relax `AGENTS.md` for the reload path.** Allow a persistent, owned override on the player's own weapon (A), or a direct native call (B), and drop the dt ≤ 50 ms abort.
3. **Adopt KF2's `7040de34` process rule for BC2:** headset-first, with no new harness tooling unless asked.

---

## Not checked

- No game run and no headset. Run outcomes come from commit messages and docs.
- Thread placement of copy Updates is inferred from the engine map's thread notes and the existence of cross-thread machinery, not traced live.
- The blame percentages are judgement, not measurement.
