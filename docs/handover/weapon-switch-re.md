# BC2 weapon switch: where the selector reads input and why a stick flick doesn't switch

Static RE, read-only, 2026-10-08. The game was not launched.

- **Exe:** `BFBC2Game.exe` build 795745, SHA-256 `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`, preferred base `0x400000`.
- **Tools:** pefile and capstone.
- **Signatures:** each is the shortest instruction-aligned prefix that occurs once in the exe's executable sections.
- **Labels:** DIS = read in the disassembly; INF = inferred.

## Answer in short

1. **Where the selector reads.** It reads an **input pair**: two EntryInput snapshots, *current* at `pair+0x1a0` and *previous* at `pair+0x1a4`. It switches only on a **rising edge** of a switch-map action (not set in previous, set in current).
   - There are two consumers:
     - **Server:** the server soldier's inventory (`soldier+0x2b4`), fed by the server player's pair (`ServerPlayer+0xc38`). **This one is authoritative.** A change marks inventory dirty bit `0x20`, which replicates to the client.
     - **Client prediction:** inventory `soldier+0x24c`, fed by a temporary pair whose previous snapshot is the last committed state and whose current snapshot is this frame's cache.
   - For action 7 (an analog action), "set" means the float at `state+0x24` is non-zero; its sign picks the direction.
   - (DIS)
2. **What gates it:**
   - the per-caller `allowSwitch` argument (server: `!0x68da60(soldier)`; client: `!0x882e30(soldier)`)
   - the rising edge itself
   - a **client-side switch filter `0x8acb50`** that runs *after* the gather and *before* the cache is committed. It defers and **clears** a switch action while the weapon is busy or while the previous action-7 value is still positive, then re-injects it later.

   In particular:
   - **Holding +1 is self-defeating** (DIS).
   - Whether the server ever receives the action-7 float through the client-to-server input path is **not resolved statically** (INF: probably not; see §5).
3. **Smallest reliable fix:** don't drive the switch through input. **Detour the server inventory update `0x6f1980` only when it is called from the server soldier (return address `0x68dc7e`).** After the original returns, if a flick is pending, the target slot differs from `inventory+0x14c`, and `allowSwitch` was true, call the native setter **`0x6d7c80(slot, 1)`** on that inventory.
   - This runs on the server thread, at the same point native selection happens.
   - Because it is inside `0x68dc30`, the caller's post-snapshot compare marks dirty `0x20`, and the client gets the change through normal replication.
   - The test range already proved `0x6d7c80` selects.

## 1. The selector chain (DIS)

| VA | Role | Signature |
| --- | --- | --- |
| `0x6f1980` | **Inventory update**, thiscall(inventory, pair*, bool allowSwitch) `ret 8` | `83 EC 08 56 8B F1 D9 86 58 01 00 00` |
| `0x6ef590` | **Selector**, thiscall(inventory, pair*) `ret 4`; returns slot or -1 | `83 EC 20 8B 81 4C 01 00 00 53 C1 E0 05` |
| `0x61c400` | **Rising edge**, thiscall(pair*, action) `ret 4` | `56 57 8B 7C 24 0C 8B F1 8B 8E A4 01 00 00` |
| `0x612970` | "Action set" on one EntryInput state, thiscall(state, action) `ret 4` | `53 8B 5C 24 08 83 FB 31 56 8B F1 74 72 57` |
| `0x61c3e0` | Current-state float of an action (`mov ecx,[ecx+1a0]; jmp 0x6128f0`) | 11-byte thunk |
| `0x6d7c80` | **Selection setter**, thiscall(inventory, int slot, bool validate) `ret 8` | `80 7C 24 08 00 56 57 8B 7C 24 0C 8B F1 74 18 8B 8E 48 01 00 00` |
| `0x6234b0` | Pair push: swap the current/previous pointers, copy the new state into current | `83 EC 08 8B 54 24 0C 53 55 56 8B F1` |

**`0x6f1980`:**
1. `inv+0x158 = max(0, inv+0x158 − dt)`, where dt is the float at `[pair+0x1a0]+0xa0`. It is a cooldown that the selector does not test.
2. If `allowSwitch`: `slot = 0x6ef590(pair)`. If `slot != -1 && slot != inv+0x14c`, it calls `0x6d7c80(slot, 1)`.
3. Finally, if the current slot is 0 or 1, it stores it as `inv+0x154` (the last primary).

