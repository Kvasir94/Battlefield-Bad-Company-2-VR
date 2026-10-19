> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 795745: level-load path, settings, menu and intro skip (static RE)

Build: BFBC2Game.exe 795745, x86, image base 0x400000. Static analysis only; nothing
here has been run in the game. Labels: **SV** = I read the disassembly that shows it.
**INF** = inferred from the disassembly or naming, not checked end to end.
Tool: [`tools/re/bc2re.py`](../../tools/re/bc2re.py) (set `BC2_EXE` to a local exe); cloud pass, 2026-10-08.
In this report **SV** is the **DIS** level of [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md).

String type: EASTL `String` is 12 bytes `{char* begin, char* end, char* capEnd}`. An
empty string points at the shared buffer 0x154c450 with capEnd 0x154c451 (SV, e.g.
0x501e4a, 0x5ecf7f). Assign is `0x4245b0` (thiscall `String::assign(begin,end)`, SV).
Allocation goes through `0x420eb0` (EASTL allocator) and frees through `0x41b8b0`.

---------------------------------------------------------------------------

## Live result (PC, 2026-10-08): step 1 is LO

The PC run in `docs/re/BC2_FAST_START_NEWSP.md` (branch
`bc2/re/pc-give-path-ghidra` `034f675`) entered at `0x7600f0`. It reached
first-person gameplay in SP_02 about 4.3 s after launch.

- **Client.Level** was swapped directly as the EASTL string field: field
  pointer from `0x4f4c40`, string ctor `0x4014c0`. It read back as
  `Levels/SP_02`.
- **Correction to the DLL route below:** the text setter `0x4f4bd0` leaves
  EASTL string fields unchanged. "Set Client.Level with `0x4f4bd0`" alone does
  not work; write the string field instead.
- **Trigger:** a hook on UIManager::update `0x959900` (vtable entry
  `0x1440860`) called `0x7600f0(1)` on the UI thread. It did this after 60
  menu frames, while `[uimgr+0x82b24]` was non-null.
- **ecx:** the live call did not pass FE in ecx and still worked, because
  newSP reads `[0x19a4ca8]` itself. Passing FE remains the safe form.
- **Not live-tested:** the `0x77bc10(FE, "startSingleplayer", "SP_02")`
  fallback.

## 0. Short answer

* **No `.cfg` loader exists in the client.** No string containing `cfg` (any case) is
  present in .rdata/.data (SV, full string scan). WinMain and the `-name value`
  lookup only read a fixed set of names (list in section 1). Retail never applies
  dotted `-Group.Field value` arguments (SV: the only callers of the settings
  string setter are three hardcoded `Network.Version` writes in 0x5657b0).
* **SP level start = the UI request queue.** `newSP` (UI cmd 0x84 = 132) calls
  `0x7600f0`. That reads `Client.Level`, keeps only the basename, and queues a
  type-3 UI request. The UI turns it into message (category 0x28, type 0x10)
  carrying `"Levels/" + name`. The Client stores the name and switches to state 5
  (StateStartServer). That calls Main::startServer, then ServerMain, then
  `Server::setPendingLevel`. Each step is in sections 2 and 3.
* **Best DLL route (INF, needs a PC test):** write `Client.Level` through the native
  settings setter, then on the game thread call either `0x7600f0(frontend, 0)` or
  the frontend command handler `0x77bc10(frontend, "startSingleplayer", "SP_02")`.
  Both reuse the stock loading-screen path. Both need the front-end objects to
  exist (UI singleton `[0x19a4ca8]` and frontend `[UI+0x82b24]`) but do not need the
  menu to be visible. Calling deeper (posting the 0x28/0x10 message, or pushing
  Client state 5) skips the UI's own loading-state setup and is riskier.

---------------------------------------------------------------------------
## 1. Configuration sources (item 1)

### 1a. Command line, SV
* WinMain loop at 0x401f00 compares argv against `-runMultipleGameInstances`
  (0x1409838), `-dedicated` (0x140982c) and `-server` (0x1409824) using 0x508540.
  It then creates the entry object with `0xbdb140(argc, argv, 0x4e26f0, 0x4e2700)`.
  The real main is `0x5703e0`, reached through 0x401df0.
* `0x501d30` copies argv into a global `vector<String>` at **0x155d008**
  (begin/end/cap at 0x155d008/0x155d00c/0x155d010). It appends `-noAsync` in some
  cases and `<no argv[0]>` when argv is empty.
