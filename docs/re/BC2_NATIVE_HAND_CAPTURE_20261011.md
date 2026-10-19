> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Bounded Standard native hand draw capture

This source checkpoint observes native draws. It does not load the capped asset,
draw independent hands, hide arms, or change the selected build/profile. The
renderer backend in `BC2_NATIVE_HAND_MESH_20261011.md` remains unintegrated.
This work was source-only: no child build, test execution, GPU or game run.

## Root's capture command and timing

Root owns the build, chosen DLL, launcher, XR host and game. Wait until the other
game session has ended. Use one BC2 process, a live existing XR host/frame channel,
and no existing injected native session. The process must inherit these settings
**when the game is created**, before injection:

```powershell
$env:FVR_NATIVE_HAND_CAPTURE = '1'
$env:FVR_HANDS_MODE = 'ik'
```

The existing launcher's automatic continuous native injection must be disabled
for this diagnostic launch. A continuous probe with this explicit capture setting
is rejected. Setting the shell environment after the game already started does
not set the game's environment. Do not introduce a second game/host or change the
installed selection to perform this capture.

Once the single configured game is at its menu and the host's actual frame channel
has been resolved into `$nativeHandFrameChannel`, use the existing capture route:

```powershell
& <repo>\Start-NativeTrace.ps1 -FrameChannel $nativeHandFrameChannel `
    -Controllers -MotionAim -BodyFollow -PoseObserve -Hands -PassEvidence -Seconds 60
```

Root must compile/select this checkpoint into the DLL that this script copies
from `build\x86\BC2NativeProbe.dll`. The script requires the configured campaign
process already running. It handles the normal local trace folder and teardown;
this helper creates no second staging route for VB/index/VS constant buffers.
Keep console output and trace files local. No DLL, asset, private binary, raw trace
or total-angle report belongs in a provider message or Git commit.

Begin loading the intended Standard-glove campaign level immediately after
injection. Hold the same equipped weapon and keep the hands neutral once the
player appears. Do not equip, reload, die, or recenter during the selected frame.
The first verified worker scope supplying the original 147 hand rig fixes a single
world/request/native-frame candidate. The draw then freezes the exact actor,
weak reference, weapon, owner/equipment/space generations, rig pose, input
generation and paired packed-output hash. Both native eye views must match this
tuple. Four LOD0 sections are targeted: ArmStandard, MOLLE, Gloves and
ArmStandard_ZOnly. No unrelated section becomes an owner because its count fits.

The **60-second outer trace** permits menu/loading and shader creation. The byte
capture admission expires **15 seconds after that first scoped pack** and admits
one frame, at most 16 pairs and 32 draw candidates; it does not survey 60 seconds of
animation. Existing asynchronous readbacks use `DO_NOT_WAIT`, with no flush,
sleep, or GPU wait on the render thread. Startup layout/shader observations are
bounded to 64 layouts and 96 shader creations, bytecode at most 256 KiB each. Upload
lineage retains at most 64 active/exact records, not the first 64 unrelated writes:
unrelated completed maps are discarded and a later write invalidates the previous
resource receipt. VB/IB/VS-CB bytes reuse `Bc2ReloadDrawCapture`'s bounded staging;
extra texture/SRV bytes are capped at 4 MiB per admitted draw and supported formats.

An in-level `-Seconds 15` run is also admitted and can recover geometry, top-mip
texture slots and direct upload lineage. **Fresh game plus the normal 30-second
fast-start injection does not guarantee observing layout/VS/PS creation.** The
public D3D11 interfaces expose no `GetDesc` on input layouts or bytecode getter on
an existing shader. Injecting at menu before the level can observe newly created
objects; the game can reuse cached objects, or exhaust the bounded creation slots,
and the receipt then states that gap. A complete layout/material contract needs
creation actually observed before the glove's first use, or a separately verified
static cached-object/bytecode binding. This checkpoint adds no new early process
initialization or guessed internal-object reader.

## Exact seams and evidence

* The original native PackHook runs once and retains its EAX incidental return.
  Observation follows packing. `ReloadProducerBinding` compares the actual 64-byte
  input to all147 observed 48-byte outputs, accepts exactly one distinct paired
  destination, and publishes only after its verified native worker scope completes
  with the same owner. `handsNamed` requires the explicit diagnostic switch,
  original 147 skeleton/fingerprint and exact wrist/finger ancestry. Default-off
  behavior and actor/rig/request/hash mismatch rejection have synthetic tests.
* `CurrentHandsView` is only a candidate scope identifier, not final proof. The
  draw's normal binding `Read` must also succeed. A stale/aborted/multiple-view
  worker, different packed hash, or other frame yields no admitted hand draw.
* Public SDK device slots 11/12/15 observe CreateInputLayout/VertexShader/
  PixelShader. Context slots 14/15/48 observe Map/Unmap/UpdateSubresource. SDK
  `offsetof` assertions live in `Bc2D3D11ContextSlots.cpp`; each runtime entry
  must belong to `d3d11.dll`. Original calls/returns occur once. Object references
  are retained so an address reused by a new object cannot masquerade as a bind.
* The direct lineage edge is the exact pack destination range inside a writable
  mapped resource, rechecked before Unmap, or inside the exact UpdateSubresource
  source range. The draw snapshots this resource's current committed lineage and
  readback. If the engine performs an intervening remap/copy from another CPU
  address, byte correspondence cannot prove that missing edge. This is an explicit
  remaining contract gap, not grounds to guess a native ABI or owner.
* Indexed draw observation records actual layout/VS/PS, target/depth views, depth
  state, bound VS-CB/SRV and PS texture views, plus a return observation after the
  original indexed call. Exact archive position/skin signatures identify geometry.
  The decoder additionally compares complete native vertex records locally.
  Shader creation yields local DXBC and D3DReflect input/resource metadata.

The archive signatures identify Standard variant
`6a9e83c77b51f172705b1f17cecb6f5d0068e1e53f0a2e8601b1eb53e0ab7d62`:

| Section | Indices | Stride | Palette | Skin FNV | Position FNV |
|---|---:|---:|---:|---|---|
| ArmStandard | 4794 | 48 | 12 | b1b4919cd9ced567 | bf112b93fc653fe0 |
| MOLLE | 1020 | 48 | 12 | 2fd3dcfd7c41a964 | 3c7d7479fd286159 |
| Gloves | 10164 | 48 | 35 | a95276bf7113e308 | 801c7a6b7e9e5d8e |
| ArmStandard_ZOnly | 15978 | 16 | 44 | 8fc17b721610ba0a | 73922dde79914067 |

## Local decoding and curated output

The existing `reports\native-trace-*` folder gets
`native-hands-contract\metadata.json`, bounded `.bin` readbacks and `.dxbc` shader
files. Schema `bc2.private.native_hand_draw_capture.v1` contains the frozen tuple,
layout descriptors, shader reflection, packs, per-eye draws, indexed-call returns,
resource views and exact committed upload edges. These are private local inputs.
The main report only reports requested/disabled/drained/local-written and scalar
error/busy/rejected counters, with `replacement_draw_active:false`.

Root runs the source decoder locally, substituting the actual capture directory
and candidate DDS paths from the existing private intake (not invented paths):

```powershell
python <repo>\tools\bc2_native_hand_contract.py `
    --capture "$nativeTraceFolder\native-hands-contract" `
    --native-input <repo>\reports\floating-hands-intake\native-input.json `
    --export <repo>\reports\floating-hands-intake\standard-floating-hands-v1-render-input.json `
    --dds "Glove_01=$gloveDiffuseDds" --dds "Glove_01_S=$gloveSpecularDds" `
    --dds "Glove_01_Nm=$gloveNormalDds" `
    --receipt "$nativeTraceFolder\native-hands-curated-contract.json"
