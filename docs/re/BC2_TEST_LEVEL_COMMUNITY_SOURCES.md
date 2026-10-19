> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (PC) web research for the VR test harness: start an empty level fast and give any weapon

Date: 2026-10-08. Every claim below cites its source. Labels used: **[confirmed]** means a primary or official source, or a file I read directly. **[community]** means a forum, guide or third-party tool says so. **[speculation]** means my own inference.

---

## 0. Priority answer: how BC2 (Frostbite 1.x) receives engine settings and arguments

The coordinator reported that the client ignores `-Server.Level/-Client.Level/-Client.LoadMenu`. Here is the evidence found on how arguments actually reach the engine.

1. **`Win32Game.cfg` is an argument file that the engine appends to the command line.** [confirmed: server build]
   - The BC2 R-build dedicated server root contains a file `Win32Game.cfg`. In one published server tree, the entire file is this single line: `-server -dedicated -feslhost bfbc2-pc-server.fesl.ea.com:19021 -theaterhost bfbc2-pc-server.theater.ea.com:19026`. Source: https://github.com/juniodbl/bfbc2-server/blob/master/bin/Win32Game.cfg (raw: https://raw.githubusercontent.com/juniodbl/bfbc2-server/master/bin/Win32Game.cfg).
   - The server's own RuntimeLog in the same repo prints the *merged* command line: the `.bat` arguments come first, then the `Win32Game.cfg` contents appended. The line is `Engine/Game/Main/Main.cpp(635): "Main": Info: Commandline arguments:  Frost.Game.Main_Win32_Final.exe -serverInstancePath "..\instances\bc2 " -mapPack2Enabled 0 -timeStampLogNames -region EU -heartBeatInterval 20000 -displayErrors 0 -displayAsserts 0 -plasmalog 1 -server -dedicated -feslhost ... -theaterhost ...`. The same log shows `Build ID: ROMEPC851434`. Sources: https://github.com/juniodbl/bfbc2-server/blob/master/bin/data/local/RuntimeLog_EC2AMAZ-4PM5QI1_server_2018.02.18_01.00.19.139.log and the batch file https://github.com/juniodbl/bfbc2-server/blob/master/start_server.bat (the `.bat` itself contains neither `-server` nor `-dedicated`).
   - The MASE emulator readme confirms the same mechanism independently: `-feslhost`/`-theaterhost` "could be included in some .bat file ... or in the Win32Game.cfg of the server directory". Source: https://github.com/L2KSolkov/mase_bc2/blob/master/readme.txt
   - A later-Frostbite comparison shows the same convention, there in a `Scripts/` subfolder. BF4's `Scripts/Win32Game.cfg` holds `-super layout.toc`, `-server`, `-dedicated`, `-Core.DisplayAsserts false`, one per line. Source: https://github.com/j1m1l0k0/BlazeEmulatorBF4/blob/master/DLL/Scripts/Win32Game.cfg
   - The BC2 server package also has a `Scripts/` folder in its root, next to `Win32Game.cfg`, `database.dbmanifest` and `ProviderID.dat`. Source: https://github.com/derkalle4/docker-pelican/blob/main/bfbc2/README.md
   - **For the client it is unverified** whether `BFBC2Game.exe` reads a `Win32Game.cfg` (in its root, or in `Scripts/`). [speculation] It is cheap to check with a strings search of the client binary for `Game.cfg`, `Commandline arguments` and `RuntimeLog`, or by dropping the file next to the exe. That route would also show how the client logs the merged command line.

