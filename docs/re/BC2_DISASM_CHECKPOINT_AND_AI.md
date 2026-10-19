> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 795745: start a level at a checkpoint, and why IsAiEnabled=false crashes

> Cloud static disassembly of build 795745 ([`tools/re/bc2re.py`](../../tools/re/bc2re.py)), 2026-10-08.
> In this report **SV** means read in the disassembly; it is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md). Nothing here has run in the game unless marked LO. Script names refer to throwaway analysis scripts that are not in the repo.

This is static RE only. SV means I read it in the disassembly. INF means inferred.
Signatures were checked for uniqueness over .text and ctr (scripts: ckpt_ai.py and sig.py).

## A. The start int is the difficulty, not a checkpoint (SV)
Chain: UiRequest+0x10 -> msg(0x28/0x10)+0x34 -> Client+0xc8 -> ServerStartParams+0xc ->
Server+0x90 (0x83c400) -> arg4 of the server level load `0x574fb0` -> AI `0x5b33c0(arg)`.
In `0x5b33c0`, 0 = "Casual" (0x1419bb0), 1 = "Normal" (0x1419ba8) and 2 = "Hard" (0x1419ba0).
Each value is written into the "AI/Difficulty" (0x141a37c) settings blob. Other values become Normal.
- newSP: `0x77c149` passes `atoi(param)` to `0x7600f0`. The live call `0x7600f0(1)` was therefore SP on Normal.
- `0x5b33c0` sig: `55 8B EC 83 E4 F8 83 EC 4C 53 56 57 33 DB 53 68 ?? ?? ?? ?? 53 68 ?? ?? ?? ??` (1 hit)

## B. A checkpoint is a serialized save blob, not an index (SV)
- Server level load `0x574fb0`: thiscall. It takes 5 stack args and ends with `ret 0x14`. The only caller is 0x85fa48 in the server update.
  - arg1 = &Server+0x74 (level), arg2 = Server+0x1e (byte), arg3 = [Server+0x18] (load progress),
    arg4 = Server+0x90 (difficulty), arg5 = &Server+4 (a `vector<u8>` holding the save blob).
  - At 0x5751b8 it logs "Load save game" (0x1418bc0) and calls `0x54edb0(this, &blob)`.
  - sig: `81 EC D8 00 00 00 53 55 56 57 6A 02 8B F1 E8 ?? ?? ?? ?? 83 C4 04`
- `0x54edb0` LoadSaveGame(const vector<u8>*): thiscall, `ret 4`. It returns at once if the blob is empty.
  Otherwise it walks the level entity list at this+0x30 and finds the first entity whose type is
  ServerSaveEntity (TypeInfo 0x1bf1800). It then calls `0x67e030(ent, blob)`.
  sig: `55 8B 6C 24 08 8B 45 04 2B 45 00 74 6E 53 57 8B 79 34 8D 59 30 3B FB`
- `0x67e030` ServerSaveEntity::load(blob): thiscall, `ret 4`. It checks "Game.GameVersion" and
  deserializes the state of every entity. At 0x67e23b it calls the AI state load `0x599820`, which runs only when IsAiEnabled is set.
  sig: `8B 44 24 04 83 EC 34 55 56 8B E9 8B 4D 40 50 E8 ?? ?? ?? ?? 8B 4D 40`
- The blob comes from the profile and travels this path: type-4 UiRequest +0x108 ptr / +0x10c size ->
  msg+0x38/+0x3c -> Client+0x54 -> params+0x1c -> Server+4.
- ServerSettings.SavePoint/LoadSavePoint have no name reader. Their field-name strings have no code refs. Treat them as dead (INF).

## C. The native "Continue" path (SV)
1. `querySavedGames` (cmd 0x81) calls `0x76ae90`. ABI: thiscall, ecx = FE+0x1b0, no args, plain `ret`.
   - It reads the profile blob "header" (`0x80d660` getBlob(key, &ptr, &size), ret 0xc). The profile object comes from the msg query (cat 0x2b, type 0x1d).
   - It builds a 0x38-byte SaveEntry:
     - +0 String level
     - +0xc and +0x18 Strings: display fields (the +0x18 string comes from mapping the level name)
     - +0x24 String (optional)
     - +0x30 int difficulty
     - +0x34 bool valid (header version == 3)
   - It push_backs the entry into the vector at FE+0x1b4 and calls AS "querySavedGamesEnd".
   - sig: `81 EC D0 00 00 00 53 57 8B F9 E8 ?? ?? ?? ?? 33 DB 89 5C 24 5C C7 44 24 60 1D 00 00 00`
