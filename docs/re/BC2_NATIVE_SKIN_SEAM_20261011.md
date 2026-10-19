> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Existing-instance skin replacement seam — 2026-10-11

The user approved pursuing the existing native skinned-instance route, with
seam proof before hands implementation. Static analysis now connects the
soldier's selected mesh list to native section submission and queued geometry.
The narrow candidate is a **private LOD view selected by the local draw
descriptor**, retaining native binding tables and palette processing. This is
static structural proof, not a working replacement or live ownership proof.
No hands code, hook, game launch, installation or package selection was changed.

## Target and evidence

Target: `D:\bc2-re\BFBC2Game_795745.exe`, 19,322,816 bytes, x86,
image base `0x400000`, SHA-256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
The saved Ghidra program reports the same executable digest and language
`x86:LE:32:default`. Addresses below apply to this binary only. ABI descriptions
come from call sites and assembly, not Ghidra's untyped function declarations.
These are evidence locations, not production hook signatures.

Private evidence root: `<repo>\reports\native-skin-seam-20261011`.
Reports contain decompilation, assembly and xrefs; they remain outside Git.

| Report | What it proves |
| --- | --- |
| `skin-seeds.txt`, `mesh-getters.txt` | Soldier list, request allocation, packed palettes and selected LODs |
| `mesh-dispatch.txt`, `render-consumers.txt` | Renderer dispatch to request consumer |
| `skin-draw.txt` | Local 24-byte descriptor and separate character/weapon calls |
| `mesh-submit.txt` | LOD binding/section lookup and per-section bone remapping |
| `geometry-submit.txt`, `geometry-state.txt` | Geometry wrapper, arena allocation and retained geometry interior pointer |
| `command-queue.txt`, `binding-queue.txt` | Retained state pointers, native pipeline-key construction and queue record copy |

Independent assembly review corroborated the principal offsets, calling
conventions and deferred-pointer hazard. It corrected misleading decompiler
types and omitted arguments; those corrections are incorporated below.

## REA diagnosis and recovery

REA 6.3.0 / Ghidra 12.1.3 started a fresh import and full autoanalysis before
serving requests. Its launcher uses `-import -readOnly -deleteProject`; this
installed launcher does not attach to the existing analyzed project. The
configured startup timeout is 3,600,000 ms, while the MCP request times out at
300,000 ms. Repeated decompiler warnings continued in the startup log. The
subsequent close request also timed out behind startup. This was an analysis
startup failure, not a game failure or evidence of a bad target hash.

Preserved `rea-timeout-ghidra.log` and process identities before stopping only
the exact owned Java PID 9056 and then its stuck REA server PID 42116. Identity
checks included parent chain, executable, command line and unique temporary
session/target. The old client subsequently returned `Transport closed`.
Unrelated Java processes were left alone. The root REA server still answers
`binary_session` with `open=false`; this does **not** mean target analysis was
repaired in REA. No configuration or package installation was altered.

The repository's permitted fallback resolved the research blockage: copy
`D:\bc2-re\proj\BC2.gpr` and `BC2.rep` to the private evidence root's `project`
directory, then query only that copy with `-noanalysis -readOnly`.
`SkinSeamQuery.java` and `query*.log`/`console*.log` preserve the exact queries.
The original project was not opened for writes. All ten bounded queries
completed against the saved analysis without repeating the import.

Reproduction pattern (use the existing private script and copied project):

```text
analyzeHeadless <evidence>/project BC2 -process BFBC2Game_795745.exe
  -noanalysis -readOnly -scriptPath <evidence>
  -postScript SkinSeamQuery.java <output.txt> <hex-VA> ...
```

Runtime used JDK `21.0.12.1+1`, Ghidra `12.1.3_PUBLIC`, `GHIDRA_HEADLESS_MAXMEM=2G`.
The script prints program identity, incoming xrefs, decompilation (45-second
function limit), and at most 2,400 instructions per function. An address lacking
a defined function prints data only; such a result is not function proof.

## Soldier to render request

`0x875da0/0x875dd0` obtain the soldier animation through vtable `+0x160(-1)`;
`0x78b0a0` returns its selected palette source. Existing `0x4044d0` packing
copies 64-byte source matrices into 48-byte arena matrices at the two known
`0x5f2860` call sites. The final authored finger pose can therefore retain the
existing producer and its native packing path.

`0x8bb460` is ECX=soldier, one output-vector stack argument, `RET 4`.
It shallow-copies the selected list at:

```text
begin = soldier + 0x3bc + (((~byte[soldier+0x475]) & 1) * 0x18)
end   = begin + 4
```

These are vector field addresses, not direct mesh fields. Bit 0 set selects
the first list. `0x882020` independently computes the same list's count.
List ownership belongs to the soldier; the pointed-to wrappers/resources may
be shared and must not be modified merely because this list contains them.

`0x5f2860` has ECX renderer and four stack arguments, ending `RET 0x10`.
It reads soldiers from 32-byte actor rows, allocates a `0xd0` arena request via
`0xbaa970`, selects each wrapper's LOD and stores the references below.
`0x975f50` is caller-cleaned, three stack arguments (resource, unused argument,
distance bits); only AL is a reliable return value. The chosen pointer is
`[resource + 0x44 + 4*AL]`. Wrapper `+0x22` supplies the parallel byte metadata.

| Request offset | Observed field |
| --- | --- |
| `+0x80 / +0x84` | Current / alternate packed-palette pointers |
| `+0x88` | Palette matrix count |
| `+0x9c / +0xa0` | Selected character LOD pointer array / count |
| `+0xa4 / +0xa8` | Wrapper byte metadata stored as uint32 array / count |
| `+0x8c / +0x90`, `+0x94 / +0x98` | Separate weapon LOD arrays / counts |
| `+0xb0 / +0xb4 / +0xb8` | Optional additional skin LOD / palette / count |
| `+0xcc` | Soldier `+0x475` bit 0 |

