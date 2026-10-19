> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Private native skin geometry construction

Static evidence: build 795745, image base `0x400000`, SHA256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
REA import was unavailable/stuck in the parent task. Queries used Ghidra headless
`-readOnly -noanalysis` on a private copy of `D:\bc2-re\proj` at
`reports/native-skin-geometry-20261011/project`; original project was untouched.
Raw decompilation AND assembly are in that report directory's `buffer-create.txt`,
`buffer-factories.txt`, `wrapper-lifetime.txt`, and `wrapper-ref.txt`.

## Native factory contracts observed

Renderer global `0x1572a60`; constructor `0x9f3d50` installs vtable `0x14497a0`.
Device field is renderer `+0x7c`. Factory virtual offsets `+0x58/+0x5c` resolve
to VB `0x9f9ac0` and IB `0x9f4300`, both thiscall(renderer, descriptor), RET4.
They call renderer vf+20 and then static caller-cleaned factories:

* VB `0x9f7bd0(ID3D11Device*, desc*)`, descriptor 0x1c: name begin/end/cap
  at +0/+4/+8, usage +c, CPU flags +10, byte size +14, initial bytes +18.
* IB `0x9f3b80(ID3D11Device*, desc*)`, descriptor 0x20: same through size;
  index-format enum +18, initial bytes +1c.

Usage enum0 produces D3D11_USAGE_IMMUTABLE; CPU flags0 produces no CPU access.
VB/IB bind flags are1/2. Index format0 means R16_UINT; nonzero means R32_UINT.
CreateBuffer is synchronous, but its HRESULT is ignored by native constructors.
Returned wrapper is insufficient proof: wrapper+c must be checked. Native OOM
also follows a null allocation with a descriptor write; these factories do not
provide a graceful allocation-failure contract.

VB wrapper is0x34, vtable0x14495b8; IB is0x38, vtable0x1449534. Both have
refcount+8 initially0 and ID3D11Buffer* at+c. VB AddRef9d8cd0/Release9f4bf0;
IB AddRef9d7d90/Release9f3b30. These take ECX=this, no stack args, RET; locked
increment/decrement+8 returns low16 count. Release-to-zero destroys/frees the
native object. An unretained constructor result cannot simply be Released:
its initial count is0. AddRef then Release is needed for balanced disposal.
Native destructors9e9eb0/9e9a50 Release COM, free scratch/native name, and do
not free caller-provided initial bytes. Successful-buffer destruction also
uses the current global renderer's lock virtuals.

Follow-up mesh load evidence (`mesh-buffer-load.txt`, `buffer-upload.txt`):
`0x985540` allocates container+14 section records at stride0x2c, then creates
container+3c VB and+40 IB. `0x985c90` writes VB descriptor+c=0, and `0x985dfd`
writes IB descriptor+c=0 before renderer factories at985cb5/985e0b. Initial
bytes are filled through wrapper vf+18(2), then vf+1c. VB unlock9e2020 and IB
unlock9e5750 create actual buffers with D3D11_USAGE_IMMUTABLE=1, CPU flags0,
and bind1/2. The source-immutable admission requirement agrees with this
native mesh-loader path. This does not claim a particular live resource was
observed; exact current source bytes still require graphics readback.
Renderer vf+20 function9d8170 is a RET stub (`resource-buffer-load.txt`).

IB getter `0x9e9b00` is exactly `8b 41 28 c3`: ECX=this, return dword+28,
no stack arguments. It occupies vtable+20. This is the sole native method the
private wrapper implementation allows to consume its clone.

## Implemented construction route and limits

`Bc2NativeSkinGeometry` uses ordinary ID3D11Device::CreateBuffer with immutable
descriptors, owned COM references, and private full-size copies of the verified
wrappers. It does not call native allocation/AddRef/Release/destructors. Native
backend9a0480 only reads wrapper+c and calls the verified IB format getter in
this route; the lifetime lane's `FINDINGS.md`, `buffer-consumption.txt`, and
`draw-and-binding.txt` retain the independent consumer trace. Private clones
must never be passed to any other native resource-management path.

Geometry+10 stream count must1; +14 IB wrapper, +18 VB wrapper, +28 inline VB
offset, +40 native declaration, +44 mode3, +48 triangle count, +4c first index.
Backend uses declaration+5d stride and DrawIndexed(indexCount,startIndex,0).
New geometry sets offset/start0 and count=replacementIndices/3. Current native
declaration/material/palette borrows retain exactly the original draw lifetime;
no cached palette or material state is replayed.

The BC2NSK01 binary loader validates bounded sizes, body/payload SHA256, exact
four-section coverage, byte skin indices/weights, finite half positions, index
bounds, native147-rig remaps, and explicit cap-layout flags. Each live original
section must additionally match exact primitive counts/stride/remaps,
immutable source buffer device identity, and asynchronously verified SHA256 of
its complete source vertex/index ranges. The graphics-thread verifier is owned
by runtime; no collector-thread immediate-context calls occur here.
Source hashes use the CURRENT submitted VB offset and first index, bounded by
the actual immutable buffer sizes. Archive offsets are provenance only: a
loader may repack the same exact bytes into different GPU ranges.

GPU validation and creation occur only on the verified graphics thread, including
BC2's observed SINGLETHREADED device. Worker submissions perform pure native
memory reads and reuse exact matching immutable cache entries without COM calls.
New entries become available at BeginFrame, never between eye submissions.

Both glove48 and depth16 replacements are built before publishing a cache entry.
The current-call LOD view uses asset-defined section actions to filter original arm/MOLLE sections from
all four buckets and points replacement parts to persistent private geometry.
Any absent/mismatching source section, pending digest, buffer failure or cache
limit returns the entire original draw. Failed GPU allocation cannot retry
until BeginFrame, preventing a second-eye retry within that native frame.

Published entries are immutable and intentionally retained to process exit,
bounded to32 resource/device identities. They own source and replacement
COM resources, preventing COM address recycling from masquerading as identity.
Current geometry/declaration snapshots are compared on each reuse; selected
player ownership is independently required for every call, without consuming a
new cache entry on every equipment switch. Cached borrowed pointers are never dereferenced
after their original scope; new calls first read current native objects.

This is source implementation and static ABI evidence, not in-game acceptance.
Build and runtime status belong to the parent integration report. Unverified
diagnostic assets with flags0 are explicitly rejected; capped appearance and
full color/depth native draws still require actual game observation.

The focused `Bc2NativeSkinGeometryTests` target exercises the production reader
with synthetic geometry only: 20 malformed header/digest/range/section/remap/
index/weight/position cases and a valid paired color/depth mesh whose local
palette ordering differs while effective native147-rig weights agree. Parent
integration owns compilation and test execution; no pass is claimed here.
