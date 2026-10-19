> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Independent BC2 native hand mesh source, 2026-10-11

Status: source implementation of a real skinned mesh backend; **not an integrated
or runtime-ready floating-hands repair**. No build, tests, GPU capture, game run,
deployment or binary selection was performed in this lane. Later local Blender
source inspection is recorded below; private asset files remain outside Git.
The existing floating mode still uses arm collapse. The reported firing-time arm
flashes have no verified cause in this work. This change does not conceal them.

## Existing assets and authority reused

The private intake directory is `<repo>\reports\floating-hands-intake`.
Its `NATIVE-DRAW-CONTRACT.md`, `HANDOFF.md`, `export-palette-compatibility.json`
and saved export were inspected before implementation. The original 147-bone
skeleton fingerprint is `a7f219a1426216ab`; units are metres. The Standard capped
export has 866 vertices and 1728 triangles per hand, including recessed closed
caps. The audit retains original bind/hierarchy and finger weights, with residual
proximal influence folded into the appropriate wrist during private asset
preparation. No new runtime folding, proxy mesh or arbitrary contact pose is used.

Private source identities:

| Input | SHA256 |
| --- | --- |
| `native-input.json` | `928eccbeb0f42f2d195f2323843f5c0fc80969532aca67af742b71988d198741` |
| `standard-floating-hands-v1-render-input.json` | `e1399ffd9ef3e7b79600cf15374a0a0a39f32081d10125f614d440725ef7e49b` |
| original `ske01.res` | `d1d713cc7301bc5ae2b011a0991ca03c2fcbb26c1187900f6fd129533d173c09` |

These are curated asset metadata, not redistributed vertices, textures,
animations, runtime traces or total-angle reports. Assets must remain private.
Existing authored support, magazine grasp, gun grip, reload, interaction arbiter
and positive stereo fixes are unchanged.

## Source implementation

`Bc2NativeHandMesh.h` builds independent bilateral geometry from a typed private
asset input. It resolves each wrist and all 15 named finger joints against the
original skeleton/bind fingerprint. Only that side's wrist/fingers may influence
vertices; cap corners must be wrist-only. It rejects duplicate/proximal/opposite
influences, unnormalized weights, malformed/cyclic bones, open/nonmanifold or
inconsistently oriented edges, negative-volume surfaces, missing caps, degenerate
triangles and invalid attributes. Per-corner normals and UV seams are retained.
Native RH coordinates are reflected into the established canonical LH basis,
with winding reversed together. No bind or authored contact is changed.

`BuildNativeHandPose` copies the **final** 64-byte native private skin palette,
extracting only its xyz columns and ignoring SIMD metadata/padding, using exactly
the conversion in `Bc2Rig.cpp`. It produces our own ordered 32-entry palette.
It does not reconstruct fingers from controller curls or retarget a static hand
proxy. It requires the original skeleton, caller-owned current owner/eye lease,
tracking and deadline. The integration adapter must pass the actual final Pack
input after current rebase, support/reload/contact/trigger layers and fallback
selection; a preceding `PublishedPose::posed` is insufficient where PackHook
selected a different palette. Do not refresh an old pose with new input IDs.

`Bc2NativeHandRenderer.{h,cpp}` implements an actual D3D11 four-weight vertex
shader on the original bind-space geometry. Each finger's native skin matrix
deforms its weighted vertices. Correct inverse-transpose normals, original UVs,
an independently verified native diffuse texture and single-sided capped
triangles are consumed through a mod-owned input layout/shader. Diffuse RGBA
payload hash and exact resource identity must match the geometry input.
Transactional upload preserves old buffers on failure. The full native context
state is swapped/restored through the same D3D11.1 pattern as existing body props;
borrowed targets are unbound before parking private state. Graphics-thread
lifetime is mandatory. Draw requires the existing `BodyAmmoBoundaryMissing`
query/eye/target/depth proof and frame freshness; the scene depth is read-only.
Only tracked sides are submitted. CPU tests and native compilation are registered
in CMake; there is no hook, loader, suppression, global renderer or active call
site in this commit.

The pixel shader presently provides diffuse surface shading. It does not
reproduce Frostbite's glove normal/specular shader or lighting. That is a material
limitation, separate from real native geometry and skeletal deformation.

## Exact existing native boundary evidence

No new engine function is called or hooked. The established rig packer is
`0x4044d0`, cdecl `(src, dst, count)`, selected by its full verified code and the
two calls in `0x5f2860` (return VAs `0x5f2b32`, `0x5f2b73`). PackHook keeps original
source bytes and verifies the 48-byte output as three columns of four floats.
`ReadFirstPersonRig` verifies canonical `inverseBind * world == skin` with maximum
error 0.002 and reconstructs native IK world joints from bind * final skin.

The existing DrawHook owned post-eye boundary supplies canonical camera matrices
from RenderView offsets `primary + 0x220` and `primary + 0x2e0`, after native eye
draw/evidence callbacks and before CaptureTrackedEye. The existing DrawIndexed
hook is immediate-context vtable slot 12. No original shader ABI or section-local
palette is guessed by this backend. Geometry signatures alone still cannot
authorize removing a native player's arm draw.

