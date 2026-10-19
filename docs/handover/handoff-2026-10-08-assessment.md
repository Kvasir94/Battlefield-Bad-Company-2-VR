# Handoff section: the "KF2 reload wiring assessment" thread, plus the older threads

Written from the thread history and its files, 2026-10-08. That thread's session was stopped, so it did not write this section itself.

## Scope

On 14:14 UTC the owner asked: "assess … if kf2's reload path and wiring would be easier to setup, implement and control. Less flakey?"

The thread stayed read-only, apart from one docs PR. Over the day it produced:

| Time | File or output | What it concluded |
|---|---|---|
| 14:19 | [kf2-vs-bc2.md](kf2-vs-bc2.md) | **Verdict: hybrid.**<br>- Keep native credit, but control it the way KF2 does: fail-closed, event-driven, with one authoritative copy.<br>- **Don't** own ammo. The reasons given were that the empty gate would still be needed, writes would be forbidden by the old rules, and the reserve pool has about 260 uses. |
| 15:29 | [reload-closed-gate-review.md](reload-closed-gate-review.md) | - Decoded Step state 2 from the exe. `ctx+0x28` is the only per-call auto-reload lever, so the start token is necessary.<br>- Found six fail-open paths. The biggest: an owner clear re-opened the gate (F1), and "supported" came from a stale profile (F2). |
| 15:29 | [reload-wiring-design.md](reload-wiring-design.md) | Proposed order:<br>1. Close F1 and F2, then make the gate the default.<br>2. Server copy as the authority, observing client1.<br>3. Event mailbox (command board).<br>4. Rack on by default. |
| 17:00 | PR #10, **merged** | `docs/re/BC2_ENGINE_MAP_20261008.md`: a byte signature for every address (exe `c603f686…`), plus `docs/WORK_LOG.md` milestones. |
| 17:29 | [friend-ai-comparison.md](friend-ai-comparison.md) | - Keep our approach. The friend's design is inferred to be the same native-credit family; their new code isn't public.<br>- Borrow their mid-reload weapon-switch test.<br>- Asked whether a dropped magazine should take its rounds. **The owner: no, not yet** (21:19). |
| 19:07 | [weapon-switch-re.md](weapon-switch-re.md) | Stick flick does nothing because the switch is server-side.<br>- Fix: hook the inventory update `0x6f1980` (server soldier, return `0x68dc7e`), then call the selector `0x6d7c80(slot,1)`.<br>- PC builds shipped this as `0b54020` (passed in scripted runs). |

## What was adopted (all on `proto/reload`)

- **Fail-closed empty gate.** `698e9ae`, `ef6b3b2`, then the F1/F2 fixes and the stock fallback in `7dfe376`. On by default since `ed749e1`.
- **Server plus client0 as the authority.** `d02f334` (`FVR_BC2_COPY_AUTHORITY`, opt-in).
- **Event mailbox.** `5998f4c`, `62c840e` (`FVR_BC2_RELOAD_MAILBOX`, opt-in, magazine only).
- **Rack on by default.** `e602ada`.
- **Server-side weapon switch.** `0b54020`.

## What Option B supersedes

The owner chose B at 21:45: credit the server copy at the seat and never run the native reload. The comparison thread's deep dive is in [kf2-vs-bc2-reload-deep-dive.md](kf2-vs-bc2-reload-deep-dive.md). B **reverses** the assessment's "don't own ammo" verdict. That verdict rested on the old rules, which the owner dropped.

- **Kept under B:** the fail-closed empty gate. It is still needed to stop the engine's auto-reload. Rack-by-default and the weapon switch are also kept.
- **Becomes moot under B:**
  - The mailbox and copy-authority flags. They exist only for the hold path.
  - The "hold expiry is benign" rule in the design doc.
  - The concern about ammo writes on three copies. B writes only the server copy, through the pickup setters `0x6cd850`/`0x6cd520`. The clients follow via Restore, pending the HUD-copy check.

## Still open

1. **Question for the friend.** Does their new block write ammo into native copies, and if so, which copy and through which function? It's worth asking now, because it would confirm or refine B. Owner: the owner. It's a message, not code.
2. **Dropped magazines keep their rounds?** Answered "no, not yet". Revisit after B. It would use the same `0x6cd520` setter at the pull.
3. **In-game mid-reload weapon switch and holster test.** Borrowed from the friend. Partly covered by the weapon-flick rows, but holster isn't. Re-scope it for B once testing resumes.
4. **Doc follow-ups from the gate review.** The F3–F6 minor leak-reporting gaps (`reload-closed-gate-review.md` §2) go moot under B, unless the hold path is kept as the fallback.

## How to pick it up

The thread's analytical work is done. Its remaining value is (1), the question for the owner's friend, plus reference material. A fresh session needs nothing from this thread to do B; use `reload.md` §4.

---

## Older threads (one line each)

- **"Help me work out a plan for a project"** (completed). The original planning and repo-cleanup thread.
  - It disabled the CI workflow.
  - It merged PRs #4 (engine-mapping docs) and #5 (test range), and closed #6 (runner fix; its focus-lock fallback is still on `feat/feel-wiring`).
  - It listed 12 merged branches to delete. That was blocked by the cloud proxy; most are now gone from the remote.
  - **Nothing open,** apart from the branch deletions folded into the merge-and-delete plan.
- **"Fast test level and loadout"** (resolved). A cloud research thread for a fast test level and weapon give, which became PR #2 and #4. It was stood down at 06:09 UTC when the owner banned cloud usage. Two edge cases were handed to the reload session:
  - capacity truncation via `_ftol2`
  - reserve −1 meaning infinite
  
  Its unfinished per-shot ammo-decrement trace continued on `re/pc-give-path-ghidra` (`42d6249`). **Nothing open** apart from cherry-picking `42d6249`.
