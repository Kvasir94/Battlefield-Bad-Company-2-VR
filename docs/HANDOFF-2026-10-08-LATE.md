> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 VR development handoff October 8 2026

Prepared October 8 at approximately 11:30 PM America/Chicago (October 9 04:30 UTC). This is the latest runtime and source checkpoint; older handoff sections remain historical. All paths below are relative to `<repo>` unless absolute.

## Start here

The implementation is on `main`, pushed to **origin-bc2 only**. Latest implementation commit is `cfc62a5ec6b25fc95b83b75ade25a1d8f6cdf430`. The working tree was clean before this documentation change.

**Source and selected binaries differ intentionally.** The normal launcher still uses `c2083ba` binaries. The `cfc62a5` native DLL is built and CPU-tested in `build/offline-x86`, but has NOT been deployed or tested in game. Do not describe its launcher fix as headset-accepted.

No `BFBC2Game.exe`, `BC2XrHost.exe`, or `BC2NativeTrace.exe` process was present at the handoff check. The latest session ended gracefully. **Do not automatically relaunch because the PC or headset reconnects.** Verify actual process state before any later launch; never kill stale remembered PIDs.

The next work is to reconcile the latest SteamVR evidence, prepare a controlled next test of the offline changes, and diagnose remaining stereo/pose problems with matched evidence. Do not blindly copy native effect objects or add guessed launcher roll offsets.

## User intent and rules

- Premium, modern VR feel comparable to KF2 VR and AS2. Reload and weapon switching are priorities; architecture refactors and complete replacements are allowed. Existing implementation and measured tuning values are evidence, not law.
- BFBC2 build 795745, 32-bit Frostbite 1.5, single-player local server plus client. Each weapon has three firing copies; the server copy is authoritative. A server ammo write propagates to the clients and HUD.
- We own the installation and every player uses the mod. Unmodded multiplayer compatibility is not a target. Permanent overrides and replacing native animation are allowed.
- Verify native signatures and calling conventions before hooking/calling. Write only to positively identified player-owned objects. Never commit game files or credentials.
- Read-only/no-commit/no-game-test restrictions from the initial discussion were later lifted. Commits and testing are authorized. Respect subsequent live-test pauses and no-relaunch instructions.
- Enable working features with an environment kill switch rather than accumulating default-off evidence gates.
- Work directly on main when coordinated and safe; preserve unrelated work. Push to `origin-bc2` (the private development repository), never `origin` (upstream).
- Up to four local sol 6.1 medium/high agents are authorized; no cloud agents. Keep one build/runtime owner and scope other work precisely.
- During worn testing, preserve observations, keep hypotheses separate, and respond briefly. Keep real controls and replay/synthetic input separate.

## Commits and build identity

| Commit | Contents | Validation |
| --- | --- | --- |
| `8b6da5e22cc96d1e6177b630e67344237dd6de93` | Fix delayed reload-failure haptic null dereference | Reproduced offline failure then passing regression; preserved symbols/dump |
| `c2083ba0c35ef21ca4d8e6433bc1f2e3a788f971` | Async GPU eye copies; viewport synchronization; menu-before-mirror ordering; smooth turn; server-owned native reload recovery on long B | CPU/fake-runtime/GPU bridge checks and worn sessions; unresolved visual symptoms below |
| `cfc62a5ec6b25fc95b83b75ade25a1d8f6cdf430` | Separate launcher visual-family admission from native switching; consistent authored fallback; periodic control/authority telemetry | Isolated x86 DLL build and four focused CPU suites passed; not deployed |

Selected normal-launcher files, both worn sessions:

```text
build/x64/BC2XrHost.exe
C3528316A8E3A4D27C50FEA1B7BC381C14EAAB714D97DFB00C1CCAB53592820A
build/x86/BC2NativeProbe.dll
E3D97B8C221592D83DA19820C1755DCB66EE4B334B88F0B8AFFB99E3C080B91D
build/x86/BC2NativeTrace.exe
8B9F901B58F0C1856D5F1B463DEE55F4BCD3529B0A08073BF7ECB43B84C779EB
```

New, unselected native DLL:

```text
build/offline-x86/BC2NativeProbe.dll
E0B3657281CD0627AF374F6519B4C4377D0BB8B22927C9FE4B407CEA920F24B9
build/offline-x86/BC2NativeProbe.pdb
A71B4A8AC28013AE110A221C82836D8CBF8D47E952A2566BA8FFE17658A7BDF5
```

`reports/OFFLINE-NEXT-BUILD.json` contains the exact source-file hashes, binary identity, tests and nondeployment status. `reports/PLAYTEST-READY.json` describes the older selected candidate and its first session; it is not sufficient evidence of the latest session status or a deployment of cfc62a5.

