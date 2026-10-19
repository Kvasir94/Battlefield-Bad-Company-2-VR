> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 native rotational recoil contract

Static investigation, 2026-10-11, build 795745 (x86). Addresses are preferred
image VAs at base `0x400000`. This is a source handoff, not an installed recoil
binding or a runtime acceptance result. The renderer/M9 executable candidate is
independent of this work.

REA was unavailable. The pass reused the analysed Ghidra project through
`-process -noanalysis -readOnly` on an ignored local copy of `D:\bc2-re\proj`.
Targeted static excerpts were authorized. Complete packets remain under
`tmp-recoil-contract-20261011`; runtime traces and the earlier total-angle
report/analysis remain local and were not printed into provider input.

## Two distinct native lanes

| Lane | Native state and behavior | Consumers / consequence |
| --- | --- | --- |
| FireLogic per-shot ramp | `0x6dd790` dispatches shot-preparation listeners, then samples authored recoil into firing-object `+0x64/+0x68/+0x6c` (X/Y/Z radians) and `+0x70` (FOV). It sets remaining time at `+0x60`; firing update `0x6e9000` decrements and clamps that timer. Axis getters `0x6cd6a0/0x6cd700/0x6cd760` return sampled angle times remaining time / duration. This lane replaces the sample rather than accumulating shots. | Camera `0x89c490` reads firing A and applies pose/zoom recoil multipliers. Camera bobbing also consumes the lane. It is not GunSway's accumulating recoil. |
| GunSway recoil | Firing-object `+0x14` owns the GunSway listener, vtable `0x1425af8`. Shot-prep callback `0x6cc860` sets its `+0xc4` latch. Native tick `0x6ec560` calls integrator `0x6d60e0`, which accumulates/clamps amplitude `+0x24` and heading `+0x28`, recovers them, writes radians at `+0x2c/+0x30`, and builds camera rotation at `+0x40`. Optional spring state is reached through `+0x3c`. | Camera output virtual slot `+0x68` reads the absolute camera transform. Raw recoil output `+0x64` and dispersion output `+0x60` are distinct. |

The first callback lead, client listener `0x80f590`, forwards animation/sound
and listener work; it is not itself the recoil accumulator. The writer after
listener dispatch and the GunSway listener must be considered separately.

The FireLogic angular conversion uses the native pi/180 constants. Its duration
is `60 / FiringFunctionData[+0x84]`, with native fallback 1. These units and the
absolute ramp differ from the unused BFSoldier degree-based math helpers.

## Selected owner and outputs

Camera resolver `0x892240` receives the soldier's `+0x120` subobject. Selection
reads the selected wrapper from the local soldier inventory. If soldier
`+0x114 & 0x01` is set, it chooses firing A (wrapper `+0x3c`), unless flag
`0x10` selects B (`+0x40`), then returns firing `+0x14`. With flag `0x01` clear,
getters `0x7efd00` / `0x7f9720` supply the alternate object's sway; that object
uses sway `+0x90` in its update and is not a normal WeaponFiring object.
Consequently, reading wrapper `+0x3c` unconditionally does not reproduce the
camera's GunSway selection. The scalar FireLogic camera lane separately uses A.

The candidate visual sample is the **absolute GunSway camera transform after
the original native tick**. Hip fire uses its native damped springs; zoom uses
raw recoil scaled into the same camera matrix. Reading only the raw radians
would discard that distinction. Sampling must not advance the integrator a
second time or sum successive absolute matrices.

This sample covers GunSway's camera response. Transferring it alone does not
transfer all native rotational effects: the FireLogic X/Y/Z camera additions
remain a separate layer, with their own stance/zoom multipliers. Preserve that
distinction in the full transfer design; FOV is not a wrist rotation.

Native storage uses padded basis columns. Native multiply `0x53db40(A,out,B)`
produces `B*A`; the spring rotation is `Rx(pitch)*Ry(yaw)`, and the raw rotation
is `Rz(roll)*Rx(pitch)*Ry(yaw)`. These describe native composition, not permission
to copy the bytes into the adapter's row-vector matrix or invert signs blindly.
Positive native Rx maps +Z toward -Y; positive Ry maps +Z toward +X. The raw
pitch writer negates atan2, while the yaw writer uses positive atan2. Camera
`0x89c490` begins with `base * cameraRecoil * localOffset`, then applies other
effects including the separate FireLogic Euler additions. Its GunSway camera
read is gated by soldier `+0x475` mask `0x10` and a non-null selected sway.

