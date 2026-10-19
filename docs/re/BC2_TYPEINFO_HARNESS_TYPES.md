> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (Frostbite 1.5, build 795745, x86) TypeInfo dump: harness-relevant types, fields and enums

Sources read: `origin/tools/typeinfo-dump` -> `profiles/typeinfo/795745/bc2_types.json` (1,991 types), `docs/BC2-TYPEINFO-DUMP.md`, `profiles/typeinfo/795745/README.md`, `tools/bc2_typeinfo_dump.py`. Nothing in the repo was modified. Every table below is generated from the JSON (offsets are exactly as dumped); text marked **[INFERENCE]** is not from the dump.

Legend: `off` = byte offset inside the object (the DataContainer header occupies 0x0..0xB, so own fields of data classes start at 0xC or later). `ptr` = field type is a class, stored as a 32-bit pointer. `inline` = value type embedded in the object. `String` = 12-byte inline type (meta flags `0x415d`, its internal layout is NOT decoded by the dump). `Boolean` = 1 byte. `ArrayBase<T>` = 16-byte field in practice (see limits).

## 0. How the dump was produced, and its limits (FACT, from branch docs/tool/JSON)

- Producer: `tools/bc2_typeinfo_dump.py` v1.0.0 (python stdlib only). **Static** parse of `BFBC2Game.exe` on disk; the game is never run. Run on the patched EA-signed exe `c603f686...` (v1.0.1.0, 19,322,816 bytes). The repo-pinned build is `3911fcc8...`; README claims identical PE timestamp 1314210909 (2011-08-24), SizeOfImage 0x1913000 and code-region fingerprints, only the Authenticode blob differs. JSON `exe.matchesRepoBuild` is `false` because the sha256 differs.
- Method: find every `B9 <TypeInfo> E8 <ctor>` registration call site in `.text`/`ctr`, walk back over the `push` args (class ctor `0x500880`/`0x500660`/`0x5007c0`: 4 pushes = meta, parent, arg3, fields; value type ctor `0x43c390`: 1 push; enum ctor `0x5008e0`: 1 push; `0x4fe260`: 1 push for primitives). Read meta (name ptr @+0, flags u16 @+4: `0x35` class / `0x29` valuetype / `0x179` enum, size u16 @+6, count u8 @+0xD, field array ptr @+0x18 for valuetypes/enums) and 24-byte field records in section `fieldinf` (name, flags, type TypeInfo @+8, element TypeInfo @+0xC, offset-or-enum-value @+0x10, extra @+0x14).
- Result: 1,991 types = 1,423 class, 281 valuetype, 231 enum, 56 'other' (19 primitives/Core types, 28 runtime entity classes with flags `0x2035` such as ClientSoldierEntity and WeatherSystemEntity, plus a few oddities such as ArrayBase/AxisAlignedBox). 7,048 class/valuetype fields, 1,952 enum values. 15/15 repo-proven anchors pass (ZoomLevelData, SoldierWeaponData, WeaponStateData, VehicleEntityData, ...). 961 class registrations are `ctorVerified` against repo-known ctor bytes.
- Limits that matter for the harness:
  1. **Types only. No instances, no values, no strings other than type/field/enum names.** There are no level names, weapon names, asset names or default values. Weapon/kit/level identities live in game bundles, not in the exe.
  2. **Does not say how settings are exposed.** No command-line option names, console command names, or singleton addresses are in the dump. The dump gives only the class layouts. Mapping `ServerSettings.Level` to a `-Server.Level` style arg is **[INFERENCE]** from Frostbite convention.
  3. **DataContainer base is unresolved**: 255 classes list `parent = <unresolved 0x01bf030c>` and 780 type refs point at it (the DataContainer TypeInfo uses a different registration form and was rejected). So root data classes (ServerSettings, ClientSettings, GameSettings, ...) have a 12-byte header (0x0..0xB) that the dump does not describe, and `layoutNotes` says 'parent bytes emitted as padding'.
  4. **Only own fields are listed per class**; inherited fields come from walking the parent chain (done below via `base chain`). Offsets are absolute in the object.
  5. **Runtime classes have no field tables**: all 158 `Server*` and ~100 `Client*` entity/component classes (ServerSoldierEntity 976 B, ServerPickupEntity 384 B, ClientSoldierEntity 1600 B, ServerSoldierSpawnEntity 232 B, ...) show `fieldCount 0`. Only `ServerSettings` and `ClientSettings` (data classes whose names merely start with Server/Client) have fields. So the dump gives sizes and parents of runtime entities but not their members.
  6. Field `ArrayBase<T>` size is ambiguous (repo observed 16-byte `{?, elemTI, begin, end}`); `String` and `FileRef` are 12 bytes; array element type shown only for arrays. `recordFlags`/`recordExtra`/TypeInfo+0x10 are undecoded.
  7. 281 registration sites were rejected: all Havok/low-level (`hk*`-style member names such as `rigidBody`, `shapes`, `xRes`), irrelevant here. No relevant game type was rejected.
  8. Per-class `arg3` (ctor arg3 stored at TypeInfo+0x10) is a **code pointer** (all 1,056 non-zero values land in `.text`; 367 classes have arg3 = 0). The dump leaves it unexplained. For the singleton-style settings it is very informative, see section 2.7.
- JSON schema: top-level keys `tool, exe, layout, classes, enums, primitives, summary, anchors`. `classes[]` holds both classes and valuetypes: `{name, kind(class|valuetype|other), size, flags, typeInfo(VA), typeInfoRva, meta(VA), metaRaw, fieldCount, registration{source,site,ctor,ctorVerified}, parent(name or '<unresolved 0x..>'), parentTypeInfo, isRoot, arg3, fieldsPtr, fields[{name, offset, typeName, kind(primitive|ref|inline|enum|array), elementTypeName, typeInfo, recordFlags, recordExtra}], layoutNotes}`. `enums[]`: `{name, size, values[{name,value}]}`. Addresses are VAs with image base 0x400000 (`typeInfoRva` = VA - 0x400000).

## 1. Route A: level load / startup configuration

### 1.1 `ServerSettings` (the strongest level and gameplay-cheat lever)

**class `ServerSettings`** size `0xcc`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Level String (@0x7c) selects the level; IsFastStartupEnabled, DisableCutscenes, DisableCheckpoints, SavePoint/LoadSavePoint, InstantSpawn, IsGodMode/IsJesusMode, HasUnlimitedAmmo/HasUnlimitedMags, UnrestrictedUnlocks, IsAiEnabled, PlayerCountNeededForMultiplayer, timeouts. This is the single richest type for the harness. TypeInfo VA 0x01bf3b3c, fields array 0x01c138b8, registration site 0x013883e0, arg3 (code ptr) 0x00534600.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | DropSpikeChance | Float32 | primitive |
| `0x10` | PacketDrops | Float32 | primitive |
| `0x14` | MaxDropDuration | Float32 | primitive |
| `0x18` | MinDropDuration | Float32 | primitive |
| `0x1c` | MaxLatency | Float32 | primitive |
| `0x20` | DifficultyMode | String | primitive |
| `0x2c` | TimeoutTime | Float32 | primitive |
| `0x30` | MinLatency | Float32 | primitive |
| `0x34` | PlayerCountNeededForMultiplayer | Uint32 | primitive |
| `0x38` | ReorderingChance | Float32 | primitive |
| `0x3c` | DedicatedServerCpu | Int32 | primitive |
| `0x40` | Playlist | String | primitive |
| `0x4c` | FilterStatUploads | Uint32 | primitive |
| `0x50` | RemoteAdminPassword | String | primitive |
| `0x5c` | OutgoingFrequency | Float32 | primitive |
| `0x60` | GameConfigurationTimeout | Float32 | primitive |
| `0x64` | DuplicationChance | Float32 | primitive |
| `0x68` | IngameTimeout | Float32 | primitive |
| `0x6c` | LoadingTimeout | Float32 | primitive |
| `0x70` | SavePoint | String | primitive |
| `0x7c` | Level | String | primitive |
| `0x88` | CorruptionChance | Float32 | primitive |
| `0x8c` | DesertingAllowed | Boolean | primitive |
| `0x8d` | DisableTelemetry | Boolean | primitive |
| `0x8e` | DisableCheckpoints | Boolean | primitive |
| `0x8f` | FilterProfanity | Boolean | primitive |
| `0x90` | AutoBalance | Boolean | primitive |
| `0x91` | IsHardcore | Boolean | primitive |
| `0x92` | UseNewVoip | Boolean | primitive |
| `0x93` | InstantSpawn | Boolean | primitive |
| `0x94` | ForcePlaylist | Boolean | primitive |
| `0x95` | IsGodMode | Boolean | primitive |
| `0x96` | IsReconfigurable | Boolean | primitive |
| `0x97` | IsWaterPhysicsEnabled | Boolean | primitive |
| `0x98` | RegulatedAIThrottle | Boolean | primitive |
| `0x99` | AutokillSoldiers | Boolean | primitive |
| `0x9a` | AutoUnspawnBangers | Boolean | primitive |
| `0x9b` | DisableGlobalStats | Boolean | primitive |
| `0x9c` | DisableFinalize | Boolean | primitive |
| `0x9d` | ThirdPersonVehicleCameras | Boolean | primitive |
| `0x9e` | DisableGeneralStats | Boolean | primitive |
| `0x9f` | IsFastStartupEnabled | Boolean | primitive |
| `0xa0` | UseFakeAnimatedCollision | Boolean | primitive |
| `0xa1` | DisableClientStats | Boolean | primitive |
| `0xa2` | MinimapSpotting | Boolean | primitive |
| `0xa3` | RemoteAdministration | Boolean | primitive |
| `0xa4` | FriendlyFireEnabled | Boolean | primitive |
| `0xa5` | IsRenderPhysicsWater | Boolean | primitive |
| `0xa6` | KillCam | Boolean | primitive |
| `0xa7` | ThreeDSpotting | Boolean | primitive |
| `0xa8` | Crosshair | Boolean | primitive |
| `0xa9` | Minimap | Boolean | primitive |
| `0xaa` | IsJesusMode | Boolean | primitive |
| `0xab` | IsSpotWhenZoomedEnabled | Boolean | primitive |
| `0xac` | AllowDestructionOutsideCombatArea | Boolean | primitive |
| `0xad` | ShowHoldGroundFailures | Boolean | primitive |
| `0xae` | IsInternetSimulationEnabled | Boolean | primitive |
| `0xaf` | IsTreeDestructionEnabled | Boolean | primitive |
| `0xb0` | IsFragalyzerLogEnabled | Boolean | primitive |
| `0xb1` | IsFragalyzerLocalLogEnabled | Boolean | primitive |
| `0xb2` | AILooksIntoCamera | Boolean | primitive |
| `0xb3` | HavokCaptureToFile | Boolean | primitive |
| `0xb4` | HavokVisualDebugger | Boolean | primitive |
| `0xb5` | DisableCutscenes | Boolean | primitive |
| `0xb6` | ShowTriggerDebugText | Boolean | primitive |
| `0xb7` | IsStaticModelsIterable | Boolean | primitive |
| `0xb8` | LoadSavePoint | Boolean | primitive |
| `0xb9` | TimeoutGame | Boolean | primitive |
| `0xba` | IsDestructionEnabled | Boolean | primitive |
| `0xbb` | DrawActivePhysicsObjects | Boolean | primitive |
| `0xbc` | LoopDuringLoadingJobEnable | Boolean | primitive |
| `0xbd` | RunAfterClientEnable | Boolean | primitive |
| `0xbe` | UnrestrictedUnlocks | Boolean | primitive |
| `0xbf` | IsRenderDamageEvents | Boolean | primitive |
| `0xc0` | HasUnlimitedAmmo | Boolean | primitive |
| `0xc1` | ScoringLogEnabled | Boolean | primitive |
| `0xc2` | JobEnable | Boolean | primitive |
| `0xc3` | IsStatsEnabled | Boolean | primitive |
| `0xc4` | IsNetworkStatsEnabled | Boolean | primitive |
| `0xc5` | IsAiEnabled | Boolean | primitive |
| `0xc6` | RespawnOnDeathPosition | Boolean | primitive |
| `0xc7` | ThreadedLoadingEnable | Boolean | primitive |
| `0xc8` | PreloadingEnabled | Boolean | primitive |
| `0xc9` | HasUnlimitedMags | Boolean | primitive |

### 1.2 `ClientSettings`

**class `ClientSettings`** size `0xfc`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Level (@0x5c), SpawnPoint (@0x50), Kit u32 (@0x18), Team u32 (@0x1c), IsMultiplayer (@0xd5), LoadMenu (@0xfa), DisableMenuFlow (@0xf9), LimitedMainMenu (@0xba), BypassMatchmaking (@0xea), IsFastStartupEnabled (@0xf0), DisableMovies (@0xf1), DisableHudMovies (@0xf2), PauseGameOnStartUp (@0xd2), UseScriptedInput (@0xc9), UnrestrictedUnlocks (@0xc8), IsMenuDevOptsEnabled (@0xf4), DebugMenuOnLThumb (@0xe9), ServerIp (@0xa4), IsSoundEnabled/IsHudDisabled. TypeInfo VA 0x01bf3b74, fields 0x01c14098, site 0x01388410, arg3 0x00534680.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | DuplicationChance | Float32 | primitive |
| `0x10` | CorruptionChance | Float32 | primitive |
| `0x14` | ReorderingChance | Float32 | primitive |
| `0x18` | Kit | Uint32 | primitive |
| `0x1c` | Team | Uint32 | primitive |
| `0x20` | CameraProximityEffectRadius | Float32 | primitive |
| `0x24` | MaxCameraProximityEffectCount | Int32 | primitive |
| `0x28` | LoadingTimeout | Float32 | primitive |
| `0x2c` | ScreenshotFilename | String | primitive |
| `0x38` | MaxLatency | Float32 | primitive |
| `0x3c` | AimScale | Float32 | primitive |
| `0x40` | DropSpikeChance | Float32 | primitive |
| `0x44` | IncomingFrequency | Float32 | primitive |
| `0x48` | MinLatency | Float32 | primitive |
| `0x4c` | PacketDrops | Float32 | primitive |
| `0x50` | SpawnPoint | String | primitive |
| `0x5c` | Level | String | primitive |
| `0x68` | MaxDropDuration | Float32 | primitive |
| `0x6c` | MinDropDuration | Float32 | primitive |
| `0x70` | SecondaryServerIp | String | primitive |
| `0x7c` | LoadedTimeout | Float32 | primitive |
| `0x80` | Ps3SoundMaxRsxMemoryUsage | String | primitive |
| `0x8c` | Ps3SoundCellCacheSize | Uint32 | primitive |
| `0x90` | Ps3SoundMinRsxAllocation | Uint32 | primitive |
| `0x94` | StreamingSoundPath | String | primitive |
| `0xa0` | MaxSoundCount | Uint32 | primitive |
| `0xa4` | ServerIp | String | primitive |
| `0xb0` | IngameTimeout | Float32 | primitive |
| `0xb4` | ResetStatsOnConnect | Boolean | primitive |
| `0xb5` | UsePS3VoipCompression | Boolean | primitive |
| `0xb6` | HavokVisualDebugger | Boolean | primitive |
| `0xb7` | AutoUnspawnDynamicObjects | Boolean | primitive |
| `0xb8` | HavokVDBShowsEffectsWorld | Boolean | primitive |
| `0xb9` | DisableTelemetry | Boolean | primitive |
| `0xba` | LimitedMainMenu | Boolean | primitive |
| `0xbb` | AlwaysShowOverlay | Boolean | primitive |
| `0xbc` | DebugCollectibles | Boolean | primitive |
| `0xbd` | ThreadedLoadingEnable | Boolean | primitive |
| `0xbe` | CheckDeviceEntitlements | Boolean | primitive |
| `0xbf` | EnableCollectibleNotifications | Boolean | primitive |
| `0xc0` | LocalVehicleSimulationEnabled | Boolean | primitive |
| `0xc1` | ShowBuildId | Boolean | primitive |
| `0xc2` | AsyncFollowWheels | Boolean | primitive |
| `0xc3` | PreloadingEnabled | Boolean | primitive |
| `0xc4` | ExtractPersistenceInformation | Boolean | primitive |
| `0xc5` | KeepEffects | Boolean | primitive |
| `0xc6` | EnableRestTool | Boolean | primitive |
| `0xc7` | TextureBakerEnable | Boolean | primitive |
| `0xc8` | UnrestrictedUnlocks | Boolean | primitive |
| `0xc9` | UseScriptedInput | Boolean | primitive |
| `0xca` | UseMouseAndKeyboardSystem | Boolean | primitive |
| `0xcb` | HavokCaptureToFile | Boolean | primitive |
| `0xcc` | MemoryStatsEnable | Boolean | primitive |
| `0xcd` | UseGlobalGamePadInput | Boolean | primitive |
| `0xce` | EnableAdvertisements | Boolean | primitive |
| `0xcf` | IsSpecialEdition | Boolean | primitive |
| `0xd0` | IsAchievementsEnabled | Boolean | primitive |
| `0xd1` | DebugAdvertisements | Boolean | primitive |
| `0xd2` | PauseGameOnStartUp | Boolean | primitive |
| `0xd3` | IsEffectsDisabled | Boolean | primitive |
| `0xd4` | IsNetworkStatsEnabled | Boolean | primitive |
| `0xd5` | IsMultiplayer | Boolean | primitive |
| `0xd6` | IsStatsEnabled | Boolean | primitive |
| `0xd7` | IsLipSyncEnabled | Boolean | primitive |
| `0xd8` | AsyncClientBulletEntity | Boolean | primitive |
| `0xd9` | IsPadRumbleEnabled | Boolean | primitive |
| `0xda` | IsEmittersDisabled | Boolean | primitive |
| `0xdb` | IsHavokEmittersDisabled | Boolean | primitive |
| `0xdc` | IsSoundEnabled | Boolean | primitive |
| `0xdd` | Ps3OnDemandSoundInRsxEnable | Boolean | primitive |
| `0xde` | IsWaterPhysicsEnabled | Boolean | primitive |
| `0xdf` | IsTerrainDisabled | Boolean | primitive |
| `0xe0` | IsSootEnabled | Boolean | primitive |
| `0xe1` | IsVariedVoiceOversEnabled | Boolean | primitive |
| `0xe2` | IsOvergrowthEnabled | Boolean | primitive |
| `0xe3` | IsSubtitlesEnabled | Boolean | primitive |
| `0xe4` | EnableAudioTweakHack | Boolean | primitive |
| `0xe5` | IsOnDemandSoundsEnabled | Boolean | primitive |
| `0xe6` | IsUITracesEnabled | Boolean | primitive |
| `0xe7` | InvertFreeCamera | Boolean | primitive |
| `0xe8` | RenderTags | Boolean | primitive |
| `0xe9` | DebugMenuOnLThumb | Boolean | primitive |
| `0xea` | BypassMatchmaking | Boolean | primitive |
| `0xeb` | DebugMatchmaking | Boolean | primitive |
| `0xec` | InvertYaw | Boolean | primitive |
| `0xed` | IsInternetSimulationEnabled | Boolean | primitive |
| `0xee` | InvertPitch | Boolean | primitive |
| `0xef` | VisualFrameInterpolation | Boolean | primitive |
| `0xf0` | IsFastStartupEnabled | Boolean | primitive |
| `0xf1` | DisableMovies | Boolean | primitive |
| `0xf2` | DisableHudMovies | Boolean | primitive |
| `0xf3` | IsHudDisabled | Boolean | primitive |
| `0xf4` | IsMenuDevOptsEnabled | Boolean | primitive |
| `0xf5` | ScreenshotToFile | Boolean | primitive |
| `0xf6` | IsPresenceSilent | Boolean | primitive |
| `0xf7` | IsHintsEnabled | Boolean | primitive |
| `0xf8` | IsPresenceEnabled | Boolean | primitive |
| `0xf9` | DisableMenuFlow | Boolean | primitive |
| `0xfa` | LoadMenu | Boolean | primitive |

