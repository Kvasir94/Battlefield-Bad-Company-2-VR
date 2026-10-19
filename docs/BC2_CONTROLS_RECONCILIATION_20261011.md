> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 controls and reload reconciliation — 2026-10-11

Review baseline: main `6449df3af83668d77e6a4260ae1aa95e0cb66d25` versus
integration `3aa78af53f63ec10d0a794523c969bc671d99595`. The main checkout's
untracked `PLAYTEST-20261011-CONTROLS-AND-RELOAD.md` was read without modification.
This review also consulted `OPEN_WORK`, `HANDOFF`, the October 8 late handoff,
the porting roadmap, `BC2_RELOAD_PARITY`, `BC2_PHYSICAL_RELOAD`, the A-button
control note and the reload feedback matrix. Older B-button instructions and
"offline only / no handle admitted" statements describe older checkpoints;
they must not override the newer source and scoped playtest observations.

This is a source reconciliation, with no game run, input injection, installation,
selection or headset acceptance. Private weapon identities, decoded assets and
runtime traces are not reproduced. Parent owns integration, compilation,
packaging and native audio. Hands and rendering have separate owners.

## Current control contract

- A short release: magazine ejection, or the existing chamber/confirmation
  fallback after the consumer's own insertion.
- A held 600 ms: existing native automatic reload/recovery request.
- Right-stick UP: jump; DOWN: other primary gun. B has no normal gameplay action
  in this mapping. Earlier short-B names in code/env describe the same preserved
  chamber policy, rather than the current physical controller button.
- Receiver-grab magazine removal is optional. The default requires A.
- Pouch pickup, guided insertion, charging handles and pistol slides keep their
  physical input. The receiver-grab preference does not disable them.

## Bounded source change in this batch

Before this patch, `Bc2PhysicalWeaponInteraction::Tick` unconditionally admitted
a fresh left grip near an inserted magazine into `pulling_`. The legacy
`Bc2MagazinePhysicalReload::TickCore` likewise supplied removal permission
without a preference. Both disagree with RELOAD-CONTROL-001.

The settings parser now defaults `magazineGrabRemove` to false and enables it
only for the exact process-start setting `FVR_BC2_MAGAZINE_GRAB_REMOVE=1`.
Both magazine construction paths forward that preference through
`Bc2MagazineFrontend::EnableGrabRemoval` to both consumers. The physical path
gates only entry to the receiver-pull gesture and its grab-hover tick. The
legacy path sets removal permission from the active cycle, button edge or
explicit opt-in; an existing cycle continues to drain normally. Ignored idle
receiver grip is neutral for legacy removal admission, so holding support grip
cannot disarm A ejection. Active carry/insertion still receives the real grip.
A ejection,
pouch/insertion/rack input, native reload authority and held-A recovery are not
changed. Existing pull-specific tests explicitly opt into their intended input.

Added focused coverage checks default receiver grip without a claim or ammo
mutation, A ejection, tactical pouch/insertion, empty-magazine A chamber fallback,
legacy A insertion/receipt completion, and exact opt-in parsing. Coverage also
checks A while the ignored grip stays held in both consumers. Existing enabled
pull/drop/return tests retain their coverage. Compilation and execution are
pending the parent-owned batch at the time of this note; `git diff --check`
passed. No runtime result is implied.

## Reconciled lanes

