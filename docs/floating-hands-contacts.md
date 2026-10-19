> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).
> Follow-up: [later native-hands implementation](re/BC2_NATIVE_SKIN_IMPLEMENTATION_20261011.md) and [hand authority](HAND-AUTHORITY-SUPPORT-GRIP-20261011.md).

# Floating BC2 hands and authored contacts

## Goal and current scope

Isolate native BC2 player hands/gloves from forearms, close the wrists cleanly,
retain usable native skinning and authored contact poses, and keep every original
asset intact and private. Reload interaction comes first: one XM8 pilot, initially
right-hand weapon and left-hand support/loading. Authored animation supplies local
contact relationships and finger poses, never a canned world-space reload path.
The player retains full translation and rotation between contacts, free magazine
swing/rotation, weapon inversion and upside-down insertion.

Future architecture: hands are independent tracked entities; each weapon is an
owned physical object assignable to either hand. Later work may add handoff,
support/draw with either hand and two different held weapons, each retaining its
own ammunition, mechanical/action state and aim/shot direction. These are future
phases, not permission to implement dual wield or melee now. Fists/punches are
deferred. Recoil wobble is a later polish note, not authorization to remove recoil.

## Current implementation boundary

`Bc2FloatingHands::SolvePresentedArms` places floating wrists at their tracked or
cosmetic contact targets without anatomical arm reach limits, retaining the
evaluated finger/attachment transforms relative to each wrist. The held item's
tracked frame also stays unclamped; raw reload contacts precede support attraction
and guided presentation. IK mode retains the existing anatomical solve.
Authored item subtrees, mechanism travel and exact contact/finger bindings remain
in use. Floating presentation grants no seat, rack or ammo receipt.

The current native palette path collapses arm skin onto the wrists. It does not
substitute the capped floating glove mesh: native draw owner, palette and layout
evidence for that route remains outstanding. CPU fixtures are distinct from
clean cuff appearance, controller alignment and headset acceptance. No new asset
extraction is required for the current frame/pose change.

## Historical evidence boundary and coordination

The following records the earlier document-only attempt; its temporary ownership
reservations are historical, superseded by coordinated implementation on main.

This plan was source-reviewed through the repository connector against BC2
`e1f86e9cc11765674e1816bace9a10e778bc2b9b`. Local runtime changes are ahead of
that snapshot. The delegated shell failed before executing even Get-Location:
`helper_unknown_error: setup refresh had errors`. It was not retried or bypassed.
Consequently this work has not inventoried current local files, extracted a mesh,
edited a Blender scene, built code, run tests, injected, launched or selected a
candidate. No official Blender MCP tool was exposed in this task.

Runtime/reload owner: “Investigate BC2 VR reload options”
(`01a11da3-9378-7d81-bdeb-b84a16413ec5`). That owner retains all runtime,
build, selection, game and GPU operations. Interface/path coordination was sent;
implementation agreement has not yet been received. Architecture/mirror owner
`01a11dd5-b106-76d1-91e5-082467d7b0c1` owns mirror integration and AGENTS
coordination; a message attempt failed with an AbsolutePathBuf error.

This branch changes only this document. No runtime/source/data paths are reserved
or edited by this task. Proposed helper names are deliberately omitted until the
owner confirms whether an existing contact type should be extended. Do not edit
AGENTS, mirror files, CMake, Gameplay, reload runtime or generated calibration
during the owner's active work. The separate docs correction commit `06e0fc3`
was not integrated here. Prefer main for safe sequential work; this concurrent
feature uses the user-authorized small isolation branch.

## Actual source and asset inventory

These are verified repository identities; installed content, hashes and active
player mesh association still require a local read. Historical counts below are
prior inspection results, not a new asset validation.

