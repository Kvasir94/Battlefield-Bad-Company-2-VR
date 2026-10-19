> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Frostbite VR research program and project handoff

Prepared for the owner. Research checked on 9 October 2026 UTC.

This is a research roadmap for three separate game-mod projects: Bad Company 2 VR, a possible Battlefield 3 VR project, and a possible Battlefield 4 VR project. It does not commit to starting either new port, promise universal Frostbite compatibility, or describe either new title as supported. No repository was edited, code imported, software installed, game launched, or content published during this research.

## Executive recommendation

1. **Resolve the recent-project check before selecting an implementation target.** This pass did not verify a newly started, native BF3 or BF4 VR project through an attributable developer announcement plus implementation evidence. It did find an October 5 claim that someone is investigating BF3/BF4, historical demos, active stereo wrappers, and a Warsaw Revamped VR suggestion. Those are materially different evidence classes. Global GitHub search timed out, so this is a qualified finding, not proof that no private or unindexed project exists.
2. **Keep BC2 as the active product and evidence source.** Its current worn-performance and crash questions remain open. Preserve the owner's selected source revision and known checkpoint rather than importing a newer public snapshot or using passing synthetic tests as a substitute for headset acceptance.
3. **Provisionally prioritize BF3 for a bounded feasibility study.** Public VU documentation gives specific camera-transform and input surfaces, a self-hosted test route, and extensive game-data references. BF3 also has title-specific evidence of native stereo output through the HD3D route. None of this yet proves a usable OpenXR or motion-controller integration.
4. **Keep BF4 as a serious alternative, especially for campaign-first work.** Its documented offline Test Range and BF4-specific Frosty profiles are concrete advantages. If BF3's permitted test route or render-only stereo scheduling is blocked while BF4's local range is reproducible, BF4 can become the better first prototype. Warsaw's access status alone should not decide the order.
5. **Share only proven tools and contracts.** Each game owns its executable identification, hooks, animation and weapon semantics, asset decoding, packaging, and acceptance. Extract common utilities only after a second real consumer demonstrates the same invariant.

The next decision is whether to authorize a short discovery study after the recent-project check, not whether to promise a finished BF3/BF4 mod.

## 1 Recent VR development check

### What was actually established

