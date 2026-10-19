> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Five additional magazine weapon mechanisms

Source integration, 2026-10-11. The five distinct additions are M416, MG36,
VSS, AKS74u and UZI. XM8C and XM8 LMG remain additional registrations through
the same magazine, physical ammunition and charging handle paths.
The table describes the extracted geometry and VR gesture;
it does not claim an in-game or headset test.

| Exact native asset | Exact configured mesh basename | Handle/contact bone | Parts moved during a VR rack | Stroke and origin |
| --- | --- | --- | --- | --- |
| `M416` | `UL_rif_HK416_Mesh` | `jntWpn_5` | `jntWpn_5` | 80 mm, explicit VR stroke on the native closed handle geometry |
| `XM8C` | `US_rif_XM8c_Mesh` | `jntWpn_17` | `jntWpnwpnJnt_16`, `jntWpn_17` | Approximately 86.2 mm, sampled native `1P_FireStart` translation |
| `MG36` | `BU_lmg_MG36_Mesh` | `jntWpn_13` | `jntWpn_12`, `jntWpn_13` | 100 mm, explicit VR stroke on the native closed handle and its carrier |
| `XM8 LMG` | `US_rif_XM8lmg_Mesh` | `jntWpn_17` | `jntWpnwpnJnt_16`, `jntWpn_17` | Approximately 86.2 mm, sampled native `1P_FireStart` translation |
| `VSS` | `BU_sni_VSSVintores_Mesh` | `jntWpn_4` | `jntWpn_4` | Approximately 66.9 mm, sampled native `1P_Reload` translation |
| `AKS74u` | `RU_rif_AKS74u_Mesh` | `jntWpn_5` | `jntWpn_5` | 80 mm, explicit VR stroke on the native closed charging lever/carrier |
| `UZI` | `BU_smg_IMIUZI_Mesh` | `jntWpn_9` | `jntWpn_9` | 45 mm, explicit VR stroke on the native closed top charging knob |

All closed transforms come from the exact weapon's authored `HandsIkPose`
reference and its explicitly paired skeleton. The contact point is the centre
of the selected part's measured bounds, transformed into weapon-local metres.
The three measured strokes are effectively linear along canonical weapon-local
`+Z` (towards the butt); VSS has less than 1 mm lateral displacement. Their
linear presentation preserves those sampled travel lengths. M416 and MG36's
80/100 mm strokes are VR design values, not native animation measurements.
AKS74u's 80 mm pull is also a VR design value. UZI's separate `jntWpn_4`
bolt has a measured 45 mm linear `1P_FireStart` displacement; that length
informs the VR knob pull but does not establish an authored `jntWpn_9` track
or a native linkage between those parts. Only the held knob moves in this
presentation. The sampled reload/fire clips did not establish charging-part
motion for AKS74u or the UZI knob.

Blender MCP was used to inspect M416 and MG36's transformed native part points
with the proposed handle highlighted. This identified M416's separate top rear
charging handle (`jntWpn_5`) and MG36's top handle (`jntWpn_13`) under its
`jntWpn_12` carrier. Each inspection used a new scene; the original scene was
preserved. The local images and source hashes remain with the extraction data.
No native animation of those two handles was established by this inspection.
Additional isolated Blender scenes inspected AKS74u's measured charging
lever/carrier and UZI's top knob in their complete native closed part sets.
The existing scenes and original active scene were preserved.

Native hand clips were also checked for a usable hand-on-handle reference.
XM8C and VSS's decoded reload clips did not provide a stable paired
wrist/handle/finger contact window under the existing mechanism sampler's
criteria. XM8 LMG's reload clip omitted the required `jntWpn_17` track. Native
part motion can therefore supply the three strokes without pretending that it
also supplies a held VR hand pose. The loading hand retains its independent
tracked wrist and existing hand presentation. A future credible native contact
sample can extend that presentation without changing rack authority.
The same paired sampler found no credible wrist/handle/finger window in
AKS74u or UZI's decoded reload clips. Those two use the tracked hand as well.

## Exact registration and runtime consumption

`Bc2MagazineMechanismProfile.h` is a generic catalog: exact native asset,
configured mesh path and rig fingerprint select one handle profile and its named
closed parts. Duplicate keys/handle IDs, malformed transforms, missing parts,
ambiguous names and parts outside the weapon subtree reject the operation.
The generated header is supplied through `BC2_MAGAZINE_MECHANISM_HEADER`.
`tools/bc2_magazine_mechanism_header.py` joins the existing extraction metadata
with explicit rack contact/stroke data and records its digest. It emits derived
metadata only; game files and complete mesh/animation payloads are not committed.

Each asset has multiple exact native weapon configuration paths. Reload
selection must use the currently observed `ReloadObservedConfig`, then carry
that admitted `MagazineFamilyEvidence.binding.profile` into interaction and
presentation. Name-only lookup deliberately rejects these ambiguous assets.
The new automatic magazine descriptors use the existing reviewed
`Reload11 -> Transfer12 -> Input1` family with their original authored values.
`ReviewedPhysicalAuthority` remains the separate semiautomatic admission used
by the preserved M9 registration; it is not substituted for AutomaticFire2.

AKS74u and UZI retain their authored nonzero bolt values (0.5 s delay, 1 s
time, hold until Fire release). Their distinct review family is
`bc2.automatic-magazine-stock-bolt-bypass.v1`: ordinary AutomaticFire2 enters
and repeats state 9, skips the type-1-only hold/bolt path through states 7/8,
and keeps the ordinary magazine reload path through 10/11/12 to 1.
The retained proof's complete Update/Step/Timed/Order/Transfer fingerprints
match the current native bindings. Generation requires the exact retained
proof digest, current target executable SHA-256 and all five fingerprints;
a zero-bolt review cannot enable these rows. No fields are zeroed and no new
native function, signature or hook is introduced.

Descriptors must resolve archive overrides before generation. The mounted
`mp2_common` VSS firing patch changes the base configuration's authored
magazine count from 3 to 4, while the weapon DBX remains in `mp_common`.
Resolving either archive alone misses this partial override. Exact matching
rejects the obsolete descriptor; the generated mounted descriptor carries
the merged source chain and its new digest. A bounded cached rejection in
the existing reload-flow report records the observed full configuration and
exact timing bits without new native reads or relaxed matching.

The existing rack recognizer, persistent physical ammunition ledger and
`CycleAction` completion handle all seven profiles. The private palette places
each part from its absolute closed pose plus the current rack travel, avoiding
a second offset on an already animated part. Native source animation buffers
are untouched. Return smoothing resets on weapon/equipment replacement, and
existing owner, configuration, input and palette lifetime checks continue to
apply. The existing XM8, F2000 and pistol mechanism paths retain their registrations.

The UZI's open-bolt ledger remains ready after an ordinary empty-magazine
reload when already cocked. Its handle also permits an optional deliberate
pull: the existing open-bolt `CycleAction` only clears the cocking requirement
and changes no ammunition. Fire is suppressed while the handle is held.
Closed-bolt handle eligibility continues to require an owed rack. A rack takes
an explicit loading-hand mechanism claim; an existing support claim transfers
directly using the original raw contact evidence. Releasing support first
would invalidate that older evidence. Busy hands, lost contact or changed
readiness cancel the grab rather than allowing an old stroke to resume.

`Bc2MagazineMeasuredBatch` is the focused offline consumer target. It exercises
the compiled exact descriptors through partial/empty reload fixtures, config
and stale-family rejection, rack completion, duplicate/time-gap rejection and
named-part presentation. Its native API is mocked; passing it does not establish
native or headset acceptance.
