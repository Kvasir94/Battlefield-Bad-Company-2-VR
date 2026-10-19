> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 hands checkpoint — 2026-10-11

Follow-up: the user approved the existing native skinned-instance route.
[Static seam research](BC2_NATIVE_SKIN_SEAM_20261011.md) traces the native draw
chain and documents queued-pointer lifetime hazards. Hands implementation
awaits remaining ownership/resource/pass proof. The checkpoint below retains
its original source and runtime status; no new hand rendering is enabled.

Paused for agent reorganization. This is tested source groundwork and completed
observation, not enabled independent hands or accepted native-title fidelity.
Checkpoint parent is detached HEAD 93a071023be1a1beef9650574c2211cde60d0263.
No game assets are included. Do not blindly cherry-pick older proper-hands work:
0d0d44d was already integrated as 64f83dd.

## Source retained

- Shared immutable final hand palette, independent of per-eye animator binding.
  Publication copies actual final Pack input after fallback/rebase, validates
  source identity/equipment and preserves its original lifetime. Held fallback
  cannot mint a new pose receipt. Optional publication remains disabled.
- First valid render eye freezes one snapshot for its stereo pair. Both eye
  cameras use it; source owner/equipment/space/tracking/layout changes and expired
  snapshots reject. This never relabels N-1 animation as current native-frame
  producer proof. WantPacked, AdmitEye and replacement admission are unchanged.
- Existing independent backend supports the full authored diffuse mip chain,
  validates dimensions and hashes, uses anisotropic filtering, and writes
  verified scene depth for self-occlusion. Read-only DSVs reject. No loader or
  production hand-render adapter is wired. Its simplified diffuse shader still
  lacks native normal/specular/lighting fidelity.
- Explicit FVR_NATIVE_HAND_MATERIAL_CAPTURE=1, together with the existing
  bounded hand-capture opt-in, observes actual native eye draw scopes without
  palette-producer authority. It reuses existing indexed draw staging, shader
  registrars and resource copies. Producer fields are cleared; output explicitly
  reports observation-only, selected draw owner unverified, replacement inactive.
  Default exact producer capture behavior is retained.

## Validation and runtime evidence

Private task root:
%USERPROFILE%\Documents\Codex\2026-10-11\task-5

- material-capture-gate-20261011-183918/status.json: native x86 DLL compiled and
  linked; NativeHandMesh and NativeHandCapture CTests passed 2/2. Capture fixture
  has nine cases, including observational eye keys that grant no ExactOwner.
  Two compiler jobs, actual shared mutex, all tracked source hashes checked
  before/after. DLL SHA256
  D3A51F12642C20DBE6456F01948EE5FF699FD8FE0B844496C53D2E6E26E93E43.
  Dirty snapshot patch SHA256
  B5990280CE3B6E4B80165DC5D8EB1A8ACB4CB24EFF011BD670D7346449821175.
  Gate snapshot excludes later audio edits. Source note changes need no build.
- material-neutral-hands-20261011-184239: preparation failed at fresh-process
  identity check, before native capture attachment; settings/selected files
  restored. No cause inferred from missing identity values.
- material-neutral-hands-20261011-184336/status.json: retry passed unchanged
  identity check with exact path/start-time receipt. Native SDK observers ready
  before level load. Final capture written, hooks disabled, settings restored,
  all six protected hashes unchanged. Driver released game mutex at
  2026-10-11T18:45:24.0707463Z. Neutral synthetic controller producer; no human
  headset acceptance.
- Final material capture: 838 native eye scopes, three catalog-count candidates;
  general sampler captured 67 frames and one count1020/stride4 candidate rejected
  by its existing layout check. Zero staged glove/arm draws, zero pack records,
  zero capture errors. This does NOT establish absence of visible hands or a
  broken left controller. Standard-arm catalog does not identify the rendered
  hand geometry in this sample. Do not repeat producer-lease experiments.
- material-capture-analysis.private.json pins exact report/capture paths.
  Main capture is material-capture-package/reports/
  native-trace-20261011-184348-013/native-hands-contract.

Prior worker diagnostic 93a0710 remains valid: retained rejected eye0 workers had
actual paired final packs and fresh copied leases whose frames were older by
1..33; no exact current-frame eye0 producer. Shared animation pose removes that
unnecessary renderer dependency, not native draw ownership requirements.