2. **Argument syntax seen on BC2 executables.**
   - **Bare camelCase names** (no namespace) on the server, all from published launch lines:
     - `-serverInstancePath`, `-mapPack2Enabled`, `-port`, `-remoteAdminPort`, `-numGameClientSlots`, `-timeStampLogNames`, `-region`
     - `-heartBeatInterval`, `-displayErrors`, `-displayAsserts`, `-crashDumpAsserts`, `-crashDumpErrors`
     - `-runtimeLog`, `-serverAdminLogs`, `-plasmaServerLog`, `-plasmalog`, `-revisionLevel`, `-revisionKey`
     - `-feslhost`, `-theaterhost`, `-server`, `-dedicated`
     - Sources: https://github.com/OpenGamePanel/OGP-Website/blob/master/modules/config_games/server_configs/bfbc2.xml, https://github.com/jkuettner/bfbc2-server (game-server/bin/run.sh), https://github.com/russianvivi/bc2rome (bc2rome.sh), https://github.com/juniodbl/bfbc2-server [confirmed as used; not every flag is confirmed to have an effect]
   - **Dotted `Group.Field` names** on the BC2 server: one panel egg launches with `-Server.ThreadingEnable true -Core.JobProcessorCount 2`. Source: https://github.com/derkalle4/docker-pelican/blob/main/bfbc2/README.md. [community; nobody verifies that these take effect]
   - [speculation] FB1 may therefore accept both forms: a dotted `Group.Field value` for settings-registry objects, and bare names for a few special-cased Main/Plasma options. `-runtimeLog 1` is a good first argument to try on the client, because it would let you see what the client parses. `-runtimeLog` appears in the OGP template above as `-runtimeLog 0`.
   - Field naming in a related engine: Venice Unleashed (BF3/FB2) documents ServerSettings fields such as `isAiEnabled` (not `IsAIEnabled`), `disableCutscenes`, `loadSavePoint`, `savePoint`, `isDesertingAllowed`, `respawnOnDeathPosition`, `unlockResolver` and `instancePath`. Source: https://docs.veniceunleashed.net/vext/ref/fb/serversettings/. BC2's FB1 names may differ. **I found no public list of BC2 client settings names** such as Client.Level or Game.Level.

3. **The settings.ini in Documents\BFBC2 only holds front-end options.** A posted file has these sections and keys:
   - `[Graphics]`: MainQuality, Effects, Soldiers, Vehicles, Overgrowth, Undergrowth, StaticObjects, Terrain, Shadows, Bloom, HSAO, MSAA, Water, Texture, DxVersion, Aniso, Detail, RenderAheadLimit, Fov
   - `[WindowSettings]`: Width, Height, Fullscreen, RefreshRate, VSync
   - `[Sound]`: Quality, VoipEnable, SpeakerCount
   - Sources: https://gamefaqs.gamespot.com/boards/957944-battlefield-bad-company-2/53843140 and https://www.pcgamingwiki.com/wiki/Battlefield:_Bad_Company_2. [community]
   - No source suggests engine settings (level, AI and so on) can be put in settings.ini. [not found]

4. **Developer console: none shipped.** The BC2 PC dev console was never planned: Johan Andersson tweeted "No Console". Source: https://n4g.com/news/458130/no-dev-console-available-in-battlefield-bad-company-2 [community/press]
   - However, the final server build still logs `Engine/Game/Debug/DebugMenu.cpp(2373): Recreating levels debug menu`, so debug-menu code is compiled into Final binaries. Source: the RuntimeLog cited above. [confirmed for the server; client unknown]

5. **`initfs_Win32` contents for BC2: nothing public found.** Searches for BC2 initfs contents or a BC2 console-unlock initfs mod returned only SWBF2/DAI results. [not found]

---

## 1. Client command line, settings files and skipping intros

- **Client command-line arguments:** none documented publicly. The only community-reported argument is `+fullproc`, and its effect is not verified. Source: https://forums.evga.com/BFBC2-Some-New-Tweaks-Some-Not-So-NEW-Reloaded-m488637.aspx [community]
  - No public evidence was found for `-level`, `-skipintro`, `-console`, `-noborder` or `-window*`. [not found]
