# Friend's FrostbiteVR reload work vs our proto/reload

Read-only assessment, 2026-10-08. No code or branches were changed.

## Sources and limits

- **Their new code is not reachable.** `justiceofrip/FrostbiteVR` has two public commits:
  - `a820f76`, checkpoint 202
  - `98c7cf8`, checkpoint 203 (2026-10-07 23:07 −0400, "Fix magazine retirement after switching weapon profiles")

  Both are already in our `proto/reload` and `main`. No other clone on the dev PC has newer upstream commits. Everything about their new block is therefore taken from the quoted summary, read against their public code and docs. **Statements about their new design are marked [inferred].**
- **Ours:** `origin-bc2/proto/reload` @ `5998f4c`, plus today's 230 native-trace reports in `<old-worktree>\reports`. I counted `seat`, `complete` and `transfer` rows in each `gun-events.jsonl`.

## Verdict: keep our approach and borrow specific pieces

Their summary does **not** describe a game-owned ammo model.

- It says the new layer separates "native ammunition changes and confirmation".
- Its tests "simulate the native responses".
- Their public code never writes ammo:
  - The `DetachableMagazine.h` comments say unseat means "native reload is safely gated, NOT that loaded rounds were removed".
  - "Seat Applied must come from the exact native transfer".
  - The same is said in their `MAGAZINE-PIPELINE-COVERAGE.md`: "mocked native transfer receipts".

So their design is most likely the same family as ours [inferred]: the native transfer credits, and a mod-side ledger tracks what the player sees. "Taking completion away from the stock animation" matches what our seat fast-forward already does: native credit on the seat frame, not after the clip.

The real differences:
- They have unit-level breadth: connected adapter tests across two profiles.
- We have in-game depth: our gates and recovery fixes are proven against the real three copies.

## 1. Which design survives the three firing copies

If anyone does move ammo ownership into the mod, the engine facts decide where writes can go:

- **Client B (+0x40) is constantly overwritten by Restore `0x6d71a0`.**
  - One XM8 run (170040) logged **3,749 `restore` rows**.
  - Across today's single-reload runs, 3 transfers (server, A and B) appeared in 32 of 76 XM8 runs and 19 of 41 F2000 runs. The rest had 2, meaning B did not transfer but was restored to the server's values. The few runs with 4–6 transfers include re-syncs at the begin.
  - **Any write to a client copy is rolled back.**
- **The server copy is the authority.**
  - A sanctioned write route exists: the ground-pickup path sets ammo on the server item through the reserve setter `0x6cd850`, then the loaded setter `0x6cd520`. Both sites are byte-checked in our give code.
  - That is the only defensible way to "write ammo"; the clients would follow through Restore.
- **Owning ammo still needs the empty gate.** The auto-reload is state-driven on every copy (Step state 2, `loaded==0`, no input needed). Even if the native reload is never asked for, it still has to be stopped. That is exactly the gate we've hardened today (fail-closed, owner-keyed, stock fallback; on by default in `ed749e1`).
- **Our approach handles the copies already:**
  - `d02f334`: the server copy and client A decide; client B's re-sync, failed holds and own transfer are counted, not failures.
  - `8efb8f6` and `3a09b02`: a cancel aborts the native reload instead of releasing it to a free 0→30 refill.

**Answer:** native credit with gates holds up better. A game-owned model would add server-copy writes and a restore race, and still need the same gate.

**How they write ammo:** unknown. Their public code doesn't [inferred from absence]. If their new block does, ask whether it writes the server item only, through `0x6cd850`/`0x6cd520`, and how it handles Restore on client B.

## 2. Evidence: anything they've proven that we haven't?

| Claim | Theirs | Ours |
| --- | --- | --- |
| XM8 physical reload in real BC2 | Earlier native sequence audit (original return, then removal and replacement, conserved ammo; checkpoint 203 doc). The new combination is **not** game-tested. | 132 runs today, 64 seated, **61 completed**, 3 seated without completing (fixed since by `b4b8ef0`, `8efb8f6`) |
| AEK | Adapter tests with mocked native responses | **1 real run (123911): seat, complete, 3 transfers.** One run, not a pass rate. |
| F2000 | Not mentioned | 47 runs, 40 seated, **39 completed** |
| Two profiles through one consumer | 12 alternating-profile CPU sequences, with focus interruption and different capacities (checkpoint 203) | Real XM8 and F2000 consecutive runs. **No in-game test that switches weapons mid-reload.** |
| Tests | 198 suites per arch | 202/202 per arch (at `d01000c`) |

**One thing they have that we lack:** a test that exercises **weapon switching during an unfinished reload**, including across profiles. Their 203 fix for it is already in our branch, but nobody has run it in-game. Everything else in their claim, we've matched or exceeded in-game.