| Area | Existing source / exact identity | What is established |
| --- | --- | --- |
| Native arms | `tools/inspect_weapon_visibility.py`: `levels/sp_common/level-00.fbrb`, `Characters/US/US_FP_Arms/US_FP_ArmsStandard_Mesh` and `Characters/US/US_FP_Arms/US_FP_Assault_Mesh` | Existing bounded resource reader; MeshData capitalization is resolved explicitly (Us versus US). Do not invent a second extractor. |
| Arm coverage | `docs/EMPTY-HANDS-RENDER-20261001.md`, `Bc2WeaponVisibilityCoverage.h` | Prior standard-arm coverage: 4 sections, 1 LOD, 6,041 vertices; assault: 4 sections, 1 LOD, 3,384 vertices. Union has 46 weighted arm hashes disjoint from 16 weapon/attachment hashes. This does not identify today's selected arm material/mesh. |
| Skin reader | `tools/bc2_mesh_geometry.py`, `tools/inspect_bc2_mesh_asset.py` | Parses section/LOD palettes, positions, four byte weights summing to 255 and triangle indices. It emits derived coverage. It is not yet a demonstrated full material/UV/normal preserving hand exporter. |
| Skeleton | `Characters/Skeletons/ske01.res`, common archive; `Bc2HandPose.h` | Current known rig fingerprint `a7f219a1426216ab`; explicit LeftHand/RightHand and Thumb/Index/Middle/Ring/Pinky joints 1–3. Full skin/export correspondence still must be checked locally. |
| Native rendering | `Bc2RigPublication.cpp/.h`, `Bc2Rig.cpp/.h`, `HandPose.h` | Existing native/private palette route and arm IK. Reuse its owner/source/freshness checks. Weapon visibility collapse is not a clean hand-only mesh or wrist cap. |
| Static authored grip | `tools/bc2_authored_hand_pose_batch.py`, `tools/bc2_authored_grip_bindings.py`, `Bc2AuthoredGrip.h` | Typed configuration → animation → skeleton → mesh joins, immutable hashes, canonical row-vector metre transforms. Finger/part samples can be retained separately. |
| XM8 support | `profiles/checkpoint202/Bc2AuthoredGrips.generated.h` | Exact `XM8_sp_s`, `Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh`, authored left/right reference transforms. This compiled profile contains wrist transforms, not its full finger-contact cache. Preserve private derivation policy. |
| XM8 magazine | `Bc2Xm8MagazineCalibration.h`, `Bc2MagazineGeometryProfile.cpp/.h` | Existing native row57 calibration reference, ItemFromHand, AttachedItem, insertion/entry frames and 15 WristFromFinger transforms. Weapon `jntWpn_1`, magazine `jntWpn_6`, loading wrist `LeftHand`. Do not replace with guessed offsets. |
| Paired authored contact | `tools/bc2_authored_magazine_geometry.py::paired_reload_grasp` | Wrist, magazine and 15 fingers sampled together at 60 Hz; 0.2 s stable window, <1 cm relative wrist translation, <0.15 rad wrist change, <0.05 rad finger change, <5 cm knuckle/bounds proxy distance. These are extractor criteria, not VR grab radii or fingertip collision proof. |
| Hand ownership | `include/fvr/interaction/HandInteraction.h` | Existing serialized two-hand arbiter, typed claims, exact item/contact keys, increasing intent, explicit transfers, dependency claims, expiry and identity handling. No second arbiter. |
| Support feel | `SupportGrip.h`, `src/interaction/SupportGrip.cpp` | Existing acquire/release distance hysteresis 0.16/0.28 m and squeeze 0.70/0.35; 100 ms ordinary release fade, immediate tracking/identity loss. These are current BC2 policy values, not asset measurements. |
| XM8 insertion | `Bc2MagazineGeometryProfile.cpp` | Current design values include 0.07/0.15 m capture/release distance, 45/75 degree angle, 0.10 m travel, 120 ms alignment. Keep this distinct from hand-grasp/support zones and from an authored full reload trajectory. |
| Action contact | `Bc2MagazineActionCatalog.h`, `ChargingHandleRack.h`, existing reload sources | The catalog explicitly labels action-kind defaults unverified against decoded empty-reload clips. It cannot establish an authored XM8 action hand/finger sample. Inspect the owner's current rack path and private samples before creating one. |
| Holsters | `BodyInventory.h`, `Bc2BodyInventory.cpp/.h`, `Bc2BodyHolster.cpp` | Physical item keys include generation and survive distinct observation epochs. BC2 adapter explicitly uses Right GunHold and requires left-hand availability in several paths; inventory has a single selected/held item. Existing holsters do not establish ambidextrous or dual-weapon support. |

The exact installed file list, archive/content hashes, material bindings, LOD
payloads and current player arm/glove selection remain unverified here. Original
and derived meshes, textures, clips, generated contact data and Blender files
must remain in an existing ignored private asset area, never public source.

## Findings that determine the pilot

1. In the inspected `Bc2RigPublication.cpp`, the arm solver adjusts `placed`
   through the solved right wrist before `BuildReloadRawContact` and
   `BuildMagazineRawContact`. The left contact starts at `freeLeft`, but the
   item frame has already passed through reach clamping. Floating hands must use
   the authoritative tracked held-object frame for interaction independently of
   arm reach. Confirm this against the owner's dirty source before changing it.
