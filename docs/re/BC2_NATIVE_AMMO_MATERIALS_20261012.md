> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Native magazine and shell materials

The flat rigid-prop path drops the original native vertex material attributes.
The private native prop extractor instead selects original rigid-part triangles
and retains their color vertex records, section order, palette and source hashes.
The native LOD variant/material bindings must be retained by the separate prop
submission. These packs are not hand or weapon replacement packs.

## Material evidence and choice

The retained installed shader database has SHA-256
`9c293ca4b59612139c08802b5427eec891b84dab7abfb82bed0891cfd72e56f3`.
Its XM8 Plastic permutations include VS 281 / PS 725 and VS 1152 / PS 725.
REA cannot disassemble DXBC; the retained D3DDisassemble output is the fallback
evidence. PS 725 reads six material textures (t2 through t7), shadow t0 and an
environment cube t1. It uses UVs at several scales, a normal map, masks, material
detail and occlusion, with environment response mixed into the lit result.
OcRGB alone is not the diffuse appearance. A flat tint or single guessed albedo
would not preserve the native material. Native rendering reuse avoids recreating
that shader contract or guessing texture bindings.

Private evidence resides under
`<repo>\reports\five-weapons-20261011\materials`, including
`9c293ca4-declarations.private.json` and the VS/PS assembly. No game shader,
texture, mesh or animation bytes belong in public source.

## Private extraction contract

`tools/bc2_native_prop_assets.py` consumes the shared ammo `SPECS`, or explicit
private `[asset, archive, mesh, part]` rows, from the installed game. It requires
the exact 147-bone skeleton content and admits only triangles whose three corners
are weighted 255/255 to the requested global part bone. Blended or cross-part
triangles touching that bone reject the pack. The complete original color VB is
retained byte for byte; only its IB is filtered. Unused original VB vertices may
still belong to other parts: runtime rigid-part validation must inspect indexed
corners, not require every unused vertex to belong to the prop.

The original XM8 depth LOD omits Plastic detail: the magazine has 502 color
triangles but only 118 corresponding rigid native depth triangles. As in the
existing native hand pack exporter, private depth geometry uses exactly the
selected color corners and winding, expressed in the native depth declaration
and original depth palette. Every depth corner is checked for exact position and
global skin equality. No new color surface, UV or material is synthesized.

Each pack is BC2NSK03 with the existing 128-byte header and 320-byte records,
limited to 8 sections and 8 MiB. Original source VB/IB hashes still admit the
native LOD; replacement VB/IB and body hashes validate the private output.
Float64/32 and half48/16 vertex layouts retain the existing reader conventions.
The native prop pool is separate from ordinary replacement: applying a prop pack
as a normal weapon replacement would suppress the rest of the gun.

The separate `native-skin-props` folder contains only `.fvrskin` packs and
`manifest.fvrnprop`. The extraction receipt is a sibling JSON outside that folder.
The bounded little-endian manifest schema is:

| Field | Encoding |
| --- | --- |
| Magic, version, entry count | `BC2NPR01`, u32 1, u32 1..32 |
| Per-entry asset, mesh, part, filename | Four u16-length printable ASCII strings, each 1..512 bytes; filename is a basename |
| Rig fingerprint, global bone, palette count | u64, u32 0..146, u32 147 |
| Inverse bind | 16 float32, canonical row-vector matrix in metres |
| Pack identity | 32 raw SHA-256 bytes |

Pack positions remain original native coordinates; inverse bind metadata is
canonical, matching the existing rigid-prop metadata convention. The native
palette publisher must apply the established canonical/native conversion rather
than mixing those coordinate systems. Submission ownership, palette lifetime,
pass selection and duplicate suppression belong to the native render consumer.
The old gray fallback must only disappear after actual native prop admission.

## Verification

The current installed extraction produces 13 entries, 4,220,248 total pack bytes
and 6,382 original color triangles: XM8, SPAS shell, AEK971, F2000, M9/M9-3,
M416, MG36, VSS, AKS74u, UZI, XM8C and XM8 LMG. The existing production-reader
test executable accepted all 13 individual files with `--packs`. This verifies
pack hashes, section layouts, color/depth corner pairing and skin mappings;
it does not establish live rendering or material appearance. Native runtime
admission and visual results must be reported separately.