- **Settings and save files:**
  - `%USERPROFILE%\Documents\BFBC2\settings.ini` holds the options.
  - Save and progress data is `%USERPROFILE%\Documents\BFBC2\GameSettings.bin`. It is binary; there is no GameSettings.ini.
  - Setting `DxVersion=9|10|11|auto` and `Fov=55` works, but the file is reverted by the menus unless you make it read-only.
  - Source: https://www.pcgamingwiki.com/wiki/Battlefield:_Bad_Company_2 [community wiki]
- **Skipping intros:**
  1. Delete or rename `<game>\Output\win32\movies\ea_logo_hd.res` and `dolbydigital.res`.
  2. Press ESC on the black screen.
  - Source: https://www.pcgamingwiki.com/wiki/Battlefield:_Bad_Company_2 [community wiki]
  - Mission cutscenes: no file-level method was found. The MOD.SP readme says "pressing SPACE skips" scenes that go black. Source: https://www.nexusmods.com/battlefieldbadcompany2/mods/51 [community]
- **Frame cap:** the game is capped at 200 FPS, and animation scripts break at high frame rates. Source: https://www.pcgamingwiki.com/wiki/Battlefield:_Bad_Company_2
- **Launchers:** Gossamer's launcher only edits settings.ini and then launches the game; it has no level selection. Source: https://www.moddb.com/mods/gossamers-launcher [community]

## 2. Levels and game modes

### Authoritative bundle list

This comes from the vanilla client `package.mft` and server `bundleManifest` shipped inside the BFBC2 Mod Loader repo, which I read directly. Sources: https://github.com/AnirohDev/FBOneTools/blob/master/BFBC2ModLoader/BFBC2ModLoader/Loader/client/original/package.mft and .../server/original/bundleManifest [confirmed]

- **Single-player levels (13 bundles):**
  - `levels/sp_01`, `sp_02`, `sp_03`, `sp_04`
  - `levels/sp_003_b`, `sp_004_b`
  - `levels/sp_005`, `sp_005_b`, `sp_006`, `sp_007`, `sp_008`, `sp_008_b`, `sp_009`
  - Shared bundle: `levels/sp_common`. It is referenced in MOD.SP's install steps: https://www.nexusmods.com/battlefieldbadcompany2/mods/51
  - Each level has `level-00.fbrb`, `loader-00.fbrb`, `ingame-00.fbrb`, `terrain-00.fbrb` and `vo/<lang>-00.fbrb`.
  - Frankelstner's dbx script mentions a file `Levels\SP_008_B.dbx`, which confirms the case and format of the name. Source: https://github.com/Powback/Bad-Company-2-Map-Editor/blob/master/Extractor/Python%20Extractor/dbx.py
  - **The mapping from SP bundle to mission name was not found.** [speculation] `mp_sp_002*` (Cold War) and `mp_sp_005*` (Heavy Metal) look like they are derived from SP levels 002 and 005.
- **Multiplayer levels (exact names from the server log `Map available:` lines and the manifest):** [confirmed]
  - Base maps:
    - `Levels/MP_001` (Panama Canal CQ), `MP_001SDM`, `MP_001SR`
    - `MP_002` (Valparaiso), `MP_002SR`
    - `MP_003` (Laguna Alta), `MP_003SR`
    - `MP_004` (Isla Inocentes), `MP_004SDM`
    - `MP_005` (Atacama CQ), `MP_005GR`, `MP_005SR`
    - `MP_006` (Arica Rush), `MP_006CQ`, `MP_006SDM`
    - `MP_007` (White Pass CQ), `MP_007GR`, `MP_007SDM`
    - `MP_008` (Nelson Bay Rush), `MP_008CQ`, `MP_008SDM`
    - `MP_009CQ`/`GR`/`SDM`/`SR` (Laguna Presa)
    - `MP_012CQ`/`GR`/`SR` (Port Valdez)
  - SP-derived maps: `MP_SP_002GR`/`SDM`/`SR` (Cold War), `MP_SP_005CQ`/`SDM` (Heavy Metal)
  - BC1 remakes: `BC1_Harvest_Day_CQ`/`GR`/`SDM`/`SR`, `BC1_Oasis_CQ`/`GR`/`SDM`/`SR`
  - Vietnam: `nam_mp_002|003|005|006|007` with suffixes `cq`, `r`, `sdm`, `sr`
  - Shared bundles: `mp_common`, `mp1_common`, `mp2_common`, `nam_common`
  - Sources:
    - https://github.com/juniodbl/bfbc2-server/blob/master/bin/data/local/RuntimeLog_EC2AMAZ-4PM5QI1_server_2018.02.18_01.00.19.139.log
    - https://strategywiki.org/wiki/Battlefield:_Bad_Company_2/Dedicated_servers, which gives the mode-to-path table
    - https://github.com/davidbinneun/PRoCon/blob/master/Configs/BFBC2.def