**`0x6ef590`:**
- It walks the switch-map entries for `inv+0x14c` (`inv + 8 + 32*current`, a hash bucket list).
- For each entry it calls `0x61c400(pair, entry.action)`. The **first action with a rising edge wins.**
- It copies that entry's target list.
- If `0x61c3e0(pair, action) < 0` (current float negative), it **reverses** the list.
- It returns the first target that the inventory's validity callback (`[inv+0x148]→vtable+4(slot)`) accepts. Special targets: 9 = last primary (`inv+0x154`); 10 = `table[0x150a0c0][inv+0x154]`, the other primary.

**`0x61c400` (rising edge):** `!set(*(pair+0x1a4), action) && set(*(pair+0x1a0), action)`.

**`0x612970` ("set"):**
- Actions with `(1<<action) & 0x7ff`, i.e. **0–10, including 7**, test the **float** at `state+8+4*action != 0`. Action 7 is the float at `+0x24`.
- Every other action tests the bit in `state+0x98/+0x9c`.
- Action 49 (`0x31`) always returns false.
- So the "words +0x98/+0x9c" are **not** what decides action 7.

## 2. The two consumers

| Path | Caller (return) | Inventory | Pair | allowSwitch |
| --- | --- | --- | --- | --- |
| **Server, authoritative** | `0x68dc30` → `call 0x6f1980` at `0x68dc79` (return `0x68dc7e`) | `server soldier+0x2b4` | the argument of `0x68dc30`, from `0x6b0780` (server soldier vtable `0x1424850` slot 71, `0x142496c`), which gets it from `ServerPlayer+0xc38` (getter `0x531c30`) | `!0x68da60(soldier)` |
| Client prediction | `0x89a710` (only when `soldier+0x114 & 1`), `0x89b790` tail, `0x891cf0` | `client soldier+0x24c` | a temporary pair built in `0x805e20`: previous = last committed client pair, current = this frame's cache | `!0x882e30(soldier)` |

- `0x68dc30` (`83 EC 20 56 8B F1 83 BE B4 02 00 00 00`) snapshots the inventory before the update (`0x6ce9f0`) and compares after (`0x6cec90`). On a change it sets dirty bit `0x20` at `soldier+0x16c`, which is how the selection reaches the client.
- **Server gate `0x68da60`** (`53 56 8B F1 8B 86 C0 00 00 00 8B 50 18`) blocks the switch when:
  - the soldier state (`vtable+0x18` on `soldier+0xc0`) is 3, 4 or 6, or
  - `[soldier+0x280]+0xf4 & 4`, or
  - `soldier+0x298 & 1`.

  The names of these states are not mapped (INF: vehicle, ladder or mantle-type states).
- **Client gate `0x882e30`** (`51 56 8B F1 F6 86 14 01 00 00 01 57`) blocks immediately when the soldier's weapon component (`+0x230` if `+0x114&1`, else `+0x234`) is null. Otherwise it queries the component.

## 3. Client input pipeline and the switch filter (DIS)

Player input update `0x81ac70` (`83 EC 08 53 55 56 57 8B F1 E8 E2 43 FE FF`; hooked as `UpdateHook`), in order:

1. Router slot 3 → **gather `0x62eee0`** (`83 EC 14 53 55 56 8B 74 24 24 57 8B D9 33 ED`; hooked as `GatherHook`) fills the cache at `ClientPlayer+0xc74`.
2. Extra bits 5 and 9; router `vtable+0x18`; `0x7ed7a0`; `0x612d90`.
3. **Soldier input filter `0x8acb50(cache)`** (`83 EC 0C 53 56 8B F1 8B 86 20 01 00 00`). For switching:
   - `0x8a1930` (`51 8B 41 0C 8B 88 0C 02 00 00 85 C9`) scans the soldier data's **switch-action list** (`[[soldier+0xc]+0x20c]`) and returns the first action that is set in the cache.
   - If none is pending (`soldier+0x3a8 == 49`), it saves that action's float in `soldier+0x3ac`.
   - **It defers** when the weapon component reports busy (`vtable+0x34`) **or `soldier+0x3b0 > 0`**. `soldier+0x3b0` is the **previous frame's action-7 float**, copied from `input+0x24` at `0x89b7b6`.
   - When deferring it **clears the action from the cache** (`0x612a00(action, 0)`, which zeroes the float for 0–10). If the action is still clear at the end, it parks it in `soldier+0x3a8`.
   - On a later frame, when the weapon is not busy, `soldier+0x3b0 == 0` and action `0x1f` is clear, it **re-injects** the parked action with the saved float (`0x61c7e0`) and clears the pending state.
   - When `0x882e30` is true, it also clears the item's own actions and `0x25`.
