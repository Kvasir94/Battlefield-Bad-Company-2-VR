# BC2 VR physical reloads: handoff (2026-10-08)

Written for a fresh session with no context. Repository the private development repository (remote `origin-bc2`), branch `proto/reload`. The worktree used here is `<worktree-reload2>`. **Last commit: `583b08c`, pushed, working tree clean.** No uncommitted work exists. The Option B code was designed but never written: the permission guard refused the file write.

**The owner's directives at handoff time (2026-10-08 22:08):**
- All threads are stopped.
- No playtesting until all work is merged to `main` and every other branch is deleted.
- No emulated testing either: no scripted runs, synthetic-hand runs or simulated-response tests.

So the "rows" named below describe what to verify once testing resumes. They are not queued now.

Run ids (for example 205329) refer to PC builds' private run reports (`reports/native-trace-*` on the owner's PC). They are not in the repo.

---

## 1. How reloads work today (hold path, "Option A")

### The three weapon copies
Each player weapon has three firing objects ("copies"). They tick independently, even in single player:

| Copy | Address | Notes |
|---|---|---|
| client0 | wrapper `+0x3c` | local prediction |
| client1 | wrapper `+0x40` | constantly re-synced from the server by Restore `0x6d71a0` |
| server | item `+0x10` | authoritative |

**Firing-object fields:**
- `+0x00` vtable
- `+0x08` firingData
- `+0x0c` ammoAddress (these three form the structural identity)
- state block `+0x3c`: current state at `+0x3c`, next state at `+0x44`, timer at `+0x50`
- `+0x7c` loaded
- `+0x80` reserve
- `+0xa8` flags (bits 8 and 16 mean non-finite ammo)

**Native firing states:**
- 2 = idle, ready
- 10 / 11 / 12 = reload begin / reload hold / transfer tail
- 7 / 8 = SPAS pump

Step state 2 enters reload (2→10) with **no input** whenever loaded is 0 (the auto-reload, decoded at `0x6e651b`).

### Hooks (x86 only)
All of these are in `src/games/bc2/Bc2ReloadFlowRuntime.cpp`:
- Update `0x6e9000`
- Step `0x6e62d0`
- Commit
- Transfer `0x6d6f40`
- Restore `0x6d71a0`
- the abort helper, called directly

### The flow of one magazine reload
The consumer is `Bc2MagazinePhysicalReload.cpp`, talking to the runtime through `MagazinePhysicalApi`. The gameplay wiring is in `Bc2Gameplay.cpp`, around line 2508.
1. **Pull.** The hand pulls the magazine (DetachableMagazine), which produces an `UnseatMagazine` request.
2. **Start.** `api.start` calls `StartMagazineRequestCycleObserved`. The consumer then sets `reloadHeld` for 100 ms, and gameplay presses Reload (`ApplyMagazinePhysicalActions`). The native reload enters 10, then 11.
3. **Hold.** The policy holds state 11 on every copy's Update by zeroing the frame delta (`ctx+0x18`), using `ReloadDeltaOverride` / `RunReloadDeltaOverride` in `Bc2ReloadHold.cpp`.
   - The policy is `Bc2MagazineReloadCycle` (magazine) or `Bc2ReloadRequestCycle` (SPAS), wrapped by `Bc2ReloadNativePolicy`.
   - Holds are lease-based: a fresh coherent read of all three copies is needed. That is why "cohort reads", quiet retries and the request lock exist.
4. **Unseat acknowledged.** `api.gate` returns `TakeMagazineUnseatAcknowledgement` once the hold is established.
5. **Seat.** The hand seats the replacement. `api.submit` calls `SubmitMagazineRequest`. On the seat frame, `ApplyAdvance` raises the delta by the remaining state-11 timer, so the native Transfer runs (`ReloadMagazineCompletion` checks it).
6. **Ack.** `api.ack` returns `TakeMagazineAcknowledgement`. `MagazineSupplyReceipt` (`Bc2MagazineInteraction.cpp`) checks the counts against a fresh reserve read.
7. **Cancel.** `CancelRequestCycle` arms the native abort on every copy (`Bc2ReloadAbort.h`) within a window. One stop per cycle is allowed, plus an orphan stop and the StopPending fire hold-off.

