> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Native skinned replacement implementation — 2026-10-11

The native route now has an implementation: allocation-scoped local-player ownership, verified character submission hooks, persistent private native geometry wrappers, asynchronous source-buffer SHA verification, and paired color/depth glove replacements. Native material bindings and the 147-bone authored palette remain in use. Original arm/MOLLE sections are filtered from the submitted private LOD view. Wrist caps use the proved native normal/tangent convention.

REA fresh-import startup stalled before MCP service availability. Evidence was preserved and static queries ran against a read-only copy of the existing Ghidra project. The original database was not modified. See BC2_NATIVE_SKIN_GEOMETRY_20261011.md and BC2_NATIVE_SKIN_CAP_LAYOUT_20261011.md for ABI and resource evidence.

The incremental x86 DLL build passed. Bc2NativeSkinRequestRegistry and Bc2NativeSkinGeometry both passed; the latter checks 20 malformed cases and paired effective rig weights. These checks do not prove live replacement or headset appearance.

The private Standard asset is 172544 bytes, SHA256 1d37fb6e14d65f4abc92caf19559e43abf2875fcef6b3165921e097894cc19f3. It is packaged beside the DLL as native-skin-hands.fvrskin, never committed. Presence enables the route; FVR_BC2_NATIVE_SKIN=0 disables it. FVR_BC2_NATIVE_SKIN=observe records the seam without replacement.

Runtime counters must show owned requests, source matches and replacements before claiming live success. Both-eye appearance, lighting, finger poses, wrist caps and absence of stretched original arms require the resulting frame evidence. Selected package and installed binaries remain unchanged.


## Live seam evidence

The first asset-enabled neutral fixture reached 7,250 owned allocations and
14,498 validated submissions. Its zero replacements correctly identified a
Standard-versus-Assault source mismatch. The selected scene uses Assault LOD0:
glove section0, arm1, MOLLE2, depth3. The writer now supports either layout
without relaxing exact source hashes or rig remaps.

The next observation identified native device creation flag1 (single threaded).
After restricting GPU work to the known DrawHook thread, source c7f1922 produced
8 exact source-buffer matches, 0 mismatches and 696 replacements. Another4,286
submissions occurred on worker threads and fell back, so that checkpoint does
not prove complete visual replacement. Worker reuse of immutable verified
geometry is the remaining implementation step at this checkpoint.

All observations used the existing fixed-pose neutral campaign fixture, no
headset or physical controller producer. Each run restored settings and verified
all six protected selected/installed paths unchanged. Full private reports are
under reports/native-skin-build-20261011 and the corresponding unselected
build/native-skin-* package directories. The compiled DLL and two focused
checks pass after the graphics-thread change.


## Completed native replacement proof

Compiled source `a005db10ce6ebc4e997b9a4f600252a144a36c67` completed the fixed-pose
Assault scene at20:08 UTC. There were2,398 owned allocations,4,796 validated
submissions and4,752 replacements. All8 GPU source ranges matched their SHA256;
there were0 source mismatches and0 rejected ownership associations. The44 warmup
fallbacks were38 graphics-thread cache misses,4 pending source checks and2 pending
frame-boundary activations. Worker cache reuse performs no COM calls.

Saved stereo pair48 shows both capped gloves and original arm removal in both
eyes. This establishes the replacement seam, not subjective appearance approval:
the new glove surfaces/caps appear dark in this scene and still need a player
appearance check. Native depth geometry is paired with exactly matching indexed
positions and effective rig weights; native materials, passes and palette remain
in use. No custom lighting renderer was introduced.

The probe exited0, the game stayed responsive through19,781ms of post-probe
observation, and no new crash report appeared. Driver cleanup restored settings,
verified six selected/installed files unchanged, and released the game lease at
20:08:25 UTC. No headset, install, selection or push occurred.

Usable unselected candidate: `<repo>/build/native-skin-a005db1-assault`.
All25 payload hashes verify. DLL SHA256:
`45a3c1fd82ee6c8661eed801390850af06bc87c7ae1d38d028c049169404d6f6`.
Private Assault asset SHA256:
`91595707d902096fb2ac558bb13e6a77e0292fcb0037096a6d8cb9847a4414b4`.
The asset enables replacement by default; `FVR_BC2_NATIVE_SKIN=0` is the kill
switch. This package targets the proved Assault mesh. The writer also supports
Standard; other character mesh variants remain native fallback until supplied
with matching verified assets.

Evidence index: `reports/native-skin-build-20261011/replacement-proof.private.json`;
raw runtime report under candidate `reports/native-trace-20261011-200726-567`;
paired images `replacement-left.png` and `replacement-right.png` in the proof
folder. REA fresh-import startup remains a tooling defect; the permitted read-only
saved-project fallback supplied the static ABI proof and runtime supplied the
actual replacement proof.

Player sequence after separately authorized candidate launch: look at both wrist
caps and finger poses; move each wrist and switch weapons; check native lighting
in bright and shaded areas; check depth against the weapon and nearby surfaces.
