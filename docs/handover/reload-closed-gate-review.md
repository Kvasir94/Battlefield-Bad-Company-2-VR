# Review: fail-closed empty gate (698e9ae + ef6b3b2) against KF2's fail-closed model

Read-only review, 2026-10-08. The code was read at `origin-bc2/proto/reload` @ `f16516f`, which includes both commits. Nothing was built or launched.

The static checks used the BC2 exe installed on this PC with pefile + capstone. Its hash differs from the documented `3911FCC8…`, but the Step jump table at `0x6e6994` and the Update call site resolve to the documented addresses.

## Summary

- The closed gate is the right lever. Statically, ctx+0x28 is the only per-call input that stops Step-2 entry, and there is no native or config switch that blocks auto-reload while still allowing an explicit Reload. The token approach is therefore necessary, not a workaround (section 1).
- The gate is still **fail-open on six paths**. The biggest is that **any owner clear re-opens it**:
  - `ClearReloadOwner` runs on 7 sites, including a single failed `ReadReloadState`, focus loss, an untracked head, and stale input over 2 s.
  - It bumps `ownerRevision`.
  - The gate requires `identity.revision == ownerRevision`.
- The second is that **switching to an already-empty supported gun** reads as "unsupported" until the request cycle re-selects its profile, so the gun auto-reloads on equip.
- There is also one **fail-stuck** risk to check: KF2's X/A always has a stock-reload fallback. BC2 needs the same guarantee before the default flips.

## 1. What Step state 2 actually tests (decoded from the exe)

Step `0x6e62d0` is entered through jump table `0x6e6994`; entry 2 is at `0x6e640d`. The 2→10 decision is at `0x6e651b`–`0x6e657e`:

```
noReserve = !InfiniteAmmo(0x6cd5a0) && [obj+0x80]==0         ; [esp+0x18]
autoEmpty = [obj+0x7c]==0 && !([obj+0xa8] & 8)               ; [esp+0x14]
explicit  = (ctx+0x2c & 4) && [obj+0x7c] != Capacity(0x6cd540)
enter 10  iff ctx+0x24 != 0 && ctx+0x28 == 0 && [obj+0x44]==2 && !noReserve && (explicit || autoEmpty)
```

- **Auto-reload depends on no input at all**, only loaded==0. This confirms the ledger's server-only 2→10: an upstream input filter, KF2-style, can't replace the gate.
- `flagsA8 & 8` is **infinite-ammo mode**, not a reload switch. `0x6cd570` and `0x6cd5a0` return reserve -1 when it is set, and it is read by Transfer `0x6d6f4f` and about 15 other sites. Don't use it as a lever.
- ctx+0x24 and ctx+0x28 gate *both* the auto and the explicit term, so blocking at empty also blocks our own Reload pulse. The token is required.
- The `HoldReloadUntilFireRelease` idea from my first note is not on this path. State 2 tests the object's byte +0x8a only in the fire branch at `0x6e645b`. **Close that lead.**
- **Step has exactly one caller:** `call` at `0x6e9163`, return `0x6e9168` = Update+0x168. There are no pointer references, so the gate's `callerMatches` check is not a hole.

## 2. Remaining fail-open paths

| # | Path | Where | Effect | Fix (owner: reload session) |
|---|---|---|---|---|
| F1 | **Owner clear re-opens the gate.** `ClearReloadOwner` runs on any failed `ReadReloadState`, stale input over 2 s, focus loss, head untracked, invalid input, native inactive, not on foot, or owner-resolve failure. It calls `ClearOwner`, which does `ownerRevision.fetch_add` (`Bc2ReloadFlowRuntime.cpp:1873`). The closed gate then fails `id->revision==ownerRevision` and passes, with mask bit 1 recorded only as `identity_misses`. | `Bc2Gameplay.cpp:120-124, 1301-1337`; `Bc2ReloadFlowRuntime.cpp` Step closed block | A single failed state read, or taking the headset off with an empty gun, lets the auto-reload run until the next publication. This is the same class as run 114713, moved one layer down. | Key the closed identity by **owner identity** (player, soldier, weapon, actor and equip generation), not by `ownerRevision`. Clear it only on an observed owner, weapon or equipment change, or on death. Replace the 10 s age bound with a cheap ownership check in the hook (copy → weapon back-pointer equals the published weapon), so a long gap stays closed. |
| F2 | **"Supported" comes from the request cycle's selected profile, not from the gun in hand.** The gate uses `requestFamilyMirror`/`requestProfileMirror`, which change only when `SelectRequestProfile` succeeds. That needs a quiet entry, a current lease, a published server copy and no `BlocksEquipment` (`Bc2ReloadFlowRuntime.cpp:1357-1395`, called from `Bc2Gameplay.cpp:1437`). | Step closed block, `ManualEmptyFamilyConfig(family, profileMirror, id->config)` | After switching from XM8 to F2000 (or back), the config does not match the stale profile, so the gate passes as "unsupported". **Switching to a gun you emptied earlier auto-reloads on equip.** | Resolve support from the **published config** in `PublishOwnerObserved`: store the matching `NativeMagazineProfileId` (or SPAS) in `ClosedIdentity`, independent of cycle selection. |
| F3 | Seqlock read miss (8 tries) → `identity_misses` → pass | `ReadClosedIdentity` | Rare, under writer contention | Keep the last good identity per thread and use it if the firing address matches. |
| F4 | Copy state read failure (`Context(firing+0x3c)`) → mask 4\|8 → silent pass, **not recorded as a leak** | Step closed block | Invisible in reports | Count it and record a leak row with a new mask bit. |
| F5 | Context read failure → mask 32 → pass | Step closed block | Inherent: the byte can't be overridden without reading it | Keep, but count it in `empty_gate_leaks`, not only `context_misses`. |
| F6 | CAS failure (the byte changed between read and CAS) → native runs | `RunMagazineEmptyByteOverride` | Rare; counted in `emptyPatchFailures` | Report it under `closed_empty_gate` too. |

These are intended, not holes:
- Families without a physical reload.
- A running cycle (phase not Idle/Arming/Finished/Cancelled), which lets the engine re-sync a client copy mid-cycle.
- The 250 ms token window after Arming.
- Request mode off (no gate at all).

## 3. Fail-stuck check before flipping the default

KF2's rule: if the VR path can't run, the stock reload plays. Under a closed gate, an empty supported gun reloads only through our token (Arming plus the Reload pulse). Before the default flips, confirm that a token is always reachable when the physical path can't start:
- the left hand is busy or not tracked
- the reserve read is deferred
- the pouch is empty
- `magazine_support` says no geometry

For example, a Reload press with no physical path available should issue the token for a plain native reload. I did not find where B maps to Arming when the physical consumer is unavailable; `EjectPress` at `Bc2Gameplay.cpp:1261` is the fixture path. Add a gate-matrix row: empty gun, left hand withheld, press Reload. It passes if the gun reloads natively.

## 4. Suggested gate-matrix additions (PC builds)

1. `--input-gap` longer than 2 s with an empty gun → expect no 2→10 (F1).
2. Fire XM8 to empty, switch to F2000, switch back → expect no 2→10 on either equip (F2).
3. The existing 15 fps hitch run, with `closed_empty_gate.identity_misses` and `context_misses` asserted to be 0.
4. Empty gun with the left hand withheld, then press Reload → expect a native reload (fail-stuck guard, section 3).