The reviewed `ClientShoot` and `ServerShoot` paths consume dispersion through
slot `+0x60`. Slot `+0x64` reads the distinct recoil pair. These paths do not
support KF2-style firing-angle subtraction or adding another recoil rotation
to the shot matrix. Preserve native shot orientation/spread while transferring
the admitted visual sample around the firing grip.

## Native update contract

Register use, callers and stack cleanup were inspected for these x86 ABIs;
Ghidra's inferred prototypes alone were not used as calling-convention proof.
`SwayContext`, `FiringContext` and matrix names below describe observed roles,
not complete recovered C++ types.

| Entry | Arguments after ECX=`this` | Stack cleanup |
| --- | --- | --- |
| GunSway tick `0x6ec560` | `float dt, SwayContext*` | `ret 8` |
| Recoil integrator `0x6d60e0` | `float dt, SwayContext*, const float* modifierPair` | `ret 0xc` |
| Camera spring step `0x6cc760` | `float dt` | `ret 4` |
| Spring matrix `0x6d5db0` | `Matrix* out, float scale` | `ret 8` |
| Raw recoil matrix `0x6dd4b0` | `Matrix* out, bool visual` | `ret 8` |
| Camera matrix `0x6d5f90` | `Matrix* out` | `ret 4` |
| Firing step `0x6ec6c0` | `FiringContext*, SwayContext*, int flag` | `ret 0xc` |

The FireLogic scalar getters use ECX=firing, no stack arguments, and return
through x87 ST0 with plain `ret`. Shot-prep writer `0x6dd790` is thiscall with
one Boolean argument (`ret 4`).

GunSway tick runs dispersion, recoil and movement-sway steps, then clears its
shot-prep latch. Native recoil applies shot increments/clamps and recovery each
step, deriving absolute pitch/yaw; camera spring recovery is another native
stage. The mod must not supply replacement angular decay or angular shot
impulses. The separate KF2-style translational presentation below does not
advance or replace this native integrator.

Outer firing step `0x6ec6c0` first invokes GunSway slot `+0x5c` with time-step
from firing-context `+0x18`, then runs firing update `0x6e9000`. A shot-prep latch
emitted by the latter is therefore processed by the next sway tick on this
path. The alternate object's `0x823ff0` also ticks sway before delivering its
queued events. Native repeated calls must not be suppressed by a render-frame
counter.

## Binding evidence

