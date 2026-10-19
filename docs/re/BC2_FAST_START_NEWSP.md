> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 fast start into gameplay (newSP from the UI update)

> How the PC test-range run at 05:27Z on 2026-10-08 got from launch to
> first-person gameplay with no clicks. It was reconstructed read-only from the
> running process, its log and config, and the uncommitted source in the
> planning session's test-range worktree (on top of
> `test/range` `84cb8d8`). The addresses and bytes below were re-checked
> against a copy of the installed exe. Build 795745, `BFBC2Game.exe` SHA-256
> `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`, PE
> timestamp `1314210909`, SizeOfImage `0x1913000`, image base `0x400000`.

## Result

The run took about 4.3 s from process creation to the newSP call. The owner saw
the game reach first-person gameplay; the log stops at the newSP call.
Log, verbatim:

```
00:27:14.168 BC2TestRange start pid=17640
00:27:14.168 assigned string field Client.Level directly
00:27:14.168 set Client.Level = Levels/SP_02 (before "Levels/SP_01", after "Levels/SP_02")
00:27:14.168 settings applied; newSP armed (difficulty 1, after 60 menu frames)
00:27:14.169 update detour call 1 this=1CA89D80 uimgr=1CA89D80 thread=25708
00:27:14.463 calling newSP(1) menu=182D41C0
00:27:14.463 newSP(1) queued on the UI thread after 60 menu frames, 0.3 s after DLL load
```

The process was created at 00:27:10 with no command-line arguments. The DLL
loaded about 3 s later (`Start-BC2TestRange.ps1 -InjectDelayMs 3000`).

## Method

1. **Launch** `BFBC2Game.exe` normally, with no arguments; the retail client
   ignores `Client.*` on the command line. Wait about 3 s, then inject a DLL
   with `CreateRemoteThread(LoadLibraryW)` from a 32-bit injector. An inject
   about 100 ms after launch makes BC2 exit silently.
2. **Verify the build** before touching anything: PE timestamp, SizeOfImage
   and the code bytes listed below. Write nothing on any mismatch.
3. **Set the level** from a worker thread (not inside `DllMain`):
   - Wait for the settings registry `*(void**)0x155cfdc`, then for each key's
     group to register (poll the getter until it returns a real field).
   - `SettingsManager::Set` `0x4f4bd0`: `thiscall(registry,
     const eastl::string* key, const eastl::string* value)`, `ret 8`. It
     converts scalars but leaves `eastl::string` fields unchanged.
   - So, for `Client.Level`, get the field with
     `0x4f4c40` `thiscall(registry, const char* name, const void* fallback)`,
     which returns the field address. Then build a fresh engine string with
     `0x4014c0` (eastl string from C string, `thiscall(string*, const char*)`),
     using the field's own allocator (4th dword). Swap `begin`, `end` and
     `capacity` into the field, and leave the old buffer allocated.
     Value: `Levels/SP_02`.
4. **Hook the UI manager update.** Wait for `*(void**)0x19a4ca8` (UIManager).
   Its `update` is `0x959900`, `thiscall(this, a, b)`, `ret 8`. Replace the
   vtable entry at `0x1440860` (slot 28 of the vtable at `0x14407f0`; the
   entry must currently hold `0x959900`) with a detour, using
   `VirtualProtect` RW around the 4-byte write.
5. **Call newSP from the detour,** on the UI thread. Count frames while the
   main menu exists (`*(void**)(uimgr + 0x82b24) != NULL`). After 60 such
   frames (about 0.3 s), call once:
   `newSP` `0x7600f0`, `thiscall(void* /*unused*/, int difficulty)`, `ret 4`,
   with difficulty 1. It reads the UIManager from `0x19a4ca8` itself and
   queues a type-3 request on it, which reads `Client.Level`. Then call the
   original `0x959900(this, a, b)`. The same update drains the queue and
   the level loads.

This is what the menu's NEW CAMPAIGN does. Calling newSP from the UI thread
inside update is what makes it safe; the queue has no lock.

## Signatures

| Address | Function | Bytes (unique in the image unless noted) |
| --- | --- | --- |
| `0x7600f0` | newSP | `81 EC 34 01 00 00 8B 0D A8 4C 9A 01 53 56 57 68` |
| `0x959900` | UIManager::update | `55 8B EC 83 E4 F0 83 EC 24 53 56 57 8B F1 E8 1D` |
| `0x4f4bd0` | SettingsManager::Set | `85 C9 74 17 8B 44 24 08 8B 54 24 04 6A 00 50 83 C1 FC 52 E8` |
| `0x4f4c40` | SettingsManager::Get | `85 C9 74 08 83 C1 FC E9`, **not unique** (20+ hits); check it at the fixed address only |
| `0x4014c0` | eastl string from C string | `53 55 56 57 8B 7C 24 14 8B C7 8B E9 8D 50 01` |
| `0x1440860` | vtable entry | holds `0x959900` |

Globals: settings registry `0x155cfdc`, UIManager `0x19a4ca8`, main-menu
pointer at UIManager+`0x82b24`.

## Config used

`<stage>\BC2TestRange.cfg`:

```
Client.Level Levels/SP_02
TestRange.Start newSP
TestRange.Difficulty 1
TestRange.WaitFrames 60
```

`TestRange.*` keys configure the DLL; the rest go to the engine. AI-off and
cutscene keys were not in this run's config.

## Caveats

- The hook is a vtable swap that is never restored for the rest of the
  session. A production version should restore the entry once newSP has
  fired.
- The `Client.Level` string swap leaks the old buffer, on purpose, to avoid
  freeing with the wrong allocator.
- This source is uncommitted in the planning session's worktree at the time
  of writing. Its owner should commit it.
