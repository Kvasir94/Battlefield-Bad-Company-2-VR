# Design: event mailbox, server authority and rack by default (BC2 reload wiring)

Follows `kf2-vs-bc2.md` and `reload-closed-gate-review.md`. Read-only design, 2026-10-08, against `origin-bc2/proto/reload` @ `f16516f`.

- The reload session owns everything below, except the scenario rules, which belong to PC builds.
- Ammo stays native throughout: Transfer `0x6d6f40` is the only credit, and the existing exact write/restore rules are unchanged.

## Order

1. **Close gate holes F1/F2, then flip the gate default.** This is small, and the matrix is already running. See the review.
2. **Server copy as the authority for the magazine hold and seat advance.** This is medium and reuses the pump's pattern.
3. **Event mailbox.** This is the largest change. Do it after 2, so the mailbox has two command targets instead of three.
4. **Rack on by default,** per profile. This is small and mostly scenario rules.

Steps 2 and 4 are independent. Step 3 depends on 2.

## Step 2: server copy as the authority (magazine)

**Today:**
- The magazine hold and the seat advance target all three copies as peers.
  - `Bc2ReloadRequestCycle::Evaluate`/`Allows`/`Finish` run per Update for each branch.
  - `Bc2MagazineReloadCycle` tracks `advanced_[3]`.
- A client re-sync (11→2→10→11) is treated as a disturbance. The probe now tolerates one, after 50 ms (b51353e).
- The SPAS pump already does it the other way (`Bc2PumpHold.h`): it advances client0 (+0x3c, which owns the effects listener), then the server, and counts the other client's engine re-sync (`settled_copies`, `native_resyncs`).

**Change:**
- In `Bc2MagazineReloadCycle.{h,cpp}` and `Bc2ReloadRequestCycle.{h,cpp}`, define the **command set** as {server (branch 2), client0 (branch 0)}.
- Branch 1 (+0x40) becomes observe-only:
  - It is never held or advanced.
  - Its 2→10→11 and its Transfer are counted as `native_resyncs`.
  - It is not required for completion.
- Completion = the server's Transfer receipt, plus client0 reaching 12 or re-syncing.
- Keep the empty gate on all three copies (it is cheap, and the server-only leak shows any copy can enter).
- Report: `magazine.settled_copies`, `native_resyncs`, matching the pump.

**Risks and evidence needed:**
- Which copy the HUD ammo and the 1P reload animation read. If it is branch 1, holding only 0 and 2 could show the 1P reload playing through while the hold is on.
  - Check from existing gun-event rows: does branch 1 advance to 12 by itself during a held reload?
  - If so, keep branch 1 in the hold set but still drop it from the completion criteria.
- `rlWeaponSwitchCancelsUnfinishedReload`: a switch during the hold must still cancel on every copy. The existing abort helper `Bc2ReloadAbort.h` is unchanged.

## Step 3: event mailbox instead of per-Update policy

**Today, per hooked Update and Step:**
- owner lease lookup (`Observation`, `Owner()`)
- cohort read (`PrepareHold`)
- `TryAbort`
- `WithRequest{requestCycle.Evaluate}` and `WithRequest{Allows}`
- `RunReloadDeltaOverride`
- `WithRequest{Finish}`
- in Step, lease eligibility and the closed gate

These locks and leases on game sim threads caused:
- the entry-gate window (7687afe)
- reserve-read deferral, with 75–85% of reads deferred (cf9d8ec)
- `WithRequest` contention

**Design:**

The **board** (new `Bc2ReloadCommandBoard.h`, portable and testable) is one seqlocked struct, like `ClosedIdentity`, written only by the gather thread. It holds:
- **owner identity:** player, soldier, weapon, actor and equip generation; the three firing addresses; the supported profile
- **per copy** `{kind: None | Hold | AdvanceOnce(seconds, serial) | Observe, cycleId, expiresNs}`
- **empty policy:** `{block: bool, tokenSerial, tokenExpiresNs}`

The **hook** (Update/Step in `Bc2ReloadFlowRuntime.cpp`):
1. Reads the board once.
2. Finds its own slot by firing address.
3. Reads only its own object (state, next, loaded, timer).
4. Applies at most one override (delta for Update, ctx+0x28 for Step) and restores it exactly.
5. Writes a **receipt** into a per-copy lock-free ring: before/after state, applied/restored, serial consumed.

There are no leases, no `WithRequest` and no reserve reads inside hooks. `AdvanceOnce` is consumed by CAS on its serial, so it runs exactly once per copy.

The **gather thread** (`Bc2Gameplay.cpp` and the request bridge) runs `requestCycle.Evaluate`/`Finish` against the receipts instead of inside hooks, decides the next commands, and republishes. Reserve reads happen at the gather boundary with no hook contention, so the quiet-moment retry becomes unnecessary.

**Expiry polarity (KF2's rule in the data):**

| Command | On expiry, or when the board is unreadable |
|---|---|
| `Hold` | Release; native continues and credits. Benign, same as today. |
| `AdvanceOnce` | Dropped; the native timer completes the reload a little later. |
| Empty `block` | **Does not expire.** It is cleared only by an owner or weapon change, or by a token. Fail-closed. |
| Firing address not on the board | Native. Not our weapon. |

**What stays:**
- Exact write/restore (`RunReloadDeltaOverride`, `RunMagazineEmptyByteOverride`).
- The receipt-driven magazine flow.
- The abort helper, called from the gather thread only when a receipt shows the copy in 10..12.

**Risks:**
- **Latency.** The gather runs at input rate, while a hook acts on the next sim Update after a publish. Seat advance and the token gain up to one gather period. Measure seat-to-Transfer before and after with the existing gun-event timing rules.
- **Address reuse.** Mitigate with the ownership back-pointer check in the hook (see review F1), not a time bound.
- **Migration size.** Every place that reads `requestCycle` inside a hook must move. Keep `requestMode` paths behind a flag (`FVR_BC2_RELOAD_MAILBOX=1`) until the XM8/F2000/AEK/SPAS scenarios pass under both settings.

**Tests:**
- Portable unit tests for the board: seqlock torn-read, CAS consume-once, expiry polarity.
- Run the existing scenarios with the flag on.
- Add the hitch and input-gap tapes.

## Step 4: rack on by default

**Today:**
- `ChargingHandleRack` and `Bc2MagazineChamber` exist (NeedsRack blocks fire after an empty reload).
- The XM8 and F2000 have handle profiles; the AEK has none (its handle never moves in any clip).
- The bolt release on the eject press works (de2648c).
- All of it is opt-in through `FVR_BC2_CHAMBER_RACK=1`.

**Change:**
1. PC builds adds rack legs to the empty-reload scenario rules (the mode 7 rack leg exists, 879f4c2). Pass condition: no shot between the seat and the rack, then a shot after the rack or the bolt release.
2. Default on **per profile**: only profiles with handle data get NeedsRack. AEK and others get no rack step, like KF2's `ActionKind 2`.
   - Keep `FVR_BC2_CHAMBER_RACK=0` as an opt-out.
   - The SPAS pump stays separate.
3. NeedsRack and slide-lock state should belong to the weapon (KF2 `VRWeaponRuntime.bSlideLockPending`): an owed rack survives a switch or stow and comes back with the gun. Check that `Bc2MagazineChamber` is keyed by weapon identity, not by the reload session.

**Risks:**
- Players need to know they must rack. The magazine hint clocks exist (c3dbb30); the handle part has no glow yet, so use the per-instance tint (3c6accb).
- The headset check of the handle grab radius (0.06 m design value) is pending.