## 3. What's worth borrowing

| Candidate | Do we have it? | Recommendation |
| --- | --- | --- |
| **Magazines that keep their remaining rounds** | Partly. The shared code we inherited records the removed magazine's `rounds`/`capacity` (`OriginalMagazine`, `DetachableMagazine.h:22-30`) and supports returning the original magazine (`AwaitingOriginalReturn`, `CompleteOriginalReturn`). But natively the gun keeps `loaded` after a pull, and the rtMagazine Transfer only tops up the missing count from reserve. So a dropped 10-round magazine costs nothing, and you can't pick it back up. Our dropped magazine (M6) is cosmetic and expires after 12 s. | **Borrow their ledger semantics if they publish them, but keep native credit.** True per-magazine rounds needs either a server-copy loaded write at the pull (`0x6cd520`, the same route as pickup) or accepting today's "rounds stay in the gun". This is a design decision for the owner, not a free port. |
| **Per-weapon profile split** | Yes: `MagazineSupportFor(asset)`, the native profile plus geometry row, and 64-bit keys (`476c634`, `ef6b3b2`, `7dfe376`). Their 203 cross-profile retirement fix is in our branch. | Nothing to port. Add their **alternating-profile CPU sequence** as a regression on our consumer, and run an **in-game mid-reload weapon switch** (XM8 ↔ F2000) in the harness. |
| **Extracted data for M416, XM8 Compact, MG36, XM8 LMG, 9A91, AKS74u, UZI** | **Already in our repo**, inherited: `docs/MAGAZINE-PIPELINE-COVERAGE.md`, `profiles/weapon-family-work-plan.json`, `tools/bc2_magazine_pipeline_coverage.py`. M416/XM8C/MG36/XM8 LMG have 12 exact zero-bolt paths; 9A91/AKS74u/UZI are deferred for nonzero authored bolt fields. | Low priority for a single-player goal. These are multiplayer kit weapons, and none appear in SP_02's pickups or kit (`docs/re/BC2_SP02_LEVEL_DATA.md`). Check campaign availability before spending effort. Next SP candidates are the SP_02 pickups: SCAR-L, AN94, AKS74u, M16, M60, MG3, QJY88. |

## 4. Their remaining work, mapped onto ours

| Their item | Our state |
| --- | --- |
| **Interrupted reload recovery** | **Mostly done in-game:** cancel during the hold aborts (`8efb8f6`, run 170455); cancel during Arming aborts (`3a09b02`); seat submission retries for 300 ms (`b4b8ef0`); a held reload survives a 500 ms reserve-read gap (`0ef2777`); stale input up to 2 s (`20c196e`); cross-profile retirement (their `98c7cf8`, inherited). **Gap:** no in-game weapon switch or sprint during a held reload. Native aborts are Deactivate `0x6e1eb0` and SprintInterrupt `0x6d6d40`, which bypass Commit. |
| **Magazine rendering during the handoff** | Done and checked in eye images: eject starts in the well (`164be72`); pouch-to-hand ease (`222489d`, `1a8ee41`); ghost clears at guided insertion (`1a8ee41`); palette rebase and hold remove one-frame native fallbacks (`cd00c1c`, audited with `0199f8a`); dropped magazine drawn in both eyes (`0b28b65`). **Gaps:** occlusion of the dropped magazine, and the 1P flicker check across a full reload in the headset. |
| **Support-grip / holster interaction** | Support grip done: the rack claims the left hand before the support grip (`c195f62`); a grabbed rack keeps the hand (`bbad169`); smooth support release (`50641dd`). **Holster interaction during a reload: not done by us.** It matches their open item and the "switch mid-reload" gap above. |
| **Automated controller testing inside BC2** | **We have it:** the headless harness with tapes, mode 7/8 probes, gate scenarios, hitch and input-gap injection. Their summary lists it as future work. |

## Recommendation

1. **Keep our approach.** It is the same native-credit family as theirs, with in-game proof on three weapons and the copy and recovery handling their summary still lists as remaining.
2. **Borrow:**
   - their alternating-profile sequence as a CPU regression
   - an in-game mid-reload weapon-switch and holster scenario in our harness (the one recovery path nobody has run in-game)
   - their removed-magazine ledger, if they publish it
3. **Ask the owner (a design decision):** should a dropped magazine take its rounds with it?
   - **Yes:** this needs a server-copy loaded write through the pickup setter `0x6cd520`. That is a new native write, so it needs its own evidence.
   - **No:** keep today's "rounds stay in the gun".
4. **Ask the friend:** does the new block write ammo into native copies? If yes, which copy and through which function, and how does it handle Restore on client B?
