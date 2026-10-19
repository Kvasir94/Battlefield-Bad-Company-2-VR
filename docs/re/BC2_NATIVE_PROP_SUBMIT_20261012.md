> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 original-material rigid prop submission

The previous BC2PROP1 renderer consumes only position/normal and uses a tint-only
pixel shader. Original XM8 Plastic is procedural and multilayered: PS725 uses
seven texture bindings including environment reflection. OcRGB is not albedo.
The fix reuses the current selected weapon's original material/variant bindings,
declarations, UV/tangent records, shaders and native render pass.

## Native seam and ownership

Evidence: `reports/native-skin-seam-20261011/skin-draw.txt`, `mesh-submit.txt`
(private, build SHA c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162).
The existing validated 5d73d0 stdcall request and 970870 cdecl submit hooks are
unchanged. Returns 5d7915 / 5d7965 identify selected weapon submissions.
Request +8c/+90 or +94/+98 selects ordinary/alternate LOD, +c0 selects variant;
+80/+84 supplies the corresponding palette and +88 its count. Character actor,
weak identity, equipment generation, native request allocation token and repeated
request/descriptor reads remain mandatory. Optional +b0 submissions are excluded.

970870 clones LOD +50 material bindings, traverses +60 section container, calls
9698c0 and immediately copies each remapped 48-byte palette matrix into the native
command arena. Descriptor, shallow LOD and private palette may therefore be local.
Geometry/wrappers persist for process lifetime because queued commands retain them.
No native palette or ordinary weapon LOD is modified. Original submit still runs.

## Private contract

`native-skin-props/manifest.fvrnprop`: BC2NPR01, version1, count1..32; four
length-prefixed printable ASCII strings (asset, mesh, part, basename .fvrskin),
u64 rig, u32 global bone, u32 palette count147, canonical row-major inverse-bind
16 floats, pack SHA256. Individual BC2NSK02/03 packs <=8MiB, aggregate <=64MiB.
The independent per-prop pool validates all pack hashes and indexed rigid bone
weights; unindexed VB records retain original attributes and other bones.
Every indexed color/depth corner has identical position and global skin weights.
Neither prop packs nor their geometry can enter ordinary hand Replace().

The desired palette row is inverseBind * canonicalPartWorld, reflected on both
sides to native RH, then transposed into three float4 vectors exactly like the
verified native packer. All other147-bone palette entries are copied unchanged.

## Eye admission and fallback

NativeProbe freezes pouch/dropped prop metadata immediately before the owned
tracked eye Draw. A thread-local scope ties submit to that exact call. Unscoped
worker submissions decline rather than borrowing an unrelated view. Current
selected asset and geometry key must match. Immutable source VB/IB hashes are
checked on the graphics thread; new entries activate only at next native frame.
Matching color and depth submissions are required before suppressing the gray
fallback. Repeated modes do not duplicate the clone. FVR_BC2_NATIVE_PROPS=0 keeps
the existing fallback. Malformed optional props do not disable valid native hands.

Native skin report adds prop_assets, prop_submissions, prop_scope_misses and
prop_rejected. Cached geometry still preserves original per-section bindings.

## Validation and remaining scope

2026-10-12: x86 NativeSkinRuntime/NativeProbe objects compiled; synthetic reader,
pool isolation, indexed rigid bone and translated/rotated palette tests passed.
Production reader accepted all13 private packs (4,220,248 bytes, 6,382 original
color triangles), including shell and M9 aliases. No private bytes are in source.

In-game native submission/appearance is pending the coordinated combined build
playtest. This does not claim GPU visibility from a CPU submit counter. A dropped
magazine belonging to a previously selected gun keeps the fallback if there is no
current matching native material submission; old material pointers are not owned
or retained. Dynamic worker dispatch outside Draw is reported as scope misses.