Rollback and selected c2083ba binaries/PDBs are preserved under `reports/headset-candidate-20261009-033855/`. The rollback host hash starts `41BE9FA3`, native DLL `5B473A51`; these are the pre-c2083ba pair, not current launcher binaries.

## Session evidence and shutdown state

| Session and trace | Runtime checked in host report | Outcome |
| --- | --- | --- |
| `reports/native-xr-session-20261009-034359-790`, `reports/native-trace-20261009-034402-338` | `VirtualDesktopXR`, reported system `Oculus Quest2` | Ended Oct 8 10:55:38 PM CDT / Oct 9 03:55:38 UTC; host exit 0; game-exit cause unestablished |
| `reports/native-xr-session-20261009-040907-302`, `reports/native-trace-20261009-040908-916` | `SteamVR/OpenXR`, system `SteamVR/OpenXR : oculus` | Ended Oct 8 11:25:45 PM CDT / Oct 9 04:25:45 UTC; stop requested, graceful host shutdown, not forced, host exit 0 |

Both sessions used the c2083ba hashes above, real controllers, no synthetic hands, and requested 1920x1080. The second session was launched elsewhere while this lane worked offline; do not attribute it to the private VDXR wrapper or assume its effective turn configuration from the first launch.

**New handoff finding:** the SteamVR report records 15,371 async copies queued, 15,241 completed, **130 timed out**, zero other copy faults and zero rejected pairs. This is materially different from the Virtual Desktop run's 23,347 queued/completed and zero copy timeouts/faults/rejects. Correlation with visible symptoms has not been analyzed. Do not merge the two runtime results or claim the async path is timeout-free across runtimes.

Both final `native-trace.json` files are empty. First bootstrap reports probe exit 0; second reports probe exit 1. Host exit 0 and a graceful stop do not prove successful native finalization. The newly added periodic authority telemetry is intended to preserve evidence when final reports are absent.

Preserved copies:

- `reports/ended-c2083ba-20261009-0404/`: first ended session/trace, SHA manifest, cadence-by-minute, assessment and stereo audit.
- `reports/ended-c2083ba-steam-20261009-0425/`: second ended session/trace and SHA manifest, copied for this handoff.
- `reports/worn-c2083ba-20261009-0351/`: another lane's bounded worn observations and delayed launcher/restart notes.
- `reports/native-xr-session-20261009-034359-790/playtest-observations.jsonl`: this lane's observation ledger; `observation-20261009-035316/` is its intermediate snapshot.

User planned a PC restart after pausing. A disconnect around 03:59 UTC and reconnect around 04:03 UTC are not independent crash evidence.

## Verified reload behavior and remaining accounting limits

`reports/reload-acceptance-c2083ba-034402-338.json` records two XM8 server-native reloads for client weapon `0x69cd740`:

- First: loaded/reserve `3 + 217 -> 31 + 189`, native state-12 transfer at tick 13326, ready chamber afterward, server shot at tick 13477 and further shots.
- Second: `21 + 189 -> 31 + 179`, transfer at tick 13818, ready chamber afterward, later same-weapon server shot at tick 25078.

All three firing copies converged. This proves native refill, finite reserve debit and subsequent firing for these cycles. Full retained/held/dropped magazine-custody conservation was not captured in the missing final authority report. Managed Transfer bypasses the old gun-event transfer counter, so a zero legacy transfer counter is not evidence that these reloads failed.

Current implementation: hold right/gun-hand B for 600 ms to request automatic native reload; short release preserves physical eject/rack. Unique source samples measure the hold. Long hold consumes once; focus/identity loss cancels without replay. Physical frontends cancel and drain before queuing the authority transaction. Only Ready emits Reload; Running stops the pulse. Fire and physical commands are blocked while owned. Retained pocket ammo may be pooled; dropped magazines remain unavailable. Native switching keeps its own ownership checks. Stowed/cancelled running transactions retain accounting obligations until native idle; live cancellation across switching is not yet accepted.

Verified native seams retained from the implementation audit:

- Step `0x6e62d0`, complete size `0x6c4` fingerprint, `void(__thiscall*)(void*,void*,float)`, stack cleanup 8; reload gate `0x6e654c` inside Step.
- Transfer uses discovered `stateBinding.code[4]`, size 254, fingerprint `d83485215d90a16a`, `void(__thiscall*)(void*,unsigned)`. Only owned SERVER firing and the ordinary state-12 return caller `base + Step.rva + 0x561` (`0x6e6831` at preferred base) are admitted. Client transfers remain suppressed.
- Existing discovered Update ABI is `void(__thiscall*)(void*,void*,unsigned)`; reload context bit is flags `& 4` at context+`0x2c`.
- Relevant files: `Bc2PhysicalWeaponRuntime`, `Bc2ReloadFlowRuntime`, `PhysicalWeaponState`, `Bc2ReloadButtonGesture`, and the Gameplay gather integration.