## Architecture / RE

Prefer preserving Frostbite's actual skinned mesh material/pass machinery if a
verified substitution seam exists. Existing evidence does not establish a safe
native skeletal-component constructor or a selected-player draw substitution
API. Keep useful shared-pose code; do not expand custom lighting while native
substitution feasibility is unresolved.

Existing engine RE in WEAPON-LOADING-AND-DEBUG-RECOVERY.md identifies
Dx11ShaderDatabase factory 0x99fc30, resource add 0x98c5d0, shader system
[0x1572b18], registration 0x9cb590, merge 0x9c3f60 and lookups
0x9b02b0/0x9b0340/0x9b03d0. Reuse these known boundaries.

Offline resource work is saved in task-5/material-resource-analysis/CLOSEOUT.md
and resource-structure.private.json. Name serialization is NAME + NUL + KEY16
(not preceding key). Standard glove material key
56995c7db880db0efaa133678ac02d40 has tail indices
3293,5004,5763,8515,8610,9798 into an 88-byte table at22549083.
Record +32 keys join the 89-byte layout table at23484263.
Glove Standard layout key e7737d407ffaf6b5fbb125a5f76220be; StreamInstancingFloat
95fb39460c2a9265451c5bddbe3596e1. All5142 DXBC containers pass bounds checks.
Permutation fields, association to 48-byte shader records, resource binding
references and layout enums remain unresolved. These links do not prove native
texture binding or UV/normal semantics.

REA 6.3.0 is installed and Codex registration aligned; current callable session
does not expose REA tools. Local doctor without registration environment reports
GHIDRA_INSTALL_DIR unset; Codex config DOES supply existing Ghidra/JDK paths.
Do not reinstall or rewrite config based on that shell-only finding. Reconnect
REA, or use repository-authorized headless fallback on a read-only copy of
D:\bc2-re\proj. No native Ghidra analysis was started in this lane before pause.

## Remaining work and dependencies

- Architecture/RE: resolve native mesh/material/permutation and submitted skin
  resource path; compare geometry substitution in original draws against a new
  native component. Use exact existing shader lookup/parser seeds above.
  One targeted memory observation may corroborate actual selected skin resource,
  section count/layout and material, after scheduling runtime with parent.
- Source implementation: wire only the chosen verified route. Preserve current
  contacts/finger poses and one shared pose per pair. Replace original arm,
  MOLLE/glove color and combined depth geometry as one presentation decision,
  with same-frame fallback. Current BodyAmmo post-eye boundary does not prove
  native query lifetime or depth/projection association.
- Assets/import: original capped147-bone mesh (866vertices/1728triangles per
  hand) remains private in floating-hands-intake. Candidate UV/normal/material
  semantics remain unverified. task-5/hand-diffuse-mips.private preserves all10
  authored diffuse mips with digests; decoding does not establish material use.
  No game bytes may be committed.
- Tests/runtime: source checks above pass. No visible independent replacement,
  latency, native material parity, bilateral depth, firing/reload/equip,
  tracking-loss or headset acceptance. Do not label this checkpoint playable
  hand replacement.
- Integration/package: selected build remains
  <repo>\build\playtest-20261011-055537-4f94cac,
  commit4f94cac0ff2e3e2bc5c966e2046bf3a338fec55e.
  Unselected prior diagnostic package173226-93a0710 is preserved.
  task-5/material-capture-package is an unselected, verified24-file dirty-source
  observation package, pinned by the gate hashes above. No permanent install,
  selection or push occurred.

## Ownership / stopped state

Hands owns the ten source/test paths in the checkpoint. NativeProbe changes are
only the approved material-observation boundary/config hunks. Audio owns dirty
Bc2Gameplay.cpp and Bc2ReloadAudioCue.h; preserve them unstaged. Root owns broader
NativeProbe/general capture, host integration and asset readers. The resource
analysis child stopped with no source edits or leases.

Hands has no running game, host, trace, IPC helper or build. Own compiler mutex
released18:39:52.8697943Z; own runtime mutex released18:45:24.0707463Z.
At closeout game mutex was available; shared compiler mutex was occupied by
another lane. Do not claim the other lane is stopped or release its mutex.
