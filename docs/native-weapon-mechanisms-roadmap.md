> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Native weapon mechanisms and proposed VR roadmap

Research date: 2026-10-09. Source baseline: `9705f342ebfd30a409b56c01c5c8b954ade34e84`.
Scope: LOOK + PLAN; one documentation artifact, no weapon implementation.

## XM8 finding first

For exact `XM8_sp_s`, the existing C2 investigation reports **no shipped
hand-operated charging-handle rack**. Its VR stroke came from native **fire-cycle**
part travel: `jntWpn_17`, below `jntWpnwpnJnt_16`, reaches 120.8 mm in
`1P_FireStart.res` (reported hash prefix `8271c742`). The return was unsupported
in `1P_Fire/1P_FireRelease`. This is existing reported asset evidence, not a
fresh clip inspection by this task. See [C2](2142-TRANSFER.md) lines 2435–2473 and
[Equipment](../src/games/bc2/Bc2MagazineEquipmentProfile.h) lines 31–59.

Consequently, the manual pull, its grip zone, owed-ready gate and spring return
are VR additions using an existing native part. **The native empty versus
tactical selected animation, and any bolt-release gesture, remain unresolved.**
The report's “no shipped clip” wording is retained as a prior-investigation
claim for its inspected XM8/F2000/AEK work; it is not independently certified for
every configuration, branch, unsupported track or BC2 weapon. Do not interpret
it as “XM8 has no charging-handle geometry” or “native empty reload has no bolt
release.”

The older [Catalog](../src/games/bc2/Bc2MagazineActionCatalog.h) explicitly calls its action/hold-open defaults
REAL-FIREARM defaults, with every `verified=false`. Those defaults are not
native animation evidence. [Chamber](../src/games/bc2/Bc2MagazineChamber.h) comments that the native empty
clip “is the rack”; that is an implementation rationale contradicted in scope
by the later C2 distinction between fire-cycle motion and a manual hand rack.
It must not certify a clip. This research does not change source or the user's
current reload policy.

## XM8 follow-up: shared cached receipt and playtest limits

The existing hands cache owner supplied these bounded findings on 2026-10-09
through existing-file reads only. No new extraction, pose, game test or runtime
mutation occurred. This task received the owner's findings/provenance; it did
not independently read private bytes or certify native interpolation.

Private receipt (local evidence, not published): `reports/floating-hands-intake/xm8-contact-receipt.json`,
SHA-256 `d7ace068d91c1ff2d1ed3d0291c37f28c5f3fdd4ce03928960ccd0ef0be48596`.
Same-frame report: `1P_Reload-same-frame-contact.json`, SHA-256
`dc8bc395d03a52e1b8da92be51b82cad59fb892d38c124de219fe022a3ceb26b`.
Both remain in the same private intake. Hands owns extraction/cache and mesh
cut/cap work; no competing cache or extraction was started.

| Evidence question | New bounded result | Profile status |
| --- | --- | --- |
| Exact referenced reload | XM8_sp_s scoped tree replacement references `Animations/Weapons/Handheld/US_rgl_XM8/1P/1P_Reload`, GUID `34687db2-3aae-479c-8fce-eb2d41b65a5d`; SHA-256 `168f331bed6e231b73b9e534b4d12f4c279511c7dee2e049a8ea6e69052e098b`, duration 2.900000095 s, 176 samples at 60 Hz. | Exact reference/hash reported by shared owner; R. |
| Empty versus partial/tactical | Compiled AnimTreeInfo condition/node payload is not decoded. The referenced sample cannot be labelled empty or partial. | **U: branch/conditions and selected clip/motion difference remain missing.** |
| Weighted handle `jntWpn_17` | Orientation changes: maximum 3x3 rotation-matrix element delta 0.2007155 from first sample; translation is numerical epsilon. | Orientation change R; **not** radians/degrees or proof of a hand pull/release. |
| Carrier `jntWpnwpnJnt_16` | Stationary within numerical precision in the referenced reload. | Scoped sampled result R; not a global absence claim. |
| Weighted part `jntWpn_4` | Maximum displacement about 2.901 mm from first sampled weapon-relative position. | Motion R; role/contact/actuation U. |
| Magazine `jntWpn_6` | Moves in referenced reload. | Existing magazine/contact lane; no bolt semantics inferred. |
| Same-frame hand data | Both wrists, 30 finger joints, 15 weighted parts, item-local wrists and wrist-local fingers; no required/unsupported-track gap reported for this sampled data. | Candidate data R; **hand actuation and action contact not established**. |
| FireStart cross-check | SHA-256 `8271c742805a359daa3a248caeceed292a69d250da6c9e0bd2c55aeeee32429a`; carrier+handle sampled displacement 119.885 mm. | Sampled maximum from first frame, not cumulative travel/continuous maximum; distinct from prior C2's reported 120.8 mm. |
| HandsIkPose | SHA-256 `3c00d55c52d8cb99251d5f67e722ab791108fa20195c2a3a84f692b3711c4ef7`. | Static reference identity R; no action-contact admission. |