2. `continueSP` (cmd 0x83) calls `0x75ffa0(FE)`. ABI: thiscall, no args, `ret`.
   - It takes the last entry of the FE+0x1b4 vector. It does nothing if the vector is empty or the entry is not valid.
   - It reads the profile blob "body". It calls AS "onStartLoadingSP". It queues UiRequest{type 4, level = entry+0, +0x10 = entry+0x30, +0x108/+0x10c = body ptr/size} with `0x956e10`.
   - sig: `8B 81 B8 01 00 00 2B 81 B4 01 00 00 81 EC 48 01 00 00 A9 FC FF FF FF`
3. `0x951e00` handles type 4 at 0x952045:
   - Message 0x918ca0 (type 0x10). The level is used as-is, with no "Levels/" prefix.
   - +0x34 = difficulty.
   - The blob is copied with a new alloc + memcpy into msg+0x38/+0x3c. UI+0x82ccd = 1.
   - IntroPlayed is not set.
   - sig at 0x952045: `C6 86 C0 2C 08 00 01 E8 ?? ?? ?? ?? 8B 40 34 3B C3 74 09 39 58 54`
4. From there it follows the same Client and Server chain as newSP. The server then runs "Load save game".

## D. DLL candidate (INF, needs a PC test)
Run this on the UI thread, at the same point as the working newSP hook (UIManager::update, FE ready).
- **Route 1 (simplest):**
  1. Play SP_02 once until the first autosave after the intro. Back up the profile save.
  2. In the hook, call `0x76ae90(FE+0x1b0)`, then `0x75ffa0(FE)`.
  3. Or call `0x77bc10(FE,"querySavedGames","")` then `0x77bc10(FE,"continueSP","")`.
  - This loads whatever save is last. If you play past the checkpoint, the profile save is overwritten.
- **Route 2 (repeatable):**
  1. Capture the type-4 request once in a `0x956e10` hook: the level String, +0x10 and the +0x108/+0x10c blob. Save it to a file.
  2. Later, build `UiRequest r` with ctor `0x75df60` and set r.type = 4, r+4 = level, r+0x10 = difficulty, r+0x108/+0x10c = ptr/size. The UI copies the blob. Then call `0x956e10(UI,&r)` and destroy r (`0x75eab0`).
  - This skips `querySavedGames` and the profile.
  - A blob is valid only for the same GameVersion (checked in 0x67e030). It must also match the IsAiEnabled state that was in effect when it was saved (0x599820 and the save 0x5b1df0 both depend on it).
- Restoring at the checkpoint should also skip the intro cutscene: the logic state is restored. This is the behaviour of the retail "Continue" button (INF, game design).
- No setting, message field or start parameter selects a checkpoint by index (SV for the start-param path).

## E. Server.IsAiEnabled=false crash (INF; partial SV)
- Readers of "Server.IsAiEnabled" (0x1418be0) go through `0x4f4c40` with Bool TI:
  - 0x57505b: server level load
  - 0x59421b: per-frame
  - 0x599843: save-state load
  - 0x59e1cc: AI update
  - 0x5b1e12: save-state save
  - A second spelling, "Server.IsAIEnabled" (0x1419b8c), is read at 0x59590f.
- SV: in `0x574fb0` the whole AI bring-up is gated by `level+0x3ae && IsAiEnabled`:
  - `0x5b4b20` creates the AI system. It allocates 0x1c bytes, constructs with 0x5b4840 and stores the result in [0x1567178].
  - The ctor 0x5b4840 sets the globals [0x18903ec], [0x18903a8], [0x1567184], [0x189037c] and [0x1894180].
  - `0x5b33c0` sets the difficulty.
  - 0x5958f0, 0x57ee10 and 0x5af5f0 run in the same block.
  - With IsAiEnabled false, all of these stay NULL. Gate sig at 0x57504f:
    `8B 0D ?? ?? ?? ?? 68 ?? ?? ?? ?? 68 ?? ?? ?? ?? E8 ?? ?? ?? ?? 80 38 00 74 4A E8 ?? ?? ?? ?? 8B 94 24 F8 00 00 00`