### 1.3 `GameSettings`

**class `GameSettings`** size `0xa4`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: FastStartupEnable (@0x98), UseBundles (@0x94) + BundleMountPoints (@0x20, array of BundleMountPoint{VfsPath,Bundle,IsResourcePath}), UseCustomKit (@0x91), MaxPlayerCount, ForceDeltaTime/ForceDeltaTicks/MaxFps/VsyncEnable (deterministic frame pacing for tests), RetailServers, ResourceReloadingEnable. TypeInfo VA 0x01bf3bc0, fields 0x01c14e48, site 0x01388460, arg3 0x00534700.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | svpunkbuster | Uint32 | primitive |
| `0x10` | clpunkbuster | Uint32 | primitive |
| `0x14` | TeamKillValueDecreasePerSecond | Uint32 | primitive |
| `0x18` | JobThreadCount | Uint32 | primitive |
| `0x1c` | InputTapMaxTime | Float32 | primitive |
| `0x20` | BundleMountPoints | ArrayBase<BundleMountPoint> | array |
| `0x30` | LogHistory | Uint32 | primitive |
| `0x34` | OnlineListenPort | Uint32 | primitive |
| `0x38` | NoInteractivityTimeoutTime | Uint32 | primitive |
| `0x3c` | PS3ContentRatingAge | Uint32 | primitive |
| `0x40` | NoInteractivityThresholdLimit | Float32 | primitive |
| `0x44` | TeamKillValueIncrease | Uint32 | primitive |
| `0x48` | TeamKillValueForKick | Uint32 | primitive |
| `0x4c` | TeamKillCountForKick | Uint32 | primitive |
| `0x50` | MaxVariableFps | Float32 | primitive |
| `0x54` | MinSmoothVariableFps | Float32 | primitive |
| `0x58` | ForceDeltaTime | Float32 | primitive |
| `0x5c` | MaxFps | Float32 | primitive |
| `0x60` | GameVersion | Uint32 | primitive |
| `0x64` | MaxPlayerCount | Uint32 | primitive |
| `0x68` | NumProcessors | Int32 | primitive |
| `0x6c` | EnabledLanguages | String | primitive |
| `0x78` | InputConfiguration | String | primitive |
| `0x84` | InputHoldMinTime | Float32 | primitive |
| `0x88` | ForceDeltaTicks | Int32 | primitive |
| `0x8c` | AdjustVehicleCenterOfMass | Boolean | primitive |
| `0x8d` | IsBeta | Boolean | primitive |
| `0x8e` | ToggleCrouch | Boolean | primitive |
| `0x8f` | RotateLogs | Boolean | primitive |
| `0x90` | HoldToZoom | Boolean | primitive |
| `0x91` | UseCustomKit | Boolean | primitive |
| `0x92` | EnableDumpLevelChecksumOnDisconnect | Boolean | primitive |
| `0x93` | Ps3HDDGameDataEnable | Boolean | primitive |
| `0x94` | UseBundles | Boolean | primitive |
| `0x95` | EnableMemoryDumpStreamingOnFirstSpawn | Boolean | primitive |
| `0x96` | VsyncEnable | Boolean | primitive |
| `0x97` | LogFileEnable | Boolean | primitive |
| `0x98` | FastStartupEnable | Boolean | primitive |
| `0x99` | ResourceReloadingEnable | Boolean | primitive |
| `0x9a` | ReplayDebug | Boolean | primitive |
| `0x9b` | XenonJobThread5Enable | Boolean | primitive |
| `0x9c` | AutoAimEnabled | Boolean | primitive |
| `0x9d` | RetailServers | Boolean | primitive |
| `0x9e` | XenonJobThread4Enable | Boolean | primitive |
| `0x9f` | XenonJobThread1Enable | Boolean | primitive |
| `0xa0` | XenonJobThread0Enable | Boolean | primitive |
| `0xa1` | XenonJobThread3Enable | Boolean | primitive |
| `0xa2` | XenonJobThread2Enable | Boolean | primitive |

**valuetype `BundleMountPoint`** size `0x1c`, base chain: none (root/value type)
  - Why: Bundle/VFS mounting, relevant if a trimmed bundle set is mounted for a tiny level.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | VfsPath | String | primitive |
| `0xc` | Bundle | String | primitive |
| `0x18` | IsResourcePath | Boolean | primitive |

### 1.4 `NetworkSettings`, `DemoSettings`, `CoreSettings`, `ResourceManagerSettings`

**class `NetworkSettings`** size `0xa4`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: SkipEntitlementCheck (@0x9c) avoids the online entitlement gate; ServerPort/ClientPort/MaxClientCount/MaxGhostCount for a local server; Single/MultiplayerFrameHistoryTime; Playback/RecordDemoFileName + TimeDemo are an alternate deterministic route. Contains Plasma account fields (PlasmaAccountName/Persona/Password, PlasmaAutoLogin, PlasmaCreateAccount) - do not store credentials in source. TypeInfo VA 0x01bf3bf8, fields 0x01c14a28, arg3 0x00534780.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | PlaybackDemoFileName | String | primitive |
| `0x18` | ClientConnectionDebugFilePrefix | String | primitive |
| `0x24` | RecordDemoFileName | String | primitive |
| `0x30` | MaxServerFrameSize | Uint32 | primitive |
| `0x34` | SingleplayerFrameHistoryTime | Float32 | primitive |
| `0x38` | PlasmaAccountPersona | String | primitive |
| `0x44` | MultiplayerFrameHistoryTime | Float32 | primitive |
| `0x48` | PlasmaAccountPassword | String | primitive |
| `0x54` | ServerConnectionDebugFilePrefix | String | primitive |
| `0x60` | PlasmaAccountName | String | primitive |
| `0x6c` | MaxClientFrameSize | Uint32 | primitive |
| `0x70` | ClientPort | Uint32 | primitive |
| `0x74` | ServerPort | Uint32 | primitive |
| `0x78` | TitleId | Uint32 | primitive |
| `0x7c` | Version | String | primitive |
| `0x88` | ProtocolVersion | Uint32 | primitive |
| `0x8c` | MaxQueriesPerSecond | Uint32 | primitive |
| `0x90` | QueryPortOffset | Uint32 | primitive |
| `0x94` | MaxGhostCount | Uint32 | primitive |
| `0x98` | MaxClientCount | Uint32 | primitive |
| `0x9c` | SkipEntitlementCheck | Boolean | primitive |
| `0x9d` | TimeDemo | Boolean | primitive |
| `0x9e` | UseFrameManager | Boolean | primitive |
| `0x9f` | PlasmaAutoLogin | Boolean | primitive |
| `0xa0` | PlasmaCreateAccount | Boolean | primitive |

**class `DemoSettings`** size `0x28`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Demo playback/record file names, TimeDemo, PauseOnStartup (@0x25). TypeInfo VA 0x01bf3c30, arg3 0x00534800.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | RecordDemoFileName | String | primitive |
| `0x18` | PlaybackDemoFileName | String | primitive |
| `0x24` | TimeDemo | Boolean | primitive |
| `0x25` | PauseOnStartup | Boolean | primitive |

**valuetype `CoreSettings`** size `0x74`, base chain: none (root/value type)
  - Why: Engine-level (valuetype, so embedded in some larger object, not a DataContainer): DisplayErrors/DisplayAsserts/CrashDump*, LogLevel, FinalBundles (@0x65), EnableMemoryDump, DiskLayoutEmulation, HostUser/SessionId.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | RandomTimeSeed | Int32 | primitive |
| `0x4` | LogMaster | String | primitive |
| `0x10` | ResourceMgrSmbMegs | Int32 | primitive |
| `0x14` | DiskLayoutPathPatterns | ArrayBase<String> | array |
| `0x24` | HostUser | String | primitive |
| `0x30` | SessionId | String | primitive |
| `0x3c` | LogLevel | CoreLogLevel | enum |
| `0x40` | Host | String | primitive |
| `0x4c` | ResourceMgrCoreBytes | Int32 | primitive |
| `0x50` | AsyncReadSleepMillis | Int32 | primitive |
| `0x54` | StudioBus | String | primitive |
| `0x60` | CrashDumpAsserts | Boolean | primitive |
| `0x61` | EnableResourceFileChangeListener | Boolean | primitive |
| `0x62` | CrashDumpErrors | Boolean | primitive |
| `0x63` | DisplayErrors | Boolean | primitive |
| `0x64` | ResourceMgrArenaEnable | Boolean | primitive |
| `0x65` | FinalBundles | Boolean | primitive |
| `0x66` | DisplayAsserts | Boolean | primitive |
| `0x67` | EnableTweaker | Boolean | primitive |
| `0x68` | EnableMemoryDump | Boolean | primitive |
| `0x69` | EnableBitstreamTypeVerification | Boolean | primitive |
| `0x6a` | EnableResolveErrorTracking | Boolean | primitive |
| `0x6b` | EnableSparseSerialization | Boolean | primitive |
| `0x6c` | EnableDrone | Boolean | primitive |
| `0x6d` | DiskSeekDelayEmulation | Boolean | primitive |
| `0x6e` | EnableRemoteLog | Boolean | primitive |
| `0x6f` | EnableDbErrorPolicy | Boolean | primitive |
| `0x70` | EnableBitstreamExtendedVerification | Boolean | primitive |
| `0x71` | EnableInstanceDirectory | Boolean | primitive |
| `0x72` | DiskLayoutEmulation | Boolean | primitive |

**enum `CoreLogLevel`** (size 4): `CllNone`=0, `CllEventRecord`=1, `CllError`=2, `CllAssert`=3, `CllFatalAssert`=4, `CllValidate`=5, `CllWarning`=6, `CllInfo`=7, `CllOutput`=8, `CllDebug`=9

**class `ResourceManagerSettings`** size `0x10`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: DeferredLoadingEnable, a load-path toggle. arg3 0x0042a8b0 (different region, not in the 0x5346xx cluster).

| off | field | type | kind |
|---|---|---|---|
| `0xc` | DeferredLoadingEnable | Boolean | primitive |

### 1.5 Level data model (what a 'level' is in the type graph)

**class `LevelData`** size `0x3c0`, base chain: WorldData -> EntityBusData -> Editable -> Asset -> DataContainer(unresolved)
  - Why: Root of a level. Inherits WorldData -> EntityBusData -> Editable -> Asset. LevelDescription inline at `0x84`; WaitForButtonBeforeStarting is at absolute `0x1ec` and StartDelayTime at `0xa8`, GameMode ptr at `0xa4`. StartLevelFadedToBlack (@0x3b0), StartLevelWithSoundFaded (@0x3af), HasAI (@0x3ae), HasFlyingAI (@0x3ad), SpawnPointManager (@0x1f8), DefaultFOV (@0x1f4), InfantryFOVMultiplier (@0x1f0).

| off | field | type | kind |
|---|---|---|---|
| `0x7c` | EmitterSystemMetrics | EmitterSystemMetrics | inline |
| `0x84` | LevelDescription | LevelDescription | inline |
| `0x1f0` | InfantryFOVMultiplier | Float32 | primitive |
| `0x1f4` | DefaultFOV | Float32 | primitive |
| `0x1f8` | SpawnPointManager | SpawnPointManagerData | ref |
| `0x1fc` | ExclusiveStaticCullTree | StaticCullTreeData | ref |
| `0x200` | MaxLocalPathfindingObjects | Uint32 | primitive |
| `0x204` | ClientTreeStaticCullTree | StaticCullTreeData | ref |
| `0x208` | ClientStaticCullTree | StaticCullTreeData | ref |
| `0x20c` | FriendlyDamageModifier | Float32 | primitive |
| `0x210` | HardcorePlayerHealthModifier | Float32 | primitive |
| `0x214` | BloodEffect | EffectAsset | ref |
| `0x218` | HardcoreRegenerateHealthModifier | Float32 | primitive |
| `0x21c` | HardcoreFriendlyDamageModifier | Float32 | primitive |
| `0x220` | MaxVehicleHeight | Float32 | primitive |
| `0x224` | AmbientSounds | AmbientSoundsAsset | ref |
| `0x230` | MinimapSettings | MinimapSettings | inline |
| `0x2f0` | SoundStates | SoundStateSettingsAsset | ref |
| `0x2f4` | ReverbSettings | ReverbSettingsAsset | ref |
| `0x2f8` | PathObjectCount | Uint32 | primitive |
| `0x2fc` | VoiceOverLines | ArrayBase<DialogLineNode> | array |
| `0x310` | SubtitleAsset | SubtitleMapAsset | ref |
| `0x314` | TriggerAnimations | ArrayBase<AnimationAsset> | array |
| `0x328` | VoiceOverAnimTree | AnimTreeData | ref |
| `0x32c` | VoiceOvers | ArrayBase<VOAsset> | array |
| `0x340` | Covers | ArrayBase<CoverData> | array |
| `0x354` | DynamicObjects | ArrayBase<GameObjectData> | array |
| `0x368` | WaypointAnimations | WaypointAnimationData | ref |
| `0x36c` | PathfindingTypes | ArrayBase<PathfindingSettings> | array |
| `0x380` | AerialHeightmapCellSize | Float32 | primitive |
| `0x384` | AerialHeightmapQuality | Int32 | primitive |
| `0x388` | DestructibleLocalPathfindingParts | ArrayBase<DestructibleLocalPathfindingPart> | array |
| `0x398` | ExplosiveObjects | ArrayBase<ExplosiveObjectData> | array |
| `0x3ac` | UseBC2SP09ShadowHack | Boolean | primitive |
| `0x3ad` | HasFlyingAI | Boolean | primitive |
| `0x3ae` | HasAI | Boolean | primitive |
| `0x3af` | StartLevelWithSoundFaded | Boolean | primitive |
| `0x3b0` | StartLevelFadedToBlack | Boolean | primitive |

**valuetype `LevelDescription`** size `0x16c`, base chain: none (root/value type)
  - Why: Per-level meta: GameMode ptr (@0x20), WaitForButtonBeforeStarting (@0x168: a 'press button to start' gate to defeat), StartDelayTime (@0x24), LoadingMovie/LoadingScreens (loading UI), FactionTeam1/2, AttackerTeam, Name/Description, Licenses/KickIfLicenseMissing/Revision (@0x16a gate).

| off | field | type | kind |
|---|---|---|---|
| `0x0` | LoadingMovieDimensions | Vec2 | primitive |
| `0x8` | LoadingMovieTopLeft | Vec2 | primitive |
| `0x10` | LevelMemoryUsage | LevelMemoryUsageData | inline |
| `0x20` | GameMode | GameModeData | ref |
| `0x24` | StartDelayTime | Uint32 | primitive |
| `0x28` | MinimapCameraOffset | MinimapCameraOffsetData | inline |
| `0x34` | Consumables | ArrayBase<String> | array |
| `0x44` | Advertisement | AdvertisementZoneData | inline |
| `0x60` | RichPresence | RichPresenceConnection | ref |
| `0x64` | Collectibles | ArrayBase<String> | array |
| `0x74` | Revision | Uint32 | primitive |
| `0x78` | Licenses | String | primitive |
| `0x84` | MatchmakingModID | String | primitive |
| `0x90` | LevelRoundTimeStatName | String | primitive |
| `0x9c` | FactionTeam2 | FactionId | enum |
| `0xa0` | FactionTeam1 | FactionId | enum |
| `0xa4` | KitImages | ArrayBase<KitImageData> | array |
| `0xb4` | GameTips | ArrayBase<GameTipData> | array |
| `0xc4` | AchivementHardDate | String | primitive |
| `0xd0` | LoadingMovieLoopEndFrame | Uint32 | primitive |
| `0xd4` | LoadingMovieLoopBeginFrame | Uint32 | primitive |
| `0xd8` | LoadingTime | LoadingTimeData | inline |
| `0xe4` | AttackerTeam | TeamId | enum |
| `0xe8` | Description | String | primitive |
| `0xf4` | Name | String | primitive |
| `0x100` | LoadingMovie | BinkMemoryAsset | ref |
| `0x104` | LoadingScreens | ArrayBase<LoadingScreenData> | array |
| `0x114` | Music | WaveAsset | ref |
| `0x118` | AchivementMediumCompleted | String | primitive |
| `0x124` | AchivementEasyDate | String | primitive |
| `0x130` | AchivementHardCompleted | String | primitive |
| `0x13c` | AchivementMediumDate | String | primitive |
| `0x148` | SoundFX | ArrayBase<LocalizedSoundFX> | array |
| `0x158` | MinimapTexture | TextureAsset | ref |
| `0x15c` | AchivementEasyCompleted | String | primitive |
| `0x168` | WaitForButtonBeforeStarting | Boolean | primitive |
| `0x169` | GatherCollectibles | Boolean | primitive |
| `0x16a` | KickIfLicenseMissing | Boolean | primitive |

**class `LevelDescriptionAsset`** size `0x190`, base chain: Asset -> DataContainer(unresolved)
  - Why: Asset wrapper (LevelName String @0x184).

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Description | LevelDescription | inline |
| `0x184` | LevelName | String | primitive |