```

The decoder pins the existing native-input and capped export hashes, checks
resource references stay inside the capture folder, rehashes submitted geometry,
maps original section bone names to the original 147 rig, and searches the native
ordered 48-byte palettes in exact VS-CB/SRV readbacks. It separately reports whether
the direct upload edge exists. Candidate DDS top-mip bytes/dimensions/format must
match the actual bound PS texture bytes; PS reflection provides declared resource
slots. Input-layout offsets/formats consumed by the captured VS provide exact
native-tail NORMAL/TEXCOORD candidates. Unknown formats, absent creations,
incomplete copies and unexpected owners remain gaps.

Schema `bc2.curated.native_hand_draw_contract.v1` contains hashes, section/eye/slot
facts, layout candidates and gaps. It hashes the frozen identity and target/depth
identities rather than exporting process addresses or matrices. It reports the
same-frame bilateral original inventory and color/depth binding association;
**native attribute semantics, material semantics, ownership authority, replacement
draw active and runtime readiness remain false.** Texture slot equality does not
interpret the native shader's UV conversion, normal encoding or material model.
That interpretation must be performed locally against the observed DXBC before
an asset reader can set the backend's verification flags. Cap UV/normal/material
selection and the private asset reader also remain outstanding.

The future substitution still needs a final Pack pose publication to the backend,
graphics-owner lifecycle/query/eye proof, and all-or-fallback replacement of the
original color and depth sections in both eyes. This read-only capture cannot
prove replacement has executed and does not diagnose the reported firing flashes.
The recording deadline is not evidence that those remaining contracts are ready.