- SV: most game-side users test [0x1567178] first (0x6119b0, 0x611be0, 0x637549, 0x53ed06). Some do not. These call `mov ecx,[0x18903a8]; mov edx,[ecx]; call [edx+0xc]` without a NULL check:
  - `0x6908d0`: guarded only by `arg+0x220 != 0`. 3 callers (0x6a3d78, 0x6b4d44, 0x6b4dc5).
    sig `8B 44 24 04 8B 80 20 02 00 00 85 C0 75 03 C2 04 00 8B 0D ?? ?? ?? ??`
  - `0x70c120` at 0x70c21d: guarded by `[ebx+0x220] && [edi+0xd84]`.
    sig `83 EC 08 56 8B F1 F6 86 E4 00 00 00 40 0F 84 4C 02 00 00 8B 86 98 00 00 00`
  - `0x72d560` at 0x72d666: same guard.
    sig `55 8B EC 83 E4 F0 83 EC 14 53 56 8B F1 8B 86 98 00 00 00 85 C0 57`
  - Each is a NULL `this` vcall if its guard passes while AI is off.
- **INF:** level data in SP still spawns scripted or AI soldiers. Such a soldier would carry a non-null +0x220 (AI/controller link). The first such soldier update then dereferences a NULL [0x18903a8], which fits a crash a few seconds into the load.
  - I did not prove what +0x220 is. A crash dump at one of these three sites would confirm it.
  - Conclusion: `IsAiEnabled=false` is not a supported SP mode. Keep it true.

## F. Follow-up: named SaveEntityData checkpoints (SV unless marked)
- ServerSaveEntity (TypeInfo 0x1bf1800) has its vtable at 0x1420998. TypeInfo getter is [2] = 0x669d50 and the dtor is [3].
- Event handler vt[52] = `0x688330`. When event hash == 0x7c8a0784 ("Save" input, INF name) and `[[this+0x40]+8]==0`, it calls `0x687ec0`.
  sig `8B 44 24 04 81 78 0C 84 07 8A 7C 75 0E 8B 51 40 83 7A 08 00 75 05 E8 ??`
- `0x687ec0` ServerSaveEntity::doSave: thiscall, no args.
  - It returns early when the local player soldier is missing or dead.
  - Body: Game.GameVersion, then [this+0x50], then each entity's state through vt+0x88, then AI state through `0x5b1df0` (only when [0x1567178] is set and IsAiEnabled is true).
  - Header: a level string (+0x48 of the server object), data+0x80 (SaveNameSID), an optional texture, the difficulty (from [0x18903ec] vt+0x34) and version 3.
  - It sets this+0x55=1 and then fires OnSaveDone.
  - sig `81 EC 5C 01 00 00 55 56 8B E9 E8 ?? ?? ?? ?? 8B 48 18 8B 01 8B 50 18 33 F6`
- The game keeps exactly one save slot per profile, the "header" and "body" blobs. A new save overwrites it. There is no checkpoint id or index field.
  - The save's name is display-only (SaveNameSID in the header).
  - Which checkpoint is restored is decided by the entity states inside the body.
  - `0x54edb0` hands the whole body to the first ServerSaveEntity, and `0x67e030` restores every entity.
- Loading at level start happens only when the start message carries a body blob, i.e. a type-4 UI request (continueSP) or type 7 (in-game doLastSavePoint).
- **Consequence:** "Initial Save" runs on every fresh start and overwrites the slot.
  - To start at "save post start cutscene", that save must have happened once: play or skip CS_11 once.
  - Then either use Continue right away (route 1), or capture the type-4 request blob in a `0x956e10` hook right after that save and replay it on later launches (route 2, section D).
  - After the restore, the saving entity's OnLoadDone path should give controls and the XM8 (INF from your level-flow data).

## G. DebugMenuItemEntityData: no retail consumer found (INF, strong)
- TypeInfo 0x1c05f84 has only 3 code refs, all reflection plumbing:
  - 0x13d1e70: static registration via 0x500880, with create-instance factory 0xb6d260.
  - 0x13d16d0: TypeInfo getter in the data vtable 0x1472ec0, which is used only by the ctor/factory at 0x13d1be0/0x13d1c30/0x13d1cd0, called from 0xb6d27d and 0xb6d333.
  - 0x13d1f60: atexit.
- No "*DebugMenu*Entity" runtime class name exists, so the data objects load but nothing builds an entity from them.
- DebugMenuSubMenuEntityData (TypeInfo 0x1c05f34) is the same kind of data-only class (INF).