Motion basis: canonical reflected-Z **row-vector metres**, bone relative to
`jntWpn_1` from the same frame: native boneWorld * inverse(native weaponWorld),
then Z reflection. Displacement is maximum Euclidean displacement from first
sample. Clip basis metadata is absent and inherits explicitly paired ske01.
Part bounds use canonical bone-local reflected positions/inverse bind; those
bounds are not motion coordinates. Target native interpolation remains
unverified. Complete sampled tracks do not establish all selected runtime clips.

Latest trusted delegated playtest observation: the owner found **no discoverable
physical bolt point** and explicitly leaves the mechanism question open.
Several logged insertions were reported chambered/`needsRack=false`; one human
replacement+B attempt resumed fire, while other attempts later failed.
Exact trace/binary/transaction correlation was not supplied to this task.
These are observations, not stable acceptance, a root-cause diagnosis, native
bolt release or an authored physical point. **No new point, offset, contact pose
or mandatory rifle action is authorized by this observation.**

Advance only `referenced_reload_part_motion` and
`same_frame_pose_data_available` to reported-asset evidence. Keep
`native_empty_vs_partial_selection`, `native_action_semantics`,
`physical_action_contact` and `headset_ready_behavior` unresolved.
These are review labels, not a new runtime schema. The existing VR rack/button
shortcut remains implementation, not native proof.

### Exact shared-cache provenance

- Clip/mesh archive: `Dist/win32/async/weapon/sp_rgl_xm8_scoped-00.fbrb`;
  **index** SHA-256 `90a2e2c7be43bd85093b592a4febc9d3bff2e43c5acad6f857905ebf223b864d`.
  This is not a whole-archive hash.
- Mesh: `Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh.res`;
  SHA-256 `131824e2e98e32371ca3bcc29d179cb03a5f52cfb701406aee07d6658d43efb0`.
- LOD: `Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh_lod0_data.res`;
  SHA-256 `84f22b14e2727f2bd9b1ef54e66de1c11d1865070e3dd681c50b774043f7b99e`.
- Skeleton: `Characters/Skeletons/ske01.res`, SHA-256
  `d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09`;
  source `Dist/win32/levels/sp_common/level-00.fbrb` **index** SHA-256
  `57835d6ace174f83eeb46ec8bbfe3acf8a3e7fde5cb8c8e49f084950e975a842`.
  Rig: 147 bones, `fnv1a64:a7f219a1426216ab`.

Selected-part hashes below cover unique original position+skin bytes and
section-local triangle indices, not all 48-byte shading channels. All three
parts have zero mixed triangles in the receipt. Color/ZOnly are separate passes;
do not sum their triangle counts as unique surface geometry.

| Bone / hash | Section | Triangles / vertices | Position+skin SHA-256 | Section-local indices SHA-256 |
| --- | --- | --- | --- | --- |
| `jntWpn_17 / f7315725` | jntWpn_12_FlatMetal | 40 / 25 | `64a850e7d30f9eab1ec58214a500de6e71b856a5457f0d7fd70a4c9115447115` | `6567e68d07cc11048624c743a92912364bff7a0682f174933b2f74c6f974ad8a` |
| same bone | jntWpn_1_Rubber | 108 / 143 | `c96dc0d2726a5f5b088739294ffa7f1a5bdb3d3ca82e262b34a68a83fa6acd04` | `5d0aa6a94f436ba4d1fe5b756f0e82b226e63068c76c38388ed3b0823ff28ddc` |
| same bone | jntWpn_12_FlatMetal_ZOnly | 148 / 81 | `9840f81062d99ca2893feb683bb5558c1bfc632a679da522f2f3679bc48411ef` | `91a783c64ef3edfd3a41635d9b1bf124b82e68d0cf4fb324f03a317cf9d7e2e2` |
| `jntWpnwpnJnt_16 / f2519cbd` | jntWpn_12_FlatMetal | 98 / 136 | `b1a7355e6f82d1906ea0c3b198b22f899fc463929d8fd3e7aa9ed4cc64feb038` | `8f6e3911adc0b5efd6378eeab0ed77bee9f68d5a4e160cb806627659cf2c3f23` |
| same bone | jntWpn_12_FlatMetal_ZOnly | 98 / 54 | `07ef686794f8596a1170d6e53b7837be4599654de5a6b7b15dd265b7f1871864` | `59fee17cd05c1dd60c87a0f06fb999ba978c01a05c16b5d7327a262b671524e2` |
| `jntWpn_4 / f7f9bcd7` | jntWpn_12_FlatMetal | 4 / 8 | `2b9fe56b21164e244f69f335a82b548ee798e1b9e5f99344c504c2c327b371f3` | `dd579b42a3f908bc336b0f3756c63ce8e850d01a6e9db33041de6e50170b0d48` |
| same bone | jntWpn_12_FlatMetal_ZOnly | 4 / 6 | `02da7f9b285718e1203d28ec5aedb1d71cee685ac8d60761c04fb797712b7357` | `7b541689d57ee58a19bb4bc203ec9132b1c16dcb38712725ca698494f1ef7292` |

