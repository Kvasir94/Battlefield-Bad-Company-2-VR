> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Physical reload expansion for the next BC2 playtests

The owner's October 10 priority is a fun, native-feeling VR game. Physical hands
should drive reload progress; original game timers are implementation details,
not a reason to make a seated magazine wait. Preserve authored item articulation
and the floating-hand presentation wherever they improve the result. This is a
source-based implementation plan, not a claim that the full weapon roster works.

## What the current selected build actually contains

The selected combined candidate is `f289b41`. Its x86 cache includes
`profiles/checkpoint204/Bc2ExperimentalMagazineGeometry.h`, containing AEK971 and
F2000 geometry. The accepted XM8 geometry is compiled separately. The native
registry has built-in scoped XM8, AEK971 and F2000 descriptors. The optional
reviewed generated registry is empty in this cache: cataloguing other weapons
does not currently give them physical magazine support.

`Bc2MagazinePhysicalReload` already shares removal, retained-original magazines,
replacement supply, guided insertion, native completion and charging behavior.
`Bc2MagazineEquipmentProfile` joins exact native configuration and geometry.
Its `MagazineSupportFor` result distinguishes missing native profile, geometry
and readiness. Reuse that reporting in diagnostics so an ordinary fallback is
visible instead of looking like a failed gesture.

The action catalog contains 25 mesh rows, but its action classifications are
design defaults, not native reload admission. Handle contacts are currently
profiled for XM8 and F2000. AEK lacks an existing mapped handle motion in the
profile source. Do not copy an XM8 handle transform onto it.

## Implementation order

The owner subsequently selected a **simple pistol after the next combined BC2
test**. Start with the exact M9 candidate, subject to finding its actual local
asset/configuration. This takes priority over adding the next rifle batch. The
test first checks the combined movement and existing interaction repairs; its
findings feed the pistol implementation. It is not a new general headset gate
for ordinary development. If the selected campaign fixture cannot supply the
M9, choose the available simple pistol explicitly rather than relabelling it.

| Batch | Work | Practical result |
| --- | --- | --- |
| Existing magazine guns | Exercise XM8, AEK971 and F2000 through partial and empty replacement, retained-original reinsertion, switch/cancel and reserve exhaustion | Make the current three-gun workflow reliable before multiplying profiles |
| Simple pistol, after the combined test | Start with the exact M9 asset: physical magazine removal/insertion and an actual slide or slide-release contact | A complete compact workflow with floating hands and authored moving item parts |
| Next detachable rifles | Compare exact authored configurations for AUG and M416 first; use whichever matches the existing native family without unresolved exceptions | Add data and contact geometry to the shared magazine consumer rather than another reload state machine |
| Other magazine guns | Expand matched rifles, magazine-fed shotguns and magazine-fed LMGs in batches; separate fire-mode or open-bolt differences where present | More roster coverage with reusable insertion and supply interactions |
| Further pistols | Reuse the first pistol's interaction where the actual slide/slide-release and chamber behavior match | Expand matching pistols through geometry/action profiles |
| Tube shotguns and bolt actions | Extend SPAS insertion/pump machinery to matching tube assets; build a distinct bolt-cycle workflow for a representative bolt action | Support shell-by-shell and post-shot cycling without pretending they are box magazines |
| Belt-fed guns | Add cover/container/feed constraints to the reusable mechanism layer; start with one exact asset | Cover, ammunition supply, feed and charge become explicit interactions |

The AUG/M416 order is a useful investigation shortlist, not proof that either
already matches the automatic-fire descriptor. Choose by the actual authored
configuration. A burst or single-shot dispatch difference needs a real adapter
decision. Existing launcher work stays deferred until the basic roster is sturdy;
this plan does not silently enable underbarrel physical reloads.

## Smallest path to each additional magazine gun

1. Locate and reuse existing local authored configuration, skeleton, skin and
   animation references. Preserve their exact resource/hash joins. Regenerate
   only missing derived metadata; do not extract whole archives again.
2. Compare the exact firing/reload configuration to the existing reviewed family.
   When that family applies, add an immutable registry row through the existing
   descriptor pipeline. When it differs, record the specific dispatch or ammo
   issue and resolve that seam rather than adding a name-based allowlist.
3. Generate its actual magazine bone/skin ownership, entry frame, extraction
   direction and grasp from authored geometry. Keep the rail assistance and
   tolerances as adjustable VR design values. Use the existing slider feel.
4. Preserve authored moving item parts and their local animation. Resolve the
   loading hand from the interaction first, then present it consistently in IK
   and floating modes. Raw controller contact remains separate from guided hand
   placement so animation/assistance cannot acknowledge its own insertion.