### Empty gate (fail-closed, on by default since `ed749e1`)
The Step hook overrides the `ctx+0x28` byte to block state 2→10 on the player's copies, so the engine never auto-reloads.
- **Identity:** a `ClosedIdentity` seqlock published in the owner publication (`Bc2ReloadFlowRuntime.cpp`, around line 2013), independent of any cycle.
- **Who passes:** only our own start token, or a stock fallback window, lets a Reload through.
- **Opt out:** `FVR_BC2_EMPTY_GATE_FAILCLOSED=0`.

### SPAS
- **Consumer:** `Bc2PhysicalReload.cpp`, with `AmmoSupply` for the shell in hand.
- **Policy:** `Bc2ReloadRequestCycle` holds state 11 and advances one shell per seat (`SubmitRequest`).
- **Pump:** `Bc2PumpHold.h`.
- **Startup carry (`startupCarry` lambda):** carries the shell through read gaps before the first all-held lease.
- **Other carries and waits:** held-gap carry (1.5 s), pending-gap wait, deferred renewal, abandon after 2 s with no shell, and start retry for 500 ms.

### Other pieces
- **Chamber rack:** on by default (`FVR_BC2_CHAMBER_RACK=0` opts out); per-weapon state in `Bc2MagazineChamber.h`.
- **Reload hints:** the amber glow.
- **Dropped magazines:** `DroppedProps` plus `Bc2DroppedFloor.h`.
- **Event mailbox (opt-in):** `FVR_BC2_RELOAD_MAILBOX=1`, magazine only.
- **Copy authority (opt-in):** `FVR_BC2_COPY_AUTHORITY=1` makes client1 tolerated.

### Build and test loop (the owner's standing rules)
- Per change, run only a quick x86 build plus the unit tests for the files touched. Do not run full suites, x64 builds or GitHub Actions.
  - Quick build: `cmake --build build\x86 --parallel 6` in a VS x86 dev shell.
  - Tests: `ctest --test-dir build\x86 -R <regex>`.
- After each push, send the coordinator the commit and the in-game rows to run. PC builds owns the game slot: **never launch BC2 yourself.**
- Push with a merge, never a rebase: `git fetch origin-bc2 proto/reload && git merge FETCH_HEAD`, then push to `origin-bc2`. Never push to `origin` (justiceofrip).
- Watch D: free space. Capture runs refuse to start under 5 GB.

---

## 2. Done, by gun

All commits are on `proto/reload`.

### Shared
- **Empty gate:**
  - `698e9ae`: fail-closed empty gate, opt-in
  - `ed749e1`: fail-closed gate on by default
  - `7dfe376`, `ef6b3b2`: closed-gate identity and the stock fallback
- **Abort and cancel:**
  - `8efb8f6`, `3a09b02`: a cancel aborts instead of releasing
  - `a52cc59`: orphan stop
  - `3dc8545`: one stop per cycle
  - `cec96b2`: restamped stop lease
  - `986f0fe`, `5d415a9`, `73b4b65`: the abort survives hitches
- **Locks, hitches and frames:**
  - `cf21d15`: lock contention no longer cancels
  - `583b08c`: any finite frame delta is held. The 50 ms cap is gone from `ContextSafe`, from both cycles' `Safe()`, and from `ReloadDeltaOverride`.
- **Copy authority:** `d02f334`, `0ef2777` (flag).
- **Mailbox:** `5998f4c`, `62c840e` (flag).
- **Hints:** `c3dbb30`, `0dbdcf1`, `642c453`.
- **Bolt release:** `de2648c`.
- **Charging-handle rack:** `e602ada`, on by default.
- **Dropped magazines:**
  - `08b0d19`: in-game draw with scene depth (`FVR_BC2_INGAME_PROPS=1`)
  - `bb206ef`: ground probe (PC builds)
  - `4e98828`: dropped mags use the depth-derived floor

### XM8 (scoped profile) and F2000
- Magazine pull, seat and credit via the hold path.
- F2000 needed the 64-bit profile mirror (run 145846).
- Seat retry over 300 ms (`b4b8ef0`).
- Passed in game: XM8 plain, the F2000 rack, the stall row (`stall_rebases 1`), weapon flick rows, arming and held cancel aborts, head-loss orphan stop.