**Exact remaining input:** decoded compiled AnimTreeInfo conditions/node
selection for XM8_sp_s empty versus partial, joined to referenced clips.
Existing cached bone/hand motion cannot supply that selection or assign
release/pull semantics. This document requires no new game test. Ask the same
owner only for an already-available graph/receipt; further bounded decoding
must stay coordinated in that cache.

## Access, evidence levels and coordination

The first shell calls failed before process creation with setup-refresh errors.
No local archive bytes, private C2 receipts, installed mesh selection or evaluated
clips were read directly by this task. The follow-up adds the shared owner's
existing cached findings and exact provenance. Authenticated repository source and derived reports were read.
No extraction, build, game launch, injection, process interruption, GPU work or
headset test occurred. Existing reports are evidence of their recorded work;
this task only verifies their contents and current source wiring.

- **S:** source/configuration/report contents checked in the pinned repository.
- **R:** geometry/motion/contact reported by an existing asset investigation;
  not independently replayed here. Dynamic spline interiors remain candidates.
- **U:** unresolved or uninspected. Never mark such a weapon native-verified.
- Native/runtime/headset acceptance are separate, feature- and build-specific
  states; none is granted by this document.

Runtime/reload owner `01a11da3-9378-7d81-bdeb-b84a16413ec5` and crash coordinator
`01a11e2f-2e9a-71cb-948b-b6d3b294e0f7` retain runtime priority. Proposed path
`docs/native-weapon-mechanisms-roadmap.md` was sent to the runtime owner, with a
request for existing C2 report provenance and main agreement. No main agreement
was received before preparing this isolated document. Active main writes justify
the small review branch; no local checkout or default-branch mutation is made.

The hands task `01a11dea-886d-7689-bff6-b1a7f72590d8` was read; messaging it
failed with AbsolutePathBuf before a new turn. Reuse its
[floating-hands contact plan](floating-hands-contacts.md):
XM8 magazine grasp plus all 15 finger transforms already exist. The shared
follow-up supplies same-frame reload wrists/fingers, but exact action hand-to-part
grasp and semantics remain unproved. Commit `5f85e6f` and separate
documentation corrections `06e0fc3` are separate work, not merged by this task.

## Native animation and moving geometry matrix

“Reload” below means the **reported sampled resource**, not proof of an empty
or tactical runtime selection. **For every row, full empty / partial tactical
selection and comparison remain U** unless a later exact graph/capture receipt
establishes them. A clip filename, varying bone or a native bolt-release flag
does not distinguish hand pull, release-button actuation or automatic movement.