| Candidate or lead | Dated evidence | Classification and implication |
| --- | --- | --- |
| `justiceofrip/FrostbiteVR` | Main commit `761b49a06a7b68c34c2996d990f30c1cefa4a6b6`, committed 2026-10-08 23:37:45 UTC | Active BC2 development. README explicitly says BF3 and BF4 are future unsupported ports. The inspected tree has `src/games/bc2` only, no BF3/BF4-named paths, one returned branch (`main`), and no returned GitHub releases. This is not evidence that BF3/BF4 development has started elsewhere. [README](https://github.com/justiceofrip/FrostbiteVR/blob/761b49a06a7b68c34c2996d990f30c1cefa4a6b6/README.md) |
| Unnamed person investigating BF3/BF4 | Reddit comment dated 2026-10-05 in the indexed thread | The comment says someone is checking the two games, without an author identity, repository, technical description, or demonstration for that work. Retain as an unresolved lead; do not turn it into either a confirmed project or a denial. [Exact comment](https://www.reddit.com/r/virtualreality/comments/1wy4pqt/comment/pdztf6d/), [readable parent thread](https://www.reddit.com/r/virtualreality/comments/1wy4pqt/than_you_random_modder_for_dropping_the_best/) |
| Warsaw Revamped VR support | Official feedback board, indexed in the week preceding this research | A Virtual-Reality Support suggestion is visible. A suggestion or roadmap mention is not a running VR implementation. The extracted listing does not establish a reliable status/date for this specific item. [Feedback board](https://feedback.warsaw-revamped.com/?tags=wr-experience-upgrades) |
| `effcol/wiz3D` BF3 support | Release `v0.0.27`, published 2026-04-14 23:14:45 UTC; inspected README at `058e60fc46c1969b6379579ea0d50c75b69acb39` | A stereoscopic wrapper. The release reports campaign side-by-side output with possible resolution issues; the current README identifies BF3 as a mostly working HD3D case with a depth issue. Useful evidence of an existing stereo path, not native tracked-hand VR or an October port announcement. [Release](https://github.com/effcol/wiz3D/releases/tag/v0.0.27), [source README](https://github.com/effcol/wiz3D/blob/058e60fc46c1969b6379579ea0d50c75b69acb39/README.md) |
| vorpX BF3 and BF4 profiles | Current official supported-games list retrieved 2026-10-09 | Both titles are listed for stereoscopic 3D. That list does not establish independent hand poses, native weapon manipulation, native reload transactions, or a new game-specific project. Controller gesture-to-button mapping should not be relabeled as those capabilities. [Official list](https://www.vorpx.com/supported-games/) |
| BF3 video credited to Hattiwatti | ShadowSix video published 2018-06-12 | Historical demo lead. Its description credits Hattiwatti; video and linked implementation could not be fully inspected in this pass. Do not classify its exact degrees of freedom or claim it is abandoned without better evidence. It clearly is not a newly published 2026 demo. [Video](https://www.youtube.com/watch?v=G24qFAHG5xw) |
| BF4 Cyberith demonstration | Cyberith video published 2014-07-23 | Historical hardware/control demonstration using a Virtualizer, Rift and Wii-Mote. It does not establish a new 2026 native six-degree-of-freedom hand/weapon integration. [Developer demonstration](https://www.youtube.com/watch?v=fbu8OUq1CUE) |

BF1942 VR, BF2142 VR, BF3 Reality Mod, BF-themed Contractors content, and a generic Frostbite-labelled repository must not be counted as BF3/BF4 VR implementations without title-specific evidence. The BF1942 project's own README is explicitly about BF1942. Reality Mod's own site identifies it as a BF3 mod using VU; “Reality” in the name is not a VR claim. [BF1942 README](https://github.com/JayBiggsGMG/BFVR-Battlefield-1942-VR-Mod/blob/main/README.md), [Reality Mod](https://bf3reality.com/)

### Search coverage and limits

The search covered full title names, BF3/BF4 aliases and VR/OpenXR/OpenVR/6DOF terms; recent date-filtered and unfiltered web queries; GitHub-indexed results; YouTube-indexed demonstrations; recent VR community threads; VU and Warsaw official material; and direct inspection of the two named repositories. An October 8 discussion of current mod projects supplied a BC2 self-report but did not validate a BF3/BF4 implementation in the text retrieved. [Discussion](https://www.reddit.com/r/virtualreality/comments/1x0y0nl/to_all_the_modders_that_visit_here_regardless_of/)

GitHub's global repository and commit search operations timed out for multiple relevant queries. Direct known-repository reads succeeded. GitHub search pages were not retrievable through the web reader. Some Reddit direct links and video pages also failed to load, although indexed descriptions were available. Private Discord work, unindexed new repositories, and private prototypes were not searched. No developer was contacted.

Representative search groups were full-title VR-mod/development queries with and without 2026/September/October filters; `BF3VR`, `BF4VR`, `Battlefield3VR` and `Battlefield4VR`; title plus OpenXR/OpenVR/6DOF; and title-restricted GitHub/YouTube queries. Global GitHub repository queries included `battlefield vr`, `bf3 vr`, `bf4 vr` and `FrostbiteVR`; commit queries included `BF3 VR`, `BF4 VR`, `Battlefield VR` and `Frostbite VR`. Their timeouts were not empty-result evidence. An additional indexed July 5 post titled “With my new found boredom I will make Battlefield VR adapter” could not be fully retrieved or tied to a specific BF3/BF4 implementation, so it remains an older unverified intent lead. [Post](https://www.reddit.com/r/Battlefield/comments/1uodk1d/with_my_new_found_boredom_i_will_make_battlefield/)

**Decision gate:** before choosing a new port, resolve any user-supplied candidate link and retry public project discovery through a working search route. A useful candidate record must have: author/project identity; original announcement date; latest relevant commit or demo date; exact supported title/build; visible implementation or an attributable developer description; stereo method; head translation; head/aim separation; controller/hand behavior; tested mode; and access/license status. A recent repository timestamp alone is not enough.

If an active project is found, compare its verified progress and license before deciding whether to contribute, independently research a missing subsystem, or select the other title. Contacting the developer or uploading local evidence is a separate action requiring the owner's authorization.

## 2 Preserve the BC2 source of truth

The owner's selected source for this study is the private development repository at `97a5b712805c609c10ad9e56436bf53a05a82996`. The connector verified that this repository is private and accessible. Its README, porting roadmap, playtest checkpoint, and a small set of interface files were inspected read-only. Historical claims in those files remain tied to their own checkpoint and cannot establish the current local runtime state. Selected tree, [playtest record](PLAYTEST-CHECKPOINT-20261008.md)

The public `justiceofrip/FrostbiteVR` snapshot is a separate comparison source. Its newest handoff explicitly records failed checkpoint245 headset acceptance, a SPAS interaction lock, and a subsequent checkpoint-reload crash. It says the first cancellation and crash cause were not established by surviving telemetry. Do not transplant that status onto the owner's selected build, import its changes, or assume its source/binaries match the owner's local workspace. Its useful program-level lesson is that extensive passing tests did not close human-input and scene-lifetime failures. [Checkpoint245 handoff](https://github.com/justiceofrip/FrostbiteVR/blob/761b49a06a7b68c34c2996d990f30c1cefa4a6b6/docs/CHECKPOINT-245-HANDOFF.md)

The existing BC2 porting document proposes a shared VR core and future adapters. For this program, retain its evidence discipline while changing the product framing to **separate game mods with optional shared libraries**. No universal BC2 adapter is assumed. [Existing roadmap](FROSTBITE-PORTING-ROADMAP.md)

### Lessons worth carrying forward

- A native render producer and an OpenXR host need a complete stereo-pair lifetime, including producer, resource, scene, tracking-space and source-pose identity. A valid texture handle is not enough.
- Camera, culling, render scheduling and first-person animation hooks are game-specific. Render-only work must not advance simulation or animation once per eye.
- Ammunition belongs to the native authoritative simulation, including local single-player authority. VR reload gestures request transactions and await exact completion; they must not synthesize ammo or retire unresolved cycle debt.
- Preserve authored native poses as input, then apply independent VR hand/object state. Never feed last frame's VR-modified palette back as fresh native animation.
- Persistent weapon identities outlive equip slots and transient pointers. Cancellation recovery must preserve durable item state while releasing inappropriate hand/control locks.
- Replay/simulated input is useful evidence but remains separate from ordinary input and actual worn-headset acceptance.
- Count fresh native stereo pairs, host submissions, compositor display frames and reused/reprojected frames separately. “90 FPS” without that distinction is not a reliable outcome.
- Failures at load, checkpoint restart, recenter, focus loss and shutdown are first-class tests. Preserve first-failure logs and the original failed diagnostic result.

These are design and test lessons, not proof of cross-title code portability. BC2's own current worn performance and crash acceptance remains an open workstream.

## 3 BF3 and BF4 comparison

EA identifies BF3 as Frostbite 2 and BF4's manual identifies Frostbite 3. The supplied program context identifies BC2 as Frostbite 1.5. Engine family names do not determine ABI, pointer size, object layout, renderer interfaces or asset formats. [BF3 primary source](https://www.ea.com/games/battlefield/battlefield-3), [BF4 PC manual](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-BF4/b4-pc-en.pdf)

| Criterion | BF3 research position | BF4 research position |
| --- | --- | --- |
| Current native VR project overlap | No newly active implementation verified in this pass; unresolved recent lead | Same; Warsaw VR suggestion is not implementation evidence |
| Campaign route | EA documents single-player. A repeatable campaign checkpoint must be proven on the owned installed build | Campaign plus a documented offline Test Range; exact current launch and protection behavior still needs local verification |
| Controllable modded sandbox | VU documents self-hosted servers and public client/server APIs | Warsaw advertises self-hosting and Luau modding; currently advertised access is early access for insiders/invited testers |
| Render-relevant starting evidence | VU has world-view and first-person transform hooks; wiz3D reports BF3 native HD3D stereo output | DirectX 11/11.1 and alternate Mantle path documented by DICE; no equivalent native-VR contract verified here |
| Asset/data research | VU types, EBX references and data documentation are public | Frosty contains a BF4 SDK/profile. This is a data-tooling advantage, not a runtime rendering SDK |
| Main unknown | Can a permitted BF3 process produce stable per-eye rendering, correct visibility and pose timing without double simulation? | Same, plus exact selected graphics backend and asset/animation differences |
| Input/authority research | VU offers input concepts/events and server scripting, which may simplify a local gameplay harness | Native campaign/Test Range authority must be mapped; Warsaw APIs/access must be inspected before relying on them |
| Multiplayer scope | Separate later project milestone; VU compatibility would not prove vanilla compatibility | Separate later milestone; local Test Range/campaign results would not prove protected online compatibility |
| Recommendation | First feasibility candidate if access and recent-project checks pass | Credible fallback or second independent study; can move first if its safe local range is the clearer route |

This is an evidence-weighted recommendation, not a measured difficulty ranking or schedule estimate. BF3 is not presumed easier because it is closer to BC2 in release order. BF4 is not rejected because Warsaw is gated.

## 4 What the ecosystems expose and what they do not

### BF3 and Venice Unleashed

VU separates client, server and shared Lua scripts and exposes WebUI plus runtime game-data access. That can support controlled map/loadout fixtures and authority observations in an authorized private VU environment. It is not evidence that the retail campaign runs through VU. [Modding overview](https://docs.veniceunleashed.net/modding/)

Specific documented surfaces are useful discovery leads:

- `Client:UpdateClientGameViewTransform(transform)` supplies a world-view transform hook. [Reference](https://docs.veniceunleashed.net/vext/ref/client/hook/client_updateclientgameviewtransform/)
- `Client:UpdateFirstPersonTransform(transform)` is a distinct first-person transform hook. [Reference](https://docs.veniceunleashed.net/vext/ref/client/hook/client_updatefirstpersontransform/)
- `Input:PreUpdate(cache, deltaTime)` and `Player:UpdateInput(player, deltaTime)` expose input stages. [Input hook](https://docs.veniceunleashed.net/vext/ref/client/hook/input_preupdate/), [player event](https://docs.veniceunleashed.net/vext/ref/client/event/player_updateinput/)
- `FPSCamera:Update(deltaTime)` is an event; its name does not establish stereo rendering or render-thread ownership. [Reference](https://docs.veniceunleashed.net/vext/ref/client/event/fpscamera_update/)

**Still unproved:** per-eye render targets, asymmetric OpenXR projections, shared/per-eye visibility, render-only replay, temporal history isolation, GPU resource ownership, authoritative muzzle translation, source-pose skinning, runtime DLL interoperability and camera/input behavior in campaign. A Lua transform callback by itself solves none of these contracts. Prototype a native bridge only after the relevant platform permits it and the missing boundary is understood.

VU's data guide distinguishes superbundles, bundles, resources, chunks and EBX metadata. A resolved weapon metadata record does not mean the mesh, bind pose, animation or all required level bundles are loaded. The same guide provides a public reference dump, useful for semantic discovery but not a substitute for the exact installed build. [Data guide](https://docs.veniceunleashed.net/vext/guides/data/)

VU documents a full up-to-date licensed BF3 installation, account ownership linkage, and a server key for hosting. The current Windows setup page describes EA-app activation, while older FAQ pages still say Origin. Treat that as documentation age to reconcile, not permission to use obsolete credential-handling commands. A private local server can still depend on internet authentication; do not label it fully offline. Server keys and account linkage require their own approved setup steps. [Prerequisites](https://docs.veniceunleashed.net/hosting/prereq/), [Windows setup](https://docs.veniceunleashed.net/hosting/setup-win/)

The official launch-argument documentation includes an unlisted-server option. Unlisted is discoverability, not access control: keep the test environment restricted and verify actual joinability. Do not open router/firewall ports or advertise a server merely to run a solo test. [Launch arguments](https://docs.veniceunleashed.net/general/args/)

Fun-bots is a separate BF3 VU mod with its own supported-map behavior. It can eventually provide reproducible moving actors, but should be excluded from the first minimal rendering baseline to avoid unnecessary variables. No bot or VU result should be described as campaign acceptance. [Project README](https://github.com/Joe91/fun-bots)

### BF4 and Warsaw Revamped

Warsaw's official site describes a BF4-specific platform for self-hosted servers and Luau mods, requiring a purchased game. Its indexed official site/translation content currently describes insider/invited-playtester early access. Direct page retrieval returned a JavaScript shell and the developer portal could not be inspected, so concrete API signatures and the owner's eligibility remain unverified. [Official site](https://warsaw.gg/), [official website strings](https://translate.warsaw.gg/projects/dragon/web/general/en/)

Those website strings also state that joining a LAN server requires internet at join. A 2025 team reply describes server operation after initial authentication. These address different stages and do not prove a cold-start, permanently offline route. A permitted future test must separate initial ownership validation, server startup, client join, ongoing play and rejoin. [Team discussion](https://www.reddit.com/r/WarsawRevamped/comments/1nabzyl/)

The stronger immediately documented BF4 research route is its own offline Test Range. The manual describes practice with unlocked multiplayer weapons/gadgets and the main vehicle types. It also documents campaign. This does not guarantee every weapon is available, campaign and range share identical owners, or today's launcher has no network dependency. Verify the installed route before selecting it. [PC manual, printed pages 7 and 47](https://akamai.cdn.ea.com/eadownloads/u/f/manuals/GAME-BF4/b4-pc-en.pdf)

### Frosty and Rime

The inspected legacy Frosty tree at `e400dcb283ad743071263ea7b34ff86780ffe910` contains `FrostySdk/Profiles/BF4SDK.dll`. The rewrite at `ee2a587af394fc784b309bb7990dab051065330f` contains an explicit BF4 profile. The rewrite README still describes early development with no released functional UI and directs users to legacy 1.0.6.3. Therefore record exact tool/version and successful read-only extraction separately; do not promise working export/import based on a profile's presence. [Legacy tree](https://github.com/CadeEvs/FrostyToolsuite/tree/e400dcb283ad743071263ea7b34ff86780ffe910/FrostySdk/Profiles), [rewrite README](https://github.com/FrostyToolsuite/FrostyToolsuite/blob/ee2a587af394fc784b309bb7990dab051065330f/README.md), [BF4 profile](https://github.com/FrostyToolsuite/FrostyToolsuite/blob/ee2a587af394fc784b309bb7990dab051065330f/FrostyEditor/Profiles/Battlefield4.json)

Frosty rewrite issue 26, “Implement support for Frostbite 2 games,” remains open, last updated 2026-05-25. This is a reason not to infer BF3 support from BF4 support. It is not proof that every external Frostbite 2 tool is absent. [Issue](https://github.com/FrostyToolsuite/FrostyToolsuite/issues/26)

VU's documentation describes Rime's intended asset role, but retrieved Rime author reports are historical. Do not make a new project depend on assumed current Rime availability or complete round-trip support. [Author progress report](https://kiwidog.me/2020/10/4-drafts-4-years/)

Review licenses independently before any code reuse. The inspected legacy Frosty README states CC BY-NC-ND 4.0; the rewrite repository presents GPL-3.0. Public visibility is not an unrestricted import license, and neither grants Battlefield asset redistribution rights. This research imports no code. [Legacy license statement](https://github.com/CadeEvs/FrostyToolsuite/blob/e400dcb283ad743071263ea7b34ff86780ffe910/README.md), [rewrite license](https://github.com/FrostyToolsuite/FrostyToolsuite/blob/ee2a587af394fc784b309bb7990dab051065330f/LICENSE)

## 5 Protection and permitted test environments

EA's current help article explicitly identifies PunkBuster as relevant to BF4 and warns that altered game environments can trigger integrity violations. Do not generalize newer Battlefield Javelin/EAC news to BF3/BF4 without title-specific evidence. Conversely, the Frosty BF4 profile's `HasAntiCheat: false` is tool metadata, not a finding that retail BF4 lacks protection. [EA PunkBuster help](https://help.ea.com/en/articles/technical-issues/punkbuster-troubleshooting/)

The BF4 service rules restrict unapproved software while using the service. This is a practical scope boundary, not a claim that a community mod is approved by EA. [BF4 rules](https://tos.ea.com/legalapp/ROC/US/en/PC/BF4)

Before any later live experiment:

1. Verify the user owns the exact game and content. Record storefront, executable path/hash/version, architecture, installed DLC and selected mode without collecting account secrets.
2. Establish a documented, permitted campaign or isolated local modding route. Observe the actual process, renderer and protection state. “Local,” “offline,” and “private” are not interchangeable guarantees.
3. Do not disable, bypass, evade, patch, or tamper with anti-cheat, ownership checks, protected services or their traffic. If the intended experiment requires that, stop and change the research question or request a supported route.
4. Keep retail online play outside the experiment. A modding platform's own safety statement does not extend to arbitrary additional native hooks or public servers.
5. Preserve the normal installation and settings, with reversible staging and an exact cleanup manifest. Do not auto-join multiplayer from a probe launcher.
6. Do not redistribute game executables, extracted meshes/textures/animations or proprietary SDK binaries. Prepare derived caches from the user's own installation where permitted; publish only mod-owned code and rights-cleared fixtures if publication is later authorized.

## 6 Architecture for separate projects

Give each title its own release identity, supported-build matrix, executable allowlist/validation, launch route, native integration, content manifests, tests and user-facing status. Repository topology can be decided later; logical ownership must be clear now. A shared toolkit does not make BC2 the runtime adapter for another game.

### Candidate shared components

| Component | Existing evidence | Reuse gate |
| --- | --- | --- |
| Coordinate math, action edges, recenter and comfort policies | BC2 source contains isolated math/interaction utilities | Run synthetic fixtures with a separately derived title coordinate conversion; no native layout assumptions |
| Pair ownership and transport | Versioned fixed-width frame and texture protocols exist | Prove matching process/device/format/epoch semantics on the new title; validate same-GPU ownership and teardown |
| OpenXR host | Separate host is part of the BC2 architecture | Use with a synthetic producer first, then prove source-pose/image pairing with the real new renderer |
| Interaction state machines | Grips, ownership, reload intent and inventory policies exist | Feed actual title identities and native receipts; exercise delayed/repeated/cancelled transitions |
| Capture and auditing conventions | BC2 has useful traces and audits | Define a versioned canonical schema plus a title-specific decoder; do not rename incompatible fields to make old audits pass |
| Asset/profile inspection tools | Data-driven authored geometry is a useful pattern | Verify each game's formats, skeleton roles, bind transforms, units and source provenance |

At the selected BC2 revision, `ModuleApi.h` exports module metadata, not a complete gameplay/render plugin ABI. `Bc2Module.cpp` deliberately advertises zero native capabilities, while the working native-probe route is separate. `FrostbiteCamera.h` explicitly contains observed BC2 camera-layout assumptions. These are evidence against assuming a ready universal module boundary. Document the real integration stages before extracting anything. [Module API](../include/fvr/engine/ModuleApi.h), [module implementation](../src/games/bc2/Bc2Module.cpp), [camera conversion](../include/fvr/engine/FrostbiteCamera.h)

The selected `IRenderAdapter` contract requires render-only eyes, stereo visibility and restoration. It is a useful invariant to preserve, not proof that any real engine implements its calls synchronously. The wire protocols are fixed-width and the current graphics descriptors constrain accepted DXGI formats. A BF4 Mantle path cannot simply pass through a D3D11 texture contract. Prefer a verified DX11 baseline for first research; DICE documents DX11/11.1 and Mantle as separate choices. [Render contract](../include/fvr/runtime/FrameCoordinator.h), [frame wire format](../include/fvr/ipc/FrameProtocol.h), [texture contract](../include/fvr/graphics/SharedTextureProtocol.h), [DICE renderer explanation](https://www.ea.com/games/battlefield/news/mantle-renderer-now-available-in-battlefield-4)

Title-specific work includes native scheduling and thread ownership; culling/temporal history; world and first-person projections; actor/input/weapon authority; animation and palette publication; materials, optics, HUD and effects; campaign scripts, vehicles and transitions; ABI and failure restoration. Keep those in each game's project even when class names look similar.

## 7 Phased evidence gates

The numbers below are proposed engineering acceptance criteria, not achieved measurements or universal comfort standards. Confirm hardware, headset, resolution and refresh before setting a performance target.

### Gate A Existing work and access

Deliver an up-to-date candidate register for new BF3/BF4 VR work, and an owned-build/test-route manifest for each title under consideration. Finish with a reasoned choice, a bounded next experiment and a stop condition. If a credible active project already solves the target subsystem, prefer evaluating that result before duplicating it.

**Pass:** recent leads are resolved or explicitly qualified; exact title/build/mode is known; permitted local/campaign route is identified; no protected-service modification is required. An unverified route is a blocker, not an implementation task.

### Gate B Baseline and read-only discovery

For one title, map camera/actor ownership, view and projection data, frame boundaries, graphics backend/device, animation evaluation, input sampling and scene transitions. Begin with static/source/document inspection; any live observation is separately authorized and limited to the selected route.

**Pass:** three repeatable unmodified launches into the same scene; exact build manifest; measured coordinate basis and scale; bounded object/lifetime report; proposed hooks supported by multiple signals rather than a copied offset. Preserve a vanilla reference capture and frame-time distribution. If the scene cannot be reproduced reliably, fix the baseline before VR work.

### Gate C Monitor-only native stereo

Choose between a verified native stereo path and explicit per-eye rendering based on evidence. BF3's HD3D result is a lead to inspect, not an automatic implementation choice. Do not default to double-ticking the game or alternating inconsistent eyes.

**Pass:** 1,000 consecutive complete fresh pairs in a simple scene; zero mixed owner/epoch/pose pairs; two genuinely different eye views; one simulation/animation update per pair; correct restoration after each injected stage failure; correct visibility of moving objects near eye-frustum edges. Visually inspect first-person meshes, shadows, particles, transparencies, depth effects and any enabled temporal processing in both eyes. Distinguish a flat/screen demo from this gate.

### Gate D OpenXR display and lifecycle

First test the host with a synthetic producer, then the real title. Record pair generation, GPU copy completion, host submission, runtime display and frame age separately.

**Pass:** a 15-minute repeatable scene run without a crash, pair mismatch or unrecovered resource fault; ten repeated focus/recenter/scene-reload cycles without stale owner state; correct source-pose association on delayed frames; successful shutdown/normal relaunch. Measure CPU/GPU P50/P95/P99 and reused-pair ratio. For a 90 Hz target, 11.11 ms is the frame interval, not a promise that this prototype meets it. Set a fresh-pair target and explicitly report any reprojection instead of counting it as new rendering. A worn check remains necessary after technical passes and stops immediately on visual instability or discomfort.

### Gate E One ordinary infantry weapon

Implement head/aim separation, one primary weapon, independent support-hand state, ordinary fire, native button reload, pause/menu interaction, and a conservative locomotion/comfort path. Physical reloads are not required to establish this gate.

**Pass:** 100 controlled single-fire actions with authoritative shot accounting; 30 equip/unequip or lifecycle transitions with no stale weapon state; no unexpected fire after focus/tracking loss; controller-aim/native-shot agreement measured at fixed distances; no native-pose feedback loop. Treat automatic-fire input pulse timing separately from an exact-one-shot test. Head motion must not silently move a collision-authoritative body through walls.

### Gate F One physical mechanism family

Use one exact weapon configuration. Define contact geometry, magazine/shell ownership, transaction IDs, server/local-authority receipts, chamber/cycle prerequisites, cancellation, resume and fallback behavior.

**Pass:** 30 ordinary-input reload cycles and explicit remove/return/discard/replacement/cancel cases; zero fabricated or lost rounds beyond native rules; acknowledgements consumed once despite duplicated/delayed packets; preserved state across holster and redraw; recoverable hand ownership after tracking loss and rejected actions. Test native conservation and headset feel separately. Do not scale to every gun after one success.

### Gate G Broader content and release readiness

Expand weapon configurations, optics, HUD, campaign cameras, vehicles and roomscale collision as separate capabilities with separate evidence. Add a campaign progression/restart matrix. Multiplayer remains a separate design and permission gate, with no implication from local authority success.

**Pass:** reproducible clean package and uninstall, exact source/binary manifests, required regression suites on supported architectures, tested build/runtime matrix, rights-cleared payload, and feature-specific acceptance. An installer or public release requires its own explicit publication decision.

## 8 Principal risks and mitigations

| Risk | Why it matters | Next evidence or mitigation |
| --- | --- | --- |
| Duplicate a new community project | Latest work may be private or newly indexed | Resolve the October lead and any user-supplied links before committing to a port |
| Safe test route unavailable | Ownership, launcher, platform and protection constraints may block probing | Verify exact route first; stop instead of bypassing controls |
| VU/WR mistaken for a rendering SDK | Gameplay hooks can leave the hardest stereo work untouched | API-to-requirement matrix with unsupported renderer contracts marked unknown |
| Shared names mistaken for shared layouts | Frostbite generations and individual builds can differ | Per-build discovery, coordinate measurements and lifetime evidence |
| High headset FPS masks stale pairs | Compositor output may repeat/reproject old source | Separate counters and source-age histograms |
| Native state machine lock | Cancelled physical action can block unrelated controls | Persistent real-input tests, exact acknowledgements and first-failure recording |
| Level changes invalidate resources | Scene, actor, device and animation owners can change independently | Generation-tagged leases, transition fault injection and verified restoration |
| Asset tooling overclaimed | A metadata reader is not a complete rig/animation exporter | Small known-asset extraction with topology/bind/material validation before batching |
| License or asset leakage | Public tools do not grant all copying/distribution rights | Keep provenance; no code imports now; separate license and payload reviews later |
| BC2 delivery slowed by speculative framework work | Generalization can create regressions before any second consumer | Separate product branches/projects; freeze BC2 checkpoint; extract minimal proven utility only |

## 9 First bounded research tasks

These are proposed tasks to authorize or assign next. They are not launches, installations, account setup or implementation already performed.

1. **Verify recent BF3/BF4 VR projects.** Resolve the October 5 lead, review any supplied developer/demo links, retry public repository discovery through a functioning route, and classify each result. Output a short register with exact dates/revisions and a confirmed/inconclusive judgment. Stop once each concrete lead is resolved or its access limit is recorded; do not keep searching indefinitely for proof of absence.
2. **Compare owned local test routes.** For BF3 and BF4, inventory the actual installed build and ordinary launcher route through the authorized computer task. Check campaign and BF4 Test Range availability without injection. Separately determine whether VU/WR access already exists. Output a capability/access manifest; stop before new credentials, account links, software installation, protection changes or external networking.
3. **Map VU requirements to APIs.** At the pinned documentation revision, map camera, first-person transform, input, player lifecycle and server authority surfaces to the VR contract. Mark everything still requiring native integration. Output the smallest monitor-only experiment proposal; no claim of campaign support.
4. **Audit BC2 boundaries read-only.** Use selected revision `97a5b7...` to identify which reusable candidates have native dependencies. Reconcile exported metadata with the working probe route, and identify telemetry needed to preserve current BC2 failures. Output a reuse-candidate list with dependencies and tests, not a framework rewrite.
5. **Plan one asset sample per candidate title.** Record a tool's exact revision/license and choose one owned weapon with known source/bundle identities. Define checks for skeleton topology, native bind pose, attachment transforms, scale and material references. Stop at a reproducible inspection plan until extraction is authorized and available.
6. **Choose one render experiment.** Only after the previous evidence, select BF3 or BF4 and write a single-scene, monitor-first experiment with explicit success/failure criteria and restoration. If the route or renderer boundary is not viable, return a no-go finding rather than launching a broad port.

These can run in parallel where read-only; one owner should control any later live native experiment. Do not give simultaneous workers ownership of the same runtime files or local game session. Keep the existing documentation owner responsible for repository integration of this roadmap.

## 10 Ready-to-use handoff prompts

### Recent project verification

Research whether native BF3 or BF4 VR mod development has very recently started. Use primary developer repositories, announcements and dated demonstrations. Resolve the October 5 unnamed BF3/BF4 investigation lead and inspect any candidate the owner supplies. Distinguish native stereo plus tracked head/hands from vorpX/HD3D wrappers, historical demos, roadmap promises and mods for other games. For each candidate return identity, original/latest date, exact title and build, revision/demo link, actually evidenced features, license/access and uncertainty. Report search failures. Do not contact developers, install anything or claim absence from an empty search.

### Owned build and test route inspection

On the specifically authorized computer, inspect the user's BF3/BF4 installations and record executable hash/version/architecture, storefront, DLC, launcher, selected renderer and available campaign/local modes. Read existing project instructions first. Use ordinary unmodified launch only if that is explicitly authorized for this task. Verify BF4 Test Range and BF3 campaign separately from VU/WR. Record observed protection state without changing it. Do not install software, enter secrets, generate keys, change firewall/security settings, join public servers, inject code or modify protected services. Return the exact safe route or blocker and the smallest next experiment.

### BF3 render feasibility proposal

Using the selected BF3 build and permitted test route, map the public VU camera/first-person/input APIs and any title-native stereo evidence to required VR rendering stages. VU gameplay scripting does not prove stereo targets, culling, render-only scheduling, temporal history or campaign support. Propose one monitor-only experiment that can prove distinct eye views from a coherent simulation state and restore original state on failure. Separate documented facts from hypotheses. Do not import BC2 offsets or implement a universal adapter. Stop if the route requires anti-cheat or ownership bypass.

### BF4 render feasibility proposal

Using the selected BF4 build, assess campaign and the documented offline Test Range as independent test contexts. Verify DX11 versus Mantle before proposing graphics interop. Evaluate BF4-specific Frosty data tooling and Warsaw APIs only to the extent actually accessible; a profile flag is not protection evidence. Produce one-scene camera/renderer/owner inventory and a reversible stereo experiment proposal. Preserve stock launch and protected multiplayer. Do not copy BF3 layouts, disable protection or redistribute extracted assets.

### Shared utility audit

Read the BC2 source at `97a5b712805c609c10ad9e56436bf53a05a82996` and identify candidate utilities for separate BF3/BF4 projects. Prioritize pair leases, fixed-width transport, coordinate math, telemetry and interaction-policy fixtures. Inspect native dependencies and document the real working probe path rather than assuming ModuleApi is a complete runtime ABI. Return a dependency/test matrix and a minimal extraction proposal that requires a second demonstrated consumer. Keep game hooks, ammunition authority, skeletons, persistent item identities and capture decoders title-specific. No repository edits or imports in this audit.

## 11 Handoff evidence package

Every later implementation handoff should contain:

- Task scope, owner, selected title/mode and allowed actions
- Exact repository revision, local changes and source/binary/config hashes
- Executable/build/storefront/DLC identity and documented permitted launch route
- Hardware, driver, graphics backend, runtime, headset, per-eye resolution and refresh
- Capability states: uninvestigated, documented, statically inspected, runtime observed, native verified, headset accepted; each with a source
- Scene/actor/weapon/device/space generation rules and restoration behavior
- Pair counts, source ages, frame-time distributions, GPU waits, loss/reuse/reprojection rates
- Ordinary versus simulated-input results and authoritative gameplay receipts
- Original failures, first-failure logs, dump availability and remaining uncertainty
- Asset and code provenance, exact tool versions and redistribution exclusions
- Next bounded experiment, expected observable result, stop condition and rollback

No field should be silently filled with an assumption. “Not measured,” “not accessible,” and “not yet authorized” are legitimate outcomes.

## 12 Source revision register

All resources below were read-only. Website content may change after the research date.

| Source | Revision or retrieval boundary |
| --- | --- |
| The owner's BC2 source | `97a5b712805c609c10ad9e56436bf53a05a82996`; selected by the project owner, private connector access verified |
| Public FrostbiteVR comparison | `761b49a06a7b68c34c2996d990f30c1cefa4a6b6`; 2026-10-08 23:37:45 UTC; inspected main tree, README, latest handoff, branches and releases |
| VU documentation source | `9ecf5fb7a122b080611369e9d124e6c29abd4029`, committed 2026-08-07; [repository](https://github.com/VeniceUnleashed/VU-Docs/tree/9ecf5fb7a122b080611369e9d124e6c29abd4029); published pages checked 2026-10-09 |
| Legacy Frosty | `e400dcb283ad743071263ea7b34ff86780ffe910`, committed 2023-09-29, resolved from `1.0.6.3`; tree and README inspected |
| Frosty rewrite | `ee2a587af394fc784b309bb7990dab051065330f`, committed 2025-08-22; tree, README and BF4 profile inspected; issue 26 currently open |
| wiz3D | `058e60fc46c1969b6379579ea0d50c75b69acb39`; README and release metadata inspected; BF3 release evidence at `v0.0.27` |
| EA BF3/BF4 pages and BF4 manual | Retrieved 2026-10-09; historical documents establish features, not today's installed process behavior |
| Warsaw | Official site/indexed first-party strings checked 2026-10-09; dynamic developer portal not inspected; API and account access unverified |
| Recent-project scan | Performed 2026-10-09 UTC; public/indexed scope with global GitHub search and some media retrieval limits disclosed above |

**Final decision status:** retain BC2 priority; no new port selected or started; BF3 is the provisional first feasibility candidate, BF4 remains a credible alternative; verify recent community work and the actual safe test routes before choosing.

## BC2 worn-test update — 9 October 2026 UTC

This remains a planning document; no new port is selected or started. The worn
existing-IK test with reported source `97a5b71` had much better **subjective** FPS
than the prior worn failure, then the XR host exited with `0xC0000005` while BC2
was still responding. This is not measured FPS or a new architectural conclusion.
See the [permanent playtest record and evidence follow-up](PLAYTEST-20261009-EXISTING-IK.md).
