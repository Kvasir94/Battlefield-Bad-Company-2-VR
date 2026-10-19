# Handoff section: KF2 comparison, decisions, rules, and branch inventory

Written 2026-10-08 ~22:15 UTC, read-only. Nothing was merged, deleted or pushed.

## 1. KF2 vs BC2 reload comparison (summary)

Full report: [kf2-vs-bc2-reload-deep-dive.md](kf2-vs-bc2-reload-deep-dive.md). It was reviewed blind by a separate agent.

- **The design is the same in both games.** KF2 (22 Sept) and BC2 both start the stock reload, pause it while the hands work, and let the game credit the rounds. What differs is the cost of the pause:
  - **KF2:** UnrealScript calls by name (`PauseTimer`, `PerformReload`) on one weapon object, on the game thread. The pause is latched.
  - **BC2:** has no pause. We zero the frame delta (`ctx+0x18`) on every Update, on three firing copies:
    - client0 `+0x3c`
    - client1 `+0x40`, which the engine re-syncs via Restore `0x6d71a0`
    - the server item `+0x10`
  - **That runs from hooks on sim threads,** with decisions made on our gather (input) thread.
- **The cost.** At least 35–40 of the 282 non-merge commits on `proto/reload` fix timing, read-miss, race, copy-order or hitch failures. Two failure classes fight each other:
  - A missed hold is a **free refill**.
  - An over-eager cancel **throws away a valid reload**.
- **Blame (judgement, revised after the blind review):** engine ~50%, our integration architecture and rules ~30%, missing decompilation ~10–15%, process ~10%.
- **Findings from the blind review:**
  - Gun admission requires bit-exact timing floats (`Bc2ReloadConfigDescriptor.h:36,43`).
  - The `dt ≤ 50 ms` safety abort turned every hitch into a failed reload. It was removed in `583b08c`.
  - KF2's turning point was `7040de34` (27 Sept, headset-first). The owner has **not** adopted it.

## 2. The owner's decisions (all 2026-10-08, in the "KF2 vs BC2 reload code comparison" thread and the timeline)

| Time (UTC) | Decision |
|---|---|
| 21:45 | **Reload Option B.** On the VR magazine seat, the mod credits the rounds itself on the server firing copy. The native reload never runs for supported guns. Native reload animation is **not** required ("almost all play will be first person"). |
| 21:45 | Replace the "restore native state" restriction. Quote: "Native feel with native modern vr controls is the real goal." Also: "Don't clutch pearls." |
| 21:48 | **Dump every AGENTS.md rule except the new goal rule.** "These docs and memories are just polluting our work." |
| 21:48 | Gun admission by weapon type plus live capacity, not exact timing floats. |
| 21:53 | **No cloud agents.** Headset testing is **not** mandatory ("We aren't there yet"). |
| 21:19 (earlier thread) | Dropped magazines do **not** take their rounds with them, for now. |
| 22:08 | **Stop all threads.** Write this handoff. **No playtesting and no emulated or scripted testing until all work is merged to main and every other branch is deleted.** |

## 3. The one rule (replaces all of AGENTS.md)

> The goal is a working, great-feeling, native-feeling VR gunplay version of BC2 with modern VR controls. We own the game install and every player runs the mod, so the mod may own game state: write fields, call native functions, patch code, keep persistent overrides. Native animation is optional.

**Operational fact, not a rule:** push to `origin-bc2` (the private development repository). `origin` is the friend's upstream `justiceofrip/FrostbiteVR`; don't push there.

**Dropped:**
- restore native state on every outcome
- preserve native animation / exact-write restoration
- evidence before enabling a feature, and default-off features
- full x86 + x64 builds and regressions on every change
- updating the transfer ledger and handoff on every change
- mandatory reading of README, ARCHITECTURE, 2142-TRANSFER and HANDOFF
- headset checks for runtime claims
- the Refractor/BF2142 separation bullets
- the binaries/credentials bullet

**Blocker:** `AGENTS.md` on `origin-bc2/main` and on `proto/reload` @ `583b08c` **still has the old rules**.
- The reload session's edit was refused by the Claude Code permission guard as self-modification. It then also refused the Option B source write, reading it as a bypass of the old rules.
- **The owner must either:**
  - paste the new `AGENTS.md` himself (exact text in `reload.md` §5), or
  - add `"Edit"` and `"Write"` to `permissions.allow` in `<repo>\.claude\settings.local.json`.

**Stale code comments to fix with it.** They restate the old rule:
- `Bc2ReloadRequestCycle.h:60-65`
- `Bc2ReloadHold.h:22-24`

## 4. Option B change list (not started)

1. **Block native reload entirely.** The empty gate (Step `0x6e62d0`, `ctx+0x28` override, `Bc2ReloadFlowRuntime.cpp` ~1018-1203) blocks 2→10 for the player's supported weapon, always.
   - Remove the 100 ms Reload pulse for physical reloads (`Bc2Gameplay.cpp:1725-1728`, `Bc2MagazinePhysicalReload.cpp:625`).
   - Keep a plain native reload when no hand path is possible.