| Exact configuration scope | Reported resource/motion and actual part | Empty / tactical evidence | Confidence / remaining limit |
| --- | --- | --- | --- |
| `XM8_sp_s` | Prior C2 fire-start travel: 120.8 mm. Shared receipt: referenced reload part 17 rotates without meaningful translation, carrier 16 stationary, part 4 moves ~2.901 mm, magazine 6 moves. FireStart sampled displacement: 119.885 mm. | Selected empty/tactical nodes and bolt-release hand action unknown; C2 reports no hand rack. | S/R. Referenced reload sampled by shared owner; empty/partial condition payload undecoded; hand actuation unproved. Fire return remains unsupported in prior C2. [C2](2142-TRANSFER.md), [Contacts](../release/CONTACT-COVERAGE.json) |
| `F2000_sp` | C2: left knob `jntWpn_6`, 100.8 mm in FireStart (`2bad662c`), return in FireRelease (`3bae931c`). Later magazine carrier `jntWpnwpnJnt_16` carries top-round children 17–19. | Empty/tactical selection unknown; no native hand rack reported. | S/R. Older Contact coverage rejected nonunique role; later C2/F2000 work supersedes that specific extraction gap, not every variant. [C2](2142-TRANSFER.md), [Geometry](../profiles/checkpoint204/Bc2ExperimentalMagazineGeometry.h) |
| `AEK971_sp` | Handle-shaped `jntWpn_5` reported static in inspected shipped clips; magazine `jntWpn_6` has paired reload evidence. | Empty/tactical selection and native release unknown; no handle stroke derived. | S/R. Static reported part is not proof no handle, no rig capability or unused bone. [C2](2142-TRANSFER.md), [Contacts](../release/CONTACT-COVERAGE.json) |
| `M9` / `M9-3` (distinct configs) | Same M9 body: rigid slide `jntWpn_3`, 71.64 mm; independent magazine `jntWpn_12`. M9 slide has 2,498 triangles. | Existing reload sample does not identify empty/tactical selection or slide-stop actuation. | S/R. Slide motion exists; no authored slide-grasp window; M9-3 capacity differs. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json), [PPSnote](../release/PISTOL-PRECISION-SHOTGUN-MECHANISMS.md) |
| `MP443` | Rigid slide `jntWpn_3`, 71.98 mm, 1,440 triangles; magazine `jntWpn_12` with child 13. | Empty/tactical selection and slide-stop unresolved. | S/R. No strict complete magazine contact, no slide contact. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `M1911` Beach configuration | Exact metadata only; no exact mesh geometry binding in this inventory. | All clip mechanics U. | S/U. No slide, magazine or geometry absence claimed. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `MP412` | Native bulk-reload metadata; wrapper unsupported. | All cylinder/reload motion U. | S/U. rtMagazine does not make it a box-magazine pistol. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `M24` base config | Existing report identifies bolt and magazine motion. Rigid `jntWpn_3`: 77.15 mm + 1.245 rad, 688 triangles; `jntWpn_6`: 336.47 mm + 1.023 rad, 576 triangles. | Reload sample alone does not prove after-shot unlock/back/forward/lock sequence or empty/tactical difference. | S/R. No strict paired contact on either ambiguous part. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json), [PPSnote](../release/PISTOL-PRECISION-SHOTGUN-MECHANISMS.md) |
| `SV98` base config | Rigid part 3: 76.12 mm + 1.245 rad; magazine part 6 has paired contact. | After-shot and empty/tactical selected motions U. | S/R. Useful magazine candidate does not admit bolt-cycle mechanics. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `M95` base config | Magazine part 6 paired; required `jntWpn_3` track absent/unsupported in reported sample. | Bolt/empty/tactical comparison U. | S/R/U. Missing track is not absent geometry or an unused bone. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `SPAS12_sp` | C2 native-pump observations/side views identify fore-end/bolt presentation via parts 4, 3, 5; saved native reload spans also contain their motion. | Generic `1P_Reload` resource absent in batch; actual staged reload graph needs resolution. Empty vs partial staged order U. | S/R. Per-shot native pump exists in prior report; physical gesture/state hold is mod work. [C2](2142-TRANSFER.md), [Pump](BC2-PUMP-RELOAD-20261001.md), [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `870MCS` base config | Single-round/bolt-action metadata; animation wrapper unsupported. | Pump/empty/tactical geometry motion U. | S/U. No copying SPAS bones/offsets. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json) |
| `PKM` base config | CPU-inspected cover 12 and co-moving 11; box 6; rigid slider 4 travels 111.02 mm, 186 triangles. Cover rotation 2.371 rad. | Sampled reload motion exists, but empty/tactical tree selection and hand rack/release semantics U. | S/R. Slider semantic role unset in machine report; do not call it a charging-handle pull solely from motion. [LMG](../release/LMG-MECHANISM-COVERAGE.json), [LMGnote](../release/LMG-AUTHORED-MECHANISMS.md) |
| `M249` base config | CPU-inspected top cover 3, 2.584 rad / 3,426 triangles; box 6; articulated feed parts. | Empty/tactical selection and charging-handle/release U. | S/R/U. Reader reports unsupported tracks 4/5; geometry for 5 omitted. Omission is not missing native geometry. [LMG](../release/LMG-MECHANISM-COVERAGE.json), [LMGnote](../release/LMG-AUTHORED-MECHANISMS.md) |
| `MG36` / `XM8 LMG` exact listed configs | Drum 6 with authored removal; later coupled-motion follow-up obtains complete wrist/fingers. | Empty/tactical and handle/release U. | S/R. Drum evidence is independent of rifle XM8 handle profile. [Drums](../release/DRUM-CONTACT-FOLLOWUP.md), [LMG](../release/LMG-MECHANISM-COVERAGE.json) |

No mesh is certified as missing an independent mechanism part by this review.
No bone is certified unused. Static, unsupported, absent from one sample, omitted
from a preview and genuinely absent are different findings. Independent rigid
geometry is proven only for the reported exact part/LOD; mixed-weight pieces
need deforming or partitioned rendering rather than rigid-part substitution.

## Contacts, ammo and current mod interactions

All capacities below are authored configuration values, not guaranteed effective
runtime capacity. Native loaded/reserve counts and transfer receipts are
authoritative. A separate chamber/lock state and capacity-plus-one have not been
proved by these asset batches.