5. Use an authored replay through the real engine for the changed boundary:
   partial replacement plus empty replacement, checking one physical completion
   against the native loaded/reserve result. Add interruption cases only where
   the new family or changed code makes them relevant. Feel tuning can follow.

Reuse `bc2_weapon_config_pipeline.py`, `build_manual_reload_catalog.py`,
`bc2_magazine_descriptor_manifest.py`, `bc2_authored_magazine_geometry.py` and
the compiled capability probe. Keep their full native/asset analysis outputs
local. Existing checkpoint geometry is retained; no new extraction or native
analysis was started to write this plan.

## Native feel and development pace

- Physical completion should advance the engine once, with matching weapon and
  ammo ownership. It may shorten or replace stock reload timing. Keep native
  ammo transactions coherent across the owned firing copies; visual seating alone
  is not evidence that the engine accepted ammunition.
- Let the loading hand finish its job and return to support naturally. Keep the
  firing hand stable and let authored item parts animate in their local frame.
  A completed magazine must not keep the support hand trapped until a stock
  animation timer expires.
- Empty versus tactical reloads can differ. Requiring a rack on every load makes
  the interaction worse. Use the actual action policy and current chamber state.
- Preserve ordinary reload for a weapon with an unresolved physical mechanism.
  A useful first version can retain stock charging while magazine insertion is
  physical; communicate that coverage accurately.
- Batch profile/source edits, run one affected incremental target at two jobs,
  and choose the smallest relevant CPU or replay check. No full suite, both-arch
  rebuild, headset approval or historical checklist is a routine prerequisite.

## Authored reload and firing replay

The owner explicitly wants hand-authored poses and the replay harness to test
reloads and firing, including BC2 single-player's authoritative server and two
client-side firing copies. This is an in-process engine synchronization test,
not an online/multiplayer networking task. Label each copy in the observations
and keep the same exact weapon/actor incarnation through the transaction.

Use the existing XM8/SPAS tapes as infrastructure references, not as reusable
M9 contact coordinates. Author M9 removal, pouch acquisition, insertion,
release-to-finish and slide/rack trajectories against its own contact frames.
Include ordinary speed and deliberate overshoot/partial-release cases to exercise
slider assistance. Synthetic poses test the mechanics; balance/feel tuning can
follow. Tape duration counts native gathers, so measure elapsed engine time
rather than interpreting the nominal tick period as actual pacing.

| Replay case | Readiness and synchronization observation |
| --- | --- |
| Partial/tactical reload | Remove/retain or replace the magazine; preserve the already chambered-round policy and avoid an unnecessary rack |
| Empty reload, magazine absent | Trigger pulls cannot invent ammunition, trigger transfer, or fire a duplicated round |
| Magazine seated, rack required | Physical seating completes once; fire stays blocked until the actual profiled charging action completes |
| Rack completed | Clear the required readiness block once; one accepted shot consumes one round consistently across the server and both client copies |
| Trigger held across insertion/rack | Readiness recovery cannot create extra shots beyond the intended trigger/fire policy |
| Reload in progress | Fire, alternate fire and equipment restrictions match the current owned reload phase, then release promptly when its physical/native work is complete |
| Cancellation, mode/weapon switch | Retire the old interaction; late seat/rack/transfer observations cannot credit or unblock the newly selected weapon |
| Duplicate or briefly missing observations | No double insertion, double ammunition credit or stale rearming; the copies converge without freezing the loading hand |
| Repeated load/fire cycles and reserve exhaustion | Loaded/reserve changes remain conserved and consistent; no negative ammo, leaked hold or permanently blocked weapon |

Compare each authoritative transfer and accepted shot with the two client
observations by exact identity, ordered receipt and source timestamps. Allow
legitimate intermediate timing differences; a single same-tick mismatch is not
automatically a replication bug. Require convergence by the relevant native
completion/ready boundary and distinguish a missing observation from a proven
missing transfer. Repeated client receipts are not independent server shots.

Use CPU fixtures first for changed ownership/readiness policy, followed by one
bounded real-engine authored replay for the changed native boundary. Keep raw
native reports and full correlations local. Do not run a competing game/capture
while the owner is playtesting another mod. The next user BC2 test comes before
starting the new pistol implementation, as requested.

## Combined build handoff

The checkpoint owner selects production artifacts and retains rollback. Include
completed source repairs and the existing accepted hand/contact/settings profile.
Document which guns have physical reload support and which still use ordinary
reload. Keep pending recoil discovery labelled diagnostic until the recoil-only
engine source is identified and transferred correctly. A new movement correction
must likewise distinguish HMD-relative input correctness from any remaining
engine restriction on sprint direction.
