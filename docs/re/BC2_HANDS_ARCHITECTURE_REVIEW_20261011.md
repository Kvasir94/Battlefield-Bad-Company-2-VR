> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).
> Follow-up: [later implementation](BC2_NATIVE_SKIN_IMPLEMENTATION_20261011.md); its earlier stop-after-research instruction is historical.

# Hands architecture review — 2026-10-11

Bounded, read-only architecture investigation against detached integration
`3aa78af53f63ec10d0a794523c969bc671d99595`. This report is a recommendation for
the next decision, not implementation authorization or a claim of working
replacement. No runtime, lease experiment, build, install, selection, source
implementation or commit was performed. Only this report is owned by this lane.

## Recommendation

Prefer replacing geometry on the player's **existing native skinned render
instance**, retaining its material, palette producer and pass submission, if a
small static investigation can identify that seam. Investigate that before
constructing a new native skeletal component. The latter adds constructor,
resource ownership, animation attachment, render registration, visibility and
teardown contracts that the current evidence does not establish.

Retain the shared final-pose publication and capped authored assets. Keep the
independent D3D backend as a fallback architecture, but do not expand its custom
lighting now. Its remaining integration is substantial, and diffuse-only
shading cannot meet native material parity. Native IA-buffer substitution in
the existing draw is a narrower intermediate option: it preserves shaders and
pass state without constructing a component, but still needs the actual selected
draw and its color/depth palette and vertex contracts.

This ranking is an engineering inference, not proof that Frostbite exposes a
safe replacement API. No such API or native skinned-component constructor is
verified by this review. If the bounded seam investigation cannot establish one,
return that negative result at its actual scope and choose between the existing
adapter's material cost and another expressly scoped native investigation.

Existing native IK already rendered stereo. Rejected exact-eye producer leases
were a limitation of that capture/ownership technique, not evidence that the
engine requires one animation producer per eye or that native IK was monocular.
Do not repeat those lease experiments.

## Evidence ledger and limitations

1. `BC2_HANDS_CHECKPOINT_20261011.md` is the latest retained state. Its material
   sample observed 838 native eye scopes, three count candidates, zero staged
   glove/arm draws, zero pack records and zero capture errors. This does not
   identify the actual submitted glove or prove absent hands. It supersedes
   older notes describing read-only scene depth and per-eye pose consumption.
2. `BC2_NATIVE_HAND_MESH_20261011.md` and private
   `<repo>\reports\floating-hands-intake\NATIVE-DRAW-CONTRACT.md`
   establish original 147-bone asset preparation and candidate native draw
   formats. The archive palette name list was hash-sorted, not local-index
   order; the private compatibility audit recovers local order from original
   IDs and checks weights. That is offline compatibility, not live GPU binding.
3. `src/games/bc2/Bc2RigPublication.cpp:492` publishes the actual final private
   Pack palette after fallback/rebase; it compares native 48-byte destinations,
   verifies source preservation and current rig/equipment, and copies the result.
   `Bc2NativeHandMesh.h:145` separates animation provenance from eye/frame.
   `NativeHandStereoPose::Select` freezes one value for a world/request/frame
   pair and rejects owner, tracking, layout, freshness and ordering changes.
   This source exists; publication remains optional and no production hand
   render adapter is wired.
4. `Bc2NativeHandRenderer.cpp:137` proves the current backend checks the owned
   eye boundary and actual targets, rejects a read-only DSV, uses verified
   projection/depth convention and writes scene depth. It renders a private
   four-weight layout with 32 wrist/finger matrices. `Upload` accepts the full
   authored diffuse mip chain with hashes. This is source behavior, not evidence
   of visible independent hands or native normal/specular/lighting parity.
5. `Bc2SelectedMeshes1p.h` and `docs/SELECTED-MESHES1P-20261001.md` prove a
   read-only selected weapon -> configured `WeaponStates.Meshes1p` -> reflected
   `SkinnedMeshAsset` path. `soleConfiguredArray` is an array address, not a
   submitted skin instance. `activeStateVerified`, `submittedSkinVerified` and
   `renderSuppressionAllowed` are explicitly false. This weapon configuration
   reader is not a proven local-player arm render-component resolver.
6. `docs/re/BC2_DISASM_ENTITY_SPAWN.md` records generic client entity factory
   `0x8b8360`, info constructor `0x874af0` and an AnimatedMeshEntityData registry
   entry. Its proved constructor example is a physics DynamicModelEntity, not
   a first-person skinned hand component. Those facts cannot justify fabricating
   EntityData/resource objects or calling an unverified skeletal constructor.