| Scope | Authored contact evidence | Native feed/action metadata | Current mod / proposed addition |
| --- | --- | --- | --- |
| XM8_sp_s | Calibrated magazine grasp + 15 fingers; static support references; shared same-frame reload wrists/30 fingers available. Handle action grasp/semantics unproved. | rtMagazine, automatic, capacity 30; exact scoped firing source retained. | Rack profile, default-on source flag; manual handle travel/60 ms return; eject-button release shortcut. All are VR policy. Magazine palm proxy is not a handle pose. |
| F2000_sp | Later paired mag sample t=0.9 s, 1.7 mm / 0.039 rad wrist drift; generated 15 fingers. | rtMagazine, automatic, 30; 3.1 s reload; zero/false bolt fields; reviewed shared dispatch shape. | Exact profile exists; actual current build/geometry inclusion and runtime behavior require owner receipt. Same manual rack/shortcut policy as XM8; native-authored action grasp missing. |
| AEK971_sp | Same-frame wrist + 15 mag fingers; checkpoint204 data reproduces earlier AEK transforms. | rtMagazine, automatic, 30; 3.2 s; shared native dispatch review. | No charging-handle registration. Preserve exact GP30 alias/config routing; invent no stroke. |
| M9 / M9-3 | Diagnostic 30 Hz mag contact, 5 windows; strict 60 Hz stable interval fails. Slide contact absent. | rtMagazine, single fire; capacities 12 / 20. | No slide/manual pistol runtime admission established by batch. Reuse slide geometry; new action pose/ready policy needs explicit proof/design. |
| MP443 | No diagnostic complete mag window or strict mag contact; slide contact absent. | rtMagazine, single fire, 17. | New contacts required; slide-stop and tactical chamber retention remain U. |
| M1911 / MP412 | No admitted mesh/contact. | rtMagazine, single fire, 7 / 6. | Resolve M1911 binding and MP412 wrapper/cylinder; no box/slide template assignment from enum. |
| M24 / SV98 / M95 | M24 strict contact fails. SV98/M95 mag candidates exist; action-hand contact absent or unsupported. | rtMagazine + fltSingleFireWithBoltAction; capacity 5 / 10 / 5; authored BoltActionTime 1.65 / 1.6 / 2.3 s. | Shared bolt recognizer is a foundation, not live family admission. No generic auto-rifle route grants these a bolt cycle. |
| SPAS12_sp / 870MCS | SPAS staged shell/fore-end work is separate from generic magazine sampler. 870 pose decode blocked. | rtSingleBullet + fltSingleFireWithBoltAction; 4; delay 0.5 s, BoltActionTime 0. | SPAS manual pump/ready hold implemented in current source/prior reports; exact worn acceptance not established here. 870 requires its own parts/contacts. |
| PKM / M249 | PKM box diagnostic contact: 16 windows; no slider or cover contact. M249 none meeting criteria. | rtMagazine bulk transfer, 100 / 200; physical belt classification separate. | Shared feed stages with exact cover/box/feed bindings proposed; no new native LMG manual reload enabled by extraction. |
| MG36 / XM8 LMG | Complete coupled drum interval + 15 fingers; interpolation/skin fit unvalidated. | rtMagazine bulk; independent drum feed proposal. | Reuse later drum route, not belt box and not rifle XM8 offsets. |

Evidence: [Contacts](../release/CONTACT-COVERAGE.json), [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json), [LMG](../release/LMG-MECHANISM-COVERAGE.json),
[Drums](../release/DRUM-CONTACT-FOLLOWUP.md), [C2](2142-TRANSFER.md), [Geometry](../profiles/checkpoint204/Bc2ExperimentalMagazineGeometry.h), [Native](../src/games/bc2/Bc2MagazineNativeProfile.h).

Source reality supersedes historical C2 “opt-in/no visual write” text:
[Gameplay](../src/games/bc2/Bc2Gameplay.cpp) line 721 makes rack default on unless
`FVR_BC2_CHAMBER_RACK=0`; [Rig](../src/games/bc2/Bc2RigPublication.cpp) lines 1336–1354 translates exact
registered parts with manual travel and a ~60 ms return. [Physical](../src/games/bc2/Bc2MagazinePhysicalReload.cpp)
lines 103–180 uses the magazine-grasp offset as a palm proxy, checks hand
occupancy but explicitly takes no shared Mechanism claim in its rack path, and
implements an eject-button shortcut that clears owed rack. Its name/comment
“bolt release” proves **mod input behavior**, not a native lever animation,
lock observation or hand contact. Fixing that ownership gap belongs in the
coordinated roadmap, not this research's code scope.

[Chamber](../src/games/bc2/Bc2MagazineChamber.h) conserves native counts and persists per-weapon VR ready state;
it does not write ammo. Unknown native chamber knowledge must stay explicit.
Older comments “none admitted/rack disabled” in Equipment are stale relative to
its exact XM8/F2000 lookup and current Gameplay wiring. Action-catalog
`verified=false` remains valid evidence status, independent of implementation.

## Profile roadmap: reuse current exact catalog, no replacement framework