| Lane | Current source/evidence | Remaining work and bounded next action |
| --- | --- | --- |
| Server reload authority | `Bc2PhysicalWeaponRuntime::Publish` writes loaded/reserve only on structurally admitted owned server firing and requires before/after readback. `Current` rechecks the local player/server soldier/inventory/item and both client branches. `EnsureCapacity` owns the per-instance total capacity override on all three copies. `BeginNativeReloadTransfer` admits only the owned server's ordinary state-12 transfer caller and checks capacity before mutation. Native Restore remains the propagation route. Historical late-handoff automatic reload cycles converged across all copies and refired. | Cumulative interruption/switch/restore behavior remains unaccepted. A source fix must retain server-only accounting and never infer a chamber/refill from unexplained native loaded counts. Use the existing three-copy/state diagnostics for the actual failure before adding a state projection. Alternate Tick transfer and expanded families remain separate work. |
| Magazine ownership and drop | Held/retained/dropped magazines are finite authority resources. Physical gestures queue commands, then change visual custody after applied receipts. Cancels retry cleanup on the original incarnation; stowed cleanup drains on the server inventory update. `DroppedMagazineStep` uses the applied physical drop receipt; current tests cover exactly-once presentation after interrupted drops. | MAG-001's 1–2-frame pop has no established cause. Compare existing palette-fallback journal, authority revision and raw/presented carry transforms at the symptom. Fix only a demonstrated source/owner/transform discontinuity. Do not extend stale pointer leases or change custody to hide a render failure. |
| Empty well and support grip | Integration removed the `!supportAttached` exclusion around magazine presentation. `MagazinePresentationWrites` retains magazine/carried-part writes and excludes wrist/finger writes when support owns the left hand. Thus support posing can coexist with Hidden/Attached/Removed/Replacement magazine presentation. This change is absent from main's reviewed baseline. | XM8-MAG-002 has a bounded source repair with focused coverage, but no later live acceptance. Preserve the fix during integration and compare support/hidden/drop behavior in the next cumulative test. No ammo resurrection is inferred from the original visual report. |
| Receiver removal preference | This batch adds button-default receiver removal and explicit physical-pull opt-in in both consumers. | Parent-owned focused compile/check remains. Do not couple the preference to A hold, pouch availability or chamber action. |
| Waist magazine discoverability | Physical `bodyAmmo_` and legacy `bodyAmmoEnabled_` default false. Ordinary magazine construction does not enable them. Only `EnableBodyInventory` enables body magazine offers in this source. The tested floating session explicitly retained BodyInventory off. The physical offer accounts for loose reserve plus retained magazine rounds; readiness and ammo pickup remain authoritative. | AMMO-001 has an implementation coverage gap even when reserve exists. Enable the existing render-only body-magazine offer independently of expanded BodyInventory in ordinary setup, then verify the host receives the weapon-specific prop. If geometry is unavailable, the user approved a visible interaction-zone fallback. Do not enable holsters or expanded weapon inventory merely to obtain a pouch hint. |
| Handle/pouch visual guidance | `GatherHook` computes five hint alphas. `DroppedMagazineStep` renders only Magazine/Magwell amber magazine overlays; source search found no caller of `ReadReloadHints` and no Handle/Pouch/Pump drawing consumer. `HoverCues` supplies a light haptic tick, not visible illumination. XM8 bolt operation was explicitly confirmed by the user. | RELOAD-001's missing illumination remains. Add a visible cue using the exact mapped handle/parts or approved zone at the existing presented weapon transform; add waist-zone drawing if prop capture is unavailable. Use the existing alpha/deadline/space contract. Do not claim published alpha or haptic entry establishes visible feedback. |
| Chamber and recovery | Authority owns chamber, inserted magazine and action state. After-insert short action may rack/confirm, with bounded latching while seat completion is in flight; it does not immediately eject the newly seated magazine. Native automatic reload remains the longer A hold. Supported action profiles are distinct from generic native ammo readiness. | Keep interrupted receipt/owner-change cancellation and no-reserve feedback explicit. No new chamber write or function call is needed for button-default removal. Native state/ready projection, unsupported mechanism recovery and subjective handle discoverability remain separate gaps. |
| Profiles and scope | Core `Bc2WeaponProfiles` has exact SPAS/XM8 and scoped-XM8 40mm entries. Magazine registry joins reviewed native configuration to exact geometry; experimental geometry stays identified. Charging handles are mapped for XM8/F2000 and registered pistol mechanisms. The action catalog alone does not enable a native consumer. | Catalog resolution of 305 weapons, or an audio candidate inventory, is not all-gun reload/aim/handling acceptance. Belt, cylinder, clip, unsupported bolt/launcher and missing exact handles retain their separately defined native fallback. Expand by exact configuration, pool, rig and mechanism, never broad label matching. |
| Haptics | Integration seat profile is .5/100 ms with gun-hand seat feedback; the existing completion contract routes successful physical completion to both hands and button release to the gun hand. The physical frontend emits success feedback at applied receipts. Main and integration share most haptic source; a fresh XR host is needed for its seat profile. The feedback matrix distinguishes actual extracted AS2 defaults/override constants from measured hardware response. | RELOAD-FEEDBACK-001's event/feel acceptance remains. Release/drop has no distinct added pulse, rear stop no new event, and audible seat/button cues remain unproved. Check event delivery/focus/freshness first. Do not increase amplitude or assign a cue until the matching action recording requested by the user exists. |
| Recoil | Integration adds optional exact-copy GunSway tick/camera observation. `ReportNativeRecoilObservation` explicitly reports transfer unimplemented and same-frame authority false. Tracked hand yaw/pitch still writes the existing native baseline. | ARM-002 does not establish a recoil cause; recoil transfer is deferred. The observation label is not proof the native tick consumed that input. Need selected tick/publication/shot-consumer composition evidence before compensating angles or presenting native recoil on the VR gun. |
| Underbarrel pose and aim | Existing `ResolveWeaponVisualFamily` separates visual admission from the full native switch route, addressing the earlier scoped-XM8 launcher fallback inconsistency. `Bc2WeaponProfiles` admits scoped launcher aim/support axes but intentionally leaves translated muzzle unverified because its flash joint resolves to the rifle barrel. Effect anchors and shot origin are different consumers. | Prior severe cant/odd grenade direction remains unaccepted. Compare model basis, common weapon getter/effect root, and actual server/client shot matrix for one exact launcher selection. Do not rotate by guessed constants or enable rifle flash-joint translated muzzle as a launcher fix. |
| Sprint direction | `SprintOwnsHeading` uses HMD heading only while Sprint is held without Fire/AlternateFire; Gather rotates body/HMD-relative axes into that native heading. Tracking loss leaves native aim responsible. Existing mapping already contains the HMD sprint remedy described by the older handoff. | SPRINT-001 is intermittent and was later described as feeling reasonably good. Keep it lower priority. The existing desired-body/submitted-axis/basis/native-sprint telemetry can distinguish a heading/basis error from cancelled sprint. No new direction rewrite is justified by the incomplete observation. |
| Weapon switching | The verified server inventory hook owns selection; client Next/Previous pulses are stripped once that hook is active. Persistent ledgers preserve ammo/custody across selection, and stowed cleanup continues. Support keeps its claim across an unsuccessful flick by default and releases on the actual owner/weapon change. | Cumulative forced-switch/death/drop/receipt timing remains acceptance work. Preserve existing chain/owner/incarnation and actual selector revalidation. Do not reset weapon state merely because selection changed or expand array size as a substitute for native inventory ownership. |

