> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Weapon capability audit — checkpoint 203

Audit date: October 7, 2026 (America/Chicago). Source baseline:
`98c7cf8b0d50be749a2c9297047614466c9f5149`.

This began as an independent offline audit of configuration coverage and its
reporting pipeline. The four reporting fixes below are now implemented. Gameplay
code, registrations, calibration, operation receipts and the native test harness
are unchanged. No game or headset was started.

## Implemented corrections

- The compiled probe now exercises reviewed operation records through synthetic
  snapshots, the shared descriptor resolver and the normally enabled holster consumer.
  It reports builtin, legacy-descriptor and common-operation routes separately,
  with matching operation-record indices and source/receipt hashes. Empty and
  incomplete bindings, missing path verification and wrong paths are negative
  controls; failure returns a nonzero exit code. The probe is also a CTest test.
- Exact-package stow admission requires a matching, explicitly verified synthetic
  configuration path. Legacy, empty, mismatched and unverified paths cannot report
  admission. Synthetic success remains distinct from a native capture.
- Reports retain component exclusions, per-package role evidence, source-input
  provenance and matching visibility/operation diagnostics. Support-only evidence
  does not become complete grip readiness, and native-admission claims in component
  role evidence are rejected.
- Cache summaries include the operation header, source receipt and source digest,
  with explicit disabled, unknown, incomplete or present-but-unverified status.
  `--probe-binary` optionally records the supplied executable's hash; it does not
  claim that executable produced an independently supplied receipt or attest its
  build inputs. The existing CMake source-closure verifier remains authoritative.

Focused regressions pass (123 tests). All five new regression methods reject the
pre-fix Python report. Full Python discovery passes 804 tests with one intentional
installed-game skip (805 total). The unchanged checkpoint203 native operation
source receipt verifies. Current full-build/probe results are recorded below.

## Original findings and rationale

### 1. The probe cannot exercise the common operation admission route

`src/games/bc2/Bc2WeaponCapabilityProbe.cpp` constructs a synthetic
`SelectedMeshesSnapshot`, populates its configuration path and meshes, then calls
`Bc2BodyHolster::AcceptsProfile`. It never populates `operationBinding`.
That field defaults to zero in `Bc2SelectedMeshes1p.h`.

`VisibilityProductionAdmission` in `Bc2VisibilityProfiles.h` requires
`NativeOperationAdmitted` for the common reviewed-operation route; the latter
rejects an incomplete binding in `Bc2NativeOperationCapability.h`. Consequently,
merely compiling the checkpoint operation header cannot make this probe exercise
that route. The separate SPAS/scoped-XM8 acceptance mask in `Bc2BodyHolster.cpp`
can still accept those builtin profiles. Do not interpret the other negative probe
rows as proof that the checkpoint runtime lacks the common operation capability.

Suggested repair: report compiled operation records separately, then exercise
matching records through explicitly synthetic snapshots with positive and negative
binding controls. Preserve the distinction between static eligibility and current
native ownership. Do not mark a synthesized binding as native capture evidence.
The initial finding was established by source tracing. Repair validation now
checks the modified checkpoint probe; the operation source receipt is unchanged.

### 2. A legacy receipt can report stow admission without a configuration path

`tools/bc2_weapon_capability_report.py:report` sets `production_stow` from a
matching asset/rig/mesh row before checking configuration-path evidence. The
path check only runs if the receipt contains `configuration_path`.

Reproduced using the existing synthetic receipt fixture, with only
`production_stow_admitted=True`: the report returns `adapter_profile_admitted`
alongside `configuration_path_binding.status=unknown`. This does not demonstrate
a live runtime bypass; the current C++ probe emits the path field. It does mean
old/incomplete receipts can overstate what the final exact-package report knows.

Suggested repair: require an explicit matching configuration path and the
appropriate synthetic proof before emitting exact-configuration stow admission.
Keep legacy evidence diagnostic-only, and name builtin versus common-operation
admission routes separately. Cover missing, empty, mismatched and unverified paths.

### 3. Component rejection reasons disappear from the final report

`bc2_weapon_component_closure.py:build` emits `component_excluded` with reasons
such as `missing exact grip digest backlink`. The final capability report copies
only the package index's `excluded`, not `component_excluded` or per-role
`component_evidence`. A support-only anchor also lives in `component_evidence`
rather than the aggregate complete-grip component, so that useful distinction is
lost in the final report.

Reproduced with the existing component fixture and magazine metadata without its
grip document: closure emits the missing-backlink rejection, while the final
report has `excluded=[]` and no `component_excluded` field. Missing component
readiness remains false, so this is diagnostic loss, not native admission.

Suggested repair: retain both exclusion collections and per-role evidence, scoped
to their package/source identities. Preserve the difference between an absent
component, a rejected component, and support-only data.

### 4. The cache summary omits inputs that control common stow admission

The report's `HEADER_OPTIONS` covers magazine registry/geometry, grip, support and
draw headers. It omits `BC2_NATIVE_OPERATION_CAPABILITIES_HEADER`,
`BC2_NATIVE_OPERATION_SOURCE_RECEIPT` and `BC2_NATIVE_OPERATION_SOURCE_SHA256`,
which `cmake/Bc2NativeOperationCapability.cmake` requires together.