- **Game modes / playlists:** `RUSH`, `CONQUEST`, `SQDM`, `SQRUSH`. Source: the official R32 protocol PDF, `admin.setPlaylist`: "Should be RUSH, CONQUEST, SQDM or SQRUSH". https://github.com/EBassie/Procon-2-Protocols-Frostbite/blob/master/doc/Protocols/Frostbite/BC2/R32/BFBC2%20PC%20Remote%20Administration%20Protocol.pdf [confirmed]
  - PC has no Onslaught. Source: https://sourceforge.net/projects/battlefieldbadcompany2mase/ [community]
- **Dev, test or empty levels:** none in the vanilla manifests. I searched both files for test, debug, sandbox, range and dev. [confirmed absent from the manifests]
  - The Mod Loader's *modded* manifest adds custom `levels/air_mp_004`, `air_mp_006` and `air_mp_sp_001cq` bundles. This proves new level bundles can be registered through `package.mft`. Source: https://github.com/AnirohDev/FBOneTools/blob/master/BFBC2ModLoader/BFBC2ModLoader/Loader/client/modded/package.mft [confirmed]

## 3. The dedicated server, RCON and local/LAN hosting

### Server binary and configuration

- The executable is `Frost.Game.Main_Win32_Final.exe`; the latest is R34, build 851434 (`ROMEPC851434`). Its root contains `dist/`, `Instance/`, `Scripts/` and `Win32Game.cfg`. Sources: https://github.com/derkalle4/docker-pelican/blob/main/bfbc2/README.md and https://github.com/davidbinneun/PRoCon/blob/master/Configs/BFBC2.def (it maps R21..R34 to their build numbers).
- Typical launch line: `Frost.Game.Main_Win32_Final.exe -serverInstancePath "Instance/" -mapPack2Enabled 1 -port 19567 -timeStampLogNames -region OC -heartBeatInterval 20000`. Source: https://github.com/alokal/bc2-emu (README) and the jkuettner run.sh.
- The instance folder holds:
  - `ServerOptions.ini`: Name, Port, RemoteAdminPort, RemoteAdminPassword, PunkBuster, Ranked, NumGameClientSlots, RevisionLevel=8, RevisionKey, GameModID=BC2|VIETNAM
  - `AdminScripts/Startup.txt`: each line runs as an RCON command at boot
  - `maplist.txt`: the first line is the mode, then `levels/<name> [rounds]`
  - Sources: https://github.com/jkuettner/bfbc2-server (game-server/bin/run.sh, maplists/*.txt); the official R32 admin manual https://github.com/EBassie/Procon-2-Protocols-Frostbite/blob/master/doc/Protocols/Frostbite/BC2/R32/BFBC2%20PC%20Server%20Administration.pdf; https://strategywiki.org/wiki/Battlefield:_Bad_Company_2/Dedicated_servers
- The server only offers levels registered in its ServerMapList by revision level. The log shows `Map revision level up to (and including) 8 is enabled`, then the list of `Map available:`, all of them MP. Source: the RuntimeLog above.
  - [speculation] Loading an SP level on the dedicated server is therefore likely to be refused unless that list is changed.