Existing [ManualCatalog](../release/MANUAL-RELOAD-CATALOG.md) and `tools/build_manual_reload_catalog.py`
already separate configurations, native-family groups, meshes, calibration jobs
and unresolved observations, retaining hashes/GUIDs, native fields and tree refs.
[Workplan](../profiles/weapon-family-work-plan.json) supplies physical-family proposals; it is not runtime proof.
[Profile](../include/fvr/interaction/WeaponProfile.h) currently covers aim/support/muzzle evidence, while
[Mechanism](../include/fvr/interaction/WeaponMechanism.h) already separates feed, after-empty-feed, after-shot and
chamber knowledge. The latter is a declarative contract with no production
consumer; it lacks a distinct bolt-release action. Extend these existing data
contracts only after owner review, rather than invent a second profile registry.

Each exact configuration's reviewed profile should join:

1. Exact weapon/configuration GUID+hash, firing chain and state/tree selection,
   mesh/LOD/rig/part identities and provenance. Shared body/clip jobs may dedupe
   only identical content and compatible bindings; variant ammo remains separate.
2. Weapon family/type plus independent physical feed, after-shot action,
   after-empty-feed action, tactical behavior and exceptions. **Native behavior**
   and **chosen VR behavior** are separate columns. Handle pull, slide pull,
   release-button actuation and automatic closure are distinct operations.
3. Native count/transfer authority and explicit chamber-knowledge source;
   conserved VR ready/lock model where the engine has no corresponding state.
   Mark retained-live-round policy as VR design until authoritative proof.
4. Contact capabilities by support/magazine/slide/bolt/cover/feed/pump:
   exact hand side, moving-object-local wrist + all 15 finger transforms,
   source clip/time/digests, missing status, skin/render binding and evidence level.
5. Required work flags: reuse native geometry; partition/deform existing skin;
   new geometry only if actual independent-part absence is demonstrated;
   new authored VR pose where contact is unavailable; virtual state/visual motion
   where native motion is insufficient. Unknown is not “absent.”
6. Per-feature source/asset/native/headset status and exact build acceptance.
   Profile existence never grants gameplay authority.

### Bounded phases and asset decisions

| Phase | Work | Native reuse / explicit added work | Completion criteria |
| --- | --- | --- | --- |
| 0: evidence reconciliation | Resolve XM8_sp_s selected full/empty/tactical tree branches first; then F2000_sp and AEK971_sp. Audit existing private C2 receipts before extracting anything. | Reuse existing reader/catalog and magazine samples. Current manual rifle rack/release remains documented VR policy pending decision. | Exact graph conditions/clip hashes, complete/unsupported tracks, part-weight joins and same-frame hand evidence attached; no universal rifle rule. |
| 1: XM8 contacts and ownership | Coordinate with hands pilot; keep movement/free magazine rotation/inverted insertion. Keep physical bolt-point/mechanism question open; inspect cached samples without inventing contact. | Existing mag/support poses. Existing handle geometry and added manual travel/return. New contact pose is a conditional future option requiring an explicit decision after evidence review. | Shared Mechanism claim replaces occupancy-only rack ownership; one winner across support/ammo/action; release cannot keep ammo authority or stale grip. |
| 2: exact profile expansion | F2000/AEK plus one M9/MP443 slide and M24/SV98 bolt pilot, each separately reviewed. | Reuse measured independent parts. New slide/bolt contact poses where absent. AEK virtual stroke only as an explicit design decision after geometry audit, not copied XM8 data. | Full/empty/tactical and after-shot tests per profile; release vs pull separated; no extra rack on a verified retained-ready tactical flow. |
| 3: feed exceptions | SPAS staged tube/pump, 870 wrapper gap, one PKM/M249 belt pilot; drums stay separate. | Reuse native pump/cover/box/feed/drum geometry; incomplete contacts/unsupported tracks remain work items. | Explicit open/feed/close/cycle receipts, interruptions and count conservation; no rtMagazine-based box assumption or shotgun-category pump rule. |
| 4: either-hand foundation | Remove right-gun/left-loader assumptions from interfaces after pilot acceptance. | Shared tracked-hand and per-item state; exact side-specific pose data or reviewed mirrored/new poses. | Either hand can claim an item/contact without moving its mechanical/ammo state to the hand. Two-hand contention/handoff tests; dual wield remains deferred. |

### Architectural invariants and acceptance tests

Preserve independent weapon mechanical state keyed to real owned item identity
and actor generation through switch/stow/return; do not put chamber, owed pump or
rack into one global “current gun” variable. Each weapon retains independent
authoritative ammo source and aim/shot routing. Existing saved chamber state is
a starting point, not proof of arbitrary simultaneous weapons.

Reuse [Hand](../include/fvr/interaction/HandInteraction.h) for GunHold, dependent support/mechanism and actual ammo
object claims. Acquire from raw tracking before cosmetic attraction; explicit
transfers and expiry/identity loss remove ownership. Rendered snapping, procedural
IK/recoil and animation sampling never become fresh contact or transaction proof.
Compose immutable contacts relative to the actual moving part, so player/gun/item
translation, rotation and inversion work naturally.