* `0x4f4cf0(const char* name, const char* default, int* iter)` (cdecl) looks up an
  option. It scans the vector for `-name` or `+name`, compared with 0x508160, and
  returns the next argv entry or `default`. Signature:
  `83 EC 24 53 8B 5C 24 34 55 56 33 ED 33 F6 3B DD`.
  All 41 callers pass fixed names. The game-relevant ones are `displayAsserts`,
  `displayErrors`, `crashDumpAsserts`, `crashDumpErrors`, `host`, `secure`,
  `remoteFileServer`, `hostUser`, `sessionId`, `dataPath`, `noCache`, `dedicated`,
  `loginToken`, `networkProtocolId`, `platform`, `heartBeatInterval`,
  `stopOnFocus`, **`requestState`**, **`requestStateParams`**, `nosound`,
  `soundsystem`, `speakerSetup`, `port`, `pingInterval`, `revisionLevel`/`Key`,
  `name`, `listen`, `maxQueriesPerSecond`, `queryPortOffset` and
  `extra_account_detail`.
  None of these is a level or a dotted setting.
* `-requestState X` (read at 0x5b4eea, parsed by 0x5b4b50) accepts only
  `State_MenuReady`, `State_Matchmaking`, `State_ConnectToGameId` and
  `State_ConnectToUserId` (SV). These are online menu states, not a level route.

### 1b. Settings registry, SV
* SettingsManager: allocated with 0x80 bytes, constructor `0x4fed50`, registered in
  0x5023df. The global **`[0x155cfdc]` = SettingsManager+4**. Every API below is a
  thiscall thunk on that pointer: `test ecx,ecx; add ecx,-4; jmp impl`.
* Groups are registered in `0x566050`, called from the Main constructor 0x56ede0 at
  0x56ef03, before the Client and UI exist. The groups are Animation, **Game**
  (GameSettings, constructor 0x13880a0, 0xa4 bytes), Network, Demo, Physics,
  Monitoring, Soot, Overgrowth, Decal, StaticCull, **Client** (constructor
  0x1387b70, 0xfc), **Server** (constructor 0x1387890, 0xcc; registered twice,
  with a re-create at 0x566ae3), IrradianceVolume, Occlusion, WorldRender and
  EmitterSystem.
  The pattern is `alloc(0x4e4eb0) → ctor → TypeInfo::0x4fe130(obj+4) →
  0x4fdb00(&String name, obj, 1, 0)`.
  The instances are heap objects. There are no static singletons. The "arg3
  stubs" 0x534600/0x534680/0x534700 are TypeInfo create-instance factories, not
  getters (SV).
* API (ecx = `[0x155cfdc]`):

| VA | Use | Evidence |
| --- | --- | --- |
| `0x4f4c80(String* group)` → settings object* | e.g. "Game", then reads `+0x94` UseBundles | SV 0x56f02c |
| `0x4f4c40(const char* "Group.Field", TypeInfo* t)` → `void*` to the field inside the object | `Server.IsReconfigurable` with bool TI 0x1bf0354, then `movzx [eax]` | SV 0x756ee1 |
| `0x4f9fd0(String* out, String* "Group.Field")` → out | used for `Server.Level` and `Client.Level` | SV 0x757024, 0x7601a0 |
| **`0x4f4bd0(String* "Group.Field", String* value)` → bool** | text setter (implementation 0x4f3fa0): splits on `.`, finds the group, calls TypeInfo vt+0x20 (set field from string). If the group is not registered yet, the value is kept in a pending map at +0x28 | SV call 0x5658e2 with `Network.Version` = `"ROMEPC795745"`; pending map INF |

  TypeInfos: Boolean 0x1bf0354, Int32 0x1bf03b4, Uint32 0x1bf03a4, Float32
  0x1bf03e4, String 0x1bf0404 (bc2_types.json).
  Signatures: `0x4f4bd0`: `85 C9 74 17 8B 44 24 08 8B 54 24 04 6A 00 50 83`.
  `0x4f9fd0`: `85 C9 74 19 8B 44 24 08 56 8B 74 24 08 50 83 C1`.
  `0x4f4c80` is the thunk followed by `8B 01` 0x20 bytes later. `0x4f4c40` is the
  identical thunk 0x40 bytes before `0x4f4c80`.
* The PC finding that the text loader 0x5017c0 and add-override 0x501ab0 have no
  callers is consistent with this: nothing in retail feeds the registry from a file
  or the command line.