### RCON (official R32 protocol)

Source for everything in this subsection: https://github.com/EBassie/Procon-2-Protocols-Frostbite/blob/master/doc/Protocols/Frostbite/BC2/R32/BFBC2%20PC%20Remote%20Administration%20Protocol.pdf [confirmed]

- **Commands:**
  - Session and server: `login.plainText/hashed`, `logout`, `quit`, `version`, `help`, `serverInfo`, `listPlayers`, `eventsEnabled`, `admin.runScript`
  - Messages: `admin.yell`, `admin.say`
  - Rounds and levels: `admin.runNextRound`, `admin.restartRound`, `admin.endRound`, `admin.runNextLevel`, `admin.restartMap`, `admin.currentLevel`, `admin.supportedMaps`
  - Playlists: `admin.setPlaylist/getPlaylist/getPlaylists`
  - Players: `admin.kickPlayer`, `admin.movePlayer`, `admin.killPlayer`
  - Lists: `mapList.load/save/list/clear/remove/append/insert/nextLevelIndex`, `banList.*`, `reservedSlots.*`, `textChatModerationList.*`
  - Level variables: `levelVars.set/get/evaluate/clear/list`
  - `punkBuster.pb_sv_command`
- **vars:**
  - Server identity and access: `serverName`, `adminPassword`, `gamePassword`, `punkBuster`, `hardCore`, `ranked` (read-only at runtime; set it in ServerOptions.ini), `serverDescription`, `bannerUrl`
  - Players and balance: `teamBalance`, `friendlyFire`, `currentPlayerLimit`, `maxPlayerLimit`, `playerLimit`, `rankLimit`, `idleTimeout`
  - HUD and cameras: `killCam`, `miniMap`, `crossHair`, `3dSpotting`, `miniMapSpotting`, `thirdPersonVehicleCameras`
  - Team kills: `teamKillCountForKick`, `teamKillValueForKick`, `teamKillValueIncrease`, `teamKillValueDecreasePerSecond`
  - Chat and filtering: `profanityFilter`, `textChatModerationMode`, `textChatSpam*`
- **levelVars:** `tickets`, `ticketBleedSpeed`, `vehicleSpawnRate`, `vehiclesDisabled` ("vehicles and stationary anti-vehicle weapons are removed"), `startDelay`, `respawnDelay` (minimum 10 s). Source: the R32 admin manual PDF above.
- **Events useful for logging:** `player.onSpawn <name> <kit> <weapons: 3 x string> <gadgets: 3 x string>`, `player.onKill <killer> <victim> <weapon>`, `server.onLoadingLevel`, `server.onLevelStarted`.
  - PRoCon's def lists weapon IDs, for example `AEK971`, `XM8`, `M16`, `SV98`, `M95`, `SPAS12` and `MP443`. Source: https://github.com/davidbinneun/PRoCon/blob/master/Configs/BFBC2.def
- **There is no RCON command to give a weapon or set a kit.** The loadout is chosen client-side in the deploy screen and is limited by unlocks. [confirmed by the absence of any such command in the official list]

### Local / LAN hosting with the retail client

- **MASE BC2 emulator** (Beta 0.9; download `Bc2emu_V09.rar` bundling Server R34):
  - Supports offline/LAN play, flat-file accounts, Windows and Linux. There are no bots.
  - The client needs the R11 patch, plus `dinput8.dll` and `bfbc2.ini` (`host=<ip>`, `connect_to_retail=0`) in the game root.
  - Source: https://sourceforge.net/projects/battlefieldbadcompany2mase/ and the readme mirror https://github.com/L2KSolkov/mase_bc2/blob/master/readme.txt [community, detailed]