## Worn observations that remain unresolved

- Black borders on both sides during head turns; twitchy guns and subjectively low FPS. User could not say better or worse.
- Muzzle flash near the muzzle but floating/rotating sideways with hand rotation. Transform cause unproven.
- Exact user wording: **smooth locomotion is functioning**. This is not acceptance of smooth turning.
- Sprint sometimes dashes sideways. Body/world rotation mismatch is a hypothesis.
- Hold-B reload looked good visually; subsequent offline event analysis supplied the specific refill/refire proof above.
- Damaged building edge differs between eyes; after an explosion, persistent ghosted building/wall lines **only in the left eye**. Screen-space displacement/postprocessing is a hypothesis.
- Grenade launcher becomes strongly sideways/canted, described as “almost gangster”; grenades launch at an odd angle. Visual model, common effect/root getter and projectile transforms must be distinguished.
- Prior issues still open: weapon in front of walls, magazine/handle cues and detached presentation, support cancellation on unsuccessful weapon flicks, consistent sprint direction, and broader modern-VR polish.

## Launcher consistency fix awaiting game validation

Before cfc62a5, Gameplay admitted the scoped 40mm visual axes only if the full bidirectional native mode-switch map resolved. RigPublication independently suppressed authored fallback for any `40mmgl` short-name profile. That could leave no admitted model axes and no authored fallback.

`ResolveWeaponVisualFamily` now returns visual family identity only, never a native command. It requires fresh selected-mesh evidence, exact scoped-XM8 configuration/persistence, unique owned selection and counterpart, and identity rechecks. Native `ObserveWeaponModeFamily` and `ResolveWeaponMode` remain unchanged. RigPublication uses the actually admitted `input.weaponProfile` for baseline suppression. No angle constants or native hooks were added.

The resolver uses a fresh QPC reading after selected-mesh observation; using Gather's earlier clock would reject same-gather fresh snapshots. The existing cached native family binding remains needed by sight/body/support consumers.

Exact launcher events in the first run: selection ticks 43967 and 44429, weapon `0x7878ce0`; six server shots at 44677, 45419, 45746, 46035, 46325 and 46606. Pose matrices/admission decisions are absent, so the observed cant is not causally proven to be this inconsistency. Launcher translated-muzzle correction is disabled, but the common weapon-getter basis still feeds upstream effects/root consumers; do not infer projectile direction is unaffected.

## Rendering findings and next diagnostic

Checked first-session counters: 46,714 viewport matches, no viewport synchronization required, no ownership/size/readback failures, and no camera restoration failures. The viewport setter at `0xb862f0` has a complete 42-byte signature: ECX=view, stack pointer to four DWORDs, writes only view+`0xc58..0xc64`, `ret 4`. Runtime also checks the existing 48-byte disk/live match. No arbitrary vtable calls were added.

Around the reported 03:51 UTC symptom, a 58.28-second sample measured 114.47 XR ended frames/s, 35.34 fresh pairs/s and 79.32 native world callbacks/s, with zero no-world submissions. These are telemetry rates, not measured headset display FPS. Most fresh pairs were 22–50 ms old. A single-lease request/render/copy/ack cycle is a plausible throughput constraint, not yet an isolated cause. Uniform pair age does not establish the cause of persistent left-only ghosting.

Offline installed-executable disassembly found a concrete unobserved native-state candidate:

- DrawView `0xbb1db0` reads per-view fields beyond camera blocks. Getter slot 47 at `0xb86580` reads view+`0x1640`; slot 52 at `0xb86630` reads +`0x1644`.
- Slot 56 at `0xb866b0` returns view+`0x15d0`. Draw at `0xbb3990` calls it, tests block+`0x25`, then `0xbb39ba..0xbb39df` forwards +`0`, +`4`, +`0x10`, +`0x20`, +`0x24` to graphics virtual slot+`0x8c`. Draw also reads view+`0x1530`.
- These effect fields are not synchronized by camera/viewport writes. Their semantics and values during the explosion remain unproven. **Do not blanket-copy the range:** +`0x1540` and +`0x1548` include owned resource pointers.
- The old selected-view callback at `0xbdbc70`, vtable `0x147b680`, is a weak fade candidate: world-draw slot 5 is `0x969d80` (`ret 4`); consumer `0xbe1d90` uses camera position for distance in DynamicCubemap work.
- Cached public upstream `origin/main` at `98c7cf8` and initial checkpoint `a820f76` use the same creation pipeline and no explicit effect-state copy. Friend's exact public build/settings remain unknown beyond Q3 with SteamVR/Steam Link.