The collector publishes the allocated request into pass lists before the
producer finishes it. A later foreground/blur companion shallow-copies the
entire `0xd0` request at `0x5f35b1`, aliasing arrays and palettes. Publication
synchronization is not established. Do not patch request arrays asynchronously
or assume a producer-return lifetime.

## Narrow draw seam and native bindings

`0x5dd7f0` dispatches pass rows to `0x5d73d0` (four stack arguments, `RET 0x10`).
The latter creates a local 24-byte descriptor and calls
`0x970870(desc*, context*)`, **cdecl**, confirmed by two pushes, caller `ADD ESP,8`
and callee plain `RET`. Character-list calls are `0x5d7803` and `0x5d7863`.
Other calls at `0x5d7910/0x5d7960/0x5d79e9` cover weapon/optional skin paths;
they cannot all be treated as hands.

| Descriptor offset | Assembly-supported meaning |
| --- | --- |
| `+0x00` | Optional context-state pointer (not the decompiler's float) |
| `+0x04` | Squared distance bits |
| `+0x08` | Byte initialized to `0x7f` |
| `+0x09` | Wrapper metadata indexing the LOD binding table |
| `+0x0a` | uint16 submission mode bits |
| `+0x0c` | Selected LOD pointer: proposed substitution field |
| `+0x10 / +0x14` | Packed-palette pointer / matrix count |

`0x970870` uses two distinct LOD branches:

- `LOD+0x50`: metadata selects a 0x14-stride entry; its `+4` pointer selects
  a per-section binding array. The selected binding is placed in native
  state `+0x10`; state flags are marked and the state is pushed by `0x9a1290`.
- `LOD+0x60`: section container. Its `+0x14` points to 0x2c-byte records.
  Four counts at `+0x18..+0x24` and byte-index arrays at `+0x28..+0x34` select
  sections according to mode bits. A null selected binding suppresses a section.

Each section supplies byte bone count `+0x13`, uint16 remap pointer `+0x14`,
and geometry pointer `+0x28`. `0x9698c0` allocates the section palette in the
native arena. Each remapped matrix is copied as 48 bytes using aligned MOVAPS;
the full rig palette and authored fingers are preserved by this path.
The routine only tests descriptor count for nonzero: it does **not** bounds
check remap indices against that count. Variant and section indices likewise
need validation before supplying any clone.

Retaining the original `+0x50` bindings, section order and metadata keeps
native state/pipeline selection on this traced path. `0x9c8400` builds a native
pipeline key through `0x98bce0` and the context vtable `+0x0c` before queueing.
Calling the indexed pointer a specific named Frostbite material class remains
an inference. Its complete texture/shader schema is not established here.
Changing vertex declarations incompatibly would still invalidate this approach.

## Geometry and state lifetime

`0x9d0140` takes ECX=context and one geometry stack argument, `RET 4`.
It calls `0x9cbee0` with ECX=context and **two** stack arguments: geometry and
context `+0x38`. The decompiler omits the second argument. `0x9cbee0` allocates
a 0x40-byte command, replaces the first argument with that command, then
tail-jumps `0x9c8400`, which ends `RET 8`.

- Command `+4` stores the **address `geometry+0x10`**, not a copied geometry
  record and not the pointer value at that offset (`LEA` at `0x9cbf2e`).
- Geometry `+0x40`, byte `+0x44`, `+0x48`, `+0x4c` feed other command fields.
  Their complete buffer/count semantics remain to be established.
- `0x9c8400` copies active state **pointers** into command `+0x0c` onward,
  with count at byte `+0x36`. `0x9a12c0` pops the context stack; it does not
  revoke those queued references.
- `0x9c39f0` copies a 16-byte queue record including the command pointer;
  it does not deep-copy the geometry, state or palette resource tree.

Thus the 24-byte input descriptor may be local, but replacement geometry and
its referenced resources must survive deferred consumption. A stack geometry
clone is disproven as a safe design. Completion/fence semantics and release
ownership are still unknown; retain resources until a proven retirement point.

## Decision and remaining proof

Proceed with the existing instance. A private LOD/section view can preserve
native binding selection while changing geometry pointers at the owned
character call. No new entity/component constructor is needed for this
structural seam. Do not mutate the shared original LOD or resource.

Before hands implementation, finish the following concrete unknowns:

1. Corroborate live local-player identity from the verified soldier through
   producer request, companion alias and character descriptor. A call site,
   first-person bit or shared mesh path alone does not establish ownership.
   Any sidecar needs request-arena generation and reuse handling.
2. Observe the exact submitted LODs, section bindings, declarations and pass
   types for both eyes. Color/depth/velocity/shadow meanings are not proved by
   numeric mode values. The historical stride-48 glove color and stride-16
   combined arm/glove depth assets do not prove active coverage here.
3. Verify geometry buffer-wrapper construction/retention and queue retirement.
   Match replacement vertex layouts, section indices and bone maps to each
   retained binding; replacing color alone leaves incorrect original depth.

The next bounded runtime observation should log that exact chain without
changing geometry, palettes, bindings or visibility: owner revision, request
and companion identity, pass/eye, character call site, LOD/variant, section,
binding, palette count/remap range and geometry descriptor identity. Use
existing selected-instance validation and discard unmatched samples. It must
not repeat count-catalog matching or manufacture an exact-eye producer lease.
No such observer is implemented or packaged by this research checkpoint.
Any new interactive run or candidate installation still requires the separate
authorization explicitly requested by the user. Static interface work can
continue without that run; the seam is not yet declared replacement-ready.