- **Hook options in the server's bfbc2.ini `[server]` section:** `ranking_min_players`, `deserting_allowed`, `instant_spawn`, `unlimited_ammo`, `health_mode`. Sources: https://github.com/jkuettner/bfbc2-server (run.sh writes them) and the MASE changelog: "[ADD] ranking_min_players, unlimited_ammo and health_mode options to the hook". [community]
- **Unlocks:** the emulator's config.ini setting `all_stats_unlocked=true` unlocks every weapon for new personas. `all_are_veteran`, `vietnam_for_all` and `specact_for_all` cover the M1 Garand, Vietnam and SPECACT. The client also has a "Remember Password" and "Auto-Login" flow. Source: the MASE readme above. [community]
- **Docker/Wine recipes exist:** https://github.com/jkuettner/bfbc2-server (master plus rush and sqdm servers), https://github.com/russianvivi/bc2rome, https://github.com/Laura7089/bfbc2-docker. Python master server: https://github.com/Tratos/BFBC2_MasterServer
- **Project Rome (Venice Unleashed / Emulator Nexus):** an online backend reached through `dinput8.dll`, with a VU account. PCGamingWiki says LAN is "Planned for a future version". Source: https://www.pcgamingwiki.com/wiki/Battlefield:_Bad_Company_2. Rome servers support data mods through the Mod Loader. Source: https://steamcommunity.com/app/24960/discussions/0/1815422173030107911/
- **One client against a local server on an MP map** is technically supported by these emulators (instant_spawn, SQDM and so on). It still goes through login, server browser, deploy-screen kit selection and an MP map, so it is neither "empty" nor menu-free. [speculation, assembled from the sources above]

## 4. Data archives and modding