7. Private material closeout at
   `%USERPROFILE%\Documents\Codex\2026-10-11\task-5\material-resource-analysis\CLOSEOUT.md`
   pins SP_02 Dx11_Single.res SHA256
   `9c293ca4b59612139c08802b5427eec891b84dab7abfb82bed0891cfd72e56f3`.
   Standard section 2 maps to US_02_Mesh_Gloves; material key
   `56995c7db880db0efaa133678ac02d40` resolves to six 88-byte records at table
   base 22549083 (indices 3293, 5004, 5763, 8515, 8610, 9798). Their +32 keys
   join the 89-byte table at 23484263. Standard layout key is
   `e7737d407ffaf6b5fbb125a5f76220be`. All 5142 DXBC containers pass structural
   checks. The corresponding 48-byte record association, permutation fields,
   resource bindings and layout enums are unresolved. These file offsets are
   decimal, not VAs. Glove_01 texture decoding does not prove glove binding.
8. `docs/WEAPON-LOADING-AND-DEBUG-RECOVERY.md` gives native material seeds:
   factory `0x99fc30`, add `0x98c5d0`, shader system `[0x1572b18]`, registration
   `0x9cb590` (thiscall, ret 8), merge `0x9c3f60` (thiscall, ret 8), lookups
   `0x9b02b0/0x9b0340/0x9b03d0` (thiscall string wrapper; zero on miss). Its
   inherited static/live evidence establishes database merging and native
   material availability; it does not establish hand-resource substitution.

### REA attempt in this review

Used the `reverse-engineer-anything` skill and the available REA MCP server.
Session initially had no target; Ghidra 12.1.3 and its JDK were available.
`open_binary` admitted `D:\bc2-re\BFBC2Game_795745.exe` as PE x86, SHA256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
A single `batch_decompile` requested only `0x99fc30`, `0x9b02b0`, `0x9c3f60`.
It returned `timed out awaiting tools/call after 300s`, with no pseudocode or
Evidence ID. No function-specific conclusion is derived from that attempt.
The provider's documented auto-analysis boundary exposes no partial results;
successful target admission is not successful decompilation. No configuration
repair, repeated query, headless fallback or additional seed expansion was made.
An owned-session close was requested after the timeout. The recommendation above
rests on the explicitly identified retained evidence and source inspection.

## Route comparison

| Route | Preserved machinery | Missing interface/proof | Relative remaining cost |
|---|---|---|---|
| Replace geometry on existing selected native skin instance | Native animation-to-palette path, shaders/material permutations, pass scheduling, lighting and depth, if replacement reaches all passes | Selected arm instance/resource/section chain; per-instance versus shared resource ownership; buffer/section layout; resource retention, update thread and invalidation; color/depth/LOD coverage | Medium-to-high RE uncertainty, lowest material implementation burden if seam exists |
| Substitute IA geometry inside matched native draws | Actual native shader, resources, samplers, constants, targets, raster/depth state and query lifetime | Selected-player draw ownership; live 35-entry color and 44-entry depth remaps; actual vertex layout and cap attributes; full bilateral pass inventory | Medium-to-high bounded render integration; no component constructor, but substantial draw matching remains |
| Construct a new native skinned component | Potentially native material/lighting and pass generation | Constructor ABI and allocator; data/resource format and reference ownership; skeleton binding; animation upload; local-player attachment; visibility bounds/LOD; render registration and teardown; suppression of original | Highest RE and lifecycle risk; generic entity factory is insufficient |
| Existing mod-owned hand renderer plus adapter | Original capped geometry/weights and final authored finger poses; own 32-bone palette, mip filtering and scene depth writes | Verified UV/normal/material inputs and private loader; live boundary/target/query wiring; original color/depth suppression; same-frame fallback; native lighting/normal/specular implementation | Lowest shader-layout coupling, substantial remaining integration and highest appearance reconstruction burden |

These are relative costs, not calendar estimates. None is currently an enabled
independent hand replacement. Native geometry substitution preserves lighting
only insofar as the actual original material/pass pipeline is retained; a mesh
name or shader database lookup by itself does not deliver that result.

## Requirements common to the decision

- **Skeleton and weights:** Preserve fingerprint `a7f219a1426216ab`, original
  147-bone hierarchy/binds and the authored final finger transforms. Each side
  retains wrist plus 15 fingers; up to four normalized influences. Private
  export has 866 vertices/1728 triangles per side and folds residual proximal
  influence into the appropriate wrist. Native substitution must map names to
  the actual section-local palette, separately for color and depth; the adapter
  already maps to its own ordered 32 entries. Do not treat skeleton index as
  a native vertex's byte bone index.