2. Support can use a typed authored profile; the fallback derives a relative
   transform from the current native pose. Do not promote a live procedural,
   recoil-modified or already-overridden pose into an immutable authored contact.
3. `Bc2HandPose.h` derives anatomy axes from bind data but uses explicitly
   tentative procedural free/mechanism curl values. Those are suitable fallback
   presentation, not proof of native authored contact fingers.
4. The paired magazine generator already keeps same-frame wrist/item/fingers.
   Extend that evidence path where necessary. Static HandsIkPose support and a
   reload-grasp sample are different contacts, not interchangeable frames.
5. Source palette preservation and successful draw submission do not prove clean
   wrist caps, hand-only skin weights, correct material culling or visual fit.

## XM8 pilot contract for owner agreement

Use the existing math and ownership types, with explicit hand side and exact
weapon/item incarnation throughout. “Right weapon / left loading” belongs only
in the initial adapter policy, never in a new generic identity or contact schema.

An immutable contact record identifies exact configuration, mesh, rig, clip and
sample time or stable interval; moving part/bone; hand side; object-local wrist
transform; all 15 wrist-relative finger transforms; and source digests. Keep
sample provenance independent of current tracking sequence and lease. Never
refresh old geometry by relabeling it with a newer input sequence.

Define row-vector matrices explicitly:
- C = authored wrist world * inverse(authored contacted-object world).
- F[j] = authored finger world * inverse(authored wrist world).
- Bound wrist = C * current contacted-object world.
- Bound finger[j] = F[j] * bound wrist.
- For a freely carried magazine, object world = inverse(C) * raw tracked wrist.

Thus applying the same rigid transform to player, gun, magazine and hands
preserves local contact. Weapon upside-down is ordinary rigid composition; there
is no world-up acceptance condition. A detached magazine follows its own raw
tracked hand and can swing/rotate freely until a valid insertion interaction.
When holding a moving action part, compose that part's current object-local pose,
not the entire original reload arm trajectory.

Interaction evaluates raw tracked wrist versus the current object-local zone
before visual attraction or finger posing. It returns eligibility/error plus
the original input sequence/deadline for an existing HandContactProof.
Acquisition requires deliberate input, correct side/contact identity and a free
or explicitly transferred shared claim. Maintain acquisition/release hysteresis;
do not let visual attraction create self-sustaining contact. Use existing support
and insertion policies where applicable instead of another competing state
machine.

Gentle attraction is bounded cosmetic wrist/finger blending toward a valid nearby
contact. Choose per-contact tuning only after measuring the actual BC2 contact;
AS2 values below are references, not defaults to paste. Blend rigid rotations
with shortest-arc quaternion interpolation, never elementwise matrix blending.
Real release removes gameplay ownership immediately; a short visual return may
decay its last correction in the moving tracked-hand frame. It must not keep
following the gun or reserve the hand. Tracking loss, actor/item replacement,
recenter and stale evidence terminate the attachment without reacquisition from
a held old gesture.

Reuse GunHold + dependent WeaponSupport/Mechanism on the same weapon; AmmoObject
refers to the actual magazine identity. Explicit transfer coordinates support
release and magazine/action acquisition. Failed transfer retains the old valid
claim according to the existing arbiter. No render or mesh operation may create
ammo, seat a magazine, rack, fire, or override the server-owned transaction result.
Owner acknowledgement, not geometric contact alone, commits those operations.

Pilot order: existing XM8 magazine grasp/fingers; typed static support/fingers;
then exact authored action contact if verified source is available. Missing action
samples remain a specific gap, not a fabricated offset. Detailed insertion
shape refinements follow later without changing this ownership boundary.

## KF2-VR / Arizona Sunshine 2 lessons actually inspected

Read KF2 source through the connector (main at inspection), not just its feature
descriptions:
- `tools/generate_floating_hands.py::cut_hands` uses Blender bmesh bisect/fill,
  retains rig/material records, interpolates cut skin weights, preserves finger
  weights and optionally folds proximal influences into the wrist. Its ActorX
  layout, skeleton names and exporter assumptions are not BC2 formats.
- `VRHandsBridge.uc::CalibrateGripPose/UpdateHandAttachment` uses a separate
  reference mesh and frozen authored finger pose, then attachment-relative wrist
  composition. Reuse the concept, not UE3 component APIs or KF2 offsets.
- `VRReloadRigSampler.uc::SampleEarlierCarryGrip` samples ammo and wrist at the
  same time/frame; the ammo notify remains timing, not a spatial target.
- `VRReloadHandPose.uc::Place` blends cached fingers/contact toward the tracked
  wrist and releases by decaying a correction in the tracked wrist's frame.
  Its own comment explicitly prevents cosmetic release from extending ownership.