4. **`0x805e20`** (`81 EC AC 01 00 00 53 56 8B F1 8B 86 70 0C 00 00`): client prediction with the temporary pair (previous = committed, current = cache).
5. **Push** the cache into the client pair `ClientPlayer+0xc70` (`0x6234b0`). Per-entry updates then read `0x7ed760` (getter for `+0xc70`). The entry update `0x8be0e0..` also appends each state to a history list at `entry+0x108` (INF: the outgoing move buffer to the server).

### Consequences for our binding

- **+1 held for several frames:** the second frame sees `soldier+0x3b0 = +1 > 0`, so the filter defers and **zeroes** action 7 in the cache. The edge exists only on the first frame, then the switch is parked until `+0x3b0` returns to 0.
- **A one-gather pulse:** survives the filter and gives client prediction one edge. The selector then still needs:
  - a switch-map target that is valid (validity callback), and
  - `0x882e30` false.

  PC builds saw the client inventory stay at 0, so on this path at least one of those failed. Which one is not proven statically.
- **The server consumer never sees our cache directly.** It reads `ServerPlayer+0xc38`, filled from the client's input stream (INF). Even a successful client prediction is overwritten by server replication if the server didn't switch.
- **Where our write lands:** if our write happens in `GatherHook` after the original, it enters the cache before the filter. If it happens anywhere after step 3 (for example an override applied after `UpdateHook`'s original returns, or a restore that runs before the push), it never reaches the pair. **Check that the InputOverride write happens inside `GatherHook`, after the original gather and before `0x81ac70` continues.**

## 4. Recommended fix (server-side direct select)

1. Detour `0x6f1980`. In the detour, call the original. Then, **only if `_ReturnAddress() == 0x68dc7e`** (the server soldier call):
   - `this` is the server inventory; check that it equals `[serverSoldier+0x2b4]`
   - a flick is pending (the gather-thread mailbox: direction ±1 and a serial)
   - the caller's `allowSwitch` argument was true
   - compute the target: the switch-map list for `inv+0x14c`, action 7, reversed when the direction is −1, first slot whose `[inv+0x148]→vtable+4(slot)` is true. Or copy `0x6ef590`'s rule and call that callback yourself.
   - the target is not `-1` and not `inv+0x14c`

   If all hold, call `0x6d7c80(target, 1)` and consume the flick serial once.
2. **Don't do this:** call the setter from the input or gather thread, or from inside an item's own Update. `0x6d7c80` and the give path may delete or replace items; see the give notes in `docs/WORK_LOG.md`.
3. **Why here:** this is the native selection point on the authoritative copy, with the native gate already evaluated. The caller's snapshot compare (`0x68dc9c` → dirty `0x20`) replicates it, so client inventory `+0x24c` follows.
4. **Fallback that keeps native semantics but needs a live check:** write action 7 as a **one-gather pulse** (a single frame, never held) inside `GatherHook`, and log `0x6ef590` calls with their return address (`0x6f19d2`), `[pair+0x1a0]+0x24` and `[pair+0x1a4]+0x24`. That shows whether the server pair ever carries it. If the server pair stays 0, input-only switching cannot work, and step 1 is required.

## 5. Not resolved statically

- **Whether the client-to-server input stream carries the action-7 float.** The server input-buffer family (vtable `0x14198a8`, `SetAnalog` `0x586dc0` and getters `0x57db50`/`0x57dc40`) maps a 13-entry index table at `0x1505158` = {0, 1, 2, 4, 5, 6, 8, 12, 14, 15, 27, 29, 49}, which **does not include 7**. However, that family looks like the AI/bot input path (ctor `0x5a7f00`, from `0x5ab619`), not the human player's. The human player's network deserializer was not found in this pass.
- The meaning of the server gate states (3/4/6) and of `[+0x280]+0xf4 & 4`.
- Whether 7 is in SP_02's soldier switch-action list `[[soldier+0xc]+0x20c]`. That is runtime data; dumping it once answers it.

## 6. One live check that settles the rest (for PC builds)

Hook `0x6ef590` as a pass-through. Log:
- the return address: `0x6f19d2` from both the server and client paths. Tell them apart by `ecx` against server `+0x2b4` and client `+0x24c`.
- `[pair+0x1a0]+0x24` and `[pair+0x1a4]+0x24`
- the bits at `+0x98/+0x9c` for 25 and 36
- the result

A flick that never shows a non-zero `+0x24` on the server inventory's call confirms that the fix in §4 is necessary.