## Button action origin defect for a later source decision

The physical frontend sets `MagazinePhysicalResult::boltReleased=true` when
`Tick` successfully *queues* a button `CycleAction`/`CloseAction`. That value is
not retained with the pending command. On a later applied receipt, `Drain`
sets `racked=true` without restoring button origin. Gameplay subsequently uses
`!magazineResult.boltReleased` for the gun-event physical-rack flag and rack-only
audition admission. A delayed button completion therefore loses its button
classification. Physical rack haptic hand selection also consults the then-current
slide/rack claim, rather than a retained admitted action origin.

This is a source defect, not a diagnosis of earlier audio proof rejection,
missing callbacks or subjective haptics. Parent explicitly deferred its repair
until the requested valid matching action recording is available. The bounded
later fix is to retain button/physical origin with the pending command and emit
completion origin/hand context only on the successful current-owner receipt;
rejected, cancelled and changed-owner commands must not manufacture an action
completion. Do not broaden audition admission or assign seat/button sounds in
this optional-removal batch.

## Observation boundaries to preserve

M9-001 held-trigger repeat remains unverified: trigger routing, exact pistol
variant/native fire mode and replication must be distinguished before changing
semiautomatic behavior. MAG-MATERIAL-003's pale dropped magazine remains a
material/lighting observation with no confirmed snow/texture/highlight cause.
XM8-FX-001 concerns visible muzzle/ejection effects; ballistic-origin correctness
does not follow from visual appearance. All three are open and are not silently
fixed by the receiver preference or support-palette repair.

The independent-hands acceptance failures (HANDS-001/002), snowy one-eye
cutscene blur, renderer resolution and deferred performance observation retain
their existing owners. This controls patch starts no duplicate runtime effort.