**class `WorldData`** size `0x7c`, base chain: EntityBusData -> Editable -> Asset -> DataContainer(unresolved)
  - Why: Modes (@0x6c, ArrayBase<String>) = the game-mode names a world supports; AuxAssets (@0x58); MaterialGrid; inherits EntityBusData.Objects/EventConnections (the level's entity graph).

| off | field | type | kind |
|---|---|---|---|
| `0x50` | MaterialGrid | MaterialGridData | ref |
| `0x54` | TrashShaderIndex | Int32 | primitive |
| `0x58` | AuxAssets | ArrayBase<Asset> | array |
| `0x6c` | Modes | ArrayBase<String> | array |

**class `EntityBusData`** size `0x50`, base chain: Editable -> Asset -> DataContainer(unresolved)
  - Why: The object graph container for a level or blueprint: Objects, DataConnections, EventConnections (+ Descriptor). Any data-driven route (add a ConsoleCommand/Spawn entity to a level) would edit these arrays.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | DataConnections | ArrayBase<DataConnection> | array |
| `0x28` | Descriptor | AssemblyDescriptorData | ref |
| `0x2c` | Objects | ArrayBase<GameObjectData> | array |
| `0x40` | EventConnections | ArrayBase<EventConnection> | array |

**class `GameModeData`** size `0x9c`, base chain: Asset -> DataContainer(unresolved)
  - Why: LevelDescription.GameMode points here: PrimaryLogicLayerName/SecondaryLogicLayerName select which logic layers load (the 'layer inclusion' concept), LoaderName, InGameMenuName, HudRootName, CrosshairsName, ModeID, AddToIngameBundleRefs/AddToLoaderBundleRefs (FileRef).

| off | field | type | kind |
|---|---|---|---|
| `0x18` | CrosshairsName | String | primitive |
| `0x24` | InGameMenuName | String | primitive |
| `0x30` | LoaderName | String | primitive |
| `0x3c` | AddToIngameBundleRefs | FileRef | primitive |
| `0x48` | AddToLoaderBundleRefs | FileRef | primitive |
| `0x54` | SecondaryLogicLayerName | String | primitive |
| `0x60` | MatchmakingQueryID | String | primitive |
| `0x6c` | ModeID | String | primitive |
| `0x78` | Description | String | primitive |
| `0x84` | PrimaryLogicLayerName | String | primitive |
| `0x90` | HudRootName | String | primitive |

**enum `GameModeTypes`** (size 4): `SinglePlayer`=0, `Multiplayer_Day1`=1, `Multiplayer_Day2`=2
  - Why: Only three modes: SinglePlayer=0, Multiplayer_Day1=1, Multiplayer_Day2=2.

**class `WorldBuildView`** size `0x2c`, base chain: Asset -> DataContainer(unresolved)
  - Why: Editor/build view with `Level` ptr to WorldData and Sections (WorldSubSection min/max). No 'SubWorld' or 'Partition' type exists in this build (see 1.7).

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Sections | ArrayBase<WorldSubSection> | array |
| `0x28` | Level | WorldData | ref |

**class `LevelSaveData`** size `0x20`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: SaveNames/SaveSize (checkpoint save slots).

| off | field | type | kind |
|---|---|---|---|
| `0xc` | SaveNames | ArrayBase<String> | array |
| `0x1c` | SaveSize | Uint32 | primitive |

**class `ProceduralLevelCreateDesc`** size `0x34`, base chain: Asset -> DataContainer(unresolved)
  - Why: An Asset with OutputPath/OutputName/SaveToDiskEnable: looks like a tool/editor 'create a procedural level' descriptor. **[INFERENCE]** editor-side, probably not usable at runtime; it is the only type that hints at generating an empty level.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | OutputPath | String | primitive |
| `0x24` | OutputName | String | primitive |
| `0x30` | SaveToDiskEnable | Boolean | primitive |

### 1.6 Matchmaking and playlists (MP route; mostly irrelevant for SP but names the map strings)

**class `MatchMakingMap`** size `0x44`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: MapName String (@0x34), GameMode String (@0x1c), UIName, Licenses, Revision. Shows maps are identified by a string name and a game-mode string.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | Licenses | String | primitive |
| `0x18` | Revision | Uint32 | primitive |
| `0x1c` | GameMode | String | primitive |
| `0x28` | UIName | String | primitive |
| `0x34` | MapName | String | primitive |
| `0x40` | KickIfLicenseMissing | Boolean | primitive |

**class `MatchMakingPlaylist`** size `0x34`, base chain: none (root/value type) -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0xc` | RoundsPerMap | Uint32 | primitive |
| `0x10` | Maps | ArrayBase<MatchMakingMapLink> | array |
| `0x20` | Name | String | primitive |
| `0x2c` | Rule | MatchMakingPlaylistRule | enum |
| `0x30` | IsDefault | Boolean | primitive |

**class `MatchMakerAsset`** size `0xb4`, base chain: Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Playlists | ArrayBase<MatchMakingPlaylist> | array |
| `0x2c` | PoolOverride | ArrayBase<MatchMakingPoolOverride> | array |
| `0x3c` | Maps | ArrayBase<MatchMakingMap> | array |
| `0x50` | EloParams | EloParameters | inline |
| `0xa0` | Sessions | ArrayBase<MatchMakingSession> | array |

### 1.7 Name-search result: types that do NOT exist in this build (FACT)

No type named `SubWorld*`, `Partition` (only `PersistentPartition`, a profile-stats thing), `LevelSetup`, `StartupSettings`, `MapList`, `Cheat*`, `Console` (only `ConsoleCommandEntityData`/`ServerConsoleCommandEntity`), `AutoPlayer`, `Bot` (only `BotPriorityConfigData`), `Campaign`, `Checkpoint*` (only a write policy enum value and `SaveEntityData`/`ContinueEntityData`/`LevelSaveData`), `Inventory`, `Loadout`, `Customization`/`CharacterCustomization`, `Gadget`, `GameModeSettings`. BC2 has no 'LayerInclusion' type; layer selection is `GameModeData.PrimaryLogicLayerName/SecondaryLogicLayerName` strings plus `WorldData.Modes`.

### 1.8 The settings-singleton cluster (arg3) **[INFERENCE built on FACT]**

FACT: for these classes the ctor arg3 (TypeInfo+0x10) is a code pointer in a tight 0x80-spaced run starting at `0x00534600`:

`ServerSettings 0x534600, ClientSettings 0x534680, GameSettings 0x534700, NetworkSettings 0x534780, DemoSettings 0x534800, PhysicsSettings 0x534880, MonitoringSettings 0x534900, WorldRenderSettings 0x534fb0, GameSootSettings 0x535260, OvergrowthSettings 0x5352e0, OcclusionSettings 0x535360, DecalSettings 0x5353e0, EmitterSystemSettings 0x535460, IrradianceVolumeSettings 0x5354e0, StaticCullSettings 0x535560`, plus `Dx9DisplaySettings 0x5bba10, Dx10DisplaySettings 0x5bb6f0, Dx11DisplaySettings 0x5bbb10, GameRenderSettings 0x5be550, VisualTerrainSettings 0xa00080, WeatherSystemSettings 0x87f720, MeshStreamingSettings 0x96a050, GlobalPostProcessSettings 0x98a460, ShaderSystemSettings 0x98be60, ResourceManagerSettings 0x42a8b0`.

INFERENCE: these are the 'global settings' classes. In Frostbite these are registered under a short name (class name without `Settings`) and each field is settable by command line / console as `Name.Field`. So `ServerSettings.Level` is most plausibly `Server.Level` and `ClientSettings.Kit` is `Client.Kit`. The arg3 stub at `0x534600` etc. is very likely the per-class create/registration thunk that contains the registered name string and the default values. Reading the bytes at `0x534600`, `0x534680` in the exe (disassemble) will confirm the exact names and defaults; that confirmation has not been done and is required before relying on any command-line name.

## 2. Route B: skip menus, movies, cutscenes, loading gates

Settings toggles (already tabled above, repeated here as a checklist; offsets from the dump):

| Type.field | off | why |
|---|---|---|
| ClientSettings.LoadMenu | `0xfa` | whether the frontend menu loads at startup |
| ClientSettings.DisableMenuFlow | `0xf9` | disables the menu flow state machine |
| ClientSettings.LimitedMainMenu | `0xba` | reduced main menu |
| ClientSettings.BypassMatchmaking | `0xea` | skip matchmaking |
| ClientSettings.IsFastStartupEnabled | `0xf0` | fast startup path (client) |
| ClientSettings.DisableMovies | `0xf1` | disable intro/loading/cut Bink movies |
| ClientSettings.DisableHudMovies | `0xf2` | disable HUD swf/movie layer |
| ClientSettings.IsHudDisabled | `0xf3` | no HUD |
| ClientSettings.PauseGameOnStartUp | `0xd2` | must be false for unattended start |
| ClientSettings.IsMenuDevOptsEnabled | `0xf4` | dev options in menu |
| ClientSettings.DebugMenuOnLThumb | `0xe9` | debug menu on left thumb click |
| ClientSettings.CheckDeviceEntitlements | `0xbe` | entitlement check |
| ServerSettings.IsFastStartupEnabled | `0x9f` | fast startup path (server) |
| ServerSettings.DisableCutscenes | `0xb5` | server never runs cutscene entities |
| ServerSettings.DisableCheckpoints | `0x8e` | no checkpoint saves |
| ServerSettings.LoadSavePoint | `0xb8` | paired with SavePoint string |
| ServerSettings.SavePoint | `0x70` | String, start from named checkpoint |
| GameSettings.FastStartupEnable | `0x98` | game-wide fast startup |
| NetworkSettings.SkipEntitlementCheck | `0x9c` | skip online entitlement gate |
| ServerSettings.GameConfigurationTimeout | `0x60` | float timeout while configuring game |
| ServerSettings.LoadingTimeout | `0x6c` | float |
| ClientSettings.LoadingTimeout | `0x28` | float |
| ClientSettings.LoadedTimeout | `0x7c` | float |

Data-side gates that live in the level graph (editable only by patching loaded data or the level, not by startup args):

- **class `CutsceneEntityData`** size `0xd0`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. IsSkippable (@0xcd) true means player can skip; ScreenFade (ptr) holds Pre/PostCutscene fade times; AfterCutsceneMenuId/CutsceneMenuId are MenuIdEnum; CameraEntitySpace; Sequence; Subtitle.
  - fields: `SoundLength`@0x80:Float32; `Radius`@0x84:Float32; `Sequence`@0x88:SequenceData; `AfterCutsceneMenuId`@0x8c:MenuIdEnum; `CutsceneMenuId`@0x90:MenuIdEnum; `Subtitle`@0x94:SubtitleDataTree; `ScreenFade`@0x98:CutsceneEntityScreenFadeData; `Camera`@0x9c:CutsceneEntityCameraData; `CameraEntitySpace`@0xa0:LogicEntityData; `SoundTemplate`@0xa4:String; `LocalizedSound`@0xb0:ArrayBase<WaveAsset>; `Sound`@0xc4:WaveAsset; `FreezeOnFinish`@0xc8:Boolean; `AllowAllAnimationsHeaps`@0xc9:Boolean; `MuteVoiceOver`@0xca:Boolean; `NormalizedTimeEnabled`@0xcb:Boolean; `MuteGameSound`@0xcc:Boolean; `IsSkippable`@0xcd:Boolean; `KeepGameSoundMuted`@0xce:Boolean; `ForceMuteSoundSetting`@0xcf:Boolean
- **class `CutsceneEntityScreenFadeData`** size `0x28`, base: -. fade times (set to 0 to avoid fades).
  - fields: `PostCutsceneFadeOutTime`@0xc:Float32; `PostCutsceneMinBlackDelay`@0x10:Float32; `PostCutsceneFadeInTime`@0x14:Float32; `StartPostFadeOutBeforeEndTime`@0x18:Float32; `PreCutsceneFadeOutTime`@0x1c:Float32; `PreCutsceneMinBlackDelay`@0x20:Float32; `PreCutsceneFadeInTime`@0x24:Float32
- **class `ScreenFadeEntityData`** size `0x90`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. FadeTime, WaitOnBlackForStreaming (@0x87).
  - fields: `FadeTime`@0x80:Float32; `FadeUI`@0x84:Boolean; `FadeInGameSound`@0x85:Boolean; `ControlledByClient`@0x86:Boolean; `WaitOnBlackForStreaming`@0x87:Boolean
- **class `ContinueEntityData`** size `0xb0`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. Used when continuing from a checkpoint: FadeToBlack/StayBlack/FadeFromBlack times, WeaponAmmo (ContinueWeaponAmmoData{WeaponSlot,MinMags}), Soldier/VehicleImmortalTimer.
  - fields: `FadeToBlack`@0x80:Float32; `StayBlack`@0x84:Float32; `FadeFromBlack`@0x88:Float32; `WeaponAmmo`@0x8c:ArrayBase<ContinueWeaponAmmoData>; `VehicleImmortalTimer`@0x9c:Float32; `SoldierImmortalTimer`@0xa0:Float32; `LookAtNearestMapMarker`@0xa4:Boolean
- **class `PlayVideoEntityData`** size `0x90`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. Plays a Bink movie in the level (BinkMovie, KeepBlackScreen).
  - fields: `BinkMovie`@0x80:BinkTextureAsset; `KeepBlackScreen`@0x84:Boolean
- **class `MenuEntityData`** size `0xa0`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. MenuId (@0x98) drives which in-game menu shows; BodySid/HeaderSid.
  - fields: `BodySid`@0x80:String; `HeaderSid`@0x8c:String; `MenuId`@0x98:MenuIdEnum
**enum `MenuIdEnum`** (size 4): `MieSpawnMenu`=2, `MiePlaying`=4, `MieSpectate`=5, `MieKilledMenu`=6, `MieZoomMinimap`=8, `MiePreSpawn`=9, `MiePostSpawn`=10, `MieDeath`=11, `MieVictoryScreen`=12, `MieLetterBox`=13, `MieMovie`=14, `MieMissionFailure`=15, `MieVoid`=16, `MieDefault`=17, `MieCreateSoldier`=18, `MiePlayingNoMinimap`=19, `MieLastMenuId`=20
  - Why: Menu states: MieSpawnMenu=2, MiePlaying=4, MiePreSpawn=9, MiePostSpawn=10, MieCreateSoldier=18, MieLetterBox=13, MieMovie=14, MieMissionFailure=15, MieVictoryScreen=12. Gating state names for a menu/cinematic detector.

- **class `MenuConfigAsset`** size `0x174`, base: Asset. IntroMovies/IntroMovie/IntroMovieMultiplayer (IntroMovieData{Movies}), GameOverMovies, PreGameOverMovie, Credits, AttractionMovie (idle attract movie), AttractionIdleTime. Emptying these arrays skips intro movies. [INFERENCE]
  - fields: `PreGameOverMovie`@0x18:UIMovieAsset; `GameOverMovies`@0x1c:ArrayBase<UIMovieAsset>; `MenuTransitionConfig`@0x30:UIMenuTransitionConfig; `IntroMovies`@0x54:ArrayBase<UIMovieAsset>; `AwardGrid`@0x68:UIAwardGridData; `VehicleStatsData`@0x88:UIVehicleStatsData; `BackgroundMusic`@0xa8:WaveAsset; `Eulas`@0xac:ArrayBase<EulaTexture>; `ExtraMovies`@0xbc:ExtraMoviesConfig; `Credits`@0x11c:CreditsData; `AttractionIdleTime`@0x144:Float32; `IntroMovieMultiplayer`@0x148:IntroMovieData; `IntroMovie`@0x15c:IntroMovieData; `AttractionMovie`@0x170:UIMovieAsset
- **class `UIMenuConfig`** size `0x10`, base: -. DefaultScene (@0xc) Boolean; with UIConfig{Hud,Menu} ptrs.
  - fields: `DefaultScene`@0xc:Boolean
- **class `UIConfig`** size `0x14`, base: -. top UI config: Hud (UIHudConfig ptr), Menu (UIMenuConfig ptr).
  - fields: `Hud`@0xc:UIHudConfig; `Menu`@0x10:UIMenuConfig
- **class `InputRestrictionEntityData`** size `0xa0`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. 32 Boolean per-action input blocks (Fire, Reload, IngameMenu, ...); logic entity that disables input (for cutscenes/tutorials); a harness must know a restriction may be active.
  - fields: `IngameMenu`@0x80:Boolean; `Sprint`@0x81:Boolean; `SwitchPrimaryWeapon`@0x82:Boolean; `ScoreboardMenu`@0x83:Boolean; `ToggleParachute`@0x84:Boolean; `ChangePose`@0x85:Boolean; `ToggleCamera`@0x86:Boolean; `Reload`@0x87:Boolean; `MeleeAttack`@0x88:Boolean; `DynamicGadget2`@0x89:Boolean; `Order`@0x8a:Boolean; `ThrowGrenade`@0x8b:Boolean; `StaticGadget`@0x8c:Boolean; `GrenadeLauncher`@0x8d:Boolean; `DynamicGadget1`@0x8e:Boolean; `SnapView`@0x8f:Boolean; `Yaw`@0x90:Boolean; `HandBrake`@0x91:Boolean; `Roll`@0x92:Boolean; `Pitch`@0x93:Boolean; `Throttle`@0x94:Boolean; `OverridePreviousInputRestriction`@0x95:Boolean; `Brake`@0x96:Boolean; `Strafe`@0x97:Boolean; `Jump`@0x98:Boolean; `Zoom`@0x99:Boolean; `ChangeEntry`@0x9a:Boolean; `ChangeVehicle`@0x9b:Boolean; `FireCountermeasure`@0x9c:Boolean; `Fire`@0x9d:Boolean; `CycleRadioChannel`@0x9e:Boolean; `AltFire`@0x9f:Boolean
- **class `MissionEntityData`** size `0xc0`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. NextLevelToRun String (@0x8c), TransferWeaponsToNextLevel, RollCreditsOnSuccess, Briefing/Success/FailureSid: mission-end flow that would chain levels/credits.
  - fields: `FailureSid`@0x80:String; `NextLevelToRun`@0x8c:String; `BriefingSid`@0x98:String; `SuccessSid`@0xa4:String; `RollCreditsOnSuccess`@0xb0:Boolean; `TransferWeaponsToNextLevel`@0xb1:Boolean
- **class `SaveEntityData`** size `0xb0`, base: LogicUtilEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. checkpoint-save logic entity: LevelName String (@0xa0), SaveFileName, SaveDelayTime.
  - fields: `SaveNameSID`@0x80:String; `SaveDelayTime`@0x8c:Float32; `SaveFileName`@0x90:String; `SaveScreenTexture`@0x9c:TextureAsset; `LevelName`@0xa0:String
- **class `PreRoundEntityData`** size `0x90`, base: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData. MP pre-round wait: PlayerCountNeeded (@0x84), NotEnoughPlayersRestartTimer; ServerSettings.PlayerCountNeededForMultiplayer relates.
  - fields: `NotEnoughPlayersRestartTimer`@0x80:Float32; `PlayerCountNeeded`@0x84:Uint32

## 3. Route C: spawn the player

Settings: `ServerSettings.InstantSpawn` @0x93, `RespawnOnDeathPosition` @0xc6, `AutokillSoldiers` @0x99, `PlayerCountNeededForMultiplayer` @0x34, `IsGodMode` @0x95, `IsJesusMode` @0xaa, `IsAiEnabled` @0xc5; `ClientSettings.SpawnPoint` String @0x50, `Team` u32 @0x1c, `Kit` u32 @0x18, `IsMultiplayer` @0xd5; `GameSettings.UseCustomKit` @0x91.

**class `SpawnEntityData`** size `0xa0`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Base spawn data entity: Team (TeamId), Amount, SpawnDelay, Immortal, FakeImmortal, AllowMulipleSpawn.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | SpawnDelay | Float32 | primitive |
| `0x84` | InitializedSpawnDelay | Float32 | primitive |
| `0x88` | Team | TeamId | enum |
| `0x8c` | Amount | Uint32 | primitive |
| `0x90` | FakeImmortal | Boolean | primitive |
| `0x91` | Immortal | Boolean | primitive |
| `0x92` | AllowMulipleSpawn | Boolean | primitive |

**class `SoldierSpawnEntityData`** size `0x120`, base chain: SoldierSpawnBaseEntityData -> SpawnEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: The soldier spawn point/definition. PlayerType (SoldierSpawnInclude: SSHumanPlayer=0 / SSAiPlayer=1) decides whether it is a human-player spawn; SoldierKit (SoldierAsset ptr @0x104) is the kit spawned; Template (SoldierSpawnTemplateData ptr @0x110) for AI kits/appearance; Squad, Vehicle, AlternativeSpawnPoints (ArrayBase<AlternateSpawnEntityData>), UseAsAlternateSpawnPoint (@0x117), SpawnSoldierFromBeginning, TryToSpawnAwayFromSight. Runtime class `ServerSoldierSpawnEntity` (size 0xE8) has no field table.

| off | field | type | kind |
|---|---|---|---|
| `0xa0` | LodDistance | Float32 | primitive |
| `0xa4` | HumanTargetPreference | Float32 | primitive |
| `0xa8` | VehiclePos | Uint32 | primitive |
| `0xac` | LocationNameSid | String | primitive |
| `0xb8` | SpawnEnemyRadius | Float32 | primitive |
| `0xbc` | SoldierSquadId | SquadId | enum |
| `0xc0` | OnSpottedType | SoldierOnSpottedType | enum |
| `0xc4` | SpawnToCheck | Uint32 | primitive |
| `0xc8` | Vehicle | ArrayBase<VehicleSpawnEntityData> | array |
| `0xdc` | AlternativeSpawnPoints | ArrayBase<AlternateSpawnEntityData> | array |
| `0xf0` | Squad | ArrayBase<SquadEntityData> | array |
| `0x104` | SoldierKit | SoldierAsset | ref |
| `0x108` | PlayerType | SoldierSpawnInclude | enum |
| `0x10c` | MeshShaderSetNumber | Uint32 | primitive |
| `0x110` | Template | SoldierSpawnTemplateData | ref |
| `0x114` | TryToSpawnAwayFromSight | Boolean | primitive |
| `0x115` | SoldierCreationPoint | Boolean | primitive |
| `0x116` | SpawnSoldierFromBeginning | Boolean | primitive |
| `0x117` | UseAsAlternateSpawnPoint | Boolean | primitive |

**class `SoldierSpawnBaseEntityData`** size `0xa0`, base chain: SpawnEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - (no own fields in the dump)

**class `AlternateSpawnEntityData`** size `0x80`, base chain: EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Priority float + Transform (inherited @0x30) = extra spawn position.

| off | field | type | kind |
|---|---|---|---|
| `0x70` | Priority | Float32 | primitive |

**enum `SoldierSpawnInclude`** (size 4): `SSHumanPlayer`=0, `SSAiPlayer`=1

**enum `SoldierOnSpottedType`** (size 4): `OSPlayer`=0, `OSEnemyBotsOnly`=1, `OSPlayerAndEnemyBots`=2

**class `SpawnPointManagerData`** size `0x1c`, base chain: Asset -> DataContainer(unresolved)
  - Why: Referenced by LevelData.SpawnPointManager; UseAiLodSystem. No spawn point list here (lists live in the entity graph).

| off | field | type | kind |
|---|---|---|---|
| `0x18` | UseAiLodSystem | Boolean | primitive |

**class `EndOfRoundSoldierSpawnEntityData`** size `0x90`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x80` | Team | TeamId | enum |
| `0x84` | Rank | Uint32 | primitive |

**class `HumanPlayerEntityData`** size `0xa0`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Logic data entity for the human player: DisplayTime, LastManStandingSid, PlayerKilledDelay.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | DisplayTime | Float32 | primitive |
| `0x84` | LastManStandingSid | String | primitive |
| `0x90` | PlayerKilledDelay | Float32 | primitive |

**enum `TeamId`** (size 4): `TeamNeutral`=0, `Team1`=1, `Team2`=2, `Team3`=3, `Team4`=4, `Team5`=5, `Team6`=6, `Team7`=7, `Team8`=8, `Team9`=9, `Team10`=10, `Team11`=11, `Team12`=12, `Team13`=13, `Team14`=14, `Team15`=15, `Team16`=16, `TeamIdCount`=17

**enum `SquadId`** (size 4): `Squad1`=0, `Squad2`=1, `Squad3`=2, `Squad4`=3, `Squad5`=4, `Squad6`=5, `Squad7`=6, `Squad8`=7, `Squad9`=8, `Squad10`=9, `Squad11`=10, `Squad12`=11, `Squad13`=12, `Squad14`=13, `Squad15`=14, `Squad16`=15, `Squad17`=16, `Squad18`=17, `Squad19`=18, `Squad20`=19, `Squad21`=20, `Squad22`=21, `Squad23`=22, `Squad24`=23, `SquadNone`=24, `SquadIdCount`=25

**enum `FactionId`** (size 4): `FactionNeutral`=0, `FactionUS`=1, `FactionRUS`=2, `FactionMEC`=3, `FactionIdCount`=4

**enum `TeamAssignMode`** (size 4): `TamEqualTeams`=0, `TamFullTeams`=1

**class `AutoTeamEntityData`** size `0x90`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Auto team balance (TeamAssignMode); ForceIntoSquad.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | TeamDifferenceToAutoBalance | Uint32 | primitive |
| `0x84` | PlayerCountNeededToAutoBalance | Uint32 | primitive |
| `0x88` | TeamAssignMode | TeamAssignMode | enum |
| `0x8c` | RotateTeamOnNewRound | Boolean | primitive |
| `0x8d` | ForceIntoSquad | Boolean | primitive |

**class `SpectateEntityData`** size `0xb0`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Spectate camera/mode; relevant if no soldier is spawned.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | VehicleCameraIndex | Uint32 | primitive |
| `0x84` | CapturePointCameraIndex | Uint32 | primitive |
| `0x88` | AirVehicleCameraIndex | Uint32 | primitive |
| `0x8c` | SoldierCameraIndex | Uint32 | primitive |
| `0x90` | Mode | SpecateMode | enum |
| `0x94` | Target | SpecateTarget | enum |
| `0x98` | Cameras | ArrayBase<TargetCameraData> | array |
| `0xac` | UseInHardcoreMode | Boolean | primitive |
| `0xad` | ShouldTargetControllable | Boolean | primitive |

**class `PlayerEntryComponentData`** size `0x100`, base chain: EntryComponentData -> ComponentData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: The player soldier entry component (inherits EntryComponentData: EntryClass @0xb8, InputMapping @0xc0, InputConceptDefinition @0xb4).

| off | field | type | kind |
|---|---|---|---|
| `0xf0` | AnimTree | AnimTreeDataAsset | ref |

Runtime classes with sizes only (FACT, no members): `ServerSoldierEntity` 976, `ClientSoldierEntity` 1600 (flags 0x2035, 'other'), `ServerSoldierSpawnEntity` 232, `ClientSoldierSpawnEntity` 108, `ServerHumanPlayerEntity` 72, `ClientHumanPlayerEntity` 44, `ServerPlayerEntryComponent` 304, `ClientPlayerEntryComponent` 560, `ServerSoldierEntryComponent` 320, `ServerEntryComponent` 288, `ServerWeaponComponent` 224, `ClientWeaponComponent` 256, `ServerWeaponInputRouterComponent` 80, `ServerPickupEntity` 384, `ClientPickupEntity` 240 (other), `ServerSoldierGripComponent` 44.

## 4. Route D: kit and weapon override

Settings: `ServerSettings.UnrestrictedUnlocks` @0xbe and `ClientSettings.UnrestrictedUnlocks` @0xc8 (bypass unlock gating), `ClientSettings.Kit` u32 @0x18, `GameSettings.UseCustomKit` @0x91, `ServerSettings.HasUnlimitedAmmo` @0xc0, `HasUnlimitedMags` @0xc9.

**class `SoldierAsset`** size `0x54`, base chain: ObjectBlueprint -> EntityBusData -> Editable -> Asset -> DataContainer(unresolved)
  - Why: Kit = SoldierAsset (blueprint) whose `Object` (inherited ptr @0x50) is a SoldierEntityData. Own fields none (fieldsPtr 0).
  - (no own fields in the dump)

**class `SoldierEntityData`** size `0x230`, base chain: ControllableEntityData -> GamePhysicsEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Kit definition. Weapons (@0x1b0, ArrayBase<SoldierWeaponAsset>) = the weapon list, WeaponUnlocks (@0x1a0, ArrayBase<SoldierWeaponUnlockData{WeaponSlot,Weapon,IsBundledWithSelectable,IsSelectable}>) = unlockable alternatives per slot, WeaponSwitching (@0x180) = switch map, WeaponDataOverride (@0x19c) = damage overrides, Category (@0x120 KitCategory), Specializations (@0xf0), MaxHealth/RegenerateHealth/ImmortalTimeAfterSpawn, Meshes1p/Meshes3p (@0x1c4/@0x1d4), AnimTree1p/3p, DeathPickup.

| off | field | type | kind |
|---|---|---|---|
| `0xc0` | CriticalHealthJesusModeTime | Float32 | primitive |
| `0xc4` | ImmortalTimeAfterSpawn | Float32 | primitive |
| `0xc8` | RegenerateHealth | Float32 | primitive |
| `0xcc` | WaterDamage | Float32 | primitive |
| `0xd0` | CameraStrafeTiltStrength | Float32 | primitive |
| `0xd4` | ReviveTime | Float32 | primitive |
| `0xd8` | SoldierOnFireHitsWater | EffectAsset | ref |
| `0xdc` | FireBulletHitEffect | EffectAsset | ref |
| `0xe0` | DeathBulletHitEffect | EffectAsset | ref |
| `0xe4` | RegenGracePeriod | Float32 | primitive |
| `0xe8` | MaxHealth | Float32 | primitive |
| `0xec` | ImpulseReactionTime | Float32 | primitive |
| `0xf0` | Specializations | ArrayBase<PersistentSpecialization> | array |
| `0x104` | ValidRoadkillTime | Float32 | primitive |
| `0x108` | NeedMoreAmmoPercentage | Float32 | primitive |
| `0x10c` | Licenses | String | primitive |
| `0x118` | MeshShaderSetNumberOverride | Int32 | primitive |
| `0x11c` | Bucket | ScoringBucket | enum |
| `0x120` | Category | KitCategory | enum |
| `0x124` | TimeForCorpse | Float32 | primitive |
| `0x128` | BelowWaterDamageDelay | Float32 | primitive |
| `0x12c` | NeedMoreHealthPercentage | Float32 | primitive |
| `0x130` | CombatReadinessCooldown | Float32 | primitive |
| `0x134` | VOTypes | ArrayBase<VOPerson> | array |
| `0x148` | Cameras | ArrayBase<TargetCameraData> | array |
| `0x15c` | FakeCollisionData | FakeAnimatedSoldierCollisionData | ref |
| `0x160` | BoneFakePhysics | ArrayBase<BoneFakePhysicsData> | array |
| `0x174` | HeadCollision | SoldierHeadCollisionData | ref |
| `0x178` | AimingConstraints | SoldierAimingConstraintsData | ref |
| `0x17c` | AutoAim | SoldierAutoAimData | ref |
| `0x180` | WeaponSwitching | WeaponSwitchingData | ref |
| `0x184` | HeadMaterial | String | primitive |
| `0x190` | FootMaterial | String | primitive |
| `0x19c` | WeaponDataOverride | SoldierWeaponOverrideData | ref |
| `0x1a0` | WeaponUnlocks | ArrayBase<SoldierWeaponUnlockData> | array |
| `0x1b0` | Weapons | ArrayBase<SoldierWeaponAsset> | array |
| `0x1c4` | Meshes1p | ArrayBase<SoldierMeshData> | array |
| `0x1d4` | Meshes3p | ArrayBase<SoldierMeshData> | array |
| `0x1e4` | StreamGroup1p | StreamGroupData | ref |
| `0x1e8` | AnimTree3p | SoldierAnimationTreeData | ref |
| `0x1ec` | DeathPickup | PickupEntityData | ref |
| `0x1f0` | Sound | SoldierSoundData | ref |
| `0x1f4` | HidableMeshParts | ArrayBase<HidableSoldierMeshWeaponPart> | array |
| `0x204` | ParachuteData | SoldierParachuteData | ref |
| `0x208` | CharacterPhysics | CharacterPhysicsData | ref |
| `0x20c` | SprintSettings | SoldierSprintSettingsData | ref |
| `0x210` | RagdollSettings | SoldierRagdollSettingsData | ref |
| `0x214` | AnimTree1p | SoldierAnimationTreeData | ref |
| `0x218` | ShareClientAnimationWithServer | Boolean | primitive |
| `0x219` | LowerGunOnOwnTeam | Boolean | primitive |
| `0x21a` | LockAimToMeleeAttacker | Boolean | primitive |
| `0x21b` | ProximityCheck | Boolean | primitive |
| `0x21c` | EnableGroundmapLighting | Boolean | primitive |

**class `SoldierWeaponAsset`** size `0x58`, base chain: ObjectBlueprint -> EntityBusData -> Editable -> Asset -> DataContainer(unresolved)
  - Why: Weapon blueprint; `Object` ptr @0x50 -> SoldierWeaponData.
  - (no own fields in the dump)

**class `SoldierWeaponData`** size `0x130`, base chain: GameObjectData -> DataContainer(unresolved)
  - Why: The weapon definition. WeaponClass @0x84 (WeaponClassEnum), WeaponStates @0x88, WeaponFiring @0x98 (ptr WeaponFiringData), Persistence @0x64 (PersistentWeapon ptr), Specializations @0x68, WeaponAssetName String @0x40, PickupSettings @0x100, Hud @0xa0 inline (HudData.InfiniteAmmo at absolute `0xee`), flags SwitchToPrimaryWhenOutOfAmmo/AllowSwitchingToWeaponOutOfAmmo/LoadOnDemand. Repo-proven anchor.

| off | field | type | kind |
|---|---|---|---|
| `0x30` | ZoomRenderFov | Float32 | primitive |
| `0x34` | AltDeployId | Int32 | primitive |
| `0x38` | AnimBaseSet | WeaponAnimBaseSetEnum | enum |
| `0x3c` | RenderFov | Float32 | primitive |
| `0x40` | WeaponAssetName | String | primitive |
| `0x4c` | SpecializationWeapons | ArrayBase<SoldierWeaponAsset> | array |
| `0x60` | MeshShaderSetNumberOverride | Int32 | primitive |
| `0x64` | Persistence | PersistentWeapon | ref |
| `0x68` | Specializations | ArrayBase<WeaponSpecializationLink> | array |
| `0x78` | StreamGroup1p | StreamGroupData | ref |
| `0x7c` | Effects | WeaponEffectsData | ref |
| `0x80` | FirstPersonCamera | FirstPersonCameraData | ref |
| `0x84` | WeaponClass | WeaponClassEnum | enum |
| `0x88` | WeaponStates | ArrayBase<WeaponStateData> | array |
| `0x98` | WeaponFiring | WeaponFiringData | ref |
| `0x9c` | AimingController | SoldierAimingSimulationData | ref |
| `0xa0` | Hud | HudData | inline |
| `0xf0` | AIData | WeaponData | ref |
| `0x100` | PickupSettings | PickupSettingsData | inline |
| `0x120` | LowerOnOwnTeam | Boolean | primitive |
| `0x121` | HideWhenOutOfAmmo | Boolean | primitive |
| `0x122` | SwitchToPrimaryWhenOutOfAmmo | Boolean | primitive |
| `0x123` | SwitchToPreviousWeaponAfterFire | Boolean | primitive |
| `0x124` | AllowSwitchingToWeaponOutOfAmmo | Boolean | primitive |
| `0x125` | LoadOnDemand | Boolean | primitive |

**enum `WeaponClassEnum`** (size 4): `wcAssault`=0, `wcShotgun`=1, `wcSmg`=2, `wcLmg`=3, `wcSniper`=4, `wcUgl`=5, `wcHgr`=6, `wcAt`=7, `wcATMine`=8, `wcAutoInjector`=9, `wcC4`=10, `wcKnife`=11, `wcLaserDesignator`=12, `wcMedKit`=13, `wcMortarStrike`=14, `wcMotionSensor`=15, `wcPowerTool`=16, `wcTracerDart`=17, `wcAmmoBox`=18, `wcCount`=19, `wcNone`=20
  - Why: Repo-proven. 20 = wcNone (no weapon). Class ordering for choosing slots.

**enum `WeaponSwitchingEnum`** (size 4): `wsPrimary1`=0, `wsPrimary2`=1, `wsGrenadeLauncher1`=2, `wsGrenadeLauncher2`=3, `wsStaticGadget`=4, `wsDynamicGadget1`=5, `wsDynamicGadget2`=6, `wsMeleeWeapon`=7, `wsHandGrenade`=8, `wsLastPrimary`=9, `wsOtherPrimary`=10
  - Why: Slot names: wsPrimary1, wsPrimary2, wsGrenadeLauncher1/2, wsStaticGadget, wsDynamicGadget1/2, wsMeleeWeapon, wsHandGrenade, wsLastPrimary, wsOtherPrimary. These are the kit slot identities.

**class `WeaponSwitchingData`** size `0x20`, base chain: none (root/value type) -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0xc` | SwitchToGadgetCooldownTime | Float32 | primitive |
| `0x10` | SwitchMap | ArrayBase<WeaponSwitchingMapData> | array |

**valuetype `WeaponSwitchingMapData`** size `0x18`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | ToWeapon | ArrayBase<WeaponSwitchingEnum> | array |
| `0x10` | Action | EntryInputActionEnum | enum |
| `0x14` | FromWeapon | WeaponSwitchingEnum | enum |

**valuetype `SoldierWeaponUnlockData`** size `0xc`, base chain: none (root/value type)
  - Why: Per-slot weapon unlock entries: WeaponSlot u32 + Weapon ptr + IsSelectable.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | WeaponSlot | Uint32 | primitive |
| `0x4` | Weapon | SoldierWeaponAsset | ref |
| `0x8` | IsBundledWithSelectable | Boolean | primitive |
| `0x9` | IsSelectable | Boolean | primitive |

**valuetype `WeaponSpecializationLink`** size `0x8`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | WeaponAsset | SoldierWeaponAsset | ref |
| `0x4` | Specialization | PersistentSpecialization | ref |

**enum `KitCategory`** (size 4): `KCAssault`=0, `KCDemolition`=1, `KCRecon`=2, `KCSpecialist`=3, `KCSupport`=4, `KCRecruit`=5, `KCRifleman`=6, `KCAntitank`=7, `KCMachinegunner`=8, `KCSpecialforces`=9, `KCSniper`=10, `KCFieldworks`=11, `KCCombatmedic`=12, `KCUnknown`=13, `KCUCount`=14

**class `KitPickupEntityData`** size `0xb0`, base chain: PickupEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: A pickup that swaps the soldier's kit: Kit (SoldierAsset ptr @0xa0), KeepAmmoState, KeepAdditionalWeapons. Pickup route: spawn this entity and have the player interact.

| off | field | type | kind |
|---|---|---|---|
| `0xa0` | Kit | SoldierAsset | ref |
| `0xa4` | KeepAmmoState | Boolean | primitive |
| `0xa5` | KeepAdditionalWeapons | Boolean | primitive |

**class `WeaponPickupEntityData`** size `0xc0`, base chain: PickupEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Weapons ArrayBase<WeaponPickupData> @0xa4; WeaponPickupData{Weapon SoldierWeaponAsset ptr @0xc, WeaponSlot u32 @0x10, AltWeaponSlot @0x14, LinkedToWeaponSlot @0x0, MinAmmo/MaxAmmo}. UseForPersistence @0xb4. Pickup route to give an arbitrary weapon into a slot.

| off | field | type | kind |
|---|---|---|---|
| `0xa0` | Soldier | SoldierSpawnBaseEntityData | ref |
| `0xa4` | Weapons | ArrayBase<WeaponPickupData> | array |
| `0xb4` | UseForPersistence | Boolean | primitive |

**class `DynamicWeaponPickupEntityData`** size `0xc0`, base chain: PickupEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Dropped-weapon pickup (WeaponSlots, KeepAmmoState).

| off | field | type | kind |
|---|---|---|---|
| `0xa0` | WeaponSlots | ArrayBase<DynamicWeaponPickupSlotData> | array |
| `0xb0` | KeepAmmoState | Boolean | primitive |

**class `PickupEntityData`** size `0xa0`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Pickup base: InteractionRadius, UnspawnOnPickup, HasAutomaticAmmoPickup (@0x94, auto pickup without interaction), ForceWeaponSlotSelection, UseHudSelection.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | InteractionRadius | Float32 | primitive |
| `0x84` | TimeToLive | Float32 | primitive |
| `0x88` | Mesh | MeshAsset | ref |
| `0x8c` | MeshShaderSet | MeshShaderSetAsset | ref |
| `0x90` | ForceWeaponSlotSelection | Boolean | primitive |
| `0x91` | IgnoreNullWeaponSlots | Boolean | primitive |
| `0x92` | DisplayInMiniMap | Boolean | primitive |
| `0x93` | UseHudSelection | Boolean | primitive |
| `0x94` | HasAutomaticAmmoPickup | Boolean | primitive |
| `0x95` | UnspawnOnPickup | Boolean | primitive |
| `0x96` | UseWeaponMesh | Boolean | primitive |
| `0x97` | UseTeamShaderSet | Boolean | primitive |
| `0x98` | PositionIsStatic | Boolean | primitive |
| `0x99` | ContentIsStatic | Boolean | primitive |
| `0x9a` | UnspawnOnAmmoPickup | Boolean | primitive |

**valuetype `WeaponPickupData`** size `0x18`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | LinkedToWeaponSlot | Int32 | primitive |
| `0x4` | MinAmmo | Uint32 | primitive |
| `0x8` | MaxAmmo | Uint32 | primitive |
| `0xc` | Weapon | SoldierWeaponAsset | ref |
| `0x10` | WeaponSlot | Uint32 | primitive |
| `0x14` | AltWeaponSlot | Int32 | primitive |

**valuetype `PickupSettingsData`** size `0x20`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | MeshRenderRotation | Vec3 | primitive |
| `0x10` | MeshRenderOffset | Vec3 | primitive |

**class `SoldierSpawnTemplateData`** size `0x4c`, base chain: Asset -> DataContainer(unresolved)
  - Why: AI spawn template: AllowedKits (SoldierSpawnKitTemplateData{MainGun u32,Kit u32}), AllowedAppearances (Body u32), Rank, Behaviour, Sensing. u32 are indices **[INFERENCE]**.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Behaviour | BehaviourTemplateData | ref |
| `0x1c` | Rank | Uint32 | primitive |
| `0x20` | Sensing | SensingTemplateData | ref |
| `0x24` | AllowedKits | ArrayBase<SoldierSpawnKitTemplateData> | array |
| `0x38` | AllowedAppearances | ArrayBase<SoldierSpawnAppearanceTemplateData> | array |

**class `SoldierSpawnKitTemplateData`** size `0x20`, base chain: Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x18` | MainGun | Uint32 | primitive |
| `0x1c` | Kit | Uint32 | primitive |

**class `SoldierSpawnAppearanceTemplateData`** size `0x1c`, base chain: Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Body | Uint32 | primitive |

**class `TeamData`** size `0x38`, base chain: TeamAsset -> Asset -> DataContainer(unresolved)
  - Why: Kits (@0x20, ArrayBase<SoldierAsset>) = the kit list a team offers; ClientSettings.Kit u32 is plausibly an index into this **[INFERENCE]**. Faction, MaxPlayerCount.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | MaxPlayerCount | Uint32 | primitive |
| `0x1c` | MaxPlayersPerSquad | Uint32 | primitive |
| `0x20` | Kits | ArrayBase<SoldierAsset> | array |
| `0x34` | Faction | FactionId | enum |

**class `PersistentWeapon`** size `0x54`, base chain: Asset -> DataContainer(unresolved)
  - Why: Unlock record: Id String (@0x40), Slot u32 (@0x28), BitIndex (@0x4c: the profile bit), Cost, WeaponType, Dependency (prerequisite weapon), Licenses.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Licenses | String | primitive |
| `0x24` | Dependency | PersistentWeapon | ref |
| `0x28` | Slot | Uint32 | primitive |
| `0x2c` | AdditionalLicenses | String | primitive |
| `0x38` | Cost | Uint32 | primitive |
| `0x3c` | WeaponType | PersistentWeaponType | enum |
| `0x40` | Id | String | primitive |
| `0x4c` | BitIndex | Int32 | primitive |
| `0x50` | Collectible | Boolean | primitive |

**class `PersistentSpecialization`** size `0x64`, base chain: PersistentWeapon -> Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x54` | KitCategory | KitCategory | enum |
| `0x58` | WeaponClass | WeaponClassEnum | enum |
| `0x5c` | SpecializationType | PersistentSpecializationType | enum |
| `0x60` | Modifier | Float32 | primitive |

**class `PersistenceData`** size `0x78`, base chain: Asset -> DataContainer(unresolved)
  - Why: Unlocks (ArrayBase<PersistenceWeaponData>), UnlockTokens, Entitlements, SPAchievements, Partitions, Final, Remapping.

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Unlocks | ArrayBase<PersistenceWeaponData> | array |
| `0x2c` | UnlockTokens | UnlockTokenData | ref |
| `0x30` | Entitlements | EntitlementAsset | ref |
| `0x34` | SPAchievements | AchievementsDataAsset | ref |
| `0x38` | Partitions | ArrayBase<PersistentPartition> | array |
| `0x4c` | Final | ArrayBase<PersistentValueTemplateData> | array |
| `0x60` | Remapping | ArrayBase<PersistentValueNameRemapping> | array |
| `0x74` | Collectibles | Boolean | primitive |
| `0x75` | Consumables | Boolean | primitive |

**class `PersistenceWeaponData`** size `0x2c`, base chain: Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Weapons | ArrayBase<PersistentWeapon> | array |

**class `UnlockTokenData`** size `0x2c`, base chain: Asset -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x18` | Tokens | ArrayBase<UnlockToken> | array |

**valuetype `UIUnlockEntry`** size `0x3c`, base chain: none (root/value type)
  - Why: UI unlock tree (RequiredRank, RequiredAwardCode, WeaponName String, Persistence ptr).

| off | field | type | kind |
|---|---|---|---|
| `0x0` | RequiredAwardCode | String | primitive |
| `0xc` | RequiredAwardCount | Int32 | primitive |
| `0x10` | RequiredRank | Int32 | primitive |
| `0x14` | Children | ArrayBase<UIUnlockEntry> | array |
| `0x24` | Persistence | PersistentWeapon | ref |
| `0x28` | WeaponName | String | primitive |
| `0x34` | GroupPosition | UIGridPosition | inline |

**valuetype `UIWeaponData`** size `0x7c`, base chain: none (root/value type)
  - Why: UI-side weapon record: WeaponName String @0x3c, UnlockID String @0x30, Abbreviation, ClipSize, RateOfFire; the only place weapon name strings are structurally named (values are in bundles). Used by UISharedConfigAsset.WeaponData/UnlocksData.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | Damage | Float32 | primitive |
| `0x4` | Accuracy | Float32 | primitive |
| `0x8` | CollectiblesDescriptionSID | String | primitive |
| `0x14` | Handling | Float32 | primitive |
| `0x18` | ProficiencyStatSID | String | primitive |
| `0x24` | ClipSize | Float32 | primitive |
| `0x28` | RateOfFire | Float32 | primitive |
| `0x2c` | Texture | TextureAsset | ref |
| `0x30` | UnlockID | String | primitive |
| `0x3c` | WeaponName | String | primitive |
| `0x48` | NameSID | String | primitive |
| `0x54` | StatDescriptionSID | String | primitive |
| `0x60` | Abbreviation | String | primitive |
| `0x6c` | DescriptionSID | String | primitive |
| `0x78` | IsSpecialItem | Boolean | primitive |

**class `WeaponFiringData`** size `0x4c`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: PrimaryFire (FiringFunctionData ptr @0x40), DeployTime, UseAutoAiming.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | WeaponSway | WeaponSwayData | ref |
| `0x10` | SpottingDistance | Float32 | primitive |
| `0x14` | VOTypes | ArrayBase<VOWeapon> | array |
| `0x28` | AiDamageModifier | Float32 | primitive |
| `0x2c` | Rumble | RumbleFiringData | inline |
| `0x38` | DeployTime | Float32 | primitive |
| `0x3c` | ReactivateCooldownTime | Float32 | primitive |
| `0x40` | PrimaryFire | FiringFunctionData | ref |
| `0x44` | AbortReloadOnSprint | Boolean | primitive |
| `0x45` | AutoSpotOnZoom | Boolean | primitive |
| `0x46` | UseAutoAiming | Boolean | primitive |
| `0x47` | AutoSpotOnFire | Boolean | primitive |
| `0x48` | FireOnDeploy | Boolean | primitive |
| `0x49` | InflictSelfDamage | Boolean | primitive |

**class `FiringFunctionData`** size `0x1f0`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Ammo AmmoConfigData inline @`0x170`: NumberOfMagazines at absolute `0x180`, MagazineCapacity at `0x184`; UsePrimaryAmmo @0x1e6; UnlimitedAmmoForAI @0x1e7; FireLogic inline @0xc.

| off | field | type | kind |
|---|---|---|---|
| `0xc` | FireLogic | FireLogicData | inline |
| `0xa0` | Shot | ShotConfigData | inline |
| `0xf0` | ReflectionSound | SoundAsset | ref |
| `0xf4` | SelfHealTimeWhenDeployed | Float32 | primitive |
| `0x100` | OverHeat | OverHeatData | inline |
| `0x170` | Ammo | AmmoConfigData | inline |
| `0x194` | OutOfAmmoSoundEffect | SoundAsset | ref |
| `0x198` | FireEffects1p | ArrayBase<FireEffectData> | array |
| `0x1a8` | WeaponDispersion | SoldierWeaponDispersion | inline |
| `0x1bc` | Dispersion | ArrayBase<FiringDispersionData> | array |
| `0x1cc` | ProjectileFireSound | SoundAsset | ref |
| `0x1d0` | FireSoundEffect | EffectAsset | ref |
| `0x1d4` | FireEffects3p | ArrayBase<FireEffectData> | array |
| `0x1e4` | IsCounterMeasureWeapon | Boolean | primitive |
| `0x1e5` | LoopingFireSound | Boolean | primitive |
| `0x1e6` | UsePrimaryAmmo | Boolean | primitive |
| `0x1e7` | UnlimitedAmmoForAI | Boolean | primitive |

**valuetype `AmmoConfigData`** size `0x24`, base chain: none (root/value type)
  - Why: NumberOfMagazines @0x10, MagazineCapacity @0x14, AmmoPickupMaxAmount, AutoReplenishMagazine @0x20/AutoReplenishDelay.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | AutoReplenishDelay | Float32 | primitive |
| `0x4` | AmmoPickupMaxAmount | Uint32 | primitive |
| `0x8` | AmmoBoxPickupAmount | Int32 | primitive |
| `0xc` | AmmoBoxPickupRate | Uint32 | primitive |
| `0x10` | NumberOfMagazines | Int32 | primitive |
| `0x14` | MagazineCapacity | Int32 | primitive |
| `0x18` | TraceFrequency | Uint32 | primitive |
| `0x1c` | AmmoPickupMinAmount | Uint32 | primitive |
| `0x20` | AutoReplenishMagazine | Boolean | primitive |

**valuetype `AmmoResupplyData`** size `0x10`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | Delay | Float32 | primitive |
| `0x4` | UpdateFrequency | Uint32 | primitive |
| `0x8` | Radius | Float32 | primitive |
| `0xc` | Ammo | Uint32 | primitive |

**class `SoldierWeaponOverrideData`** size `0x20`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Per-kit weapon damage overrides: Overrides ArrayBase<WeaponOverrideData{Values: WeaponOverrideValue{Value,ValueType}, Data}> (WeaponOverrideValueType: BulletEntity StartDamage/EndDamage/Falloff...).

| off | field | type | kind |
|---|---|---|---|
| `0xc` | Overrides | ArrayBase<WeaponOverrideData> | array |

**class `WeaponOverrideData`** size `0x20`, base chain: none (root/value type) -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0xc` | Values | ArrayBase<WeaponOverrideValue> | array |
| `0x1c` | Data | <unresolved 0x01bf030c> | unresolved |

**valuetype `WeaponOverrideValue`** size `0x8`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | Value | Float32 | primitive |
| `0x4` | ValueType | WeaponOverrideValueType | enum |

**enum `WeaponOverrideValueType`** (size 4): `OverrideValue_BulletEntity_DistributeDamageOverTime`=0, `OverrideValue_BulletEntity_StartDamage`=1, `OverrideValue_BulletEntity_EndDamage`=2, `OverrideValue_BulletEntity_DamageFalloffStartDistance`=3, `OverrideValue_BulletEntity_DamageFalloffEndDistance`=4

**valuetype `ContinueWeaponAmmoData`** size `0x8`, base chain: none (root/value type)
  - Why: ContinueEntity: guarantee MinMags per slot on checkpoint continue.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | WeaponSlot | Int32 | primitive |
| `0x4` | MinMags | Int32 | primitive |

**valuetype `HudData`** size `0x50`, base chain: none (root/value type)
  - Why: valuetype inside SoldierWeaponData.Hud: InfiniteAmmo (@0x4e), IsSpecialItem, crosshair ids.

| off | field | type | kind |
|---|---|---|---|
| `0x0` | HintAction1 | EntryInputActionEnum | enum |
| `0x4` | WeaponClass | String | primitive |
| `0x10` | HintAction2 | EntryInputActionEnum | enum |
| `0x14` | HintLine2 | String | primitive |
| `0x20` | HintLine1 | String | primitive |
| `0x2c` | CrosshairTypeId | String | primitive |
| `0x38` | CrosshairScaleMax | Float32 | primitive |
| `0x3c` | CrosshairScaleMin | Float32 | primitive |
| `0x40` | CrosshairOpacityMin | Float32 | primitive |
| `0x44` | CrosshairOpacityModifier | Float32 | primitive |
| `0x48` | CrosshairOpacityMax | Float32 | primitive |
| `0x4c` | IsSpecialItem | Boolean | primitive |
| `0x4d` | HideCrosshairWhenAimOnFriend | Boolean | primitive |
| `0x4e` | InfiniteAmmo | Boolean | primitive |

**valuetype `SoldierWeaponDispersion`** size `0x14`, base chain: none (root/value type)

| off | field | type | kind |
|---|---|---|---|
| `0x0` | MoveZoomedDispersionAngle | Float32 | primitive |
| `0x4` | DecreasePerSecond | Float32 | primitive |
| `0x8` | MoveDispersionAngle | Float32 | primitive |
| `0xc` | JumpDispersionAngle | Float32 | primitive |
| `0x10` | ProneTransitionDispersionAngle | Float32 | primitive |


## 5. Route E: console, cheat, debug, test entities

FACT: there is no `Cheat*` type and no global console-command type. The cheat-like levers are the booleans in `ServerSettings` (`IsGodMode` @0x95, `IsJesusMode` @0xaa, `HasUnlimitedAmmo` @0xc0, `HasUnlimitedMags` @0xc9, `InstantSpawn` @0x93, `UnrestrictedUnlocks` @0xbe, `AutokillSoldiers` @0x99, `IsAiEnabled` @0xc5, `DisableCutscenes` @0xb5, `DisableCheckpoints` @0x8e) and `ClientSettings` (`UnrestrictedUnlocks` @0xc8, `IsMenuDevOptsEnabled` @0xf4, `DebugMenuOnLThumb` @0xe9, `UseScriptedInput` @0xc9, `EnableRestTool` @0xc6, `ShowBuildId` @0xc1). The in-level console route is the logic entity below.

**class `ConsoleCommandEntityData`** size `0x90`, base chain: TestEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Commands = ArrayBase<String> @0x80. A logic entity placed in a level that executes console command strings (runtime class `ServerConsoleCommandEntity`, 44 B, no members). If a level graph can be edited/extended, this is the generic 'run any console command' hook, including (INFERENCE) the Settings commands. TypeInfo VA 0x01c05efc.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | Commands | ArrayBase<String> | array |

**class `GameSettingEntityData`** size `0x90`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: ChangeSetting (ChangeGameSettingType: CGSNone=0, CGSInvertVerticalLookAxis=1). Narrow.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | ChangeSetting | ChangeGameSettingType | enum |

**enum `ChangeGameSettingType`** (size 4): `CGSNone`=0, `CGSInvertVerticalLookAxis`=1, `CGSCount`=2

**class `DebugMenuItemEntityData`** size `0xa0`, base chain: TestEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Debug-menu logic: Text, ItemType (DmitItem=0 / DmitToggle=1), Parent.

| off | field | type | kind |
|---|---|---|---|
| `0x80` | Text | String | primitive |
| `0x8c` | ItemType | DebugMenuItemType | enum |
| `0x90` | Parent | DebugMenuSubMenuEntityData | ref |

**class `DebugMenuSubMenuEntityData`** size `0x90`, base chain: TestEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x80` | Text | String | primitive |

**enum `DebugMenuItemType`** (size 4): `DmitItem`=0, `DmitToggle`=1

**class `TestEntityData`** size `0x80`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - Why: Base 'Test' entity family: DebugMenuItem/SubMenu, DebugText, ConsoleCommand, GameTest, ExplosionImpulseTest, ProjectileImpulseTest are all TestEntityData children; GameTestEntityData has TimeoutTime @0x80 (runtime ServerGameTestEntity 60 B). These are the dev/test harness entities Frostbite ships.
  - (no own fields in the dump)

**class `GameTestEntityData`** size `0x90`, base chain: TestEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x80` | TimeoutTime | Float32 | primitive |

**class `DebugTextEntityData`** size `0xa0`, base chain: TestEntityData -> GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x80` | TextColor | Vec3 | primitive |
| `0x90` | DebugText | String | primitive |
| `0x9c` | Centered | Boolean | primitive |

**class `DebugTestEntityData`** size `0x80`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - (no own fields in the dump)

**class `SquadDebugMenuEntityData`** size `0x80`, base chain: GameEntityData -> LogicEntityData -> EntityData -> GameObjectData -> DataContainer(unresolved)
  - (no own fields in the dump)

**enum `ImmortalType`** (size 4): `ItNone`=0, `ItGod`=1, `ItJesus`=2
  - Why: ItNone/ItGod/ItJesus (god mode semantics).

**class `DifficultyData`** size `0x80`, base chain: none (root/value type) -> DataContainer(unresolved)
  - Why: Name String @0x54 plus HumanHealthModifier/EnemiesHealthModifier/accuracy modifiers; ServerSettings.DifficultyMode String @0x20 selects difficulty by name **[INFERENCE]**.

| off | field | type | kind |
|---|---|---|---|
| `0x10` | SnapBoxModifier | Vec3 | primitive |
| `0x20` | StickyBoxModifier | Vec3 | primitive |
| `0x30` | HumanInCriticalHealthDamageModifier | Float32 | primitive |
| `0x34` | HumanInCriticalHealth | Float32 | primitive |
| `0x38` | VehicleDamageModifier | Float32 | primitive |
| `0x3c` | CriticalHealthJesusModeTimeModifier | Float32 | primitive |
| `0x40` | SuckZoomModifier | Float32 | primitive |
| `0x44` | HelicopterHoverTimeMultiplier | Float32 | primitive |
| `0x48` | ReadinessReactionTimeMultiplier | Float32 | primitive |
| `0x4c` | FriendsHealthModifier | Float32 | primitive |
| `0x50` | EnemiesHealthModifier | Float32 | primitive |
| `0x54` | Name | String | primitive |
| `0x60` | HumanHealthModifier | Float32 | primitive |
| `0x64` | AccuracyIncreaseTimeModifier | Float32 | primitive |
| `0x68` | FinalAccuracyModifierStanding | Float32 | primitive |
| `0x6c` | FinalAccuracyModifierCrouching | Float32 | primitive |
| `0x70` | StartAccuracyModifierStanding | Float32 | primitive |
| `0x74` | StartAccuracyModifierCrouching | Float32 | primitive |
| `0x78` | UsePitchZoomSnap | Boolean | primitive |

**class `AISettingsData`** size `0x9c`, base chain: Asset -> DataContainer(unresolved)
  - Why: AI behaviour constants root; DebugConstants ptr (DebugConstantData).

| off | field | type | kind |
|---|---|---|---|
| `0x18` | TimingConstants | TimingConstantData | ref |
| `0x1c` | LookConstants | LookConstantData | ref |
| `0x20` | DebugConstants | DebugConstantData | ref |
| `0x24` | EnvironmentConstants | EnvironmentConstantData | ref |
| `0x28` | FollowConstants | FollowConstantData | ref |
| `0x2c` | PositionEvaluationConstants | PositionEvaluationConstantData | ref |
| `0x30` | DefaultSensingTemplate | SensingTemplateData | ref |
| `0x34` | DefaultBehaviourTemplate | BehaviourTemplateData | ref |
| `0x38` | DefaultVehicleType | AIVehicleBehaviourData | ref |
| `0x3c` | KynapseConstants | KynapseConstantData | ref |
| `0x40` | TraversalConstants | TraversalConstantData | ref |
| `0x44` | ReadinessLevels | ReadinessLevels | inline |
| `0x68` | SearchAndDestroyConstants | SearchAndDestroyConstantData | ref |
| `0x6c` | NavigationConstants | NavigationConstantData | ref |
| `0x70` | SoldierControlConstants | SoldierControlConstantData | ref |
| `0x74` | CarControlConstants | CarControlConstantData | ref |
| `0x78` | SensingConstants | SensingConstantData | ref |
| `0x7c` | SoundEnvironmentConstants | SoundEnvironmentConstantData | ref |
| `0x80` | CoverConstants | CoverConstantData | ref |
| `0x84` | TargetEvaluationConstants | TargetEvaluationConstantData | ref |
| `0x88` | VOConstants | VOConstantData | ref |
| `0x8c` | CombatConstants | CombatConstantData | ref |
| `0x90` | TankControlConstants | TankControlConstantData | ref |
| `0x94` | HelicopterControlConstants | HelicopterControlConstantData | ref |
| `0x98` | TurretControlConstants | TurretControlConstantData | ref |

**class `DebugConstantData`** size `0x28`, base chain: none (root/value type) -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0xc` | HoldGroundErrorDelayTime | Float32 | primitive |
| `0x10` | MinDecisionTime | Float32 | primitive |
| `0x14` | GraphDataBuffer | Uint32 | primitive |
| `0x18` | BotDebugDistance | Float32 | primitive |
| `0x1c` | PathdataViewDistance | Float32 | primitive |
| `0x20` | InViewModeFOV | Float32 | primitive |
| `0x24` | InaccuracyEnabled | Boolean | primitive |

**class `BotPriorityConfigData`** size `0x30`, base chain: none (root/value type) -> DataContainer(unresolved)

| off | field | type | kind |
|---|---|---|---|
| `0x10` | DebugColor | Vec3 | primitive |
| `0x20` | AngleOffset | Float32 | primitive |
| `0x24` | Period | Int32 | primitive |
| `0x28` | Distance | Float32 | primitive |
| `0x2c` | AppliesToSensing | Boolean | primitive |
| `0x2d` | AppliesToExecution | Boolean | primitive |
| `0x2e` | AppliesToDecisions | Boolean | primitive |


## 6. All other `*Setting(s)` types with every field (exhaustive)

Display/render/physics/sound/etc. settings are lower value for the harness, but here is every remaining type whose name contains `Setting`. Windowing (`Dx9/10/11DisplaySettings.WindowSize/FullScreen/HWnd`, `GameRenderSettings.Fullscreen/Renderer/Dx10Enable/Dx11Enable/ForceFov/MultisampleCount`, `SceneSettings`) is relevant to running windowed and headless-ish.

- class AISettingsData size=156 parent=Asset :: TimingConstants@0x18:TimingConstantData; LookConstants@0x1c:LookConstantData; DebugConstants@0x20:DebugConstantData; EnvironmentConstants@0x24:EnvironmentConstantData; FollowConstants@0x28:FollowConstantData; PositionEvaluationConstants@0x2c:PositionEvaluationConstantData; DefaultSensingTemplate@0x30:SensingTemplateData; DefaultBehaviourTemplate@0x34:BehaviourTemplateData; DefaultVehicleType@0x38:AIVehicleBehaviourData; KynapseConstants@0x3c:KynapseConstantData; TraversalConstants@0x40:TraversalConstantData; ReadinessLevels@0x44:ReadinessLevels; SearchAndDestroyConstants@0x68:SearchAndDestroyConstantData; NavigationConstants@0x6c:NavigationConstantData; SoldierControlConstants@0x70:SoldierControlConstantData; CarControlConstants@0x74:CarControlConstantData; SensingConstants@0x78:SensingConstantData; SoundEnvironmentConstants@0x7c:SoundEnvironmentConstantData; CoverConstants@0x80:CoverConstantData; TargetEvaluationConstants@0x84:TargetEvaluationConstantData; VOConstants@0x88:VOConstantData; CombatConstants@0x8c:CombatConstantData; TankControlConstants@0x90:TankControlConstantData; HelicopterControlConstants@0x94:HelicopterControlConstantData; TurretControlConstants@0x98:TurretControlConstantData
- class AnimatedSoldierRagdollSettingsData size=36 parent=<unresolved 0x01bf030c> :: InstantRagdollImpulseThreshold@0xc:Float32; InstantRagdollSpeedThreshold@0x10:Float32; Ragdoll@0x14:RagdollAsset; RagdollMaterial@0x18:String
- valuetype AnimationSettings size=6 parent=None :: PreferSPUSampling@0x0:Boolean; EnableFeetIk@0x1:Boolean; EnableHandsIk@0x2:Boolean; EnableStats@0x3:Boolean; EnableJobs@0x4:Boolean; EnableFullCulling@0x5:Boolean
- class AudioMasterSettingsAsset size=44 parent=Asset :: SoundSystemSettings@0x18:ArrayBase<SoundSystemSettings>
- class BigWorldSetting size=32 parent=<unresolved 0x01bf030c> :: MinDelayTimeInMinutes@0xc:Float32; MaxDelayTimeInMinutes@0x10:Float32; MaxDistance@0x14:Int32; Sound@0x18:SoundTemplate; MinDistance@0x1c:Int32
- class BigWorldSettingsAsset size=44 parent=Editable :: Settings@0x18:ArrayBase<BigWorldSetting>
- class CompressorSettings size=28 parent=<unresolved 0x01bf030c> :: Attack@0xc:Float32; Release@0x10:Float32; Ratio@0x14:Float32; Threshold@0x18:Float32
- class DecalSettings size=56 parent=<unresolved 0x01bf030c> :: PendingDecalsRingBufferSize@0xc:Uint32; ClipAngle@0x10:Float32; AlphaFadeAt@0x14:Float32; ActiveDecalsRingBufferSize@0x18:Uint32; DecalSetRingBufferSize@0x1c:Uint32; Bias@0x20:Float32; ProximityDistance@0x24:Float32; PendingImpactsRingBufferSize@0x28:Uint32; NormalOffset@0x2c:Float32; DrawDebugMemUsage@0x30:Boolean; DrawDebugTransforms@0x31:Boolean; UseVisualMeshes@0x32:Boolean; DrawEnable@0x33:Boolean; Enable@0x34:Boolean; DumpDebugInfo@0x35:Boolean
- class Dx10DisplaySettings size=60 parent=<unresolved 0x01bf030c> :: WindowSize@0xc:Vec2; Adapters@0x14:ArrayBase<Dx10DisplayAdapterInfo>; RefreshRate@0x24:Float32; RenderAheadLimit@0x28:Uint32; HWnd@0x2c:Uint32; RenderJobAffinity@0x30:Uint32; Dx10PlusEnable@0x34:Boolean; FullScreen@0x35:Boolean; Dx10DotOneEnable@0x36:Boolean; NvPerfHudEnable@0x37:Boolean; DebugInfoEnable@0x38:Boolean
- class Dx11DisplaySettings size=56 parent=<unresolved 0x01bf030c> :: WindowSize@0xc:Vec2; RenderAheadLimit@0x14:Uint32; Adapters@0x18:ArrayBase<Dx11DisplayAdapterInfo>; RefreshRate@0x28:Float32; HWnd@0x2c:Uint32; RenderJobAffinity@0x30:Uint32; NvPerfHudEnable@0x34:Boolean; FullScreen@0x35:Boolean; DebugInfoEnable@0x36:Boolean
- class Dx9DisplaySettings size=56 parent=<unresolved 0x01bf030c> :: WindowSize@0xc:Vec2; RenderAheadLimit@0x14:Uint32; Adapters@0x18:ArrayBase<Dx9DisplayAdapterInfo>; RefreshRate@0x28:Float32; HWnd@0x2c:Uint32; RenderJobAffinity@0x30:Uint32; NvPerfHudEnable@0x34:Boolean; UseVsync@0x35:Boolean; FullScreen@0x36:Boolean; DebugInfoEnable@0x37:Boolean
- class EdgeModelBuilderSettings size=84 parent=Asset :: MinSpacing@0x18:Float32; MaxSpacing@0x1c:Float32; CornerPartExclusionPatterns@0x20:ArrayBase<String>; VolumeMap@0x30:ArrayBase<EdgeModelVolumeMapItemData>; PartExclusionPatterns@0x40:ArrayBase<String>; EnableEdgeModels@0x50:Boolean
- class EdgeModelGenerationSettings size=36 parent=<unresolved 0x01bf030c> :: OrientationVolumes@0xc:EdgeModelVolumeAsset; SharedSettings@0x10:EdgeModelBuilderSettings; VolumeMap@0x14:ArrayBase<EdgeModelVolumeMapItemData>
- valuetype EdgeModelPlacementSettings size=56 parent=None :: PlacementType@0x0:EdgeModelType; Probability@0x4:Int32; InwardOffsets@0x8:ArrayBase<FloatRange>; HorizontalAngles@0x18:ArrayBase<FloatRange>; VerticalAngles@0x28:ArrayBase<FloatRange>
- class EmitterSystemSettings size=120 parent=<unresolved 0x01bf030c> :: QuadHalfResSlice1ThresholdLow@0xc:Float32; QuadZOcclusionBias@0x10:Float32; QuadHalfResSlice1ThresholdHigh@0x14:Float32; FreezeAtEmittableCount@0x18:Uint32; MinScreenArea@0x1c:Float32; QuadTechnique@0x20:Int32; QuadPs3CullingFactorMultiplier@0x24:Float32; QuadKeptParticlesAlphaBoost@0x28:Float32; QuadProjectionScaleRampEnd@0x2c:Float32; QuadProjectionScaleRampStart@0x30:Float32; DrawBoundingBoxes@0x34:Uint32; MeshStreamingPriorityMultiplier@0x38:Float32; MeshDrawCountLimit@0x3c:Uint32; GlobalResetStartTimeInterval@0x40:Float32; QuadHalfResSlice2ThresholdHigh@0x44:Float32; TimeScale@0x48:Float32; MeshCullingDistance@0x4c:Float32; DrawStats@0x50:Uint32; DebugOverdrawCount@0x54:Int32; QuadHalfResSlice2ThresholdLow@0x58:Float32; QuadGroupsJoinNone@0x5c:Boolean; MeshDrawCullStats@0x5d:Boolean; QuadGroupsJoinNiceAndSimple@0x5e:Boolean; MeshDrawBoundingBoxes@0x5f:Boolean; MeshShadowEnable@0x60:Boolean; MeshViewCullingEnable@0x61:Boolean; MeshDrawTransforms@0x62:Boolean; QuadVertexShadowsEnable@0x63:Boolean; MeshRenderingEnable@0x64:Boolean; MeshHavokRenderingEnable@0x65:Boolean; QuadGroupsJoinAll@0x66:Boolean; EnableRendering@0x67:Boolean; EnableRealtimeTweaks@0x68:Boolean; QuadNiceRenderingEnable@0x69:Boolean; QuadEnableRendering@0x6a:Boolean; EnableFixedTimeStep@0x6b:Boolean; Enable@0x6c:Boolean; EnableJobs@0x6d:Boolean; EnableFixedDelta@0x6e:Boolean; QuadSimpleRenderingEnable@0x6f:Boolean; QuadEnableProjectionScaleCulling@0x70:Boolean; QuadEnableWireframe@0x71:Boolean; QuadHalfResEnable@0x72:Boolean; QuadEnableZOcclusion@0x73:Boolean; QuadEnableOverdrawRendering@0x74:Boolean; QuadEnableOpaque@0x75:Boolean; QuadEnableSorting@0x76:Boolean; QuadColorShaderCostsEnable@0x77:Boolean
- class EqualizerSettings size=32 parent=<unresolved 0x01bf030c> :: HighShelfGain@0xc:Float32; HpCutoffFrequency@0x10:Float32; HighShelfFrequency@0x14:Float32; LowShelfFrequency@0x18:Float32; LowShelfGain@0x1c:Float32
- class GameRenderSettings size=164 parent=<unresolved 0x01bf030c> :: ViewDistance@0xc:Float32; NearPlane@0x10:Float32; EdgeModelScreenAreaScale@0x14:Float32; ForceFov@0x18:Float32; SkipMipmapCount@0x1c:Uint32; Ps3FrameLocalBufferSize@0x20:Uint32; JobPriority@0x24:Uint32; JobThreadIndex@0x28:Uint32; EdgeModelDefaultMinScreenArea@0x2c:Float32; ForceBlurAmount@0x30:Float32; StaticModelCullJobCount@0x34:Uint32; ForceGlobalFadeAmount@0x38:Float32; ForceWorldFadeAmount@0x3c:Float32; EdgeModelLodScale@0x40:Float32; EdgeModelForceLod@0x44:Int32; StaticModelPartOcclusionMaxScreenArea@0x48:Float32; EdgeModelMaxVisibleInstanceCount@0x4c:Uint32; MultisampleQuality@0x50:Uint32; XenonPresentImmediateThreshold@0x54:Uint32; Dx9RefreshRate@0x58:Uint32; Dx9AdapterIndex@0x5c:Int32; XenonRingBufferSize@0x60:Uint32; MultisampleCount@0x64:Uint32; Ps3FrameMainBufferSize@0x68:Uint32; Renderer@0x6c:SelectedRenderer; StaticModelPartCullEnable@0x70:Boolean; EdgeModelDrawStats@0x71:Boolean; StaticModelPartFrustumCullEnable@0x72:Boolean; EdgeModelOcclusionCullEnable@0x73:Boolean; StaticModelPartOcclusionCullEnable@0x74:Boolean; Dx10PlusEnable@0x75:Boolean; EdgeModelFrustumCullEnable@0x76:Boolean; EdgeModelCullEnable@0x77:Boolean; EdgeModelDrawBoxes@0x78:Boolean; Dx10Enable@0x79:Boolean; EdgeModelAdditionalCullEnable@0x7a:Boolean; FadeWaitingEnable@0x7b:Boolean; FadeEnable@0x7c:Boolean; ResetLockedView@0x7d:Boolean; BlurEnable@0x7e:Boolean; ForegroundBlurEnable@0x7f:Boolean; ShadowsEnable@0x80:Boolean; LockView@0x81:Boolean; StaticModelDrawStats@0x82:Boolean; StaticModelDrawBoxes@0x83:Boolean; StaticModelPartShadowCullEnable@0x84:Boolean; StaticModelCullSpuJobEnable@0x85:Boolean; Fullscreen@0x86:Boolean; ForceVSyncEnable@0x87:Boolean; BuildJobSyncEnable@0x88:Boolean; XenonBufferTwoFramesEnable@0x89:Boolean; JobEnable@0x8a:Boolean; EntityRenderEnable@0x8b:Boolean; EmittersEnable@0x8c:Boolean; DecalsEnable@0x8d:Boolean; DrawInfo@0x8e:Boolean; GcmHudEnable@0x8f:Boolean; Ps3CellMemoryTexturesEnable@0x90:Boolean; Ps3LinearFrameCmdBufEnable@0x91:Boolean; DrawShaderStats@0x92:Boolean; PerfOverlayVisible@0x93:Boolean; PerfOverlayEnable@0x94:Boolean; DebugRendererEnable@0x95:Boolean; EdgeModelSpuInstancingEnable@0x96:Boolean; Dx11Enable@0x97:Boolean; Dx11DebugInfoEnable@0x98:Boolean; EdgeModelUseLodBox@0x99:Boolean; Dx10DebugInfoEnable@0x9a:Boolean; EdgeModelUseMainLodEnable@0x9b:Boolean; EdgeModelCastShadowsEnable@0x9c:Boolean; NvPerfHUDEnable@0x9d:Boolean; GpuProfilerEnable@0x9e:Boolean; InitialClearEnable@0x9f:Boolean; EdgeModelsEnable@0xa0:Boolean; CompositeMeshBatchingEnable@0xa1:Boolean; RigidMeshBatchingEnable@0xa2:Boolean
- class GameSettingEntityData size=144 parent=GameEntityData :: ChangeSetting@0x80:ChangeGameSettingType
- class GameSootSettings size=32 parent=<unresolved 0x01bf030c> :: ExpandAroundExplosionMin@0xc:Float32; ExpandAroundExplosionMax@0x10:Float32; ExpandAroundPartMin@0x14:Float32; ExpandAroundPartMax@0x18:Float32; DrawPartBounds@0x1c:Boolean; SootForBulletsEnable@0x1d:Boolean; SootForExplosionsEnable@0x1e:Boolean
- class GlobalPostProcessSettings size=432 parent=<unresolved 0x01bf030c> :: ForceBloomScale@0x10:Vec3; ForceVignetteScale@0x20:Vec2; Ps3Saturation@0x30:Vec3; PCBrightness@0x40:Vec3; ViewSize@0x50:Vec2; PCSaturation@0x60:Vec3; PCContrast@0x70:Vec3; FilmGrainTextureScale@0x80:Vec2; FilmGrainColorScale@0x90:Vec3; Brightness@0xa0:Vec3; Saturation@0xb0:Vec3; ForceVignetteColor@0xc0:Vec4; Ps3Contrast@0xd0:Vec3; Ps3Brightness@0xe0:Vec3; Contrast@0xf0:Vec3; HsaoFogAmount@0x100:Float32; HsaoBilateralBlurRadius@0x104:Float32; HsaoFogDistOfHalfStrength@0x108:Float32; HsaoPowerExponent@0x10c:Float32; HsaoRandomTextureWidth@0x110:Uint32; Hue@0x114:Float32; HsaoGaussianBlurSize@0x118:BlurFilter; HsaoDirCount@0x11c:Float32; HsaoBilateralBlurEdgeThreshold@0x120:Float32; HsaoBilateralBlurSharpness@0x124:Float32; DebugMode@0x128:PostProcessDebugMode; HsaoRadius@0x12c:Float32; BloodEffectFrameSize@0x130:Float32; HsaoRandomDirVariationCount@0x134:Uint32; HsaoStepCount@0x138:Float32; HsaoQuality@0x13c:Uint32; PCHue@0x140:Float32; Ps3Hue@0x144:Float32; HsaoMaxFootprintRadius@0x148:Float32; HsaoContrast@0x14c:Float32; HsaoAngleBias@0x150:Float32; HsaoAttenuation@0x154:Float32; ForceDofFarDistanceScale@0x158:Float32; ForceDofScale@0x15c:Float32; ForceDofBlurFilterDeviation@0x160:Float32; ForceDofNearDistanceScale@0x164:Float32; LdrBloomRange@0x168:Float32; ForceBlurAdd@0x16c:Float32; ForceDofFocusDistance@0x170:Float32; ChromostereopsisScale@0x174:Float32; ChromostereopsisOffset@0x178:Uint32; ForceDofEnable@0x17c:Int32; ForceVignetteExponent@0x180:Float32; DownsampleAverageStartMipmap@0x184:Uint32; DirectExposureEnable@0x188:Boolean; DrawDebugInfo@0x189:Boolean; QuarterDownsamplingEnable@0x18a:Boolean; BloomBlurEnable@0x18b:Boolean; BlurBlendEnable@0x18c:Boolean; DownsampleLogAverageEnable@0x18d:Boolean; ExposureClampEnable@0x18e:Boolean; HdrBloomEnable@0x18f:Boolean; HsaoGaussianBlurEnable@0x190:Boolean; HsaoGaussianBlurHalfResEnable@0x191:Boolean; HdrBlurEnable@0x192:Boolean; HsaoBilateralBlurEnable@0x193:Boolean; HsaoBilateralBlurVectorizedEnable@0x194:Boolean; Ldr16BitBloomEnable@0x195:Boolean; HsaoOnlyEnable@0x196:Boolean; HsaoEnable@0x197:Boolean; HsaoHalfResEnable@0x198:Boolean; FilmicTonemappingEnable@0x199:Boolean; FilmGrainLinearFilteringEnable@0x19a:Boolean; FilmGrainEnable@0x19b:Boolean; ChromostereopsisEnable@0x19c:Boolean; HsaoHalfResDepthEnable@0x19d:Boolean; PostTonemapBlurEnable@0x19e:Boolean; DownsampleBeforeBlurEnable@0x19f:Boolean; DownsampleAverageFromBloomEnable@0x1a0:Boolean; BloomEnable@0x1a1:Boolean; HsaoNormalTextureEnable@0x1a2:Boolean; HsaoVectorizedEnable@0x1a3:Boolean; Ps3BackBufferAsLdrTargetEnable@0x1a4:Boolean
- class HdrSetting size=52 parent=<unresolved 0x01bf030c> :: WindowAttackTime@0xc:Float32; WindowReleaseTime@0x10:Float32; WindowSize@0x14:Float32; Headroom@0x18:Float32; CompressFactor@0x1c:Float32; RmsAttackTime@0x20:Float32; RmsReleaseTime@0x24:Float32; RmsSmoothFactor@0x28:Float32; WindowMinBottom@0x2c:Float32; WindowMinTop@0x30:Float32
- class IndoorDampingSetting size=20 parent=<unresolved 0x01bf030c> :: Damping@0xc:Float32; MaxArea@0x10:Float32
- class IrradianceVolumeSettings size=36 parent=<unresolved 0x01bf030c> :: Fade@0xc:Float32; ScaleDefault@0x10:Float32; MinVal@0x14:Float32; ScaleVehicle@0x18:Float32; ScaleSoldier@0x1c:Float32; Enable@0x20:Boolean; Debug@0x21:Boolean
- class MaterialSoldierSoundSettings size=24 parent=<unresolved 0x01bf030c> :: LandSound@0xc:SoundAsset; ProneSound@0x10:SoundAsset; FootStepSound@0x14:SoundAsset
- class MeshLodSettingsAsset size=60 parent=Asset :: LodDistance3@0x18:Float32; LodDistance4@0x1c:Float32; MinScreenArea@0x20:Float32; LodDistance2@0x24:Float32; Ps3LodScale@0x28:Float32; LodDistance0@0x2c:Float32; LodDistance1@0x30:Float32; Lod4Enabled@0x34:Boolean; Enable@0x35:Boolean; Lod3Enabled@0x36:Boolean; Lod0Enabled@0x37:Boolean; Lod1Enabled@0x38:Boolean; Lod2Enabled@0x39:Boolean
- class MeshStreamingSettings size=56 parent=<unresolved 0x01bf030c> :: MaxPendingLoadCount@0xc:Uint32; SimulatedSeekTime@0x10:Float32; ListViewPageIndex@0x14:Uint32; ListViewSortOrder@0x18:Uint32; SimulatedMbPerSecond@0x1c:Float32; PoolSize@0x20:Uint32; Ps3CellPoolSize@0x24:Uint32; DrawLoadingListEnable@0x28:Boolean; DrawMeshListEnable@0x29:Boolean; DrawNonStreamedListEnable@0x2a:Boolean; SimulationEnable@0x2b:Boolean; Enable@0x2c:Boolean; DrawPriorityListEnable@0x2d:Boolean; PrioritizeVisibleMeshesFirstEnable@0x2e:Boolean; PrioritizeVisibleLodsFirstEnable@0x2f:Boolean; UpdateEnable@0x30:Boolean; SpuJobEnable@0x31:Boolean; DrawStatsEnable@0x32:Boolean; DrawMissingListEnable@0x33:Boolean; PrioritizeVisibleLoadsEnable@0x34:Boolean; ClosestPointDistanceEnable@0x35:Boolean
- valuetype MinimapSettings size=192 parent=None :: ObjectColor@0x0:Vec3; TerrainColor@0x10:Vec3; RoadColor@0x20:Vec3; VegetationColor@0x30:Vec3; WaterColor@0x40:Vec3; AltitudeCurveDistance@0x50:Int32; TextureResolution@0x54:Int32; WaterHeightModifier@0x58:Float32; RoadHeightOverTerrain@0x5c:Float32; ObjectsToReplace@0x60:ArrayBase<MeshAsset>; ObjectBoundingBoxMesh@0x74:MeshAsset; MinObjectBoundingRadius@0x78:Float32; MinimapObjects@0x7c:ArrayBase<MeshAsset>; VegetationDensity@0x90:VegetationDensity; MinimapVegetationObjects@0x94:ArrayBase<MeshAsset>; VegetationObjectsToReplace@0xa8:ArrayBase<MeshAsset>; GenerateAllLayers@0xbc:Boolean; GenerateMinimap@0xbd:Boolean; GenerateRiverMesh@0xbe:Boolean; GenerateLakeMesh@0xbf:Boolean
- class MonitoringSettings size=44 parent=<unresolved 0x01bf030c> :: LogBufferSize@0xc:Uint32; ConnectionRetryInterval@0x10:Uint32; ReceiveTransactionTimeout@0x14:Uint32; HubPort@0x18:Uint32; HubAddress@0x1c:String; Enable@0x28:Boolean; LogEnable@0x29:Boolean
- class OcclusionSettings size=60 parent=<unresolved 0x01bf030c> :: ScreenWidth@0xc:Uint32; SkipClipTriangleDistance@0x10:Float32; OccluderMeshJobCount@0x14:Uint32; RasterizeJobCount@0x18:Uint32; CoverageViewDistance@0x1c:Float32; NormalViewDistance@0x20:Float32; ScreenHeigth@0x24:Uint32; HighPriorityViewDistance@0x28:Float32; TerrainViewDistance@0x2c:Float32; DrawOccludedDynamicEntities@0x30:Boolean; DrawCoverageBuffer@0x31:Boolean; DrawZbuffer@0x32:Boolean; DrawOccludedStaticEntities@0x33:Boolean; DrawProcessedOccluders@0x34:Boolean; DrawOccluderMeshesWireframe@0x35:Boolean; OccluderMeshEnable@0x36:Boolean; JobsEnable@0x37:Boolean; Enable@0x38:Boolean; RasterizeAlphaEnable@0x39:Boolean; RasterizeEnable@0x3a:Boolean; OccluderAlphaMeshEnable@0x3b:Boolean
- valuetype OrderEventSettings size=16 parent=None :: Immortal@0x0:EventSendOption; VoiceOvers@0x4:EventSendOption; Sensing@0x8:EventSendOption; Target@0xc:EventSendOption
- class OrderSettings size=44 parent=Asset :: Orders@0x18:ArrayBase<OrderReadiness>
- class OvergrowthSettings size=116 parent=<unresolved 0x01bf030c> :: AdaptiveLodMVertexBudget@0xc:Float32; GlobalLodScale@0x10:Float32; BillboardShadowOffset@0x14:Float32; MaxBillboardCount@0x18:Uint32; BillboardScale@0x1c:Float32; AdaptiveLodSpeed@0x20:Float32; InputsPerJob@0x24:Int32; InputsPerSpuJob@0x28:Int32; AdaptiveLodMaxModifier@0x2c:Float32; AdaptiveLodSpeed2@0x30:Float32; AdaptiveLodMinModifier@0x34:Float32; ForceTechnique@0x38:Int32; BranchWindAnimationSpeed@0x3c:Float32; BranchConstWind@0x40:Float32; HiQualityLod@0x44:Int32; AdaptiveLodMargin@0x48:Float32; ShadowMaxLod@0x4c:Uint32; BranchWindPower@0x50:Float32; ZPassLod@0x54:Int32; ForceBendingType@0x58:Int32; LodBlendOverlap@0x5c:Float32; LodBlendBillboardOverlap@0x60:Float32; UseMaxShadowLOD@0x64:Boolean; WindEnable@0x65:Boolean; AlmostOnlyBillboardsEnable@0x66:Boolean; OnlyBillboardsEnable@0x67:Boolean; HiLodEnable@0x68:Boolean; InstancingEnable@0x69:Boolean; LodBlendEnable@0x6a:Boolean; CastShadowsEnable@0x6b:Boolean; ZeroViewportEnable@0x6c:Boolean; AdaptiveLodEnable@0x6d:Boolean; JobsEnable@0x6e:Boolean; BillboardsEnable@0x6f:Boolean; Enable@0x70:Boolean
- class PathfindingSettings size=120 parent=Asset :: MaxPitch@0x18:Float32; VerticalBoxSize@0x1c:Float32; EntityHeight@0x20:Float32; EdgeRadius@0x24:Float32; OverConnectionRatio@0x28:Float32; DetailedZoneSettings@0x2c:PathfindingZoneSettings; ForbidHeight@0x30:Float32; WaterLevelAdjustment@0x34:Float32; CollisionModel@0x38:PathdataCollisionModel; OutputType@0x3c:PathfindingType; Generator@0x40:PathdataGenerator; NbSectors@0x44:Int32; StepMax@0x48:Float32; GroundSlopeMin@0x4c:Float32; MaxDeltaHeight@0x50:Float32; StepLength@0x54:Float32; HoleMax@0x58:Float32; GroundSlopeMax@0x5c:Float32; VerticalPitch@0x60:Float32; GraphAccuracy@0x64:Float32; Pitch@0x68:Float32; UseBoxApproximation@0x6c:Boolean; BuildRoads@0x6d:Boolean; UseForbid@0x6e:Boolean; UseExclusions@0x6f:Boolean; BuildCompositeShapes@0x70:Boolean; VehicleSeeds@0x71:Boolean; InfantrySeeds@0x72:Boolean; ForbidCollidingEdge@0x73:Boolean; ForbidWater@0x74:Boolean; BuildBridges@0x75:Boolean
- class PathfindingZoneSettings size=24 parent=<unresolved 0x01bf030c> :: NbSectors@0xc:Int32; GraphAccuracy@0x10:Float32; Pitch@0x14:Float32
- class PhysicsSettings size=32 parent=<unresolved 0x01bf030c> :: CollideJobCount@0xc:Uint32; IntegrateJobCount@0x10:Uint32; NumberOfThreads@0x14:Uint32; HeightfieldRSXStreaming@0x18:Boolean; SingleStepCharacter@0x19:Boolean; EnableJobs@0x1a:Boolean; EnableAIRigidBody@0x1b:Boolean; ForestEnable@0x1c:Boolean
- valuetype PickupSettingsData size=32 parent=None :: MeshRenderRotation@0x0:Vec3; MeshRenderOffset@0x10:Vec3
- class ReverbSettingsAsset size=44 parent=Editable :: Reverbs@0x18:ArrayBase<ReverbAsset>
- class SceneSettings size=128 parent=<unresolved 0x01bf030c> :: CameraTransform@0x10:LinearTransform; RenderSettings@0x50:WorldRenderSettings; MouseSensitivityY@0x54:Float32; EmitterSystemMetrics@0x58:EmitterSystemMetrics; MouseSensitivityX@0x60:Float32; FieldOfView@0x64:Float32; NearPlane@0x68:Float32; ViewDistance@0x6c:Float32; CameraTranslationScale@0x70:Float32; CameraForceHeight@0x74:Float32; LockCamera@0x78:Boolean; ResetLockedCamera@0x79:Boolean; UseCamera@0x7a:Boolean; CameraForceHeightEnable@0x7b:Boolean
- class ServerGameSettingEntity size=44 parent=ServerEntity :: 
- class ShaderSystemSettings size=240 parent=<unresolved 0x01bf030c> :: DebugNonFiniteColor@0x10:Vec3; Ps3DispatchOnlyJob@0x20:Int32; XenonTrilinearThreshold@0x24:Uint32; DrawCallMultiplier@0x28:Uint32; Ps3DispatchJobSpuCount@0x2c:Uint32; Ps3DispatchBlocksPerJob@0x30:Uint32; Ps3DispatchMinJobBlockCount@0x34:Uint32; MipmapBias@0x38:Float32; ZOnlyMaxAnisotropy@0x3c:Uint32; MaxAnisotropy@0x40:Uint32; Ps3DispatchJobCount@0x44:Uint32; XenonDispatchBlocksPerJob@0x48:Uint32; XenonDispatchJobCount@0x4c:Uint32; XenonDispatchOnlyJob@0x50:Int32; XenonDispatchMinJobBlockCount@0x54:Uint32; XenonDispatchCmdBufferSize@0x58:Uint32; OverdrawMaxLayerCount@0x5c:Uint32; XenonDispatchCmdBufferSegmentSize@0x60:Uint32; Ps3GeometryJobCount@0x64:Uint32; Ps3GeometryJobSpuCount@0x68:Uint32; Ps3ZCullBehindLimit@0x6c:Uint32; Ps3ZCullInFrontLimit@0x70:Uint32; Ps3MaxMipmap@0x74:Uint32; Ps3MinMipmap@0x78:Uint32; FrameMemoryBufferSize@0x7c:Uint32; Path@0x80:ShaderRenderPath; ForceAlphaRef@0x84:Int32; ColorizeMode@0x88:ShaderColorizeMode; Ps3DrawEdgeGeometryEnable@0x8c:Boolean; Ps3DrawNonEdgeGeometryEnable@0x8d:Boolean; Ps3GeometryJobWithoutDispatchJobEnable@0x8e:Boolean; Ps3GeometryDebugRenderEnable@0x8f:Boolean; Ps3TriangleCullZPassEnable@0x90:Boolean; Ps3TriangleBackfaceCullEnable@0x91:Boolean; Ps3TriangleCullEnable@0x92:Boolean; Ps3TriangleFrustumCullEnable@0x93:Boolean; Ps3CmdBufClearEnable@0x94:Boolean; Ps3FinishEnable@0x95:Boolean; Ps3PixelShaderRedundancyCheckingEnable@0x96:Boolean; Dx11DelayedShaderLoadingEnable@0x97:Boolean; Dx10DelayedShaderLoadingEnable@0x98:Boolean; Ps3GeometryJobEnable@0x99:Boolean; Ps3FlushEnable@0x9a:Boolean; Ps3DispatchJobEnable@0x9b:Boolean; Ps3PixelShaderHashStoreEnable@0x9c:Boolean; Ps3CmdBufTexturesInlineEnable@0x9d:Boolean; Ps3CmdBufVertexShaderInlineEnable@0x9e:Boolean; Ps3TxpDemotionEnable@0x9f:Boolean; Ps3SpuClipPlaneRejectEnable@0xa0:Boolean; Ps3SpuCullVolumesEnable@0xa1:Boolean; Ps3ZCullEnable@0xa2:Boolean; Ps3DispatchSpuBufferLinkingEnable@0xa3:Boolean; Ps3DispatchFrameBlockEnable@0xa4:Boolean; Ps3SCullEnable@0xa5:Boolean; Ps3DispatchFrameGpuBlockEnable@0xa6:Boolean; Ps3SpuInstancingEnable@0xa7:Boolean; Ps3TriangleCull4BoneSkinningEnable@0xa8:Boolean; Ps3TrianglePartCullEnable@0xa9:Boolean; Ps3TriangleCullDoubleSidedEnable@0xaa:Boolean; Ps3TriangleCull1BoneSkinningEnable@0xab:Boolean; Ps3CmdBufTexturesEnable@0xac:Boolean; Ps3PartitionClipPlaneCullEnable@0xad:Boolean; Ps3PartitionVolumeCullEnable@0xae:Boolean; Ps3DrawBlockCullEnable@0xaf:Boolean; Ps3PartitionFrustumCullEnable@0xb0:Boolean; DepthEnable@0xb1:Boolean; DepthWriteEnable@0xb2:Boolean; DepthTestEnable@0xb3:Boolean; ForceDoubleSided@0xb4:Boolean; ForcePointFiltering@0xb5:Boolean; ZeroViewportEnable@0xb6:Boolean; UseHasZPass@0xb7:Boolean; XenonZPassEnable@0xb8:Boolean; XenonDirectConstantEnable@0xb9:Boolean; ClipPlanesEnable@0xba:Boolean; SinglePrimitiveEnable@0xbb:Boolean; DrawCallEnable@0xbc:Boolean; ForceAlphaToMask@0xbd:Boolean; DrawTransparent@0xbe:Boolean; DrawTransparentDecal@0xbf:Boolean; DrawTransparentDepthWrite@0xc0:Boolean; FlushEnable@0xc1:Boolean; DrawZOnly@0xc2:Boolean; DrawOpaque@0xc3:Boolean; AlphaBlendEnable@0xc4:Boolean; AlphaToMaskEnable@0xc5:Boolean; ForceAlphaTest@0xc6:Boolean; DrawOffset@0xc7:Boolean; StreamingSimpleShadingEnable@0xc8:Boolean; AlphaTestEnable@0xc9:Boolean; SortJobEnable@0xca:Boolean; SortSpuJobEnable@0xcb:Boolean; PixelConstantsEnable@0xcc:Boolean; StatesEnable@0xcd:Boolean; PixelShaderSrgbConversionEnable@0xce:Boolean; BoneConstantOptimizationEnable@0xcf:Boolean; DispatchExecuteEnable@0xd0:Boolean; XenonDispatchJobEnable@0xd1:Boolean; XenonDispatchBlockOnCmdBufEnable@0xd2:Boolean; VertexConstantsEnable@0xd3:Boolean; PixelConstantRedundancyCheckingEnable@0xd4:Boolean; VertexConstantRedundancyCheckingEnable@0xd5:Boolean; PseudoInstancingEnable@0xd6:Boolean; SortBlocksEnable@0xd7:Boolean; SimpleTexturesEnable@0xd8:Boolean; SimpleTextureFilteringEnable@0xd9:Boolean; XenonGprChangeEnable@0xda:Boolean; PixBlockEventsEnable@0xdb:Boolean; GcmReplayMarkersEnable@0xdc:Boolean; ShaderPixScopeEnable@0xdd:Boolean; SimplePixelShaderEnable@0xde:Boolean; SimpleVertexShaderEnable@0xdf:Boolean; LogEnable@0xe0:Boolean; SingleFrameLogEnable@0xe1:Boolean; SingleFrameBlockLogEnable@0xe2:Boolean
- class SoldierRagdollSettingsData size=128 parent=<unresolved 0x01bf030c> :: StaticBulletImpulse@0x10:Vec3; StaticCollisionImpulse@0x20:Vec3; StaticDeathImpulse@0x30:Vec3; StaticExplosionImpulse@0x40:Vec3; BulletImpulseMultiplier@0x50:Float32; MaxImpulse@0x54:Float32; HitBoneImpulseModifier@0x58:Float32; CollisionImpulseMultiplier@0x5c:Float32; DeathSpasms@0x60:ArrayBase<RagdollImpulse>; DeathImpulseMultiplier@0x70:Float32; ExplosionImpulseMultiplier@0x74:Float32
- class SoldierSprintSettingsData size=56 parent=<unresolved 0x01bf030c> :: ThrottleInputRequired@0xc:Float32; FovOutTime@0x10:Float32; InterruptingActions@0x14:ArrayBase<EntryInputActionEnum>; RecoverTime@0x24:Float32; FovInDelay@0x28:Float32; Fov@0x2c:Float32; FovOutDelay@0x30:Float32; FovInTime@0x34:Float32
- class SoundStateSettingsAsset size=52 parent=Asset :: ScreamThreshold@0x18:Float32; ImpairedHearingImpulseThreshold@0x1c:Float32; SoundStates@0x20:ArrayBase<SoundState>
- class SoundSystemSettings size=48 parent=<unresolved 0x01bf030c> :: MainMixVolume@0xc:Float32; ReverbVolume@0x10:Float32; PostEffectsGain@0x14:Float32; DistortionClipLevel@0x18:Float32; MasterDialogGain@0x1c:Float32; Compressor@0x20:CompressorSettings; Equalizer@0x24:EqualizerSettings; MasterLfeGain@0x28:Float32; MasterVolume@0x2c:Float32
- class StaticCullSettings size=20 parent=<unresolved 0x01bf030c> :: MergePassCount@0xc:Uint32; UseOldCullAgainstOcclusion@0x10:Boolean; Enable@0x11:Boolean
- class VisualTerrainSettings size=224 parent=<unresolved 0x01bf030c> :: OccludedMinDistance@0xc:Float32; PatchSlotCount@0x10:Uint32; PatchSampleSize@0x14:Uint32; MemoryBufferSize@0x18:Uint32; UndergrowthUpdateCellCount@0x1c:Uint32; UndergrowthBuildJobCount@0x20:Uint32; UndergrowthPoplessDistanceRatio@0x24:Float32; DynamicMaskPoolWidth@0x28:Uint32; DynamicMaskTileSize@0x2c:Uint32; BuildJobCount@0x30:Uint32; DynamicMaskPoolHeight@0x34:Uint32; MaxNonVisibleTextureUpdateCount@0x38:Uint32; DynamicMaskDecalWidthScale@0x3c:Float32; DynamicMaskPixelsPerMeter@0x40:Uint32; UndergrowthCellSize@0x44:Float32; UndergrowthViewDistanceScale@0x48:Float32; UndergrowthDensityScale@0x4c:Float32; UndergrowthSinkSafeRadius@0x50:Float32; UndergrowthViewDistanceScaleMax@0x54:Float32; UndergrowthMaxCullInstanceCount@0x58:Uint32; UndergrowthInstanceCullJobCount@0x5c:Uint32; UndergrowthOverlapDensityScale@0x60:Float32; UndergrowthForceCellSideCount@0x64:Int32; UndergrowthCellCacheTextureSize@0x68:Uint32; UndergrowthCellMaskTextureSize@0x6c:Uint32; UndergrowthMaxCellSideCount@0x70:Uint32; UndergrowthSinkVelocity@0x74:Float32; UndergrowthWindSpeed@0x78:Float32; UndergrowthMaxHeight@0x7c:Float32; DecalZPassDistance@0x80:Float32; DecalBias@0x84:Float32; ForceGeometryLodLevel@0x88:Int32; ZPassDistance@0x8c:Float32; SlotReuseWaitCount@0x90:Uint32; GeometryLodScale@0x94:Float32; ViewMode@0x98:TerrainViewMode; DecalOffsetY@0x9c:Float32; DecalZPassBias@0xa0:Float32; DrawPatchesEnable@0xa4:Boolean; DrawEnable@0xa5:Boolean; DrawMainPatchesEnable@0xa6:Boolean; DrawDecalEnable@0xa7:Boolean; DrawSurroundingPatchesEnable@0xa8:Boolean; DrawPatchBoxesEnable@0xa9:Boolean; DrawTextureTileBoxesEnable@0xaa:Boolean; UndergrowthBuildSpuJobEnable@0xab:Boolean; DrawDecalZPassEnable@0xac:Boolean; DrawOnlyDecalZPassEnable@0xad:Boolean; DrawUndergrowthDecalsEnable@0xae:Boolean; Enable@0xaf:Boolean; Ps3UndergrowthInstanceCellMemoryEnable@0xb0:Boolean; UndergrowthInstanceCullDynamicAllocEnable@0xb1:Boolean; DrawUndergrowthInstanceBoxesEnable@0xb2:Boolean; OccluderJobEnable@0xb3:Boolean; CullSampleBoundingBoxHeightEnable@0xb4:Boolean; OccludedEnable@0xb5:Boolean; OccluderSpuJobEnable@0xb6:Boolean; UndergrowthInstanceAdditionalCullEnable@0xb7:Boolean; WireframeEnable@0xb8:Boolean; UndergrowthInstanceCullEnable@0xb9:Boolean; UndergrowthSpuInstancingEnable@0xba:Boolean; UndergrowthInstanceCullSpuJobEnable@0xbb:Boolean; UndergrowthInstanceOcclusionCullEnable@0xbc:Boolean; UndergrowthInstanceFrustumCullEnable@0xbd:Boolean; UndergrowthInstanceCullBoxTestEnable@0xbe:Boolean; UndergrowthUpdateJobsEnable@0xbf:Boolean; SlotDebugOutputEnable@0xc0:Boolean; ZeroViewportEnable@0xc1:Boolean; DynamicMaskEnable@0xc2:Boolean; BuildDrawJobEnable@0xc3:Boolean; PatchMaterialSortingEnable@0xc4:Boolean; DrawWaterEnable@0xc5:Boolean; SimpleWater@0xc6:Boolean; PatchLodTransitionsEnable@0xc7:Boolean; BuildSpuJobEnable@0xc8:Boolean; UpdateJobsEnable@0xc9:Boolean; CullSpuJobEnable@0xca:Boolean; PrepareBuildSpuJobEnable@0xcb:Boolean; ModifiersEnable@0xcc:Boolean; HeightModificationEnable@0xcd:Boolean; PrioritizeCullJobsEnable@0xce:Boolean; RegenerateTexturesEnable@0xcf:Boolean; UndergrowthTerrainColoringEnable@0xd0:Boolean; UndergrowthGenerateEnable@0xd1:Boolean; DrawUndergrowthDebugCacheTexturesEnable@0xd2:Boolean; DrawUndergrowthStatsEnable@0xd3:Boolean; UndergrowthSimpleShaderEnable@0xd4:Boolean; DrawDebugTextEnable@0xd5:Boolean; UndergrowthRegenerateEnable@0xd6:Boolean; UndergrowthZPassEnable@0xd7:Boolean; DrawDebugTexturesEnable@0xd8:Boolean; UndergrowthEnable@0xd9:Boolean; HeightfieldMipmapsEnable@0xda:Boolean; CastShadowsEnable@0xdb:Boolean; DrawUndergrowthCellBoxesEnable@0xdc:Boolean; DrawUndergrowthDebugMaskTexturesEnable@0xdd:Boolean; UndergrowthCastShadowsEnable@0xde:Boolean; DrawUndergrowthEnable@0xdf:Boolean
- class WeatherSystemSettings size=48 parent=Asset :: FogType@0x18:FogType; PanoramicCloudBlendMode@0x1c:PanoramicCloudBlendMode; StateFadeTimeSeconds@0x20:Float32; InterpolationMode@0x24:WeatherstateInterpolationMode; ActiveState@0x28:Uint32; Enable@0x2c:Boolean; SkydomeEnable@0x2d:Boolean; VolumetricCloudsEnable@0x2e:Boolean
- class WorldRenderSettings size=256 parent=<unresolved 0x01bf030c> :: ClearColor@0x10:Vec3; DrawDebugDynamicCubemapMipLevel@0x20:Int32; MaxPointLightCount@0x24:Uint32; OnlyShadowmapSlice@0x28:Int32; MemoryBufferSize@0x2c:Uint32; ViewMode@0x30:WorldViewMode; GenericEntityMaxStaticEntityCount@0x34:Uint32; GenericEntityMaxDynamicEntityCount@0x38:Uint32; MultisampleCount@0x3c:Uint32; ReflectionCubemapSize@0x40:Uint32; MultisampleQuality@0x44:Uint32; DynamicCubemapFilterWidth@0x48:Float32; Ps3ZPassSlopeScaleBias@0x4c:Float32; Dx9ShadowSlopeScaleBias@0x50:Float32; Ps3ZPassDepthBias@0x54:Float32; Ps3ShadowDepthBias@0x58:Float32; Ps3ShadowSlopeScaleBias@0x5c:Float32; DynamicCubemapSidesPerFrameCount@0x60:Uint32; DynamicCubemapFilterMode@0x64:MipmapFilterMode; ScopeFilterColorScale@0x68:Float32; Dx9ZPassDepthBias@0x6c:Float32; Dx9ZPassSlopeScaleBias@0x70:Float32; XenonFastHdrColorScale@0x74:Float32; XenonFixedHdrColorScale@0x78:Float32; ShadowmapHeight@0x7c:Uint32; ShadowmapSliceSchemeWeight@0x80:Float32; ShadowmapSliceCount@0x84:Uint32; ShadowmapWidth@0x88:Uint32; ShadowViewportScale@0x8c:Float32; ShadowmapMinFov@0x90:Float32; ViewportScale@0x94:Float32; MinScreenArea@0x98:Float32; ShadowMinScreenArea@0x9c:Float32; ShadowmapExtrusionLength@0xa0:Float32; ShadowmapMinScreenArea@0xa4:Float32; ShadowmapSizeZScale@0xa8:Float32; ShadowmapViewDistance@0xac:Float32; ShadowmapFirstSliceScale@0xb0:Float32; ShadowmapSliceOverlapFactor@0xb4:Float32; ForegroundEnable@0xb8:Boolean; BackgroundEnable@0xb9:Boolean; ForegroundZPassEnable@0xba:Boolean; ForegroundBlurEnable@0xbb:Boolean; BilateralHalfResCompositeEnable@0xbc:Boolean; ForegroundDepthClearEnable@0xbd:Boolean; ForegroundBlurFilterEnable@0xbe:Boolean; ScopeFilterEnable@0xbf:Boolean; GenericEntityRendererEnable@0xc0:Boolean; JobsEnable@0xc1:Boolean; FogEnable@0xc2:Boolean; ColoredTilingEnable@0xc3:Boolean; DynamicCullZBufferTestEnable@0xc4:Boolean; DynamicCullSpuJobEnable@0xc5:Boolean; ManualTilingEnable@0xc6:Boolean; DynamicCubemapUpdateEnable@0xc7:Boolean; DynamicCubemapEnable@0xc8:Boolean; ZBufferShadowTestEnable@0xc9:Boolean; TilePredicationEnable@0xca:Boolean; IrradianceVolumeEnable@0xcb:Boolean; DynamicCubemapMipmapGenEnable@0xcc:Boolean; DynamicCullDrawOccludedBoxesEnable@0xcd:Boolean; WeatherSystemHalfResEnable@0xce:Boolean; HdrEnable@0xcf:Boolean; XenonFastHdrEnable@0xd0:Boolean; ColoredShadowmapSlicesEnable@0xd1:Boolean; Enable@0xd2:Boolean; XenonFastHdrResolveScaleEnable@0xd3:Boolean; Ps3SCullEnable@0xd4:Boolean; LinearZ@0xd5:Boolean; Ps3TilingEnable@0xd6:Boolean; Ps3ZCullEnable@0xd7:Boolean; Ps3Shadowmap16BitEnable@0xd8:Boolean; Ps3ShadowmapTilingEnable@0xd9:Boolean; ShadowmapFixedMovementEnable@0xda:Boolean; ShadowmapFixedDepthEnable@0xdb:Boolean; CascadedBoxShadowmapEnable@0xdc:Boolean; GenerateShadowmapsEnable@0xdd:Boolean; SimpleShadowmapsEnable@0xde:Boolean; CascadedBoxShadowmapClipMethodEnable@0xdf:Boolean; ApplyShadowmapsEnable@0xe0:Boolean; DrawDebugShadowmaps@0xe1:Boolean; DrawDebugDynamicCubemap@0xe2:Boolean; DrawFrustums@0xe3:Boolean; DrawShadowFrustums@0xe4:Boolean; DrawDebugLUT@0xe5:Boolean; DrawDebugVisibleEntityTypes@0xe6:Boolean; WireframeEnable@0xe7:Boolean; DrawDebugZBufferEnable@0xe8:Boolean; DrawDebugHalfRes@0xe9:Boolean; TonemapEnable@0xea:Boolean; ShadowExtrusionCullingEnable@0xeb:Boolean; GammaCorrectionEnable@0xec:Boolean; ShadowmapsEnable@0xed:Boolean; DrawLightSources@0xee:Boolean; DrawLineBoundingBoxes@0xef:Boolean; DrawBoundingSpheres@0xf0:Boolean; DrawPointLightInfo@0xf1:Boolean; DrawSolidBoundingBoxes@0xf2:Boolean

Name/value settings records used by `GameSettingsAsset` (profile-style persisted settings, not startup args):

- class GameSettingsAsset size=40 parent=Asset :: Categories@0x18:ArrayBase<GameSettingsCategory>
- valuetype GameSettingsCategory size=68 parent=None :: Strings@0x0:ArrayBase<StringSetting>; Binaries@0x10:ArrayBase<BinarySetting>; Ints@0x20:ArrayBase<IntSetting>; Category@0x30:GameSettingsType; Floats@0x34:ArrayBase<FloatSetting>
- ENUM GameSettingsType size=4: GstAudio=0, GstGraphics=1, GstRender=2, GstInput=3, GstAI=4, GstGameplay=5, GstPlayerProfile=6, GstPersistence=7, GstBinary=8, GstMenuTracker=9, GstStatsCache=10, GstCount=11
- valuetype IntSetting size=20 parent=None :: Value@0x0:Int32; Name@0x4:String; UserExposed@0x10:Boolean
- valuetype FloatSetting size=20 parent=None :: Value@0x0:Float32; Name@0x4:String; UserExposed@0x10:Boolean
- valuetype StringSetting size=28 parent=None :: Value@0x0:String; Name@0xc:String; UserExposed@0x18:Boolean
- valuetype BinarySetting size=16 parent=None :: Name@0x0:String; UserExposed@0xc:Boolean

## 7. Ranked levers

Reading guide: **FACT** = present in the dump (type/field/offset/enum). **INFERENCE** = how it likely behaves; must be confirmed on the exe or live.

### 7.1 Forcing a level

1. **`ServerSettings.Level` (String @0x7c) and `ClientSettings.Level` (String @0x5c).** FACT: both exist, same name, in the two singleton settings classes. INFERENCE: set via startup args `-Server.Level` / `-Client.Level` (or console) before the session is created; value is a level path string (names come from game bundles, not the dump). Set both; for SP they presumably agree.
2. **`ClientSettings.IsMultiplayer` (@0xd5) + `ServerSettings.PlayerCountNeededForMultiplayer` (@0x34).** FACT. Pick MP vs SP code path; INFERENCE: SP mode is `GameModeTypes.SinglePlayer=0` in `LevelDescription.GameMode->GameModeData`, so a level's mode is a property of the level data, and `Level` + the level's own `WorldData.Modes` decide which mode loads.
3. **Fast-path booleans: `ServerSettings.IsFastStartupEnabled` @0x9f, `ClientSettings.IsFastStartupEnabled` @0xf0, `GameSettings.FastStartupEnable` @0x98.** FACT. INFERENCE: skip frontend/boot steps.
4. **`ServerSettings.SavePoint` (String @0x70) + `LoadSavePoint` (@0xb8)**: start straight at a named checkpoint (SP). FACT for fields.
5. In-memory write route if startup args cannot be set: the settings are plain data objects, so a write to the singleton instance fields is possible; the instance addresses are NOT in the dump. Locate by following the `arg3` stub (0x534600/0x534680...) or by the TypeInfo VA (e.g. ServerSettings TypeInfo `0x01bf3b3c`) to find the instance **[INFERENCE]**. The `String` field layout (12 B) must be reverse engineered first.
6. `NetworkSettings.PlaybackDemoFileName` + `DemoSettings.PlaybackDemoFileName` is an alternate way to enter a level without a campaign mission (replaying a recorded demo) **[INFERENCE, needs a demo file]**.
7. Near-empty level: FACT: no empty-level type exists; `ProceduralLevelCreateDesc` is an editor-ish asset. INFERENCE: pick the smallest shipped level (and set `ServerSettings.IsAiEnabled=false`, `LevelData.HasAI` is data) and/or mount a trimmed bundle set via `GameSettings.UseBundles` + `BundleMountPoints`.

### 7.2 Skipping menus and cinematics

1. **`ClientSettings.DisableMenuFlow` @0xf9, `LoadMenu` @0xfa, `LimitedMainMenu` @0xba, `BypassMatchmaking` @0xea, `IsFastStartupEnabled` @0xf0, `PauseGameOnStartUp` (keep false) @0xd2.** FACT; effect INFERENCE.
2. **`ClientSettings.DisableMovies` @0xf1 and `DisableHudMovies` @0xf2**: intro/loading Bink and HUD movies.
3. **`ServerSettings.DisableCutscenes` @0xb5** (and `DisableCheckpoints` @0x8e): prevents cutscene entities from running on the server. Check `CutsceneEntityData.IsSkippable` @0xcd per cutscene for the fallback of pressing skip.
4. **`LevelDescription.WaitForButtonBeforeStarting`** (absolute in LevelData `0x1ec`), `StartDelayTime` (`0xa8`), `LevelData.StartLevelFadedToBlack` @0x3b0, `StartLevelWithSoundFaded` @0x3af: level-start gates that a data patch can flip (FACT fields; effect INFERENCE).
5. `NetworkSettings.SkipEntitlementCheck` @0x9c, `ClientSettings.CheckDeviceEntitlements` @0xbe: avoid online entitlement blocking.
6. For detection (not skipping): `MenuIdEnum` values (MiePlaying=4 is the 'in gameplay' state; MieMovie=14, MieLetterBox=13, MieSpawnMenu=2, MieMissionFailure=15) are the menu state vocabulary.

### 7.3 Spawning the player

1. **`ServerSettings.InstantSpawn` @0x93** (FACT) + **`ClientSettings.SpawnPoint` String @0x50, `Team` u32 @0x1c, `Kit` u32 @0x18** (FACT; these three read like a 'spawn me at X as team T with kit K' request) -> the lowest-friction spawn path **[INFERENCE]**.
2. `ServerSettings.RespawnOnDeathPosition` @0xc6, `IsGodMode` @0x95 / `IsJesusMode` @0xaa, `AutokillSoldiers` @0x99 (keep false), `IsAiEnabled` @0xc5 (false for a quiet scene).
3. Level-graph spawn: `SoldierSpawnEntityData` with `PlayerType=SSHumanPlayer(0)`, `Team`, `SoldierKit` ptr @0x104 (the kit that spawns); `AlternateSpawnEntityData` (Priority, Transform) as extra positions. Requires editing the level's entity graph or finding an existing human spawn in a level.
4. `MenuEntityData.MenuId` MieSpawnMenu=2 / MieCreateSoldier=18 are the UI states around spawn; a harness that must auto-click the spawn menu can key off these.

### 7.4 Overriding the player's kit/weapons

1. **Unlock gating off: `ServerSettings.UnrestrictedUnlocks` @0xbe + `ClientSettings.UnrestrictedUnlocks` @0xc8** (FACT) so any weapon in `SoldierEntityData.WeaponUnlocks` is selectable without profile unlocks.
2. **`ClientSettings.Kit` u32 @0x18 and `GameSettings.UseCustomKit` @0x91** (FACT): kit selection by number, custom-kit toggle. INFERENCE: Kit indexes `TeamData.Kits` (@0x20) or `KitCategory`; verify by experiment.
3. **Data patch of the loaded kit**: `SoldierEntityData.Weapons` (@0x1b0, array of `SoldierWeaponAsset*`) and `WeaponUnlocks` (@0x1a0, `{WeaponSlot,Weapon*,IsBundledWithSelectable,IsSelectable}`): replace a slot's `SoldierWeaponAsset*` with any weapon asset found at runtime (asset pointers are not in the dump; find via `WeaponAssetName` String @0x40 in SoldierWeaponData or by walking WeaponUnlocks/UIWeaponData). Slot semantics: `WeaponSwitchingEnum` (wsPrimary1.. wsHandGrenade). INFERENCE: array order = slot order; verify live. Follows the repo rule 'exact-write restoration' (write must be restorable).
4. **Pickup route**: spawn/drive a `WeaponPickupEntityData` (`Weapons[WeaponPickupData{Weapon*, WeaponSlot}]`, `HasAutomaticAmmoPickup` @0x94) or `KitPickupEntityData.Kit` (SoldierAsset* @0xa0, KeepAmmoState/KeepAdditionalWeapons) near the player. Runtime classes `ServerPickupEntity`/`ClientPickupEntity` have no members in the dump.
5. Ammo: `ServerSettings.HasUnlimitedAmmo` @0xc0 / `HasUnlimitedMags` @0xc9 (FACT), or data values `FiringFunctionData.Ammo` (`NumberOfMagazines` abs `0x180`, `MagazineCapacity` abs `0x184`), `SoldierWeaponData.Hud.InfiniteAmmo` abs `0xee`, `ContinueEntityData.WeaponAmmo`.
6. Damage override per kit: `SoldierEntityData.WeaponDataOverride` (@0x19c) -> `SoldierWeaponOverrideData.Overrides[WeaponOverrideData.Values{ValueType,Value}]`.
7. What the dump cannot tell: the identity of any weapon (names/GUIDs live in bundles). The repo already reads `SoldierWeaponData`/`SoldierEntityData` at runtime (anchors pass), so enumerating live `SoldierEntityData.Weapons`/`WeaponUnlocks` gives the actual weapon list.

### 7.5 Console / command route

1. No global console type in the dump. The only console-ish data type is `ConsoleCommandEntityData.Commands` (ArrayBase<String> @0x80); runtime `ServerConsoleCommandEntity` (44 B). INFERENCE: this entity executes the same console grammar as the settings; useful only if a level graph can be extended.
2. The Settings classes themselves (section 1.8) are the practical console surface; their registered short names must be recovered from the stubs at `0x534600`.

### 7.6 Verification to do before depending on any of this

- Disassemble the arg3 stubs `0x534600..0x535560` and the nearest string xrefs to confirm the settings names and defaults and the command-line grammar (not in the dump).
- Reverse the 12-byte `String` (`flags 0x415d`) layout before writing `Level`/`SpawnPoint`.
- Find the singleton instances (not in the dump) and confirm field offsets by reading live values (e.g. `GameSettings.MaxPlayerCount` vs expectation) per the repo rule 'verify native signatures, ABI, object relationships and ownership before hooks'.
- Test each toggle for effect with the actual game; per AGENTS.md, no feature is enabled without specific evidence and headset/runtime claims need real game checks.