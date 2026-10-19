# Documentation index

Ordered for a developer picking this project up cold. Read the four sections in
order; each lists the overview first, then the detailed records.

Root [AGENTS.md](../AGENTS.md) is the development policy and
[STATUS.md](STATUS.md) is the maintained overview. Source and the selected
package determine actual behavior. Dated records describe their own checkpoint:
their "current", "next", branch assignments and approval restrictions are
history, not standing instructions. Keep their signatures, layouts, geometry,
failure cases and reproduction details even when the implementation moved on.

## 1. Engine map (Frostbite 1.5 / BC2 build 795745)

Start here:

- [Engine and architecture reference](handover/engine-and-mapping.md): process
  topology, hook points, frame channel, render path and source map.
- [Engine map signature index](re/BC2_ENGINE_MAP_20261008.md): one table of
  addresses, byte signatures and evidence levels (DIS / SV / LO / INF).
- [Architecture and source map](ARCHITECTURE.md) and the full
  [architecture diary](ARCHIVE-ARCHITECTURE-20261013.md).

Reverse-engineering records in [re/](re/):

- Weapons and firing: [weapon runtime](re/BC2_WEAPON_RUNTIME.md),
  [firing states](re/BC2_DISASM_FIRING_STATES.md),
  [pump and dry fire](re/BC2_DISASM_PUMP_AND_DRY_FIRE.md),
  [weapon sounds](re/BC2_DISASM_WEAPON_SOUNDS.md),
  [native recoil](re/BC2_NATIVE_RECOIL.md),
  [recoil aim basis](re/BC2_RECOIL_AIM_BASIS_20261012.md),
  [five weapon mechanisms](re/BC2_FIVE_WEAPON_MECHANISMS.md),
  [weapon skin branch](re/BC2_WEAPON_SKIN_BRANCH_20261012.md),
  [foreign weapon loading](re/bc2-foreign-weapon-loading.md),
  [weapon switch input](handover/weapon-switch-re.md).
- Soldier, inventory and give paths:
  [soldier and weapon lookup](re/BC2_DISASM_SOLDIER_AND_WEAPON_LOOKUP.md),
  [give path](re/BC2_DISASM_GIVE_PATH.md),
  [give cross-check](re/BC2_PC_GIVE_CROSSCHECK.md),
  [give-weapon verification asks](re/BC2_GIVE_WEAPON_PC_ASKS.md),
  [entity spawn](re/BC2_DISASM_ENTITY_SPAWN.md),
  [checkpoints and AI](re/BC2_DISASM_CHECKPOINT_AND_AI.md).
- Level start and test levels: [SP start path](re/BC2_DISASM_SP_START_PATH.md),
  [newSP fast start](re/BC2_FAST_START_NEWSP.md),
  [cutscene skip](re/BC2_DISASM_CUTSCENE_SKIP.md),
  [SP_02 level data](re/BC2_SP02_LEVEL_DATA.md),
  [community test-level sources](re/BC2_TEST_LEVEL_COMMUNITY_SOURCES.md),
  [test level routes](BC2_TEST_LEVEL_ROUTES.md).
- Rendering: [render pipeline](re/BC2_RENDER_PIPELINE_RE_20261010.md),
  [eye parity](re/BC2_EYE_PARITY_POST_RE.md),
  [late-latch depth](re/BC2_LATE_LATCH_DEPTH_RE.md),
  [context retarget](re/BC2_CONTEXT_RETARGET_RE.md),
  [resolution apply](re/BC2_NATIVE_RESOLUTION_APPLY.md),
  [graphics settings contract](re/BC2_GRAPHICS_SETTINGS_CONTRACT.md),
  [effect FOV compensation](re/BC2_EFFECT_FOV_COMPENSATION_20261011.md),
  [null client weapon FOV](re/BC2_NULL_CLIENT_WEAPON_FOV.md),
  [native prop submit](re/BC2_NATIVE_PROP_SUBMIT_20261012.md),
  [rendering reconciliation](BC2_RENDERING_RECONCILIATION_20261011.md),
  [runtime eye quality](BC2_RUNTIME_EYE_QUALITY.md).
- Hands, skinning and meshes: [hands architecture review](re/BC2_HANDS_ARCHITECTURE_REVIEW_20261011.md),
  [hands ready](re/BC2_HANDS_READY_20261011.md),
  [hand capture](re/BC2_NATIVE_HAND_CAPTURE_20261011.md),
  [hand mesh](re/BC2_NATIVE_HAND_MESH_20261011.md),
  skin [geometry](re/BC2_NATIVE_SKIN_GEOMETRY_20261011.md),
  [cap layout](re/BC2_NATIVE_SKIN_CAP_LAYOUT_20261011.md),
  [seam](re/BC2_NATIVE_SKIN_SEAM_20261011.md) and
  [implementation](re/BC2_NATIVE_SKIN_IMPLEMENTATION_20261011.md),
  [ammo materials](re/BC2_NATIVE_AMMO_MATERIALS_20261012.md).
- Type information: [typeinfo dump](BC2-TYPEINFO-DUMP.md),
  [harness types](re/BC2_TYPEINFO_HARNESS_TYPES.md); the generated dump lives in
  `profiles/typeinfo/795745/` and `tools/bc2_typeinfo_dump.py` regenerates it.
- Crashes: [NAM hand grenade crash](re/BC2_NAM_HANDGRENADE_CRASH_RE.md),
  [campaign exit](CRASH-20260925.md).