- **Wrist cap:** Keep recessed closed caps and consistent winding under the
  established handedness conversion. Cap vertices are wrist-only. Original
  corner seams are preserved, but new cap normals/tangents/UV/material choice
  still need a verified native attribute contract. Native material reuse does
  not solve undefined cap attributes. No shader/pose arm collapse should stand
  in for the requested capped geometry.
- **Pose timing:** Reuse the final source actually passed to Pack after contact,
  support, reload, trigger and fallback layers. Do not regenerate fingers or
  refresh an older pose's deadline/input generation. For the adapter, one
  immutable pose is selected for the stereo pair. A native route must establish
  when its retained instance consumes its palette and keep that pose coherent
  across both eyes; it need not inherit the obsolete exact-eye producer rule.
- **Material/depth:** Standard archived color glove format is stride 48 with a
  35-bone palette; combined ArmStandard_ZOnly is stride 16 with 44 bones.
  Glove color alone cannot replace combined arm/glove depth. A native route must
  cover the actual active color/depth and any relevant shadow/velocity/LOD
  submissions discovered at the seam. Do not assume the catalog is exhaustive
  after its latest zero-match sample. The adapter's late draw requires actual
  scene target/projection/depth and query-boundary evidence, not merely return
  from BodyAmmo's post-eye callback.
- **Original rig suppression:** Keep the original skeleton/animation producer
  for poses and contacts; replace its rendered arm/MOLLE/glove geometry as one
  scoped presentation change. Never hide the whole soldier or weapon because
  a configured mesh path or count matches. Establish the local-player instance
  before writes; a shared asset pointer is insufficient. Prepare replacement
  buffers/resources before committing suppression. Same-frame failure must
  leave usable original IK presentation, including matching depth and both
  eyes; prior-frame renderer health is not enough.
- **Both-eye criterion:** The same owned geometry and final pose must appear in
  each eye through its actual camera, with correct stereo disparity, closed
  wrists, finger/contact behavior, self/world occlusion and no old arm color
  or depth. Later runtime proof must include firing/reload/equip, tracking loss,
  recenter and forced replacement failure. This describes acceptance evidence
  for a future implementation, not a prerequisite for today's source review.

## Follow-up: route chosen, static seam traced

The user approved pursuing the existing native skinned instance. See
[October 11 seam research](BC2_NATIVE_SKIN_SEAM_20261011.md) for the recovered
soldier/list/request/descriptor/native-section/queued-geometry chain, assembly
ABI corrections and REA fallback diagnosis. A private descriptor's LOD pointer
is the narrow candidate; original binding tables and palette processing can
remain native. Queued geometry retains interior pointers, so stack clones are
unsafe. Live ownership, resource construction/retirement and complete pass
coverage remain unproven. This supersedes the route-decision pause but does
not authorize implementing hands before the remaining seam proof.

## Original next minimal proof, before implementing a route

The discriminating unknown is the active local-player **submitted skin resource
and section seam**, not another producer lease. Next read-only static task:
follow the existing first-person rig/skin consumer and its resource submission
to one instance-scoped geometry/section handle. Record exact VAs, caller/callee
ABI evidence, field accesses, allocation/reference lifetime, palette binding
and color/depth submission use. A successful result must distinguish a selected
instance field from a shared asset, and show how native material selection
survives a geometry change. Stop after that one chain; do not implement calls or
cast generic entity factory evidence into a skeletal API.

Existing starting points in `BC2_ENGINE_MAP_20261008.md` are soldier skin getters
`0x875da0/0x875dd0`, animation virtual getter `+0x160(-1)` -> `0x78b0a0`, and
character preparation `0x5f2860`. Its two Pack calls return at
`0x5f2b32/0x5f2b73`; verified packer `0x4044d0` is cdecl `(src,dst,count)`, copying
64-byte inputs into 48-byte request-arena matrices without retaining source.
The packer prefix is not unique; existing discovery checks all `0xce` bytes and
both call sites. These inherited signature/ABI facts identify a starting chain,
not an authorized mesh-write field or submitted draw.

For the material half, use the known shader database factory/parser and lookup
seeds to explain one glove record through layout/permutation/resource binding;
do not reverse every shader. The smallest asset-format proof is the 88-byte to
48-byte association plus the consumed layout enums and one matching shader's
input/resource declarations. This benefits either substitution or the fallback
adapter and provides cap attribute semantics.

If the static chain is established, propose one separately scheduled passive
runtime observation of the exact selected instance, skin resource, sections and
material. That observation should corroborate the new seam, not repeat the old
count-catalog/exact-eye lease experiment. Only then decide between original
instance replacement and IA substitution. A new component becomes worthwhile
only if existing-instance replacement demonstrably cannot express the required
geometry or visibility, with that limitation recorded explicitly.