- **fbrb format** (Frankelstner's `archive.py`):
  - Big-endian. Magic `FbRB` plus the size of the gzipped metadata.
  - The metadata holds `00000002`, the size of a string section, the strings, a file count, and 24-byte entries: path offset, a not-deleted flag `00010000`, payload offset, size, alternate size, and a type offset.
  - Then a payload-gzipped flag byte and the uncompressed payload size.
  - The inner files are `.dbx` (binary XML), `.bin`, `.res` (typed resources) and `.dbmanifest`.
  - Source: https://github.com/Powback/Bad-Company-2-Map-Editor/blob/master/Extractor/Python%20Extractor/archive.py [confirmed, code comments]
  - Older partial struct notes: https://kiwidog.me/2012/08/battlefield-bad-company-frostbite-legacy-file-research/
- **dbx to XML converter:** `dbx.py` by Frankelstner, Python 2.7. Same repo: https://github.com/Powback/Bad-Company-2-Map-Editor/blob/master/Extractor/Python%20Extractor/dbx.py
- **Tools:**
  - BFBC2 Toolkit: edits fbrb and dbx, previews itexture and binkmemory. https://github.com/HeicoDev/BFBC2Toolkit (now AnirohDev/FBOneTools) and https://www.nexusmods.com/battlefieldbadcompany2/mods/15
  - BFBC2 Mod Loader: https://www.nexusmods.com/battlefieldbadcompany2/mods/4. It changes only "two tiny text files", namely `package.mft` (`BundleSource`/`BundleTarget` lines) and `bundleManifest`, and loads extra archives from `<root>\package\mods`. It works for SP, MP and Vietnam.
  - Unity map editor that loads dbx-converted levels: https://github.com/Powback/Bad-Company-2-Map-Editor
- **Weapon bundles:** on-demand weapon bundles live in `async/weapon/*.fbrb`, for example `bu_rif_g3`, `mec_sni_gol`, `m1garand`, `type5` and `nam_*`. Source: the vanilla client package.mft above. [confirmed]
  - Kit and pickup authoring (KitPickup and similar): **no public documentation found.** [not found]
- **SP weapon mods:**
  - MOD.SP adds MP and Vietnam weapons to the campaign by replacing `sp_common`, `async0`, `bundleManifest` and `package.mft`. Source: https://www.nexusmods.com/battlefieldbadcompany2/mods/51 [community]
  - "Bot mods" put SP AI on MP maps; they are by Napisal. Source: https://steamcommunity.com/app/24960/discussions/0/1815422173030107911/ [community]
- **Trainers:** they offer super health and ammo, no reload, teleport and save position. None was found that gives an arbitrary weapon. Source: https://games.softpedia.com/get/Cheat-Solutions/Battlefield-Bad-Company-2-12-Trainer.shtml [community]
- **Console and debug:** see section 0.4. No public method to enable a console or debug menu was found.

## 5. Prior art: automated testing and benchmarks

- Reviewers benchmarked by hand: for example, Fraps recording "60 seconds of gameplay from the level 'Cold War'". BC2 had no built-in timedemo. Source: https://www.techspot.com/review/378-nvidia-geforce-gtx-590/page10.html [press]
- No scripted or automated BC2 test harness and no quick-start-to-map launcher was found. [not found]
- The closest automation is server-side: `Startup.txt` lines run as RCON commands at boot, the Python `CommandConsole.py`/`EventConsole.py` ship with the server, and PRoCon/B3 plugins exist. Source: the R32 admin manual PDF above.

---

## Ranked routes to "empty level fast + any weapon"

1. **Find the client's argument path, then try the client's real setting names. This is the cheapest test.**
   - Evidence: FB1 reads `Win32Game.cfg` and appends it to the command line; the server logs the merged line at `Main.cpp(635)`. Dotted `-Server.X`/`-Core.X` and bare `-camelCase` arguments are both used on the BC2 server. **[confirmed on the server; client not verified]**
   - Next steps:
     - Strings-search `BFBC2Game.exe` for `Game.cfg`, `Commandline arguments`, `runtimeLog` and setting field names such as `LoadMenu`, `Level` and `DisableCutscenes`.
     - Try `-runtimeLog 1`, so the client prints what it parsed.
     - Try a client-side `Win32Game.cfg` in the root and in `Scripts/`.
     - Note that the Final client may simply ignore a level argument while the menu flow is active. **[speculation]**
2. **Local R34 server + MASE emulator + retail client, on a small MP level (SQDM).**
   - Use `levelVars.set all vehiclesDisabled true`.
   - Use the hook settings `instant_spawn=1` and `unlimited_ammo=1`.
   - Use the emulator setting `all_stats_unlocked=true` (plus veteran/vietnam/specact) so every weapon can be chosen in the deploy screen.
   - Use `player.onSpawn`/`onKill` events to log kit and weapons.
   - **[community-confirmed it works for play; nobody reports using it for automated tests]**
   - Drawbacks: emulator login, server browser and deploy screen, an MP map rather than an empty one, and loadout limited to legal kit combinations. Speed depends on Auto-Login. RCON cannot give an arbitrary weapon.
3. **Data mod through the Mod Loader / `package.mft` BundleSource/BundleTarget.**
   - Change the dbx of an SP level, or a cloned level bundle, to strip the mission logic and author weapon pickups or the spawn loadout.
   - Evidence: custom level bundles and SP weapon additions exist (MOD.SP, air_mp_* levels). **[community-confirmed mechanism]**
   - BC2's dbx entity names for pickups and kits are undocumented publicly. **[speculation on specifics]**
   - Note: this changes game data, so per AGENTS.md it must stay out of the repo, and its effect on native ownership needs checking.
4. **Native in-engine route:** hook the client's level-load request, or settings-registry writes before the menu, plus a native give-weapon or kit-swap function. The server's `DebugMenu.cpp` and its levels debug menu show that level-switch code exists in Final builds. **[speculation; needs RE evidence per AGENTS.md]**
5. **Intro skip:** rename `Output\win32\movies\ea_logo_hd.res` and `dolbydigital.res`. **[community wiki, low risk]** This saves seconds only.

**Not found anywhere:** a documented BC2 client level argument, a BC2 client console, `initfs_Win32` contents, an SP level-to-mission mapping, shipped test or empty levels, or BC2 kit/pickup dbx documentation.