The complete CMake cache is hashed, so changes affect its digest, but the report
does not expose or hash the operation header/receipt individually. Its existing
`production_binary_match=unknown` qualification is correct and should remain.

Suggested repair: include these proof inputs and their validation status, and pin
the probe executable hash with its build composition. A current file hash alone
must not be presented as proof of which inputs an older executable used.

## Configuration inventory

The checked-in visibility descriptor table contains 46 rows: 35 with an exact
configuration backlink and 11 without one. The 35 are variants, not 35 distinct
guns or 35 accepted reload implementations. The following is a source inventory
of `Bc2VisibilityDescriptorData.h`; it is not regenerated asset evidence.

All paths below begin `Objects/Weapons/Handheld/`. Each stem is an exact suffix
under its listed directory, without the package index's `.dbx` extension.

| Directory | Configuration stems | Count |
| --- | --- | ---: |
| `BU_rif_AN94Abakan` | `BU_rif_AN94Abakan`, `BU_rif_AN94Abakan_Kobra`, `BU_rif_AN94Abakan_Scoped`, `SP_rif_AN94_Kobra`, `SP_rif_AN94_Scoped` | 5 |
| `RU_rgl_AEK971` | `RU_rgl_AEK971`, `RU_rgl_AEK971_Kobra`, `RU_rgl_AEK971_Scoped`, `SP_rgl_AEK971`, `SP_rgl_AEK971_Kobra` | 5 |
| `US_rgl_XM8` | `US_rgl_XM8`, `US_rgl_XM8_Kobra`, `US_rgl_XM8_Scoped`, `SP_rgl_XM8`, `SP_rgl_XM8_Scoped` | 5 |
| `BU_rif_F2000` | `BU_rif_F2000`, `BU_rif_F2000_Kobra`, `BU_rif_F2000_Scoped`, `SP_rif_F2000`, `SP_rif_F2000_Kobra` | 5 |
| `RU_rif_AKS74u` | `RU_rif_AKS74u`, `RU_rif_AKS74u_Kobra`, `RU_rif_AKS74U_Scoped`, `SP_rif_AKS74u`, `SP_rif_AKS74u_Kobra` | 5 |
| `UL_rif_FNSCARL` | `UL_rif_FNSCARL`, `UL_rif_FNSCARL_Kobra`, `UL_rif_FNSCARL_Scoped`, `SP_rif_FNSCARL`, `SP_rif_FNSCARL_Scoped` | 5 |
| `RU_hg_MP443Grach` | `RU_hg_MP443Grach`, `SP_hg_MP443Grach` | 2 |
| `BU_smg_IMIUZI` | `BU_smg_IMIUZI`, `BU_smg_IMIUZI_Kobra`, `BU_smg_IMIUZI_Scoped` | 3 |

The 11 pathless descriptor labels are `XM8_sp_s` (the bare-mesh row),
`SPAS12_sp`, `XM8C`, `M416`, `Mk14EBR`, `M16`, `M16k`, `PP2000`, `UMP`, `9A91`
and `UMPk`. Do not invent configuration backlinks from these display labels.
SPAS has its separate tube/builtin runtime path; absence from this exact-package
inventory is not a claim that shell reload or builtin stow is unavailable.

`Bc2MagazineNativeRegistry.cpp` supplies two builtin registrations:
`US_rgl_XM8/SP_rgl_XM8_Scoped` and `RU_rgl_AEK971/SP_rgl_AEK971` (under the same
path prefix). The checkpoint script leaves the optional reviewed registry empty.
Thus 33 of the 35 path-bound visibility configurations have no exact registration
in that magazine registry composition. Visibility admission cannot fill this gap.

Scoped XM8 has builtin magazine geometry. The published checkpoint geometry
header supplies one experimental AEK row; default builds do not select that
header. Its carry/rail values are explicitly design estimates. Neither row grants
native acceptance to same-name attachment variants or other configurations.

The private visibility/resource evidence and authored JSON documents needed to
rebuild immutable package/component closure are absent from this checkout. The
historical 20 grip / 10 magazine-contact counts in `WEAPON-SYSTEM-PIPELINE.md`
are therefore not newly reproduced by this audit. Checked-in C++ calibration
headers cannot substitute for those immutable resource receipts.

## Remaining capability boundaries

| Capability | Audit conclusion |
| --- | --- |
| Exact magazine geometry/native join | Available only where both registries actually join; generated visibility does not enroll reload |
| Stow | Builtin acceptance and common-operation acceptance are different routes; current probe does not exercise the latter |
| Body/ammo display | The report explicitly leaves package-to-local-cache binding unknown; magazine contacts are not an ammo mesh cache |
| Manual pump/bolt/handle/slide | Portable policy/presentation groundwork does not establish a production action consumer |
| Chamber/fire readiness | Native chamber knowledge remains unadmitted; loaded count is not a separate chamber receipt |
| Native cross-profile recovery | Checkpoint 203 CPU evidence exists; combined actual-game transition acceptance remains open |
| Headset feel | No new acceptance in this audit |

