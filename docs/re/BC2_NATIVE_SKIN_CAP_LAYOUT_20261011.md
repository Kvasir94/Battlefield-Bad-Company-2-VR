> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Native Standard glove cap layout — 2026-10-11

The original Standard glove DX11 declaration and shader pair now establish
the attributes needed to construct wrist caps in the existing native mesh.
The normal is at byte 16, the stored negative tangent at byte 24, and the
half at byte 6 controls bitangent handedness. This is static shader and file
format proof; it does not claim an installed replacement or in-game acceptance.

## Target and private evidence

Executable: `D:\bc2-re\BFBC2Game_795745.exe`, preferred base `0x400000`, x86,
SHA-256 `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
The existing saved Ghidra program reports the same digest. REA's previously
documented startup failure remains applicable. Queries used the permitted
private project copy with `-noanalysis -readOnly`; no original project, game
file, native memory, installation or selected package was modified.

Shader resource: cached SP_02 `Dx11_Single`, 24,293,941 bytes, SHA-256
`9c293ca4b59612139c08802b5427eec891b84dab7abfb82bed0891cfd72e56f3`.
Private source path:

```text
<repo>\reports\floating-hands-intake\cache\274a9e9eefea330e6eca9ae01b11a5e78d470cd03f3b396b6a39698400b0f76b\9c293ca4b59612139c08802b5427eec891b84dab7abfb82bed0891cfd72e56f3.bin
```

Evidence root: `<repo>\reports\native-skin-seam-20261011`.
`SkinDeclarationSeeds.java` and `SkinDeclarationAssembly.java` preserve the
bounded string/assembly searches. `SkinSeamQuery.java` produced
`declaration-dx11.txt`, `declaration-parser.txt`,
`declaration-load-contract.txt` and `declaration-resource-parse.txt`.
`parse_shader_declarations.py` saves `glove-declarations.private.json` and
selected shader bytecode locally. `disassemble_glove_shaders.py` uses the
existing system `D3DDisassemble` solely to decode those offline DXBC files;
`glove-vs-150.asm.txt`, `glove-vs-603.asm.txt` and `glove-ps-484.asm.txt` contain
the decisive shader operations. Raw shaders and mesh/texture bytes remain
outside Git.

## Native parser and material join

`0x9d4110` invokes the DX11 shader database readers in order.
`0x9d3460` reads vertex shader records: key16, bytecode length/data, three
array-reference indices, additional signature-bytecode length/data,
declaration count and 28-byte declaration records, then semantic strings.
`0x9d2e30` reads those NUL-terminated strings; at `0x9d3710` they replace
the declaration records' first dword. `0x9d3754` calls device vtable `+0x2c`
with declaration pointer/count and signature bytes to create the input layout.
This is the SDK-shaped `D3D11_INPUT_ELEMENT_DESC` contract, not a guessed
interpretation of the compact mesh layout enums.

The bounded parser starts the vertex table at decimal file offset 1,352,661:
1,608 vertex records end exactly at 7,875,287. The following 1,926 pixel records,
read by `0x9ce570`, end exactly at the known fixed48 count offset 22,038,979.
The parser checks that boundary. It does not claim to decode the unrelated
prefix resource tables.

`0x9ce760` reads the fixed48 table at 22,038,983 and the fixed88 table at
22,549,083 in corresponding record order. Fixed48 `+0x20/+0x24` are vertex/pixel
shader ordinal references, replaced using the database's native shader arrays.
The same ordinal joins below are now supported by that loader, rather than
merely matching two table counts.

Material key `56995c7db880db0efaa133678ac02d40` names Standard gloves.
Fixed88 `+0x20` selects the layout key. Standard is
`e7737d407ffaf6b5fbb125a5f76220be`; StreamInstancingFloat is
`95fb39460c2a9265451c5bddbe3596e1`.

| Material permutation index | Layout | Vertex ordinal | Pixel ordinal |
| --- | --- | --- | --- |
| 3293 | StreamInstancingFloat | 603 | 484 |
| 5004 | StreamInstancingFloat | 603 | 911 |
| 5763 | Standard | 150 | 484 |
| 8515 | Standard | 616 | 548 |
| 8610 | Standard | 150 | 911 |
| 9798 | StreamInstancingFloat | 1571 | 548 |

VS150 bytecode begins at 1,962,415, length 5,716, SHA-256
`b2230579d910047288374caa4e570cc218fb3eefe4b755335463148187816b17`.
PS484 begins at 11,527,135, length 9,436, SHA-256
`4843bd65d2da4ffdb307d4f794c1ee740518797aaffeaf3c9b33af6f0b5ef784`.
The smaller VS616/1571 omit the normal/tangent/UV inputs. Naming their active
runtime passes color/depth/shadow still requires observing the actual draw.

## Actual 48-byte input contract

Standard compact layout record99 is at decimal offset 23,493,074. Its active
four-byte elements `(format, usage, offset, stream)` are:

```text
(7,1,0,0), (5,7,6,0), (12,2,8,0), (12,3,12,0),
(8,4,16,0), (8,5,24,0), (6,0,32,0), (6,0,36,0), (6,0,40,0)
```

The serialized declaration consumed by VS150 resolves these source bytes:

| Offset | DX11 semantic | Native DXGI format | Shader meaning |
| --- | --- | --- | --- |
| 0 | TEXCOORD0 | 10: R16G16B16A16_FLOAT | XYZ position; W handedness |
| 8 | TEXCOORD1 | 30: R8G8B8A8_UINT | Four section-local bone indices |
| 12 | TEXCOORD2 | 30: R8G8B8A8_UINT | Four weights; shader divides by 255 |
| 16 | TEXCOORD3 | 10: R16G16B16A16_FLOAT | Normal XYZ; W unused |
| 24 | TEXCOORD4 | 10: R16G16B16A16_FLOAT | Negative tangent XYZ; W unused |
| 32 | TEXCOORD5 | 10: R16G16B16A16_FLOAT | UV pair at 32 and UV pair at 36 |
| 40 | TEXCOORD6 | 34: R16G16_FLOAT | Third UV pair |

All are vertex stream0 with per-vertex classification and step rate0.
VS603 adds stream1 instance rows at offsets0/16/32, float4 format2, per-instance
classification/step rate1; its stream0 attribute contract agrees with VS150.
Bytes44..47 are not read by these declarations. The compact usage7 at byte6
is consumed as TEXCOORD0.w in the actual shader declaration. This report
does not extrapolate a complete generic engine enum table from one layout.

## Frame semantics and cap authoring

VS150 skins and normalizes `v3.xyz` as N. It skins and normalizes `-v4.xyz`
as T. Before skinning it computes `cross(v3.xyz, -v4.xyz) * v0.w` as B.
The emitted three frame rows are `(T.x,B.x,N.x)`, `(T.y,B.y,N.y)` and
`(T.z,B.z,N.z)`.
PS484 decodes sampled tangent-space normal XY to [-1,1], reconstructs positive
Z using `sqrt(max(1-x*x-y*y,0))`, then takes three dot products with those
rows and normalizes the result. Thus offset16 is the normal axis and offset24
is the stored negative tangent axis. Position W is the bitangent sign; it is
not position homogeneous W or an unused half. Existing source corner W must
be retained exactly, including both -1 and +1 values.

The bounded cap writer can now use:

- Normal at16: outward unit geometric face normal N in the preserved native
  asset basis and winding; normal W=0, which these shaders do not consume.
- Tangent at24: store `-T` for a chosen unit T orthogonal to N; tangent W=0.
- Position W=+1 for new cap corners, yielding B=`cross(N,T)`.
- Three UV pairs at32/36/40: the deliberately authored constant `(0.5,0.5)`;
  bytes44..47 zero. This is a cap material design choice, not a recovered
  original cap texture coordinate or a claim of visual acceptance.
- Four byte weights summing255, with cap corners assigned to the appropriate
  wrist in the verified section-local palette. Preserve original mesh corner
  position W, raw tail, bone ordering and attribute seams elsewhere.

PS484 uses the first UV pair for normal/material sampling, the second for a
separate material mask and the third for another material modulation. Setting
all three is necessary even when the chosen cap UV is constant. Retaining the
native material bindings avoids fabricating texture-slot ownership from names.

This evidence resolves native layout and cap shader attribute semantics for
the selected Standard resource. Mesh buffer creation/rebasing, active submitted
passes, local-player descriptor ownership, deferred geometry lifetime and
bilateral in-game appearance remain governed by the existing replacement seam
and runtime implementation; they are not established by this offline decode.

## Assault US01 and Standard US02 glove compatibility

The same cached shader database names Assault glove material
`Characters/US/US_FP_Arms/US_FP_Assault_Mesh/Characters/US/US_Models/Shaders/US_01_Mesh_Gloves`
with key `26d9fc50e42d90db9ad4e35d3bc19339`. Its material-name entry is at
decimal offset23,511,342 in the name table beginning at23,494,320. The
comparison joins this key through the same fixed88/fixed48 ordinal contract
above to the Standard US02 glove material key `56995c7db880db0efaa133678ac02d40`.

| Assault permutation | Standard permutation | Layout | Vertex ordinal | Pixel ordinal | Fixed48 resource +0x2c: Assault / Standard |
| --- | --- | --- | --- | --- | --- |
| 6053 | 5763 | Standard | 150 | 484 | 847 / 275 |
| 6791 | 8610 | Standard | 150 | 911 | 871 / 295 |
| 1541 | 3293 | StreamInstancingFloat | 603 | 484 | 847 / 275 |
| 6882 | 5004 | StreamInstancingFloat | 603 | 911 | 871 / 295 |
| 9583 | 8515 | Standard | 616 | 548 | 1345 / 1345 |
| 4450 | 9798 | StreamInstancingFloat | 1571 | 548 | 1345 / 1345 |

Each pair references the **same native vertex and pixel shader objects**,
including their serialized declaration, bytecode and shader-local resource
reference indices. For all six pairs, fixed88 bytes16..87 are also identical;
only that record's leading material key differs. Fixed48 `+0x28` is1336 in
every pair. The remaining fixed48 differences are its leading key, dword
`+0x18`, and the shaded pairs' resource reference `+0x2c`. The native DX11
loader `0x9ce760` resolves `+0x28/+0x2c` through the database resource-block
array at database `+0xf0`, populated by `0x9ce4a0`; see
`declaration-resource-parse.txt`. Their differences must not be discarded as
merely another name or shader hash.

This proves that the 48-byte normal/tangent/handedness and three-UV input
contract above applies unchanged when the Standard-derived capped geometry
uses the native Assault glove binding. The cap authoring contract therefore
needs no US01-specific attribute adjustment. Assault also has ZOnly layout
permutations4249 (VS206/PS548) and10082 (VS879/PS548); they are separate
position/bone layouts, not evidence that a 48-byte vertex can replace a
16-byte depth vertex without the matching depth asset.

It does **not** prove identical US01/US02 textures or material appearance.
The shaded resource-block indices differ, and this comparison does not
decode those blocks into texture identities or prove matching atlas regions.
Preserving the native Assault material retains its actual resource bindings.
Standard-derived original UVs remain structurally compatible but may place
Assault detail, masks or decals differently; the three centered cap UV pairs
remain an intentional authored material treatment. Likewise the original
Assault and Standard meshes differ in topology and local palette ordering:
the existing verified remap to the Assault44 palette is required separately
from this shader proof. No runtime appearance result is claimed.

Private comparison evidence: `compare_assault_glove.py`,
`assault-glove-declarations.private.json`, `assault-glove-compare.txt` and
`assault-standard-glove-records.private.json` under the evidence root above.
Original asset and shader bytes remain outside Git.

## Nearly black glove diagnosis and variant-native correction

The supplied screenshots under
`<repo>\reports\native-skin-build-20261011` (`replacement-left.png`,
`replacement-right.png`, `native-baseline.png`) show reduced brightness across
the replacement's original glove surface and fingers, with ribbed material
detail still visible. The dark region is broader than the newly authored
wrist cap. These are observations of existing captures; this analysis did
not run the game or produce a new capture.

Offline source attribute comparison now identifies a specific material
sampling risk, beyond the earlier declaration compatibility proof:

| Glove source | Third UV U range (byte40) | Third UV V range (byte42) |
| --- | --- | --- |
| Standard US02 | 0.312744..0.444580 | 0.390381..0.613770 |
| Assault US01 | 0.035126..0.541016 | 0.026642..0.250488 |

The V ranges are disjoint. VS150 forwards byte40/42 unchanged from `v6.xy`
to `o11.xy`; PS484 consumes this as `v11.xy`. Its final operations include:

```text
sample ... r0.w, v11.xyxx, t9.yzwx, s1
...
mul o0.xyz, r0.wwww, r0.xyzx
```

The swizzle means the red channel of the texture at t9 directly multiplies
final RGB. PS911 has the same third-UV sample and final multiplication.
The shader contains other UV-dependent material/detail samples too. Combined
with the different shaded resource-block references, transplanting Standard
UVs into the native Assault binding can therefore sample an unrelated atlas
region that suppresses brightness over whole original triangles. This is the
leading evidenced cause of the observed dark glove. The actual t9 texel
values and causal resolution have not been measured; a corrected matching
capture must determine whether any additional shading issue remains.

The original Standard and Assault native N/T attributes are both approximately
unit length (all lengths within0.99967..1.00034), orthogonal (maximum absolute
dot below0.00051), and predominantly aligned with their native geometric face
normals (mean dots0.9440 and0.8820 respectively). Both preserve position W
values-1/+1. There is no evidence here for swapping channels, negating native
normals or replacing native tangents. The shader-derived encoding above
remains valid. The asset author reports unchanged original raw tails and
position W; only new cap attributes are authored.

Likewise the current native asset reader in `Bc2NativeSkinGeometry.cpp::Parse`
requires color and depth indexed corners to have identical XYZ half bytes
and identical effective native147-rig weights, after each part's local
palette remap. This guards against a replacement depth silhouette left as
the original full arms or a different skin for the same corner. It does not
prove all lighting/pass behavior, but no color/depth shape divergence has
been identified in this lane.

The bounded correction is to derive each variant's capped hands from that
variant's native glove topology, preserving its original UVs, normal/tangent
tails, position W and native material binding, with the corresponding local
palette. Use matching capped color/depth geometry and suppress only that
variant's identified unwanted arm sections. Declaration compatibility alone
is insufficient justification to reuse Standard material coordinates for
Assault or other variants.

Constant `(0.5,0.5)` cap UVs affect only cap triangles; shader sampling has no
state that spreads that choice onto original glove triangles. They are not
a guarantee of a bright or visually suitable cap: the third sample can be
dark at that coordinate too. A variant-specific cuff atlas patch, authored
from adjacent native cuff coordinates, is a grounded alternative cap policy.
Either policy must retain all three UV pairs and the proved frame encoding;
no global material brightening or shader change follows from this evidence.

Private numeric evidence: `compare_glove_attributes.py` and
`glove-attribute-comparison.private.json` under the evidence root. The input
is the existing `native-input.json`, SHA-256
`928eccbeb0f42f2d195f2323843f5c0fc80969532aca67af742b71988d198741`.
The shader evidence is the previously decoded offline DXBC, so this update
requires no new executable import or native REA session.

## NVA64/32 and Beach64/16 layouts

Stride64 identifies two different native input contracts; it must not choose
the position or skin offsets by itself. New offline material joins use the
same proven native DX11 parser VAs above, with two privately extracted
installed shader resources:

| Shader resource | Bytes | SHA-256 |
| --- | --- | --- |
| Dist SP_01 `Levels/SP_01/shaders/Dx11_Single.res` | 15,313,990 | `a7f78cc9e8b714e85e57790447255bbfd510e5d1cc2a1ab542bb46ba6ae8c7a9` |
| Package NAM_MP_002CQ `Levels/NAM_MP_002CQ/Shaders/Dx11_Single.res` | 10,553,944 | `8ea82113051a0e2af77d7148ac283e435600706487d8206b93ddfe7f60e8ef43` |

Nam common contains the NVA mesh but no DX11 shader resource. The selected
NAM_MP_002CQ resource contains its exact material names. The bounded parser
locates the first vertex-table count24 bytes before the first DXBC header,
then validates every declared bytecode, signature/declaration string count,
vertex/pixel reference bound and corresponding fixed48/fixed88 table count.
It does not claim to decode the earlier resource-block prefix or all later
file content.

| Table position (decimal) | SP_01 | NAM_MP_002CQ |
| --- | --- | --- |
| Vertex count | 714929 (1166 records) | 530305 (817 records) |
| Pixel count | 5373329 (1182 records) | 3711269 (798 records) |
| Fixed48 count/base | 13455193 / 13455197 | 9022193 / 9022197 |
| Fixed88 base | 13910769 (9491 records) | 9362521 (7090 records) |
| Compact89 count/base | 14745977 / 14745981 (105 layouts) | 9986441 / 9986445 (88 layouts) |
| Material-name count | 14755326 | 9994277 |

### NVA float-position explicit frame

`Characters/Vietnam/1P/NVA_Recon/NVA_Recon_FP_Mesh` joins Main material key
`9fd926ded986d53fb6c51bec2c6a532f` (name entry9994281) and Straps key
`7b97a47f7978670de050bedb572f34ec` (9994392). Main permutation4039 selects
VS529/PS445; Main1029 uses VS529/PS753, with instanced variants VS513.
Straps63 selects VS612/PS301, Straps2730 selects VS612/PS661, with instanced
variants VS616. Main5652 and Straps3679 reference the position/skin VS314,
PS212. Each join is fixed88 material key -> corresponding fixed48
`+0x20/+0x24` shader ordinals.

| NVA source offset | DX11 declaration | Meaning in selected shaders |
| --- | --- | --- |
| 0 | TEXCOORD0, DXGI6 R32G32B32_FLOAT | Float3 position; no source position W |
| 12 | TEXCOORD1, DXGI30 R8G8B8A8_UINT | Local bone indices |
| 16 | TEXCOORD2, DXGI30 R8G8B8A8_UINT | Byte weights scaled1/255 |
| 28 | TEXCOORD4, DXGI10 half4 | Normal XYZ; W unused |
| 36 | TEXCOORD5, DXGI10 half4 | Explicit bitangent XYZ; W unused |
| 44 | TEXCOORD6, DXGI10 half4 | Stored negative tangent XYZ; W unused |
| 52 | TEXCOORD7, DXGI10 half4 | UV pairs52/56 |

VS529 skins/normalizes source normal, source bitangent and negative source
tangent separately. It emits frame rows `(-v5.x,v4.x,v3.x)`, with corresponding
Y/Z rows. PS445 reconstructs a tangent-space normal and dots it against these
rows, proving the frame orientation. There is no position-W handedness field
and no shader cross product deriving the bitangent in this layout. Main
VS529 uses UV52 only; Straps VS612 forwards both UV52/56 in swapped output
order for its material sampling. Bytes20..27 and60..63 are unconsumed by
these selected declarations. The depth32 declarations use only position0,
indices12 and weights16; bytes20..31 are unconsumed.

For new NVA caps use float3 positions without half quantization, outward N
at28, chosen orthogonal actual T stored as `-T` at44, and explicit
`B=cross(N,T)` at36. Set the unused frame W halves0, write both UV pairs
from the adjacent native cuff policy, fill the unconsumed half4 at20 with
ones, and zero60..63. Matching depth corners retain the exact first20 bytes
and zero20..31. Preserve every original native frame/tail rather than
regenerating original bitangents. The chosen positive cap frame is authored;
the original source frame remains unchanged.

VS529 bytecode offset2598880, length5404, SHA-256
`27b7699dc206d875a121838bcf9ebc0eef06ef9cf9d6a87605a474ecc606025e`.
Compact layout15 is at9987780; it also corroborates stride64 and the
normal/bitangent/tangent/UV offsets above.

### Beach half-position implicit frame and intake correction

Beach's three color sections are stride64, while its combined depth section
is stride16. The previous stride-based intake treated color64 as float3,
indices12 and weights16, then failed because bytes16..19 are not skin
weights. The actual serialized DX11 declarations prove half4 position0,
indices8 and weights12. For example the first glove record's weight bytes
are152/103/0/0, summing255; the incorrectly selected bytes16..19 are zeros.
The failure was in the decoder, not invalid native weight quantization.

Beach material keys and representative joins are:

- `US_Infantry_Gloves`: `9a66f4db514b584eef924d13cfcd6252`, name14755757,
  permutation2742 VS606/PS225;7252 VS606/PS239; instanced VS463.
- `US_infantry_hands`: `c4547685b510bc1150202516c3d5b5c8`, name14755883,
  permutation6558 VS890/PS157;5650 VS890/PS292; instanced VS652.
- `US_infantry_main`: `d381fee07f8d5d4b62471062d069f811`, name14756008,
  permutation671 VS353/PS917;6672 VS353/PS361; instanced VS444.

| Beach source offset | DX11 declaration | Meaning |
| --- | --- | --- |
| 0 | TEXCOORD0, DXGI10 half4 | Position XYZ; W bitangent handedness |
| 8 | TEXCOORD1, DXGI30 byte4 uint | Bone indices |
| 12 | TEXCOORD2, DXGI30 byte4 uint | Weights scaled1/255 |
| 16 | TEXCOORD3, DXGI10 half4 where selected | Auxiliary material control; not normal |
| 24 | TEXCOORD4, DXGI10 half4 | Normal XYZ |
| 32 | TEXCOORD5, DXGI10 half4 | Stored negative tangent XYZ |
| 40 | TEXCOORD6, DXGI10 half4 | UV pairs40/44 |
| 48 | TEXCOORD7, DXGI34 half2 where selected | Additional Main material UV pair |

Gloves VS606 emits the frame from N24, `-T32` and
`cross(N24,-T32)*positionW`; Hands VS890 does the same. Normal/tangent W are
unused. Gloves forwards source16.xy to an auxiliary interpolant, and PS225
uses its X component to blend material terms; Hands does not consume16.
It must not be overwritten with a normal or guessed generic UV. Main uses
the additional UV at48. Bytes52..63 are unconsumed by these declarations.

New Beach caps therefore use half4 position W=+1, N24, stored `-T`32,
unused frame W=0, cuff-derived UV40/44/48 and cuff-derived auxiliary half4
at16. Zero52..63. Preserve exact original packed position W per corner;
reconstructing its sign from XYZ/tail identity is ambiguous for some native
corners. Depth16 uses matching first16 bytes. VS606 bytecode offset3130264,
length5716, SHA-256
`9fd95f8310029e05fceaef7d4ff0345767e1da9b77d454a0e86f3cea8ad7e42a`.
Compact layout14 at14747227 corroborates stride64 and the half/skin layout.

Private evidence root for these variants:
`<repo>\reports\native-hands-ready-20261011`. It contains
`parse_variant_shaders.py`, the two `*-declarations.private.json` outputs,
offline `a7f78cc9-*`/`8ea82113-*` shader disassemblies, and the exact
`beach64-cap-contract.private.json`/`nva64-cap-contract.private.json` authoring
contracts. `intake_beach_verified.py` applies only the proved Beach position
and skin offsets while retaining the existing bounds, palette, triangle,
authored-weight-count and sum255 checks. It produced
`beach-native-input.private.json`, SHA-256
`fbd79cb6e4cef7d208d13a783856c2d56e46769874390c30c2fd23431e477e75`:
all four sections pass, with4/4/3/4 maximum active weights respectively.
No shared source decoder, game installation, runtime hooks or build was
changed in this evidence lane. Layout proof is not a runtime appearance
claim for these newly covered variants.