### AEK
- Has a native profile (`Bc2MagazineNativeProfile.h`). No AEK-specific failures or rows are known from this session; treat it as **unverified in game**.

### SPAS
- Long chain: `e88aace`, `d6fa168`, `50ee968`, `8a2247e`, `842c653`, `2cb8a4a`, `d8b4830`, `bf0d944`, `96c9c3b`, `5bb6b97`.
- **The 96e9036 batch (16 runs):** 13 clean, 2 early cancels, 1 probe-only failure.
  - Three shells 4/4, cancel-between 3/4, abandon 3/4, abandon + hitch 4/4.
  - No `submission_rejected`.

---

## 3. Open failure modes (hold path)

The coordinator asked not to chase these on the hold path, because Option B removes them.

| Run | Row | Symptom |
|---|---|---|
| 214609 | SPAS cancel-between | `keepalive_rejected` at 10326, before any credit; probe fail 11; submit_retries 36. Ring: cancel 11, then 6, then 9 |
| 214744 | SPAS abandon | `missing_source` after the first credit, with grip F and shell_held F; probe fail 14 |
| 215536 | SPAS abandon + hitch | probe phase 8 failure 1 with a correct outcome: the fixture's own post-check. Fixture issue (PC builds) |
| 205149 | SPAS | `unsafe_input` from a 101 ms stale input packet. Not reproduced since: input-publish gaps maxed at 32–47 ms |

---

## 4. Option B: credit at the seat (the owner's decision; NOT written yet)

**Goal:** a physical reload never runs the native reload. The gun stays idle in state 2, the empty gate keeps blocking auto-reload, and the hands do the reload. At the seat, credit the rounds to the server copy inside its own Update hook through the game's pickup ammo setters. The clients follow via Restore. No hold, no leases on the hot path, and no free refill, because nothing native can refill.

### Native functions
Byte-checked in `tools/testrange/BC2TestRange.cpp:276-281`. Both are thiscall `(firing, int)`, `ret 4`. RVA = VA − 0x400000.
- **`0x6cd850` SetReserve.** Pattern: `51 56 8B F1 F6 86 A8 00 00 00 08 75 2A 8B 46 0C 8B 40 10 83 F8 FF`. It writes `+0x80` only when `!(+0xa8&8)`, NumMags != −1 and round(+0x78×NumMags) != −1, so **read back after the call.**
- **`0x6cd520` SetLoaded.** Pattern: `8B 44 24 04 89 41 7C C2 04 00`.
- **Order:** reserve, then loaded (as the pickup path at `0x6c5f68` does). If the reserve read-back is wrong, do **not** set loaded.

### New file `src/games/bc2/Bc2SeatCredit.h` (portable, unit-testable)
The class `Bc2SeatCredit` implements, behind `MagazinePhysicalApi`, exactly the evidence the consumer already expects, so the hand state machine is unchanged.

**Gather-thread methods:**
- **`Start(control, unseat, currentIdentity, reserve, now)`:** records the cycle and returns `Started`. No Reload pulse.
- **`Inspect(id, cycle)`:** returns `Started`, `RegisteredCancelled` or `NotStarted`.
- **`Keep(control)`:** true while the cycle is live.
- **`Lease(id, cycle, ReadReserve(), now)`:** builds a `ReloadMagazineLease` from a fresh reserve read.
  - `allThreeHeld=true`, because the idle gun's counts are the hold.
  - Deadline = min(reserve deadline, observed + 200 ms).
  - Remember it as `lastLease`.
- **`TakeUnseat(id, cycle, now)`:** returns a `ReloadMagazineGateAcknowledgement` from `lastLease` with `semantic={unseat.id, nativeOwner(id), UnseatMagazine, Applied}`.
- **`Submit(request, vtable, firingData, ammoAddress, client0)`:**
  - **Checks:** a `SeatMagazine` request, `heldLease.allThreeHeld`, and `reservedUnits == min(capacity−loaded, reserve)`.
  - **Then:** queues a `SeatCreditJob{server=firing[2], client0?firing[0]:0, cycle, serial, vtable, firingData, ammoAddress, loadedBefore, reserveBefore, units}` and sets an atomic `pending`.