- Discovery overview: [native discovery](BC2_NATIVE_DISCOVERY.md).

## 2. Reload and weapon system

- [Physical reload system and path to AS2 quality](handover/reload-system.md)
  and the [October 8 reload handoff](handover/reload.md).
- Why BC2 is harder than KF2: [code-level deep dive](handover/kf2-vs-bc2-reload-deep-dive.md),
  [wiring comparison](handover/kf2-vs-bc2.md),
  [upstream reload comparison](handover/friend-ai-comparison.md).
- Design: [event mailbox and server authority](handover/reload-wiring-design.md),
  [fail-closed empty gate review](handover/reload-closed-gate-review.md),
  [shared weapon-system pipeline](WEAPON-SYSTEM-PIPELINE.md),
  [weapon profile pipeline](WEAPON-PROFILE-PIPELINE.md),
  [magazine pipeline coverage](MAGAZINE-PIPELINE-COVERAGE.md),
  [native mechanisms roadmap](native-weapon-mechanisms-roadmap.md).
- Current reload records: [physical reload](BC2_PHYSICAL_RELOAD.md),
  [reload parity](BC2_RELOAD_PARITY.md),
  [feedback matrix](BC2_RELOAD_FEEDBACK_MATRIX.md),
  [native reload sampling](re/BC2_NATIVE_RELOAD_SAMPLING.md),
  [reload lifecycle](re/BC2_PHYSICAL_RELOAD_LIFECYCLE_20261011.md),
  [reload audio](re/BC2_PHYSICAL_RELOAD_AUDIO_20261014.md),
  [audio/animation identity](re/BC2_RELOAD_AUDIO_ANIMATION_IDENTITY_20261014.md),
  [hand retention](re/BC2_RELOAD_HAND_RETENTION_20261014.md),
  [SPAS pump lifecycle](re/BC2_SPAS_PUMP_LIFECYCLE_20261014.md),
  [pump rack grasp evidence](re/BC2_PUMP_RACK_GRASP_EVIDENCE_20261011.md),
  [native action capture](re/BC2_NATIVE_ACTION_CAPTURE_20261012.md),
  [support acquisition](re/support-handoff-20261012.md),
  [recoil transfer](BC2_RECOIL_TRANSFER.md),
  [belt-fed plan](BELT-FED-RELOAD-PLAN.md).
- Dated weapon, magazine, SPAS, XM8, optic, holster and hand records
  (`RELOAD-*`, `SPAS-*`, `XM8-*`, `OPTIC-*`, `BODY-*`, `HAND-*`, `WEAPON-*`)
  sit at the top of this folder and keep their original citations.

## 3. Harness, tests and tooling

- [Test tooling survey and harness plan](handover/test-tooling.md).
- [Replay input and tapes](REPLAY_INPUT.md), [test range](BC2_TEST_RANGE.md),
  [gun event log](GUN-EVENT-LOG.md), [native trace](NATIVE_TRACE.md),
  [in-game harness scripts](../tools/harness/README.md).
- Transport: [frame channel](FRAME_CHANNEL.md), [D3D11 bridge](D3D11_BRIDGE.md),
  [OpenXR host](OPENXR_HOST.md).
- Running it: [launcher](LAUNCHER.md), [controls](CONTROLS.md),
  [building](../BUILDING.md), [PC build and test loop](handover/pc-builds.md).
- Simulated-player and headset runs (`SIMULATED-PLAYER-*`, `HEADSET-*`,
  `PLAYTEST-*`) record what was observed, including failures. They do not
  establish the current build or universal weapon support.

## 4. Open work and roadmap

- [Current status](STATUS.md) and [open work](OPEN_WORK.md), with the full
  [open-work archive](ARCHIVE-OPEN_WORK-20261013.md).
- Roadmaps: [Frostbite porting](FROSTBITE-PORTING-ROADMAP.md),
  [full-body IK](FULL-BODY-IK-ROADMAP.md), [optics and ADS](OPTICS-ADS-ROADMAP.md),
  [single-player release plan](SINGLEPLAYER-RELEASE-PLAN.md),
  [VR quality review](VR-QUALITY-REVIEW.md),
  [Frostbite VR research program](FROSTBITE-VR-RESEARCH-PROGRAM.md),
  [Battlefield 2142 transfer](2142-TRANSFER.md),
  [vehicles and campaign](BC2-VEHICLES-CAMPAIGN.md).
- Handoffs: [October 8 handoff](handover/HANDOFF-2026-10-08.md) with its
  [assessment](handover/handoff-2026-10-08-assessment.md) and
  [comparison](handover/handoff-2026-10-08-comparison.md) sections, later
  `HANDOFF-*` notes, the [handoff archive](ARCHIVE-HANDOFF-20261013.md) and
  the [October 18 reconciliation](BC2-OWNER-RECONCILIATION-20261018.md).

## Historical records

The former [status](ARCHIVE-STATUS-20261013.md),
[handoff](ARCHIVE-HANDOFF-20261013.md), [open work](ARCHIVE-OPEN_WORK-20261013.md),
[work log](ARCHIVE-WORK_LOG-20261013.md) and
[architecture diary](ARCHIVE-ARCHITECTURE-20261013.md) are preserved in full.
The [handover/](handover/) folder holds the October 8 project reports written
outside the repo, copied in unchanged apart from personal-data redaction; their
line numbers refer to the checkpoint they reviewed.

Update the relevant current guide when behavior changes; put detailed run
history in a dated record. Use `git ls-files '*.md'` to find tracked docs.