* `/data/local/GameSettings.ini`, `GameSettings.bin` and `Settings/GameSettings`
  (0x831ed0, 0x94bf65) belong to the player-profile options (audio, graphics,
  input). They are not the engine GameSettings (INF).
* The `/settings` and `/settings/*` web pages (0x501bf0) are registered on a dev
  web-server interface. I found no listener for it in retail (INF, not pursued).

### 1c. Settings read by name (strings pushed to 0x4f4c40 or 0x4f9fd0, SV)
`Client.Level` (only at 0x76016b, inside `0x7600f0`), `Server.Level` (only at
0x756feb, inside the ServerMain constructor), `Client.LoadMenu` (0x964fbb),
`Client.LimitedMainMenu` (0x95c874), `Server.IsAiEnabled` (5 sites),
`Server.DisableCheckpoints` (0x84cb0b), `Demo.PauseOnStartup`.
The Client and Server group-name strings are referenced only by registration.
There is **no** name string for `Client.DisableMovies`, `Client.IsFastStartupEnabled`,
`Client.DisableMenuFlow`, `Server.DisableCutscenes`, `Server.SavePoint` or
`Server.LoadSavePoint`.
**INF:** those fields are probably dead on PC, or are read only through a cached
object pointer. Do not count on them.

### 1d. Defaults from the constructors (SV for the stores; values partly INF)
* ClientSettings (0x1387b70): `Level`@0x5c = **"Levels/SP_01"**, `LoadMenu`@0xfa
  = al (1, INF), `IsMenuDevOptsEnabled`@0xf4 = al (1?, INF),
  `LimitedMainMenu`@0xba = 0, `PauseGameOnStartUp`@0xd2 = 0,
  `IsFastStartupEnabled`@0xf0 = 0, `DisableMovies`@0xf1 = 0,
  `DisableHudMovies`@0xf2 = 0, `DisableMenuFlow`@0xf9 = 0.
* ServerSettings (0x1387890): `Level`@0x7c = "" (empty), `SavePoint`@0x70 = "1st",
  `DisableCheckpoints`@0x8e = 1 (as stored), `DisableCutscenes`@0xb5 = 0,
  `LoadSavePoint`@0xb8 = 0.

---------------------------------------------------------------------------
## 2. The SP start chain (item 2), top to bottom

Objects:
* **UI singleton** `UI = [0x19a4ca8]`, set by the base constructor at 0xfa0f23
  (SV). It is a large object. Fields used here: +0x82b24 frontend*, +0x82c18
  command-name map, +0x82cc8 intro movie index, +0x82ccc intro-active flag,
  +0x82cd0 request vector (0x110-byte elements), +0x82ee0 profile flags,
  +0xc3ec0 UI state machine.
* **Frontend** `FE = [UI+0x82b24]`. It is allocated with 0x1d50 bytes, constructed
  by `0x783560` and stored at 0x951d8f. Its primary vtable is 0x142b4e8 (SV).
* **ClientGameContext** at `0x15713d8`, returned by `0x7ff060()`. Its +0x18 is the
  message manager: vt+0x08 registers a listener (cat, obj); vt+0x24 queues
  (cat, msg, float delay); vt+0x20 sends (cat, type, 0, delay). Its +0x34 is the
  online/session object; when that is non-null, `[+0x34]+0x54 != 0` blocks a
  start (SV).
* **Client**: 0x118 bytes, constructor `0x561c10`, vtable 0x14183d4, stored at
  Main+0xb4. Current state is at +0xac, the state queue (deque) at +0x80, the
  level String at +0xb8, `+0xc8` int, `+0xd0` mode (0 = SP), `+0xd4` byte, and
  `+0xdc` the Main+0x90 starter interface. Main has no global pointer; the Client
  is reachable only through Main or a hook (INF).

### 2a. `0x7600f0` FE::startLoadingSP(int arg) — `newSP`, SV
ABI: thiscall, ecx = FE (the dispatcher passes `edi` = FE), one stack argument
(`atoi(param)`), `ret 4`. Called only from the command dispatcher (0x77c15d, case
0x84).
```
void FE::startLoadingSP(int arg) {
  UI->callAS("onStartLoadingSP");                 // 0x929a70, ecx=[0x19a4ca8]
  UiRequest r;  r.type = 3;                        // 0x110-byte local at esp+0x30; sub-object ctor 0x5ecf60
  String lvl = Settings->getString("Client.Level");   // 0x4f9fd0 @0x7601a0
  r.level = basename(lvl);                         // 0x513120: text after the last '/' or '\'
  r.int10 = arg;                                   // r+0x10
  UI->queueRequest(&r);                            // 0x956e10 @0x760231
}
```
Signature: `81 EC 34 01 00 00 8B 0D ?? ?? ?? ?? 53 56 57 68 ?? ?? ?? ?? 8B F9 E8`.
Request layout: +0 type, +4 String level, +0x10 int, +0x18 sub-object (vtable
0x141120c), +0x108 and +0x10c pointer/size of a blob. Constructor for a free-standing
request is `0x75df60`, destructor `0x75eab0`.
`startSingleplayer` (cmd 7, 0x77bc93) does the same with
`param = "<name>[,<int>]"` split on `,`. The name is used as-is, so pass a bare
name such as `SP_02`; the queue step adds "Levels/".