- **`Ack(id, cycle, now)`:** non-consuming. Returns a `ReloadMagazineAckEvidence` with `serverInvocation=serial`, `sampleSequence=heldLease.sequence`, and before/after counts.
  - It is restamped: `observed = max(appliedNs, now − 150 ms)`, `deadline = observed + 200 ms`.
  - That way a reserve read taken after the clients catch up still passes `MagazineSupplyReceipt`, which needs the read to be ≥ observed and the counts equal to after.
- **`Cancel()`:** marks the cycle cancelled and drops an unapplied job. **A cancel never credits.**
- **`Retire(id, cycle, now)`:** clears the cycle and returns a verified `ReloadCycleRetirement`.

**Hook-thread methods:**
- **`Claim(firing)`:** `try_lock` only. Returns the job when `pending` is set and `firing == job.server`.
- **`Finish(job, refusal, loadedAfter, reserveAfter, now)`:** records the credit, or re-queues a Structure, State or Counts refusal up to 60 times. Write refusals are final.
- **`CheckSeatCredit(job, copy)`:**
  - `Structure`: vtable, firingData or ammoAddress differ, or flags `&(8|16)`
  - `State`: current state in 10..12 or above 15
  - `Counts`: loaded or reserve not equal to the before-counts, or units out of range
- Mutex-guarded. `Counts()` gives the report stats.

### Wiring in `Bc2ReloadFlowRuntime.cpp`
1. A global `Bc2SeatCredit seatCredit`, plus `seatCreditEnabled` from `FVR_BC2_SEAT_CREDIT` (default **on** once the rows pass; `=0` gives the hold path).
   - Client0 write: `FVR_BC2_SEAT_CREDIT_CLIENT0=1`, off by default. Turn it on only if the HUD check (below) shows the HUD reads client0 and client0 doesn't follow the server.
2. Lazily verify both setter byte patterns at `base+(VA−0x400000)`. If they mismatch, report seat credit as unavailable and fall back to the hold path.
3. At the very top of `Update` (around line 1247, before any request-mode early returns), add `SeatCreditUpdate(firing)`:
   - Claim the job.
   - Read vtable, firingData, ammoAddress, state, loaded, reserve and flags with `memory.read`.
   - `CheckSeatCredit`.
   - Inside `__try`: call SetReserve(reserve − units) and read it back. Then SetLoaded(loaded + units) and read it back. Optionally do the same on client0.
   - `Finish`.
4. Exported functions, declared in `Bc2ReloadFlowRuntime.h`:
   - `StartSeatCredit`, `InspectSeatCredit`, `KeepSeatCredit`, `SeatCreditLease` (calls `ReadReserve()`), `TakeSeatCreditUnseat`, `SubmitSeatCredit` (vtable from `base+binding.state.firingVtableRva`; firingData and ammoAddress from `ReadClosedIdentity()` or the owner lease), `SeatCreditAck`, `CancelSeatCredit`, `RetireSeatCredit`.
   - Report block `seat_credit{started, submitted, credited, refused, dropped, contended, refusals[6], last}`.

### Wiring in `Bc2Gameplay.cpp` (around line 2508)
- When seat credit is on, point `api.start`, `inspectStart`, `keep`, `keepObserved`, `lease`, `leaseObserved` (Ready plus the lease), `gate`, `submit`, `ack`, `cancel` and `retire` at the functions above. Keep the existing `gunFvr.Seat` and `gunFvr.Credit` logging.
- Before `ApplyMagazinePhysicalActions` (around line 1657), set `magazineResult.reloadHeld=false` when seat credit is on. **No Reload pulse.**
- Keep the empty gate, the fire hold-off (`blocksWeaponActions`), the chamber rack and switch masking unchanged.

### Reload sound
The native clip never starts. Start the learned reload-clip GUID through `animSoundStartOriginal` (`0x881650`) on the anim thread at the magazine pull, reusing the ReloadSoundGate replay path (`Bc2Gameplay.cpp:439-473`, `Bc2ReloadSoundGate.h`).

### Unit tests (`tests/Bc2SeatCreditTests.cpp`; add to the CMake foreach list)
- start → lease → unseat → submit → claim → finish → ack, and the ack passes `MagazineSupplyReceipt` against an after-count reserve
- a cancel before Claim drops the credit (no ack)
- a Counts refusal re-queues and gives up at 60 attempts
- a write refusal is final
- the restamped ack stays valid when the reserve read lags