Preserve server-owned ammo/transactions ([Server](../src/games/bc2/Bc2ReloadServer.cpp)); render/hand state
cannot credit rounds, release native fire or complete a cycle. Native receipt
plus the selected reviewed VR readiness policy determines completion; duplicates
cannot credit twice. No bonus chamber round from pooled loaded count.

Acceptance after implementation, owned by the runtime coordinator:

- Compare exact selected native empty/full and partial/tactical animations and
  after-shot action; correlate graph conditions, moving mesh weights, hand
  trajectory/fingers and native count/event timing. Explicitly distinguish pull,
  release, closure and recoil/fire-cycle motion.
- Same-frame source contact remains immutable under visual/recoil layers;
  translate/rotate/invert player+gun+part together; free magazine rotation,
  gun movement between contacts and upside-down insertion remain valid.
- Raw acquisition/release hysteresis, deliberate fresh input, claim contention,
  support-to-action transfer, failed transfer, expiry, tracking loss/recenter and
  item reuse; no hand owned by two systems or stale gesture reacquisition.
- Empty with reserve, empty/no reserve, partial reload, original-mag return,
  interrupted insertion/action, shot before ready, early/late pump/bolt,
  repeated rack/release, switch/stow/return and identity replacement. Ammo
  conserved across server and both client copies; no duplicate transfer.
- Exact geometry/material/LOD and one-sided visual checks for moving part/hand
  clipping; unsupported pieces not silently rendered at identity. Any new
  geometry/pose has its own private asset receipt and source provenance.
- Exact binary/configuration/runtime receipts first, then owner-controlled
  headset handling/comfort. Saved replay or synthetic passes do not certify a
  worn candidate or resolve the reported crash/performance failure.

## Remaining bounded inspection, using existing tools

Locate existing private installed inventory, common metadata, animation batch,
manual catalog, hand pose batch and C2 motion/CPU-preview receipts first.
Verify archive/resource hashes against the intended exact configurations. The
current task cannot access those files; no redundant extraction is requested.

Resolve the typed animation-tree branch/parameters for loaded=0 versus loaded>0
and staged start/loop/end/post nodes. Generic `1P_Reload` samples and archive
co-occurrence do not prove selection. Enumerate all required tracks and ancestor
chains, supported formats, duration, control payload and mesh weight mappings.
[Decoder](../release/WEAPON-ANIMATION-PIPELINE.md) explicitly labels dynamic spline interior evaluation
`decoded_spline_candidate`; compare against existing evaluated native captures
where available. Static endpoints do not validate the whole clip.

Use `bc2_weapon_animation_pipeline.py`, `inspect_reload_animations.py`,
`bc2_authored_mechanism_geometry.py`, `bc2_authored_magazine_geometry.py` and
`build_manual_reload_catalog.py`; add only missing narrow evidence after owner
coordination. The 30 Hz diagnostic motion/contact sampler and strict 60 Hz
magazine sampler remain different. “30 configs”, “27 profiles” and “73 definitions”
are not playable or verified gun counts.

Store original bytes, meshes, clips, private matrices and previews privately;
this document carries only derived findings and identity metadata. Negative
claims require a complete scoped resource/track/weight search: a constant bone,
failed wrapper or missing generic resource never proves an absent mechanism.

## Exact configuration identity index

These rows are selected from the linked saved coverage, not a new installed
inventory. Other optics/SP/MP variants remain separate rows in those reports.
Full hashes and GUIDs make each matrix scope reviewable.

- **XM8_sp_s** — `Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped.dbx`; GUID `6A066A2D-AB87-4356-AFA4-567741C395BF`; SHA-256 `23a7261d6cbaffe7c642af167edfca1a8ac818ebef551538b01a5784ef8f472f`. [Contacts](../release/CONTACT-COVERAGE.json)

- **F2000_sp** — `Objects/Weapons/Handheld/BU_rif_F2000/SP_rif_F2000.dbx`; GUID `46404A9E-9E38-43A7-831E-FB4B2F2692C6`; SHA-256 `62d1514ab5eb00161f763e67d73768d9ef8a2ee611027a2a41b6deacefa86925`. [Contacts](../release/CONTACT-COVERAGE.json)

- **AEK971_sp** — `Objects/Weapons/Handheld/RU_rgl_AEK971/SP_rgl_AEK971.dbx`; GUID `C0088CEE-FB7F-4FC2-A69C-2C59372F4F81`; SHA-256 `eef52077b1721323a66be8875c1bb4c7df1a2755b1264a36d9004534a91bfbc1`. [Contacts](../release/CONTACT-COVERAGE.json)