## Reproduce the original findings / check the corrections

Run from the repository root. These use synthetic test data only and do not
modify fixtures or contact a game:

```powershell
@'
import sys
sys.path[:0] = ['tests', 'tools']
import test_bc2_weapon_capability_report as t
import test_bc2_weapon_component_closure as c
r = t.receipt()
r['configured_visibility'][0]['production_stow_admitted'] = True
row = t.m.report(t.index(), r)['rows'][0]
print(row['production_stow'], row['configuration_path_binding'])
i, g, m, v = c.fixture()
closure = c.c.build(i, [m], v)
out = t.m.report(closure)
print(closure['component_excluded'], out['excluded'])
print('component_excluded' in out)
print('BC2_NATIVE_OPERATION_CAPABILITIES_HEADER' in t.m.HEADER_OPTIONS)
'@ | python -B -
```

Before correction: admitted stow with unknown path binding; one missing-grip-backlink
exclusion disappears from the final report; both final booleans are false.
After correction: stow is unknown, `component_excluded` preserves the rejection
separately from package-level `excluded`, and both final booleans are true.

## Original audit validation (before repairs)

`python -B -m unittest discover -s tests -p test_bc2_weapon_*.py` passed all
118 tests. The reporting reproductions above also completed successfully.

Both `Build.ps1 -Architecture x86 -Jobs 2` and the corresponding x64 invocation
configured successfully with optional headers empty, but D: exhausted its free
space during compilation/linking. The x86 log records link exit 1201; x64 records
C2471 (cannot update program database). The remaining x86 process was interrupted
after the failure. Neither complete build nor its CTest phase passed. Logs remain
in `reports/x86-build.log` and `reports/x64-build.log`. Only `.obj`, `.pdb` and
`.ilk` files created since each new cache's creation were removed from these two
build directories, recovering approximately 1.1 GB. Existing binaries, source and
logs were preserved. Full validation requires sufficient free disk space and reruns.

The x86 capability probe linked before the failure and ran successfully afterward.
Its SHA-256 is
`2bf3cbb96e42e0f453d45cb63c39382bbef89da2dbe724bf5fa13b97d3dae958`.
Its local output is `reports/capability-audit-203-x86-probe.json`:

| Default x86 probe observation | Result |
| --- | --- |
| Visibility rows / nonempty paths | 46 / 35 |
| Native magazine registrations | 2, both enabled/resolved/reviewed |
| Scoped XM8 magazine prerequisites | Present |
| AEK magazine prerequisites | Missing geometry (`magazine_ready=false`) |
| Synthetic stow accepted | Scoped XM8 and SPAS builtin routes only |

This is an independently run probe from a partially completed default build,
not a full-build pass, checkpoint composition or production-DLL attestation.
The source-level limitation in finding 1 still applies when auditing checkpoint
operation admission. No checkpoint-header build or live test is claimed.
This historical result is superseded by the repair validation below. No gameplay
fix or broader native/headset acceptance is implied by the tooling corrections.

## Repair validation

All verification completed on D: after the user freed disk space:

| Check | Result |
| --- | --- |
| Focused weapon-pipeline Python regressions | 123 passed |
| Full Python discovery | 805 total: 804 passed, one intentional installed-game skip |
| New Python regressions against pre-fix report | All five methods reject the old implementation |
| `Build.ps1 -Architecture x86 -Jobs 2`, default configuration | Build and all 186 CTest suites passed |
| `Build-Checkpoint.ps1 -Architecture x64 -Jobs 2`, followed by `Build.ps1` with its retained configuration after probe correction | Build and all 186 CTest suites passed |
| Default x86 probe | 46 visibility rows, 0 common-operation rows, 2 builtin rows |
| Checkpoint x64 probe | 46 visibility rows, 35 common-operation rows, 2 builtin rows; 36 distinct stow-admitted rows because scoped XM8 belongs to both routes |
| Checkpoint negative controls | Empty/incomplete operation bindings, missing path verification and wrong paths rejected for the descriptor route |
| Compiled receipts through report with a synthetic exact AEK test package | Default stow unavailable; checkpoint static admission present; native capture and runtime enablement remain false |
| Existing checkpoint203 source receipt | Verified unchanged |

The first repaired checkpoint probe correctly failed its positive control: using
a zero builtin mask disabled the whole holster consumer. The final probe isolates
the actual descriptor resolver and checks its result against the normally enabled
consumer. The failing log is retained in
`reports/capability-repair-x64-first-tests.log`; final x86/x64 build/test logs
contain the successful reruns.

Machine-readable local results are in
`reports/capability-repair-x86-probe.json`,
`reports/capability-repair-x64-probe.json` and
`reports/capability-repair-probe-comparison.json` (probe-output and executable hashes,
cache digests and operation-proof inputs). Full Python output is in
`reports/capability-repair-python.log`.

The earlier disk-exhaustion failures remain historical. No current validation gap
remains for these four tooling fixes. Regenerating full private-data package closure
and actual game/headset acceptance remain outside this offline verification.