2. **Credit on the seat.** Credit the server copy (item `+0x10`) through the pickup setters:
   - reserve `0x6cd850`, then loaded `0x6cd520` (byte-checked in the give code; `docs/re/BC2_WEAPON_RUNTIME.md:212`)
   - inside the server copy's Update hook, consuming a hand event from a lock-free queue, with no leases
3. **First check:** which copy the HUD ammo reads, and whether client0 follows the server write. The PC builds session was asked; see its section.
4. **Reload sound.** The clip no longer starts, so start the learned reload-clip GUID ourselves through `0x881650`, on the magazine pull. Reuse `ReloadSoundGate`'s replay path (`Bc2Gameplay.cpp:439-473`).
5. **Then delete** the state-11 dt hold, the `ApplyAdvance` fast-forward, the magazine-path leases and cohort reads, and the abort windows.
6. **SPAS** follows after the magazine path.

## 5. Friend's FrostbiteVR (upstream `origin`)

- `justiceofrip/FrostbiteVR` main is `98c7cf8` (checkpoint 203, 2026-10-07). It has only two public commits, `a820f76` and `98c7cf8`, and both are already in our main.
- Their newer work is not public. From their summary, it is the same native-credit family as our old approach.
- We decided not to wait on it. Earlier write-up: [friend-ai-comparison.md](friend-ai-comparison.md).

## 6. Repo and branch inventory (`origin-bc2`, fetched 22:10 UTC)

**main** is `6de115d` (PR #10 merge, 13:14 CDT).

**Update 22:15 UTC:** `proto/reload` is now `5812241`, 193 ahead of `main`. It adds the `tools/harness` scripts on top of `583b08c`. No new branches appeared.

**PR #9** (`proto/reload` → main):
- draft
- mergeable, CLEAN
- 191 commits ahead of main, 0 behind
- head `583b08c` "Held reloads accept any finite frame delta" (16:49 CDT)
- no CI checks reported on the PR

**No Codex-authored commits** on any branch. Every commit is Kvasir94 or Claude with Claude co-author trailers.

### Remote branches other than main

| Branch | Head, date | Commits not on main | Already on proto/reload? | Recommended action |
|---|---|---|---|---|
| `proto/reload` | `583b08c`, 10-08 | 191 | (is it) | **Merge** via PR #9 after the AGENTS.md rewrite and any in-flight session pushes land |
| `docs/engine-mapping-oct8` | `5ae40d8`, 10-08 | 0 (merged as #10) | yes | **Delete** |
| `test/range-merged` | `1f52f7b`, 10-08 | 0 (merged as #5) | yes | **Delete** |
| `feat/reload-parity` | `32b209e`, 10-08 (Claude) | 3 | yes, patch-identical (`git cherry` all `-`) | **Delete** (already contained) |
| `feat/reload-parity-docs` | `24f4d8c`, 10-08 | 4 (PR #8 closed) | yes, patch-identical | **Delete** (already contained) |
| `re/pc-give-path-ghidra` | `42d6249`, 10-08 | 4, docs only | 3 of 4 identical in content. `42d6249` adds 20 lines narrowing the per-shot ammo decrement to a firing listener | **Cherry-pick `42d6249`** onto proto/reload (RE fact, docs only), then delete |
| `feat/feel-wiring` | `37cd41f`, 10-08 | 2 (PR #6 closed). Replay-runner focus lock (`d084313`) and tape idle frame (`37cd41f`) | no, real diffs | **Drop.** Replay/emulated tooling, now frozen by the "no emulated testing" directive and not needed under Option B |
| `feat/spas-sim` | `081c5ed`, 10-08 (Claude) | 1, "WIP… unbuilt, untested" | no | **Drop.** An offline sim of the hold-based SPAS that Option B replaces |
| `test/any-loadout` | `230a6e3`, 10-07 | 1, XR muzzle smoke test on any loadout | no | **Drop** (test harness tweak; superseded by the test range) |

### Local-only branches on the dev PC (`<repo>`), never pushed

All are fully contained in `proto/reload` (`git cherry` shows no missing patches):
- `feat/xm8-slider-probe`
- `gate-default-flip`
- `wip/haptics`, `wip/pump`, `wip/pump2`
- `wip/sounds`, `wip/sounds2`
- `wip/spas-as2`
- `proto-work`, the same commit as proto/reload

**Delete all of them.**

**Local branches whose upstream is gone:**
- `fix/reserve-lease-diagnostics` (merged as #7)
- `test/range`
- `tools/typeinfo-dump`

**Delete them.**

### Worktrees to remove once merged

These must be removed before their branches can be deleted:
- `%USERPROFILE%\worktree-anyloadout`, `worktree-feel`, `worktree-range`, `worktree-typeinfo`
- `<worktree-ci>` (detached)
- `<worktree-re>`
- `<worktree-reload>` (`wip/pump2`)
- `<worktree-reload2>` (`proto-work`)
- `<worktree-tr>`
- `<old-worktree>` (`proto/reload`)

The main checkout `<repo>` is on main, 141 behind `origin-bc2/main`, and clean.

**Not checked:** any WIP pushed by the PC builds or reload sessions after 22:10 UTC. Re-fetch before merging.