- `docs/HAND_ASSETS.md` records culling/winding and cuff failures that survived
  ordinary Blender previews. Validate one-sided output, not just a two-sided
  beauty render; preserve native appearance rather than copying KF2 redesigns.

AS2 extracted notes inspected:
`docs/re/ARIZONA_SUNSHINE_2_HANDLING.md` and
`docs/re/ARIZONA_SUNSHINE_2_RELOADS.md` in KF2-VR. They cite local prefab,
settings, IL2CPP and disassembly work. Raw extraction artifacts under
`build/re/tools/as2-meta/` were not locally read by this task.
The handling note reports item-local anchor composition, primary-hand roll
authority, 25 ms support engage / 100 ms release blending, and 70 ms item-release
return. The reload note separates capture, alignment and seating and describes
25% insertion release completion with 40–250 ms settling. These suggest bounded
transitions and clear states. They do not establish BC2 geometry, scale, skeleton,
engine calls or correct thresholds. Do not copy AS2 assets, poses or offsets.

## Local asset work, in order

1. In the working local BC2 environment, read current AGENTS and git status,
   confirm owners/paths, locate the existing private installed-asset inventory,
   skin reports, native rig dump and XM8 calibration source. Verify game archive
   and resource content hashes before reusing them. No full asset clone.
2. Use the existing bounded reader to resolve the actual selected arm/glove
   mesh and every required section/LOD/material. Establish position, UV, normals,
   tangent and skin layouts; current coverage reader alone is insufficient for
   a faithful export. Preserve source bytes and all original files.
3. Open a private duplicate through supported official Blender MCP where
   available. If it is unavailable, record that blocker; do not install tooling
   or silently substitute another MCP. The user must authorize any required
   installation separately.
4. Cut each wrist on its own anatomy/cuff boundary, retain distal geometry and
   finger weights, interpolate newly cut vertices, and add a recessed closed cap
   with deliberate UV/material treatment. Preserve original skeleton/bind data.
   Only remaining proximal influences on the derived hand are candidates for
   normalization into its wrist bone; retain/report the original weights.
5. Audit each derived hand for manifold cut closure, outward winding, no
   degenerate triangles, retained UV/material/normal correspondence, normalized
   weights and no forearm-driven stretch. Exercise native open/grip/contact poses
   and direct 6DOF wrist placement, including inversion. Inspect one-sided renders
   and reimported output. Save private before/after counts and hashes.
6. Agree the runtime consumption route with the owner. The existing private skin
   palette remains the starting point; do not add a parallel renderer or assume
   the rigid-prop renderer supports deforming hands. Suppress duplicate native
   arm/hand drawing only for the identified player mesh and verified replacement.

## Validation and phase gates

- Source/readback gate: confirm current local version of every reused interface
  and source sample, including the private row57 capture provenance and pre-layer
  sampling stage. The present connector review does not pass this local gate.
- Pure CPU gate, after implementation: shared rigid-transform equivariance under
  translations/rotations and 180-degree inversion; unchanged contact record under
  visual IK/recoil layers; no ownership duplication; failed transfer; expiry and
  identity reuse; release/reacquire; hysteresis boundary behavior. Use synthetic
  geometry for source tests, not redistributable game samples.
- Asset gate: private mesh export/reimport and deformation checks above. Counts
  alone do not pass visual fit; asset validation does not pass gameplay.
- Runtime gate belongs solely to the owner after integration: exact binary/hash,
  server and both client ammunition/mechanical state, upright/inverted contacts,
  moving player and gun between contacts, free magazine rotation, ownership
  contention, switch/stow/return state, dropout/recenter and both-eye visuals.
  Recording and replay remain separate from real controls, never a hybrid overlay.
- Human headset request only after the owner verifies the exact new server-owned
  reload binary and its existing acceptance gates. No final candidate acceptance
  is implied by prior XM8 28/180, shotgun 2/22, or complete stereo-pair runs.
  Scripted magazine release is not visual proof of a correctly visible dropped
  prop. Recoil wobble remains a separate later observation.

After the XM8 pilot, expand only exact source-backed contacts. The existing
magazine batch reports 27 paired state profiles across 13 exact names / seven
base meshes, not 27 ready guns. It explicitly reports missing rigid magazine roles,
unstable intervals or mesh gaps for other families. Tube/pump, belt-fed, action
variants and en-bloc mechanisms need their own contacts and state behavior.
Ambidextrous adapters come after reload acceptance; simultaneous weapons and
per-object shot routing are later separate work.