The following exact prefixes each matched once in the executable code ranges
of SHA-256 `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
Uniqueness in this image is static evidence; an adapter must still validate
the ownership chain, call links and calling convention before installing a hook.

| Entry | Exact bytes |
| --- | --- |
| GunSway tick `0x6ec560` | `83 EC 2C 53 8B 5C 24 38 8B 03 8B 50 14 55 56 8B E9 57 8B CB FF D2 84 C0` |
| Recoil integrator `0x6d60e0` | `83 EC 10 53 8B 5C 24 1C 56 8B F1 57 8B BE E0 00 00 00 C7 07 00 00 00 00` |
| Camera output `0x6d5f90` | `55 8B EC 83 E4 F0 81 EC 8C 00 00 00 D9 41 40 56 8B 75 08 D9 1E 0F 57 C0` |
| Outer firing step `0x6ec6c0` | `56 8B F1 8B 4E 14 85 C9 57 8B 7C 24 0C 74 13 8B 54 24 10 D9 47 18 8B 01` |
| Shot-prep writer `0x6dd790` | `83 EC 14 53 56 8B F1 8B 46 08 8B 58 40 83 C3 0C 80 7C 24 20 00 57 74 05` |
| Camera owner resolver `0x892240` | `56 8B F1 F6 46 F4 01 74 08 8B 8E 2C 01 00 00 EB 06 8B 8E 28 01 00 00 E8 04 C7 E3 FF` |

## Remaining publication check

**2026-10-11 implementation clarification:** the scheduling discussion below
describes what is required to claim that a native tick consumed a particular
tracked input. It does not prevent an owner-bound presentation layer from using
the latest completed native absolute local rotation. The production route at
the end of this document preserves the native firing basis and makes no
same-input or same-frame simulation claim.

The native writer and output are identified; the unresolved boundary is the
selected copy's tick versus the adapter's rig-publication epoch and scheduling.
`GatherHook` publishes tracking inside the enclosing player update, whereas
native weapon and camera consumers have their own update paths. Static call
links alone do not establish a coherent sample for that publication.

The existing `total_angles_radians` and `FireRecord` schemas contain neither
GunSway identity/state nor its native tick serial. Repeating their existing
capture cannot close this gap. The next observation must associate native
tick completion, selected camera read and rig publication with the same local
actor, equipment incarnation and input/space epochs. No new capture is requested
by this document; integration owns build, deployment and runtime coordination.

The smallest observer has two seams, with a third only if ambiguity remains:

1. After original `0x6ec560`, record admitted selected sway, exact firing or
   alternate branch, actor/equipment generation, QPC/thread, native dt, monotonic
   tick serial, zoom flag, raw pair and camera matrix. Preserve native call count.
2. Before existing rig publication, record the same validated identity/epochs
   and latest matching tick serial. Retain its original time; do not make an old
   sample appear fresh by assigning publication time to it.
3. If copy/gating ambiguity remains, observe camera output `0x6d5f90` at the
   camera return site `0x89c649`, recording consumed identity/serial.

One shot through recovery, an equipment change and a hip/zoom transition can
check sample order, invalidation and native branch selection in a future
coordinated capture. Raw observations stay local. A bounded observer can be
implemented before that capture; do not claim a completed same-frame transfer
from the static evidence alone.

Once that boundary is resolved, publish an immutable owner/copy/epoch-bound
sample and apply it once before the final firing-wrist/weapon IK solution.
Attached support follows that item; raw controller contacts and free/loading
hands retain their independent input. Preserve the stable HMD and native shot
orientation. Coordinate changes to `Bc2RigPublication.h/.cpp` with its owner
before implementing the publication extension.

See [BC2_RECOIL_TRANSFER.md](../BC2_RECOIL_TRANSFER.md) for the source integration
constraints and historical diagnostic coverage.

## Bounded source observer, 2026-10-11

`Bc2NativeRecoilTransfer.h/.cpp` and `Bc2Gameplay.cpp` now supply an opt-in
ordering observer. Set `FVR_BC2_NATIVE_RECOIL_OBSERVE=1` in the game's inherited
environment before a deliberate integrated session. This is source work,
not a deployment, capture or implemented recoil transfer. The default path
installs no new sway hook and allocates no observation ring.

The observer checks unique exact prefixes at the tick, camera output and
selected resolver VAs above. Before hook creation it also compares complete
tick (`0x15f` bytes), camera getter (`0x147` bytes) and resolver (`0x74` bytes)
against the live image, checks tick `ret 8` at `0x6ec6bc` and camera `ret 4` at
`0x6d60d4`, and validates the GunSway vtable links at `+0x5c/+0x68`. Native
camera's 17-byte call sequence at `0x89c638` is file/live checked, including
`call edx` at `0x89c647` and return `0x89c649`. The exact x86 hook ABIs are
`void __fastcall(this, edx, float dt, SwayContext*)` and
`void __fastcall(this, edx, Matrix* out)`; both call their original once.
Both hooks participate in the existing gameplay in-flight counter and stop
loop. If either hook cannot be created, the diagnostic is left uninstalled.

An immutable atomically published context identifies the admitted local
on-foot actor, exact selected wrapper/equipment, actor and equipment generations,
tracking space, input sequence, rig equipment generation and original QPC
deadline. Native callback threads never read the mutable gameplay generation
counters directly. The existing `Resolve` validates the local actor and selected
inventory route. Repeated scope/branch reads validate firing A/B using the
already admitted WeaponFiring vtable and GunSway vtable/slots. The resolver is
not called again. With `soldier+0x114 & 1` clear, the observer emits an unsupported
alternate publication row and admits no alternate tick; the different alternate
object layout remains explicit.

The three seams are:

1. `NativeSwayTickHook`: after the original `0x6ec560`, copy selected sway
   `+0x40` and raw pair `+0x2c/+0x30`, native `dt`, QPC and thread. Each admitted
   completed call receives its own monotonic serial, including repeated calls
   during one tracked input. It never calls or suppresses the integrator.
2. `PublishRecoilObservedTracking`: immediately before the existing
   `rigPublication::PublishTracking` calls inside `GatherHook`, including the
   deferred holster/detached-magazine routes. Invalid publication routes clear
   the context and latest tick. Valid routes capture the current selected state
   plus a matching latest tick serial and its original time.
3. `NativeSwayCameraHook`: after the original camera getter, only for return
   `0x89c649`, copy the actual caller output. This observes which local selected
   sample was available to the native camera without recomputing its matrix.

The completed local `native-trace.json` gains
`gameplay.native_recoil_observation`, with a 4096-row circular history, total,
overwrite count, selected tick count, unlabelled/rejected callback counts and
QPC frequency. `seam` is 0=tick, 1=pre-publication, 2=camera; `branch` is
0=unknown, 1=A, 2=B, 3=alternate. `status` follows
`NativeRecoilReadStatus` in the helper. `native_camera_columns` contains four
native basis/translation columns with three components each; native padding
is omitted. `raw_radians` is evidence only. This report and derived analysis
remain local; do not print/upload their values into provider input.

`matched_tick` means identity/copy/gating match with a captured tick no more
than 100 ms old. It is an observation classification, not firing or rendering
authority. `qpc` is the seam time; `tick_qpc` stays the original tick time, and
`tick_input_context` records the tracking label available at that tick. A
repeated publication does not renew it. Matrix equality is reported separately
as `camera_matrix_matches_tick`; a mismatch remains visible. Native callbacks
can execute concurrently, so repeated reads do not establish simulation
atomicity. Rows are retained in insertion order with explicit QPC/thread values.

The report always states `transfer_implemented=false`,
`same_frame_authority=false`, `zoom_context_observed=false` and
`alternate_branch_supported=false`. Hip/zoom output changes remain native;
calling an unverified context virtual to label them would weaken the contract.
No total-angle residual, sign conversion, native state write, wrist/weapon
rotation, shot adjustment, standalone-hand transform or HMD change is made.

`Bc2NativeRecoilTransferTests` contains authored CPU fixtures for exact A/B
selection, unsupported alternates, owner/vtable/state rejection, branch changes
during reads, absolute repeated native samples, distinct tick serials, original
time preservation, stale/epoch invalidation and bounded retention. The recoil
source owner has not compiled or executed these tests; integration owns those
checks and any later game session.

`Bc2NativeRecoilAbiTests` is a separate x86 CPU MinHook fixture sharing the
runtime's declared tick/camera types. It checks float/context/output transport,
exactly-once original calls, repeated stack cleanup, output canaries and hook
disable behavior against authored functions. It executes no game or GPU work.
The source owner has not executed this fixture either. The opt-in runtime
uses three cheap candidate reads to discard unrelated native sway calls before
running the full local-owner admission; this filter supplies no authority.

The remaining observation is still selected tick versus publication/camera
epoch. One selected firearm through a shot/recovery, equipment change and
hip/zoom transition can now expose that relationship in a future coordinated
capture. Full pose transfer also requires the adapter matrix/pivot composition
to be established and the separate FireLogic camera layer to remain explicit.
The existing shot paths still support preserving native firing orientation,
not adding or subtracting another recoil rotation.

The final hand renderer's owner confirmed that it consumes the exact final
native skin palette after the current rig/contact/reload/finger layers and
applies no additional wrist or recoil transform. Eventual admitted recoil
therefore belongs before the existing final weapon/right-wrist/support solve
in `Bc2RigPublication.cpp`; that renderer must continue to consume the resulting
palette once. No pose-publication source edits were made in this checkpoint.

The ignored `CONTRACT.local.md` indexes the individual static instruction and
decompilation packets. `signatures.local.json` records all 11 final signatures,
their lengths and unique matches across `.text` and `ctr`. These are exact
build-specific signatures; some longer sibling-disambiguating signatures include
absolute references and must not be shortened or treated as cross-build patterns.

## October 11 continuation: shot-base ordering

REA's fresh offline analysis timed out after 300 seconds without returning new
function evidence. The permitted saved-project copy was used with `-process`,
`-noanalysis` and `-readOnly` for these targeted checks; no game was attached.

- ClientShoot `0x81c170` begins with effects `+0x30` basis. It composes authored
  ShotConfig at `0x81c35f` (return `0x81c364`), then obtains dispersion through
  shot context `+0x14`, virtual slot `+0x60`. The observed composition is therefore
  a shot base, not a final projectile direction.
- Server builder `0x6cddf0` transforms effects `+0x30/+0x40/+0x50` basis through
  the `+0x70` 3x3 and adds `+0xa0` XYZ to `+0x60` position.
- Client constructor `0x832790` calls base `0x6ef420`, then installs vtable
  `0x1432300` (ClientShoot at slot `+0x4c`). The base initializes both rotational
  bases to identity and `+0xa0` translation to zero. This does not identify their
  later writer or establish that the `+0x70` rotation is recoil.
- Candidate `0x819c20` is unrelated sorting. Setters `0x6ced40`, `0x6cbde0` and
  `0x6cb9d0` target different offsets/owners; none establishes the missing writer.

Retained local packets are in `tmp-recoil-contract-20261011/shot-base-writer-20261011`,
`shot-base-setters-20261011`, `shot-base-owner-20261011`,
`shot-base-parent-20261011` and `shot-base-table.local.txt` beneath that directory.
No new native call or hook signature was introduced.

The existing opt-in observer now captures before/after VR aim writes and owned
client/server shot bases with actual shot-sway identity, original input/rig
deadlines and an exact immutable context check. Existing enum values 0–2 remain;
3/4 are Before/AfterVrBaseline and 5/6 Client/ServerShotBase. Matrix conventions
remain explicitly named. Focused CPU ordering tests pass. These records must not
be treated as same-frame authority or used to add/subtract a ballistic recoil
rotation. The presentation-only contract below does not depend on resolving the
additional server basis writer.

## Presentation-only transfer contract

The missing writer of effects `+0x70` is not a blocker for moving the native
GunSway camera response to the visible held item while preserving native firing
rotation. The necessary distinction is between the private pose and the native
weapon getter output: `Bc2RigPublication` writes its placement to a caller-owned
getter result that feeds native root/effect consumers. Adding recoil to that
result could indirectly change the shot baseline. Preserve the original tracked
placement for that native output; apply recoil only to the private weapon/hand
pose and its immutable visual muzzle publication. Existing shot-origin
translation may follow the visual muzzle; native shot orientation and subsequent
dispersion remain native.

### Exact response and lifetime

Use the camera transform at selected GunSway `+0x40` immediately after the
original `0x6ec560` tick. This already includes the correct hip/zoom branch:

- Integrator `0x6d60e0` queries the context's slot `+0x14`. For zoom it calls its
  own slot `+0x64` with `out=this+0x40` and `visual=true`, producing the scaled
  raw response directly in `+0x40`.
- Hip response uses native spring stepping and `0x6d5db0` with visual scale
  `+0xb0`. Reading only raw `+0x2c/+0x30` would discard that recovery.
- Getter `0x6d5f90` copies `+0x40` and composes an identity rotation: `FLDZ` at
  `0x6d5ffc` and `0x6d600f` supplies the additional cosine/sine arguments. There
  is no additional zoom scaling to recover from the getter.

Retained evidence is `sway-constructor/function-006d60e0.local.txt`,
`sway-spring/function-006d5db0.local.txt` and
`sway-outputs/function-006d5f90.local.txt`. This is static evidence from the
already analysed exact executable, not a new live capture.

Publish the absolute rotation with exact local actor, equipment incarnation,
selected firing/sway copy, reference-space identity and original tick QPC.
Consume only a bounded, still-current sample. A newer tracked input may use the
latest completed local rotation without asserting that the native tick consumed
that input. Never renew its timestamp, accumulate successive absolute matrices,
step the integrator again, or infer a new sample from a render counter. Missing,
invalid, expired or wrong-owner data leaves the ordinary tracked pose intact.

### Conversion and pivot

Use the existing `FrostbiteCamera::CanonicalCamera` convention: native padded
basis vectors become the adapter's row-vector storage, homogeneous padding is
sanitized, then both local and world Z axes are reflected. Do not additionally
transpose, invert, convert degrees or negate extracted Euler angles. Preserve
the native matrix's axis coupling and hip/zoom response.

Let `Rc` be this canonical rotation, `A` the un-recoiled tracked aim rotation
with zero translation, and `p` the resolved firing wrist position. In the
adapter's row-vector multiplication convention:

```
Q = inverse(A) * Rc * A
delta = T(-p) * Q * T(p)
visualWeapon = trackedWeapon * delta
visualRightWrist = resolvedRightWrist * delta
```

The aim frame carries the tracked gun's heading and roll. Using only the upright
body frame would apply recoil in the wrong axes when the player turns or rolls
the gun. The wrist pivot remains stationary, the stable weapon-in-wrist
attachment remains intact, and attached support follows the same recoiled item.
Raw controller inputs, free/loading hands and all raw interaction contacts must
retain the un-recoiled placement; cosmetic motion cannot manufacture contact or
alter grab ownership. Apply the absolute response once to each newly solved
private pose, never to a previously recoiled pose.

This transfers GunSway's native rotational response. The separate FireLogic
scalar camera ramp and FOV effects remain distinct. Native timing/hip-zoom
semantics are established statically; headset feel and exact visual/ballistic
alignment remain playtest observations rather than claims from these formulas.

## KF2-style hand kick and authored strength

KF2 VR source at `d0f5362e1d6aaf4137f324a4c7c85c5fe77f012a` adds a separate
rearward translation to the native angular response. In
`script/KF2VR/Classes/VRHandsBridge.uc:3034`, each new native FlashCount adds
0.8–4 cm according to the logarithmic authored recoil-strength curve, halves
that impulse while actually braced, caps accumulated displacement at 5 cm,
attacks over 45 ms, and recovers exponentially over a strength-dependent
0.15–0.6 seconds. Weapon placement at line 3096 anchors the rotation at the
primary grip before subtracting this displacement along the final bore. The
primary and attached support wrists then follow the moved weapon. Raw tracked
hands and interaction ownership remain independent.

The corresponding BC2 strength is a read-only authored-data sample:

1. Integrator `0x6d60e0` loads the selector pointer at owned GunSway `+0xe0`.
   Constructor `0x6e5db7–0x6e5dc6` establishes this as an eight-byte allocation:
   resolved pointer at `+0`, constructor data pointer at `+4`.
2. Selector `0x6d0d80` receives native stance and zoom state and resolves into
   that data object. Stance 0 uses offsets `0x138/0x1c4`, stance 1
   `0x23c/0x28c`, and stance 2 `0x6c/0xbc` (zoom/hip respectively).
   A null result or any other resolved displacement is not an admitted sample.
3. Resolved data `+0x10` is authored amplitude in degrees. Instruction
   `0x6d618a` multiplies that value by the native degrees-to-radians conversion;
   constants at `0x14d9640`, `0x140a0b0` and `0x14d9488` are respectively pi,
   180 and 0.5. The native shot amplitude ranges from half to all of this
   authored amplitude, giving `authoredMeanDegrees = 0.75 * amplitudeDegrees`.

The shape weight can convert that authored mean to KF2 rotator units using
`degrees * 65536 / 360`, then use KF2's 50–1200 logarithmic range. This is a
mapping for the added visual displacement and haptics, not a replacement for
BC2 native recoil magnitude. Zero authored amplitude must produce no added
kick; it must not be raised to the lightest nonzero kick by the curve clamp.

Native runtime amplitude modifiers remain distinct. Tick `0x6ec560` builds an
eight-float stack array, applies the modifier list through `0x6da250`, and
passes the final pair at `0x6ec667–0x6ec673`. Integrator `0x6d61d5` multiplies
the randomized amplitude by pair element 0; `0x6d61a9` multiplies heading by
element 1. These are transient stack values, not fields that can be read after
the tick. The authored strength sample therefore does not claim perk-modified
instantaneous strength. The separately transferred native camera rotation
continues to include native runtime modifiers and recovery.

Read the selector/data/amplitude only through the already verified owned
selected firing/sway chain. Re-read the selector pointer, both selector words,
amplitude and owner/copy before admission; reject changes, invalid pointers or
nonfinite amplitude. Bind the integrator's selector-read and amplitude-read
code spans as well as the existing sway signatures. No new native call or
native zoom write is required. The resolved hip/zoom choice remains the
game's; real support ownership halves only the added translational kick.

Retained exact-executable evidence is
`sway-constructor/function-006d60e0.local.txt`,
`sway-constructor/function-006e5bc0.local.txt`,
`recoil-readers/function-006ec560.local.txt`, and
`recoil-strength-20261011/function-006d0d80.local.txt` under the existing ignored
`tmp-recoil-contract-20261011` directory. The selector packet was obtained from
the existing read-only Ghidra project copy after REA could not complete the
targeted request. This is static evidence, not a new runtime observation.