Smallest useful stereo diagnostic: rearmable capture of four consecutive complete pairs after the late symptom. Key every record by exact lease/request/native frame/eye. Capture raw owned view+`0x1530..0x1660` and all four camera blocks before visibility, before Draw and after Draw; final eye color; ordered viewport/shader/RTV/DSV/PS-SRV/depth/blend/constants transitions. Preserve repeated transitions and reduced-resolution passes. Use bounded staging and nonblocking polling. The current observer only captures the startup frame, hardcodes a 1920x1080 pass filter, and deduplicates resource combinations; it cannot establish the late explosion cause.

## Test and launch operations

c2083ba passed x64 ActionPolicy, ControllerInput, AsyncFrameChannel, OpenXrFeedback, fake OpenXrPresentation with LegacyFenced async on/off, and fake OpenXrRecovery scenarios 1/2/3/4/6. GPU bridge checks passed 8 synchronous and 32 queued pairs, including x86-to-x64 at 1920x1080. x86 camera, ledger, controller, physical-interaction and hook-ABI checks passed. A corrupt ActionPolicy object/library caused one intermediate link failure; rebuilding that object/library and relinking repaired it, and affected tests passed. This was not a runtime crash.

cfc62a5 built with MSVC x86, Ninja, RelWithDebInfo, GPU tests off in `build/offline-x86`. WeaponModeTests, Bc2AuthoredGripTests, PhysicalWeaponStateTests and ControllerInputTests all returned 0. See `reports/offline-followup-build.log`, `reports/offline-followup-tests.json`, and `reports/OFFLINE-NEXT-BUILD.json`. Do not overwrite the selected build merely because an offline build exists.

Normal entry points are `Play-BC2VR.ps1` and `Start-BC2VRSession.ps1`. `reports/Start-Headset-Candidate.ps1` is a private hash-checking wrapper for the OLD c2083ba candidate: it explicitly selects VDXR, smooth 90 degrees/s, async copy on, viewport sync on, and 1920x1080. It must be reconciled with a new selection rather than silently reused as proof of cfc62a5 deployment.

Relevant environment controls:

```text
FVR_TURN_MODE=smooth or snap
FVR_SMOOTH_TURN_DEG_PER_SEC=90       accepted range 30..360
FVR_XR_ASYNC_CONSUMER_COPY=0        synchronous-copy kill switch
FVR_BC2_VIEWPORT_SYNC=0             viewport-sync kill switch
XR_RUNTIME_JSON                   process-local runtime selection
```

Smooth turning uses a 0.2 deadzone, source-sample integration once, 50 ms maximum integrated step, and neutral recovery after gaps over 150 ms. Vertical weapon flicks suppress simultaneous turning. Native effective mode was not in c2083ba periodic telemetry; cfc62a5 adds `controls` and `physical_weapon_authority` every second. Authority snapshots use try-lock, cached state only, and serialization outside the mutex. Busy samples explicitly report unavailable. No live native reads/calls or game writes occur in this telemetry path.

## Resume order and remaining lanes

1. Recheck branch, uncommitted work, processes, selected hashes and actual OpenXR runtime. Preserve any later session before overwriting artifacts.
2. Analyze the latest SteamVR run's 130 copy timeouts versus the VDXR run; correlate age, resource epochs and no-world intervals. Do not increase timeout/pose-age limits blindly.
3. Coordinate a deliberate selection and test of cfc62a5 when requested. Verify effective controls and token/count telemetry, exact launcher profile/poses, native reload/refire and switch/cancel behavior. Keep replay separate from real controls.
4. Add the bounded late stereo capture before patching unidentified effect fields. Investigate visual root, projectile and muzzleflash frames independently.
5. Continue broader architecture and polish work. Existing IK remains the immediate baseline. Floating hands assets and palette mapping are under `reports/floating-hands-intake/`; actual GPU palette remap/owner/layout proof remains outstanding. Vehicle spawning remains lower-priority offline reverse engineering; do not call guessed native creators.

Other documentation/mechanism lanes had unmerged work reported earlier. Reinspect their actual branches/commits before merging; this handoff does not certify their current state. Local agents used in this lane finished their scoped tasks; their findings and changes are integrated above. Do not infer that an agent-message relay authorizes messaging another user-owned chat.

Original context sources include the repo engine maps, `<kf2-repo>`, and the earlier supplied HANDOFF, reload, weapon-switch, KF2-vs-BC2 reload and PC-build reports. Preserve checked-versus-inferred distinctions when continuing.