- **M9** — `Objects/Weapons/Handheld/US_hg_M9Beretta/US_hg_M9Beretta.dbx`; GUID `F4692BA9-A65B-420E-86E3-B34693FA384D`; SHA-256 `5e671d40c30fa8d718c16e75fb22220e47e7b40325eba4be6a95cbfefcb29f91`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **M9-3** — `Objects/Weapons/Handheld/US_hg_M9Beretta/US_hg_M9-3.dbx`; GUID `5BD5CC30-2813-44A3-BF08-6D23EAA66394`; SHA-256 `8ba71d710a9822745622e66c77ae601367fb5230676ed8834611f0ebc74d9279`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **MP443** — `Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach.dbx`; GUID `A1617BA1-B5EF-4A8C-94D2-79CD1AFD7B8A`; SHA-256 `e2057d4456fc5619b556c5db67716d5fd2f0c91eb07e7a27276811e8e670f412`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **M1911** — `Objects/Weapons/Handheld/Beach/M1911A1/M1911A1.dbx`; GUID `E9E77835-FFE4-4F7B-8784-0970C71E051B`; SHA-256 `7d94e90adc48c527abaec2a3d6f540b2f22417c4a42a5b0df0b3293e2f8e80c0`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **MP412** — `Objects/Weapons/Handheld/MEC_hg_MP412rex/MEC_hg_MP412rex.dbx`; GUID `811B1F2B-4413-41A1-B7F0-0AED8A1D527A`; SHA-256 `6479ac2e4e3d68887ae0c202e3dee4d703a65fa5a83188e8f6d963660e854a2e`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **M24** — `Objects/Weapons/Handheld/US_sni_M24/US_sni_M24.dbx`; GUID `3632D971-D742-493D-8962-358ADE024C39`; SHA-256 `84252e7fa90e376d2cfe3131a1e83423a00d9a1dda52d0974b5d4d8d3d8de82c`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **SV98** — `Objects/Weapons/Handheld/RU_sni_SV98/RU_sni_SV98.dbx`; GUID `33F9E790-E758-4468-B3DB-00CD6C3B1A4D`; SHA-256 `aaab64e3c8c783167e401e9c27a240135aa69bbc54a1cb40b1b2e1efb30d696e`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **M95** — `Objects/Weapons/Handheld/BU_sni_M95/BU_sni_M95.dbx`; GUID `CB2A15FE-8F0A-4772-9102-64548E0DB3B6`; SHA-256 `992535507a1b074634772678f5cd6bc8a71a90a599ac5b469c71d1c7bfd560a3`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **SPAS12_sp** — `Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12.dbx`; GUID `CD43BC40-BBD7-4BAB-B3D6-8AE934374944`; SHA-256 `800b46aead4e2704cb771176e5d7ae58f72838beb89fe4cb3f9d7192089f2113`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **870MCS** — `Objects/Weapons/Handheld/US_shg_Remington870MCS/US_shg_Remington870MCS.dbx`; GUID `FBCC489E-ACC3-448F-998A-DDFB85582047`; SHA-256 `5b205a695ea01761b0c21727655967f554a5b0db0c826317e24efb7f4fde8925`. [PPS](../release/PISTOL-PRECISION-SHOTGUN-COVERAGE.json)

- **PKM** — `Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM.dbx`; GUID `eccf2b1d-eb98-439d-b5eb-cd8ad428e001`; SHA-256 `748809a7c23502232ed78def704291c0746804c7e4ffa09349d0d62c67fc5535`. [LMG](../release/LMG-MECHANISM-COVERAGE.json)

- **M249** — `Objects/Weapons/Handheld/UL_lmg_FNMinimi/UL_lmg_FNMinimi.dbx`; GUID `46c1e3c8-85e3-47cf-8fd2-9d55acca92d3`; SHA-256 `87ce26be3af226195387d0da68df0aeb8046990fc31ff14ce65a250dd01138f3`. [LMG](../release/LMG-MECHANISM-COVERAGE.json)

- **MG36** — `Objects/Weapons/Handheld/BU_lmg_MG36/BU_lmg_MG36_Scope.dbx`; GUID `d7c9100e-a312-4084-871e-2471b63da284`; SHA-256 `6f2c2e40d3efc9a3f143ea2b89d9e08f0e617eaa9f831b51ea64bbd3367ff63d`. [LMG](../release/LMG-MECHANISM-COVERAGE.json)

- **XM8 LMG** — `Objects/Weapons/Handheld/US_rif_XM8lmg/US_rif_XM8lmg.dbx`; GUID `16ff338b-657b-4966-8a9e-bba5d3c40834`; SHA-256 `515f82d772129e026d2c713ef33b0cc665f86f364ac4a754d52c7c465b7046ef`. [LMG](../release/LMG-MECHANISM-COVERAGE.json)

## Review validation

This artifact is documentation only. Repository readback and diff checks must
confirm exactly this one added Markdown path, complete evidence links, valid
configuration GUID/hash transcription and no game asset/transform payloads.
No source tests or runtime tests are implied by these document checks.