### 2b. `0x956e10` UI::queueRequest(const UiRequest*), SV
thiscall, ecx = UI, `ret 4`. It pushes the request into the vector at
UI+0x82cd0. Signature: `8D 81 D0 2C 08 00 8B 48 04 3B 48 08 73 12 85 C9`.

### 2c. `0x951e00` UI::processRequest() — runs from the UI update (caller 0x959ee0), SV
It pops the last request and switches on `type` (0..0xb, jump table 0x952540).
* type 3 (SP) at 0x951f6b: sets the profile flag `IntroPlayed`, sets UI+0xc3efc
  = 1, and returns early if the online object is busy. It allocates a 0x40-byte
  message (constructor `0x918ca0`, vtable 0x143bee8, type id **0x10**), sets
  `msg+0x28 = "Levels/" + r.level` (0x45be60) and `msg+0x34 = r.int10`, then
  `ctx->msgMgr->vt[0x24](0x28, msg, 0.0f)`.
* types 0/1: message 0x918d70 (type 0x13), level used as-is, `+0x34 = (type==1)`.
* type 9: plain `onStartLoading`, the MP/host path.

Signature: `81 EC EC 01 00 00 53 55 56 8B F1 8B 8E D4 2C 08`.

### 2d. `0x56a890` Client message category 0x28 handler, SV
Reached from Client vt[1] `0x56ec10(cat, type, msg, …)`. For cat 0x28 it calls
`0x56a890(type, msg, …)`.
* type 0x10: `client+0xb8 = msg+0x28` (level), `+0xd4 = 1`, `+0xd0 = 0` (SP),
  `+0xc8 = msg+0x34`, copies the msg+0x38/+0x3c blob to client+0x54, then
  `0x557150(client, 5)`, which queues StateStartServer.
* type 0x13: the same with `+0xd0 = 1`.

Signature: `8B 44 24 04 83 C0 F0 83 F8 07 56 8B F1`.

### 2e. Client state machine, SV (state names from the table at 0x1504fc8)
`0 Startup, 1 FrontEnd, 2 FastStartup, 3 Ingame, 4 Loading, 5 StartServer,
6 WaitingForLink, 7 WaitingForLoad, 8 WaitingForLoaded, 9 Linked,
10 ConnectToServer, 11 Void, 12 WaitingUIFrontend, 13 WaitingUILoading,
14 WaitingUIIngame, 15 WaitingUIUnload.`
* `0x557150(state)` queues a state. Signature:
  `8B C1 8B 90 88 00 00 00 3B 90 98 00 00 00 8D 88 80 00 00 00 53 8B 5C 24`.
* `0x56a6d0(state)` is the per-state update. It returns the next state.
  **State 5 calls `0x565450` and then returns 7 (WaitingForLoad).**
  Signature: `55 8B 6C 24 08 56 8D 45 FD 83 F8 0B 57 8B F1`.
* The Client starts in state 12 (set in the init at 0x5623bc). Entering state 12
  sends (0x2e, 0x10) to the UI. The UI (0x964faf) reads `Client.LoadMenu`:
  * true: the UI state machine goes to 0, the main menu.
  * false: the UI sends (0x2e, 0x15), and the Client goes to state 1 (FrontEnd)
    with no menu UI.

  **INF:** nothing then starts a level automatically. Client.Level is read only in
  `0x7600f0`. `LoadMenu=false` therefore skips the menu screen, but a start call is
  still needed.