### Open risk to verify first
`ReadReserve()` requires all three copies to agree (`ReloadReserveCopiesAgree`). Right after the server write, the clients disagree until Restore re-syncs them; reads reject as `CohortGap` until then.
- If client1 doesn't follow, add the client1 write, or relax agreement for client1 (copy authority).
- If client0 doesn't follow, use `FVR_BC2_SEAT_CREDIT_CLIENT0`.
- PC builds' **HUD-copy check** (server `+0x7c` set to 30, 8 s after slide lock, with screenshots and per-copy samples) answers both. The result goes to the coordinator.

### Order of changes
1. Write `Bc2SeatCredit.h` and its tests. Quick build, tests.
2. Runtime hook, setters and exports, plus the report block. Gameplay wiring behind `FVR_BC2_SEAT_CREDIT` (default off for the first run).
3. **Rows:** XM8 plain, F2000 plain, AEK plain, partial-mag (eject) reload, cancel mid-reload (no credit), weapon switch mid-reload, hitch row.
   - **Pass:** HUD and counts correct, `seat_credit.credited` equals the seats, no native state 10–12 on any copy, and no ammo change on a cancel.
4. Reload sound.
5. **SPAS:** one shell per seat. Credit +1/−1 through the same job. Drop the pump hold for physical shell loads. The SPAS consumer (`Bc2PhysicalReload.cpp`) uses `PhysicalReloadApi` the same way. Rows: three shells, cancel-between, abandon, abandon + hitch.
6. Flip the default on (`=0` opts out).

### What to delete after B passes
- The state-11 delta hold and the `ApplyAdvance` seat fast-forward (`Bc2ReloadHold.cpp`, `RunReloadDeltaOverride` call sites in `Update`).
- The hold leases and cohort/quiet reads used only by the hold path (`Bc2MagazineReloadCycle` holding and advancing, and `ReloadMagazineCompletion`).
- The abort-window machinery for magazine reloads (`Bc2ReloadAbort.h`, the orphan stop, StopPending replay), once no native reload can start.
- The startup pulse and the stock-token start path for physical reloads. **Keep** the plain-native fallback for a Reload press when no physical path exists.
- Then the SPAS equivalents (the request cycle hold, the pump hold for loading, and the carry and wait windows in `Bc2PhysicalReload.cpp`).
- Also the mailbox (`FVR_BC2_RELOAD_MAILBOX`) and copy authority (`FVR_BC2_COPY_AUTHORITY`) flags, which only exist for the hold path.

---

## 5. AGENTS.md blocker

The owner approved replacing the repo rules ("Dump all of those rules except the new one we just made"). The permission guard refused my edit to AGENTS.md as self-modification, even after his "You may edit it" in the thread. It then also refused the Option B source write, reading it as a bypass of the old rules. **The owner has to paste this himself, or add `"Edit"` and `"Write"` to `permissions.allow` in `<repo>\.claude\settings.local.json`.** Exact text, replacing the whole file, on both `main` and `proto/reload`:

```
# Frostbite VR development rules

- The goal is native feel with modern VR controls. We own the game install and
  every player runs the mod, so the mod may own game state: write fields, call
  native functions, patch code and keep persistent overrides when that gives a
  better VR result. Restoring native state is not required.
- Native animation is optional. Replace it where VR hands do the job better
  (e.g. reloads).
- Safety still applies: verify a function's signature and calling convention
  before calling or hooking it, and only write to objects we've identified as
  the player's own (soldier, weapon, firing copies) so a bad pointer can't crash
  the game. Never commit game files or credentials.
- Enable a feature once it works in game. Keep an env kill switch instead of
  default-off evidence gates.
- Push to origin-bc2 (the private development repository). origin is justiceofrip's
  upstream: never push there.
```

**The owner's decisions that go with it:**
- Headset testing is not mandatory, and KF2's headset-first process rule is **not** adopted.
- In-game scripted runs on his PC stay the test loop.
- No cloud agents.
- Stop appending to `docs/2142-TRANSFER.md`. Commit messages carry the detail; don't delete the docs, but nothing should require reading them.
- Once AGENTS.md changes, fix the stale contract comments that restate the old rule: `Bc2ReloadRequestCycle.h:60-65`, `Bc2ReloadHold.h:22-24`, and the "no native calls / ammo writes" comment in `Bc2MagazineReloadCycle.h`.