REA/Ghidra MCP tools were unavailable in the callable inventory. Existing
source and curated contracts sufficed for this backend; no new decompilation or
headless Ghidra invocation occurred. The VAs above are inherited verified source
evidence, not a new reverse-engineering result.

## Remaining integration blockers

1. The private export explicitly sets `native_attribute_semantics_verified=false`.
   `prepare_standard_hands.py` calls its UV layers `CandidateNativeUV0..2`; cap
   corner native tails are null. Supply verified per-corner normal/UV fields and
   intentional cap normal/UV/material treatment from an asset-reader receipt.
   Do not choose one candidate UV pair silently. This backend rejects that input
   until its real attribute receipt is supplied.
2. `native-material-receipt.json` sets
   `material_to_texture_mapping_verified=false`; candidate Glove_01 diffuse,
   normal and specular textures decode successfully but do not prove the selected
   native glove binding. Supply the exact selected diffuse resource mapping and
   decoded pixel hash. Native normal/specular reconstruction remains separate.
3. Add a bounded private loader for these verified inputs. No such admitted input
   has been created here. Do not flip the existing export's readiness booleans or
   reinterpret its asset validation as native render acceptance.
4. Join the renderer to the exact actual final Pack palette and current
   actor/weak/weapon/equipment/input/space/native frame/eye/request. Preserve the
   existing reload/sight/finger fallback selection and never mint contacts in the
   renderer. Current owner metadata alone cannot establish that the arm mesh is
   the selected player's submitted native draw.
5. Replace original arm, MOLLE, glove color **and** combined arm/glove depth as one
   presentation decision for both eyes. No suppression is added until that exact
   draw/owner join and same-frame replacement/fallback exist. Prior-frame renderer
   health alone cannot prove that this frame's hands will draw. Zeroing arms
   without replacement would repeat the rejected concealment-only presentation.
6. The existing body-ammo post-eye proof does not automatically provide all
   `nativeQueriesObserved` and depth/target associations. Wire verified current
   boundary observations; returning from Draw alone does not fill them in.

The mod-owned route removes the requirement to discover the game's 35/44-entry
GPU palette remap for rendering the replacement. It does not remove these actual
asset appearance, native-owner suppression or lifetime requirements. The original
shader substitution route still additionally needs its live input-layout/palette
receipt as stated in the private native-draw contract.

## Verified local Blender source inspection

The installed Blender Lab addon on localhost was verified by a harmless read of
the current scene before using its source-confirmed execute endpoint. Blender
reported 5.2.1 LTS. The initial scene was backed up locally; the existing capped
library was imported into a separate inspection scene. The original scene was
restored and the original capped `.blend` remains unchanged at SHA256
`99c922ece3d47460077dfed9622b34d3c3f5a4c276cbe3dbb96e2a78fa4497e5`.

Each independently capped hand has 866 vertices and 1728 triangles, no boundary
or nonmanifold edges, and consistent edge winding. All vertex weights are finite
and normalized, with at most four influences and exactly 16 weighted native
wrist/finger groups per side; no proximal groups influence the capped hands.
The 147 bone names, hierarchy and bind matrices match the pinned native input
within a maximum matrix-component tolerance of 0.0002. These are source asset
facts and do not establish a currently submitted player's draw.

Three separate local inspection scenes use existing authored same-frame contact
samples: `HandsIkPose` sample 0 of 4, `1P_Reload` sample 88 of 176 and
`1P_FireStart` sample 9 of 10. Each imports both wrists and all 30 fingers from
that one recorded sample. Canonical reflected-Z row matrices are converted back
to native Blender column matrices using the exact offline sampler convention.
The resulting bone matrices match their recorded source within 0.0002; both
15-finger contact sets are present. These are sampled inspection poses, without
invented offsets, animation selection claims or runtime interpolation claims.
They do not replace the final native Pack palette required by this backend.

The three finite, nonzero `CandidateNativeUV` layers per hand remain candidates.
Both source material slots contain review shading and no texture nodes. Neither
Blender import nor an authored pose proves effective GPU normal/UV conversion,
native glove texture binding or wrist-cap appearance. No readiness/admission
boolean was changed as a result of this inspection.

Private outputs under the existing intake directory are
`blender-native-source-inspection-20261011.json`,
`blender-authored-hand-source-inspection-20261011.json` and
`standard-floating-hands-authored-inspection-20261011.blend`. The JSON receipts
contain allowlisted counts, source hashes and match booleans. Geometry, poses,
textures and original asset bytes remain local and are not included in this
source checkpoint. No GPU rendering or game action was performed.

## Focused checks for the integration owner

Build/run only `Bc2NativeHandMeshTests`, then compile the affected native DLL when
the shared build gate permits. The added synthetic test covers both 15-finger
palettes, mixed-weight deformation, SIMD-padding immunity, source preservation,
seam/winding conversion, common-world transform and 180-degree inversion,
owner/deadline/rig mismatch, collapsed wrist rejection, wrong-side/proximal/duplicate
weights and malformed/open/inward geometry. These checks have not been executed.
They cannot certify the private assets or native eye pixels.

After real input/call-site integration, verify the original native pose contacts
and the closed wrist geometry in both eyes while firing/reloading, upright and
inverted, with tracking loss, recenter, equip changes, source fallback and backend
failure. The integration owner alone owns runtime/build/package selection.