### 2f. `0x565450` Client::startLocalServer(), SV
thiscall, ecx = Client. It builds `ServerStartParams` on the stack:
```
+0x00 String level   = client+0xb8
+0x0c int            = client+0xc8   (becomes Server+0x90 via 0x83c400; start index, INF)
+0x10 bool isSP      = client+0xd0==0
+0x11 bool           = client+0xd4
+0x12 bool dedicated = 0
+0x14 ptr, +0x18 ptr (persistence: "Persistence/SP_Template"), +0x1c = client+0x54 blob
```
It then calls `client->starter(+0xdc)->vt0` and connects to
`localhost:<Network.ServerPort>`. The local server is started first.
Signature: `B8 E4 41 01 00 E8 ?? ?? ?? ?? 53 55 56 57 8B E9 E8 ?? ?? ?? ?? 0F B6 85 D5 00 00 00 0F B6 8D D4 00 00 00`.

### 2g. `0x54b0b0` Main::startServer(ServerStartParams*), SV
This is vtable 0x1418140[0], at the Main+0x90 interface. It allocates 0x30 bytes
and calls `ServerMain(0x756d40)(params, …)`.
Signature: `53 55 56 33 DB 8B F1 88 1D ?? ?? ?? ?? 88 9E 90`.

### 2h. `0x756d40` ServerMain ctor, SV
It creates the Server object (`[0x15704a4]`, through 0x853b00). Then:
```
if (params->level.empty()) {
    String q = server->takeQueuedLevel();          // 0x858830 (Server+0x80)
    if (q.empty()) q = Settings->getString("Server.Level");   // 0x4f9fd0 @0x757024
    server->setPendingLevel(q, 0);                 // 0x858900
} else server->setPendingLevel(params->level, 0);
server->setStartIndex(params->int0c);              // 0x83c400 → Server+0x90
```
Signature: `81 EC 90 00 00 00 53 55 56 57 33 DB 53 8B F9 68 ?? ?? ?? ?? 89 7C 24 24`.
So **`Server.Level` is used only when the client passes an empty level.**

### 2i. `0x858900` Server::setPendingLevel(const String&, bool), SV
thiscall on `[0x15704a4]`, `ret 8`. It assigns Server+0x74 and stores the flag at
Server+0x8c. The server update then loads the level. That loader below was not
traced (INF).
Signature: `8B 44 24 04 56 8B F1 8D 4E 74 3B C1 74 1A 8B 50`.

---------------------------------------------------------------------------
## 3. What a DLL can call, ranked (item 2 conclusion)

All of these must run on the game/main thread, for example from a per-frame hook.
The UI request processor runs from the UI update, so a call from another thread
races the request vector (INF).

1. **Set the level, then let the native command run (recommended, INF).**
   * `0x4f4bd0([0x155cfdc], &String("Client.Level"), &String("Levels/SP_02"))`.
     Alternatively take `p = 0x4f4c40("Client.Level", 0x1bf0404)` and call
     `0x4245b0(p, b, e)`. Restore the original string afterwards.
   * Wait until `[0x19a4ca8] != 0` and `FE = [UI+0x82b24] != 0`, and the Client
     has reached the front end. Then call `0x7600f0(FE, 0)` (thiscall).
     Equivalent: `0x77bc10(FE, "startSingleplayer", "SP_02")`, which needs no
     settings write. That handler is FE vtable 0x142b4e8 index 12; signature
     `81 EC 08 0A 00 00 8B 84 24 0C 0A 00 00 53 55 56`.
   * This runs the stock path: loading screen, IntroPlayed, message, server.
2. **Post the message (0x28, 0x10) directly (INF).** Build it with `0x918ca0` on
   game-heap memory, set +0x28 to `"Levels/SP_02"` and +0x34 to 0, then call
   `0x7ff060()->[+0x18]->vt[0x24](0x28, msg, 0.0f)`. This skips the UI state
   changes done in 2c, so the UI may not show the loading screen.
3. **Push Client state 5 with an empty level and `Server.Level` set (INF).** This
   needs the Client pointer (Main+0xb4; no global) and leaves the UI state
   unsynchronised. Not recommended.

`Client.LoadMenu = false` (set before the Client reaches state 12, so right after
settings registration) hides the main menu. Combine it with option 1 and test
whether `0x7600f0` still works when the UI was never in the menu state (INF; the
type-3 request handler itself has no menu precondition, SV).

---------------------------------------------------------------------------
## 4. Console, debug and command systems (item 3)
* No developer console. `ConsoleCommandEntityData` exists only as level data.
  There are no `Cheat*` strings.
* The UI uses a Scaleform fscommand map of 226 names to ids, in a table at
  0x143c898 of `{char*, id}` pairs. The full list is not committed; dump it from the table with the helper. The map
  lives at UI+0x82c18 and the lookup is `0x929c40`. Dispatchers:
  * frontend `0x77bc10` (jump table 0x77d8b0, index table 0x77d964)
  * 0x776528 and 0x753579 / 0x95e680 for in-game commands
  
  Level-relevant commands:

| Command | Id | Note |
| --- | --- | --- |
| `newSP` | 0x84 | calls 0x7600f0 |
| `startSingleplayer` | 0x7 | |
| `continueSP` | 0x83 | calls 0x75ffa0 |
| `startSavedSingleplayer` | 0x11 | |
| `skipMovie` | 0xe | |
| `skipCutscene` | 0x9a | |
| `legalDone` | 0x61 | sets UI+0x82ccc=1 and starts the intro movies |
| `doRestartLevel` | 0x13 | |
| `doLastSavePoint` | 0x14 | |
| `doContinueMission` | 0x15 | |
| `activateMission` | 0x4c | |
| `debugdata` | 0xd2 | |
| `setSetting` | | profile options |
| `svSetValue` | | |

  The handlers for `skipCutscene`, `doRestartLevel` and `activateMission` live in
  the in-game dispatcher 0x776528 (jump table 0x777234) and were not traced.

---------------------------------------------------------------------------
## 5. Intro, movie and cutscene skip (item 4)
* Intro movies: `0x760270` FE::updateIntro (signature
  `51 53 55 8B 2D ?? ?? ?? ?? 56 8B F1 8D 9E 5C 01 00 00 57 8B CB E8`) plays
  FE+0x114 (vector<String>), indexed by UI+0x82cc8. It runs only while
  `UI+0x82ccc` is set; `legalDone` sets it. At the end it calls `exitIntroState`
  and sets the profile flag `SplashMoviesPlayed`.
  **Skip candidates (INF):** keep UI+0x82ccc at 0, or set UI+0x82cc8 to the
  vector size so the next update finishes the intro. Option B in the docs
  (renaming the movie files) still applies.
* `skipMovie` (0x77bfcf) stops the current movie (0x7438b0 / 0x743a30) once
  `SplashMoviesPlayed` is set or the FE+0x124 list is non-empty (SV).
* `ClientSettings.DisableMovies`@0xf1, `ServerSettings.DisableCutscenes`@0xb5 and
  `Client.IsFastStartupEnabled`@0xf0 exist (dump), but no code reads them by name.
  Treat them as unverified. Client state 2 (StateFastStartup) is never queued by
  any direct `0x557150` call I found (INF).
* In-level cutscene skip: the `skipCutscene` UI command, plus the per-cutscene
  `CutsceneEntityData.IsSkippable` (data). Not traced.

---------------------------------------------------------------------------
## 6. Field offsets (item 5, from bc2_types.json, matching the stores in the constructors)
* ServerSettings (0xcc): `SavePoint` S@0x70, `Level` S@0x7c,
  `DisableCheckpoints`@0x8e, `InstantSpawn`@0x93, `IsGodMode`@0x95,
  `IsFastStartupEnabled`@0x9f, `DisableCutscenes`@0xb5, `LoadSavePoint`@0xb8,
  `UnrestrictedUnlocks`@0xbe, `HasUnlimitedAmmo`@0xc0, `IsAiEnabled`@0xc5,
  `HasUnlimitedMags`@0xc9.
* ClientSettings (0xfc): `Kit`@0x18, `Team`@0x1c, `SpawnPoint` S@0x50,
  `Level` S@0x5c, `LimitedMainMenu`@0xba, `PauseGameOnStartUp`@0xd2,
  `IsMultiplayer`@0xd5, `BypassMatchmaking`@0xea, `IsFastStartupEnabled`@0xf0,
  `DisableMovies`@0xf1, `DisableHudMovies`@0xf2, `IsMenuDevOptsEnabled`@0xf4,
  `DisableMenuFlow`@0xf9, `LoadMenu`@0xfa.
* GameSettings (0xa4): `BundleMountPoints`@0x20, `UseCustomKit`@0x91,
  `UseBundles`@0x94 (read at 0x56f05c, SV), `FastStartupEnable`@0x98.

---------------------------------------------------------------------------
## 7. Gaps
* Nothing has been run. The server level-load body after Server+0x74, the timing
  of the UI request when `LoadMenu=false`, and thread affinity all need a PC check.
* I did not trace the in-game command handlers (`skipCutscene`, `doRestartLevel`),
  what reads DisableMovies or DisableCutscenes through a cached pointer, or the
  meaning of Server+0x90 (the start index).