---

## 6. Gun admission (open, not started)

**Today:** a gun is admitted to physical reload only by bit-exact timing floats.
- `MatchesReloadDescriptor` (`Bc2ReloadConfigDescriptor.h:51`), e.g. XM8 ReloadTime 2.8.
- The registered profiles in `Bc2MagazineNativeProfile.h` (`FindMagazineNativeProfile`).
- `IsDiagnosticSpasConfig` for the SPAS.

So every new gun is a hand-reviewed profile.

**Change:** admit by structure instead.
- **Structure:** ReloadType (magazine vs. shell), FireLogicType, and live capacity read from the copy.
- **Geometry:** keep per-gun data only where it's actually needed (magazine geometry and the mesh profile for the hands).
- **Exact descriptors:** keep them as diagnostics in the report, not as gates.

With Option B the timing floats no longer matter at all, since no native reload runs. Do this after B passes on XM8, F2000 and AEK.

---

## 7. Dropped-mag floor and ground probe

- **What's done:**
  - **`bb206ef` (PC builds):** `fvr::bc2::ground::Latest(now, maxAgeNs)` in `src/games/bc2/Bc2GroundHeight.h`, from an async depth readback at the post-eye boundary. Opt-in: `FVR_BC2_GROUND_PROBE=1`.
  - **`4e98828` (me):** `Bc2DroppedFloor.h`.
    - A sample no older than 250 ms and within 5 m of the hand is the floor (native Z is negated into prop space).
    - Without one, the play-space floor is corrected by the last observed ground offset.
    - The 0.5 m other-level cull uses the corrected floor.
    - Prop rows are labelled `drop_ground`, `drop_offset`, `eject_ground` or `eject_offset`. The report has `dropped_magazines.floor{ground, offset, play_space, fixed, offset_observed, offset_rejected, offset_m}`.
- **Pending check (queued with PC builds):**
  1. Drop an XM8 mag in SP_02 with the probe on. Expect `floor_mm` about 173710, not 174020, and `offset_m` about −0.30.
  2. Sink capture: `FVR_BC2_TEST_PROP_SINK_MM=80` plus `FVR_BC2_TEST_DEPTH_CAPTURE=2`. Expect the sunk mag to change about 0 px, while the unsunk one still changes about 500 px.
  3. Frame-time check.
- **Next step:** when the check passes, make `FVR_BC2_GROUND_PROBE` default on (`=0` opts out). The coordinator approved this as an engineering default. One-line change where NativeProbe reads the flag (`src/games/bc2/NativeProbe.cpp`, near the `FVR_BC2_INGAME_PROPS` read, around line 1830).
- **The owner's decision:** dropped magazines don't take their rounds with them (yet). Nothing does that today.

---

## 8. Every other open item

| Item | State | Next step | Rows to verify |
|---|---|---|---|
| 583b08c hitch-hold change | pushed, not run | PC builds runs the mid-hold hitch (`FVR_BC2_TEST_HITCH_MS=90,1000,hold`), XM8 plain and SPAS three shells | no `Timing`/`UnsafeInput` cancel; shells credit |
| 5bb6b97 submit retry | in game: no `submission_rejected` in 16 runs | none | — |
| SPAS early cancels 214609 / 214744 | open on the hold path | covered by Option B; revisit only if B doesn't cover SPAS | SPAS rows on B |
| Probe post-check failure (215536) | fixture issue | PC builds owns the fixture | — |
| HUD copy check | PC builds is running it | it decides client0 / client1 writes for B | — |
| Reload sound for B | designed (§4) | implement after the magazine B rows pass | listen and check the sound-gate rows |
| AEK | profile exists, unverified | include it in the B rows | AEK plain |
| Mailbox and copy-authority flags | opt-in, hold path only | delete after B | — |
| Stale contract comments | blocked with AGENTS.md | fix in the same commit as AGENTS.md | — |
| Gun admission by structure | not started (§6) | after B | one new, unprofiled magazine gun in game |
| Ground probe default on | waiting on the floor check | flip the flag | floor, sink and frame-time rows |
| `docs/2142-TRANSFER.md` / `HANDOFF.md` | stale | stop appending; no requirement to read them | — |
