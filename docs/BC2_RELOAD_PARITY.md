> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 physical reload parity: Arizona Sunshine 2, KF2 VR, BC2

## Current refactor disposition

This section supersedes older adapter-absence claims below. Source implementation
and offline checks do not establish native or headset acceptance. The goal is
responsive physical handling using authored item mechanisms where useful, without
making every stock animation timer a prerequisite for tracked actions.

| Finding / capability | Disposition and next evidence |
| --- | --- |
| Unmanaged B Reload lost under global authority | Implemented selected-item routing, enabled-consumer reservation, conservative initialization/suspension and neutral rearming. Native empty autoreload remains separate. |
| Body versus stick reload admission | Implemented shared owned admission, refreshed body draw/holster checks and explicit server-selector revalidation. Pending commands continue to drain. Logical hide is not native deactivation. |
| READY versus counts / required action | Implemented owned readiness interface, conservative hint inputs, cached diagnostic cue and distinct receipt/confirmation haptic. No new text overlay; headset tuning remains. |
| Floating hands constrained by arm reach | Implemented floating wrist/item frame independence through existing private palette publication. IK behavior retained; clean capped-mesh draw remains blocked on native draw/palette ownership evidence. |
| Authored item animation, hands and sounds | Reused evaluated item descendants, exact authored contacts/fingers, native mechanisms and existing receipt/sound boundaries. Do not replace moving-part animation with a guessed whole-hand world path. Per-item feel needs worn acceptance. |
| Continuous insertion, overshoot and late release | Reused ReloadInsertion slider travel, depth latch, release finish and contact hysteresis. Authored/experimental geometry quality remains per rig; no blanket recalibration or guessed offsets. |
| Physical completion independent of stock reload | Reused owned command projection, chamber/action ledger and per-instance deploy delay suppression. No new ABI or blanket state reset. |
| Native interrupt completion | Ordinary Transfer now checks capacity before mutation. Alternate Tick transfer stays unadmitted: its state2/2 plus positive phase requires separate transfer settlement, fire replay and intentional VR-ready projection policy. |
| Selective Restore / three-copy state | Reused capacity reassertion and exact receipt matching. Matching ammo is not full readiness; bolt/release/flag projection needs selected-family native evidence and correction ordering before expansion. No blind native count import. |
| Cancel, switch, drop, death, tracking loss | Reused persistent identity/custody and cancellation-with-receipt-drain. New admission prevents new selection during obligations; forced lifecycle scheduling and native round trips remain acceptance work. |
| All-gun and special mechanisms | Exact profiles remain required. Belt, cylinder, clip, bolt and launcher mechanisms need native mode/pool/closure evidence and authored contacts; the proposed coverage inventory is not enabled coverage. |
| Expanded inventory / either-hand guns | Reused logical item identity and chamber/debt persistence. Native selection routes/serialization/entity lifetimes and simultaneous firing ownership remain blockers; increasing arrays or display slots cannot resolve them. |
| Native event timing and odd clues | Repeated begin, end-before-transfer, pre-projection Restore callbacks and unused Transfer emit argument prohibit generic event-to-readiness mapping. Detailed private native evidence stays local; no hook ABI is inferred from untyped pseudocode. |
| Stereo building edges / apparent per-eye LOD | Researched, not changed by this refactor. Shared view prototype/culling does not prove shared object LOD, destruction state or draw-cache representation. Next bounded evidence needs geometry bindings, draw arguments and object/LOD identity across one stereo pair; preserve legitimate per-eye visibility while investigating representation consistency. |

Implementation boundaries: `Bc2PhysicalWeaponRuntime`, `Bc2Gameplay`,
`PhysicalWeaponReadiness`, `Bc2PhysicalWeaponInteraction`, `Bc2FloatingHands` and
`Bc2RigPublication`. Relevant regressions cover routing rearm, admission/capacity,
conserved chamber state, receipt feedback, floating/IK frame behavior, existing
insertion and authored contacts. Native timing, rapid interrupted/switching
reloads, corrected three-copy behavior and real-hand comfort remain explicit
acceptance work; preserving a useful native animation is compatible with physical
control of the action that actually grants readiness.

Status: **design reference and backlog, 2026-10-08, on `main`** (written on
`feat/reload-parity` over `feat/feel-wiring` `75b7a03`, then moved to `main`).
Single-player only. Built and unit-tested on Windows x86/x64; nothing here has
been played in a headset or checked in game. The AS2 column comes only from data
already extracted for KF2 VR; nothing was decompiled again. The KF2 column is the
KF2 VR mod ([Killing Floor 2 VR](https://github.com/Kvasir94/Killing-Floor-2-VR) development source) at commit `43452e24`. The
BC2 column is this repository at `75b7a03`; later gap PRs update their rows.

Goal: a native, physical reload on the level of AS2 and the KF2 VR mod. This
document says, feature by feature, where BC2 stands, what is missing, what
engine mapping each gap needs and how urgent it is. The backlog at the end is
ranked.

## Sources and how to read the evidence

| Key | Source |
| --- | --- |
| **AS2-R** | `kf2-vr/docs/re/ARIZONA_SUNSHINE_2_RELOADS.md` (shipped `GameSettings`, `Item.reloadFeatureData`, tips and slide-lock disassembly) |
| **AS2-H** | `kf2-vr/docs/re/ARIZONA_SUNSHINE_2_HANDLING.md` (handling, haptics, pouch, part travel) |
| **AS2-G** | `kf2-vr/docs/re/ARIZONA_SUNSHINE_2_RIG.md` (rig and pouch placement) |
| **KF2-D** | `kf2-vr/docs/VR_PHYSICAL_RELOAD.md` |
| **IR** | `kf2-vr/script/KF2VR/Classes/VRInteractiveReload.uc` |
| **IG** | `kf2-vr/script/KF2VR/Classes/VRReloadInsertGuide.uc` |
| **MP** | `kf2-vr/script/KF2VR/Classes/VRManualPump.uc` |
| **BC2 paths** | relative to this repository |

Evidence cells give `file:line`. AS2 rows cite the doc line that records the
shipped value. "Design" marks a KF2 or BC2 choice with no AS2 counterpart.

Priority: **P0** blocks a reload that feels physical at all; **P1** is needed
for AS2/KF2-level polish; **P2** is polish; **P3** is optional or deferred.
"Portable" means the work fits in `include/fvr/interaction` or the BC2 adapter
without a new native address. "Engine" means it needs a mapped native offset,
hook, asset or a live check first.

## BC2 constraints that shape every row

- **No chamber, no lock, no pump input.** The engine keeps one loaded count
  per firing copy (`+0x7c`, three copies). There is no chamber, slide-lock or
  magazine-seated state, and the only reload input is `EiaReload` (29)
  (`docs/BC2_PHYSICAL_RELOAD.md`, *Engine sources*). Chamber and lock are
  VR-side models, as in KF2.
- **Native reload is a held state machine.** 2 → 10 → 11 (held) → 12
  (transfer) → 1 → 2; a shell reload loops 12 → 11 per round
  (`docs/GUN-EVENT-LOG.md`, *BC2 native facts*). The mod holds 11 until a
  physical seat; only the transfer receipt (`0x6d6f40`) proves ammunition.
- **Magazine out = state 11.** BC2 cannot fire during the hold, so KF2's
  retained-chamber shot needs a reordered adapter (see row C4).
- **Offline only.** The replay sim covers the detachable-magazine consumer.
  SPAS sim work is owned by `feat/spas-sim`; this branch adds no sim work.

### Firing states and events from static RE (2026-10-08, not live-tested)

Firing state at `+0x3c`:

| State | Meaning | State | Meaning |
| --- | --- | --- | --- |
| 0 | deploy | 8 | bolt cycle |
| 1 | post-reload wait | 9 | auto fire |
| 2 | ready | 10 | reload begin |
| 3 | pre-fire | 11 | wait to transfer |
| 4 | zoom delay | 12 | transfer |
| 5 | shot prep | 13 | hold-charge |
| 6 | shot | 14 | release |
| 7 | bolt owed | 15 | hold-release fire |

Transitions seen by Commit (`0x6dd6a0`): 2 → 10 reload begin; 12 → 11 next
shell; 12 → 1 reload done; 10 or 11 → 1 sprint abort; bolt and pump 6 → 7,
7 → 8, 8 → 1. Deactivate (`0x6e1eb0`) writes 1 without a Commit on a weapon
switch, so that abort must be derived from a state diff.

Other events: dry fire is listener slot `+0x1c` at `0x6e64e7`; last round is
`+0x7c` going 1 → 0, and the gun is locked open while state is 2 with loaded 0;
the charging-handle window is state 1 plus `PostReloadSequenceTime`. There is
no engine event for magazine out: model it in VR during states 10 and 11.

Rule: derive these from the existing Commit, Transfer and Update reads, not new
detours. This branch does that for the lock (`slide_lock` rows, below).
Still owed from this list: a `dry_fire` row (needs the `+0x1c` listener or a
gather-side derivation from a trigger press in state 2 with loaded 0), a
`reason` on 10/11 → 1 (sprint abort) and on a Deactivate state diff (switch
abort), and the state 7/8 hold for a physical pump (S3).

## 1. Magazine path

| # | Feature | AS2 (shipped) | KF2 VR | BC2 now | Gap | BC2 engine mapping | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| M1 | **Magazine eject** | Eject input (`OnEjectPressed`) or pull; `autoEjectClipWhenEmpty=false` (AS2-R:47-50) | X/A or the pouch grab ejects; dry fire only clicks (KF2-D 464-490; IR:2392 `TryPouchReload`) | Eject is a physical pull (`DetachableMagazine`, 9 cm pull; `src/interaction/DetachableMagazine.cpp:146`, `:206` abandon paths) or the native reload start; `physicallyRemoved` feeds the chamber (`src/games/bc2/Bc2MagazinePhysicalReload.cpp:434`) | No button eject while a magazine is in; auto-reload on empty is suppressed natively but not offered as "dry click, keep the magazine" | Map a release button to the existing pull/drop path; the empty-ammo gate already exists (`auto_reload` rows) | P1 |
| M2 | **Pouch draw** | `AmmoPouchInteractable.TryGrabAmmo` spawns the opposite hand's weapon ammo; nothing if the other hand is empty (AS2-H:276-283) | Grip squeeze at a body-frame belt (`BeltOffset=(24,-14,-24)`, `BeltRadius=14` UU; IR:3957-3958); hover tick 0.1/12 ms with 1.25× exit inflation (IR:2414); pouch grab starts the reload (IR:2392-2428) | `AmmoSupply` acquires an `AmmoObject` claim on a fresh grip edge inside the pouch sphere, only with a GunHold in the other hand (`src/interaction/AmmoSupply.cpp:95-137`); default pouch `(-0.23,-0.55,0.02)` m, r 0.18 m (`src/games/bc2/Bc2PhysicalReload.cpp:34-36`) | Done on proto/reload: `AmmoSupply::Hover` gives a `reload_hover` tick (0.1/12 ms) on entry at r, leaving only beyond 1.25r; grab haptic shipped earlier. Needs a headset check | None (portable) | P1 |
| M3 | **Hand-to-mag** (prop flies to the hand) | `itemPickupTimeOverDistanceCurve`: 0.05 s at 0 m to 0.15 s at 1 m (AS2-H:209-211) | 80 ms + 10 ms/UU, capped 250 ms, for the drawn hand easing onto a part (KF2-D 501-504) | The held prop is drawn at the hand from the first frame (`Bc2MagazinePresentation.cpp`) | No ease from pouch to hand | Renderer prop pose only (portable math, BC2 presentation) | P2 |
| M4 | **Insertion and seat** | `MovableHandle` slider, 25% release-to-finish, 0.04-0.25 s settle, Hermite tangents 2.779/0.062 (AS2-R:31-45) | `VRReloadInsertGuide` (IG:59-160) | **At parity on XM8 and the generated AEK row**: `ReloadInsertionFeel::Slider` (`include/fvr/interaction/ReloadInsertion.h:25-52`, `:458-491`); XM8 opted in (`src/games/bc2/Bc2MagazineGeometryProfile.cpp:24`); hand latch through the settle (`Bc2Gameplay.cpp:619`, `Bc2MagazinePhysicalReload.h:140`) | Other admitted magazine rows still Legacy; no headset tuning | Per-row geometry admission (offline pipeline) | P0 (headset) |
| M5 | **Slam** | Overshoot is full depth (slider) | Pushing past the latch seats (KF2-D 56-80) | Slider seats past the latch; motion guard replaces the 7 cm step cap (`ReloadInsertion.h:377-383`, `:431`) | Legacy rows still cancel on overshoot | Same as M4 | P1 |
| M6 | **Drop on miss** | Early release drops the magazine; ejected magazines despawn after 15 s (AS2-H:282-283) | Released spare falls with the stock shell-bounce sound, 12 s lifetime, three cosmetic slots (KF2-D 290-296; IR:3229-3242 `DropMagazine`, IR:3243 `StartReleasedProp`) | On proto/reload: the old magazine released at RemovedHeld -> WellEmpty falls (`DroppedProps.h`: gravity, damped bounce, play-space floor, 12 s, three slots) and is drawn through the magazine bone until the replacement grab (`droppedWorldMeters`); prop rows drop/land/expire | The prop vanishes at the pouch grab (one magazine bone; a persistent prop needs its own renderer instance); no drop sound; the floor is the play space, not the game ground | Renderer prop with gravity (portable); ground height needs a BC2 world query or a fixed floor from the play-space | P1 |
| M7 | **Failed insertion haptic** | Profile 11: two-peak curve over 0.1 s (AS2-R:166-167) | Two 0.3 peaks of 30 ms, 60 ms apart (IR:2794-2795) | None | Missing | New `FeedbackKind` (IPC change: host and DLL must ship together) | P2 |

## 2. Action: chamber, rack, lock

| # | Feature | AS2 | KF2 VR | BC2 now | Gap | BC2 engine mapping | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| C1 | **Chamber model** | Advanced never auto-chambers; top-up with a round chambered needs no rack (AS2-R:145-152) | `MagazineFeedState` (`kf2-vr/native/adapter/MagazineFeedState.h:9`) | Ported unchanged, 417 checks (`include/fvr/interaction/MagazineFeedState.h`); owned per weapon by `Bc2MagazineChamber` (`src/games/bc2/Bc2MagazineChamber.h:23-31`) | None in the model | — | done |
| C2 | **Rack / charging handle** | Slide hand line 1.2-4.3× art travel (AS2-H:237-248); `Gun Slide Activated` 0.1 s/1.0 (AS2-R:163) | 3.5×/2×/1× throw by stroke length (KF2-D 330-340); rear stop and return are full 0.1 s beats (IR:2575-2576) | `ChargingHandleRack` with `MotionGuard::Rack()` (`include/fvr/interaction/ChargingHandleRack.h:58`); `EnableChamberRack()` **off** (`Bc2MagazinePhysicalReload.h:135`) | No mapped handle pose; no throw scale; no rack haptic; rack does not reach the game | Per-weapon handle pose (offline mesh pipeline); a real effect needs firing states 7/8 | P1 |
| C3a | **Last round / lock observed** | `slideLockOnEmptyMag` when magazine plus chamber reach 0 (AS2-R:139-143) | `EmptyMagBlendNode` lock on the empty reload (KF2-D 13-22) | **Done here:** native `slide_lock` row derived in `GunCopyTracker::Observe` (`src/games/bc2/Bc2GunEventLog.cpp`, `Observe`) from state 2 + loaded 0, with `last_round` on a shot to 0 | Not yet fed to the chamber model or a lock pose | None for the row; a live trace to confirm states 2/3 on dry fire | P1 |
| C3 | **Slide/bolt lock and release** | `slideLockOnEmptyMag` per gun; release button chambers onto a loaded magazine; grab volume doubles while locked (AS2-R:139-153) | `EmptyMagBlendNode` lock, X/A release, short-pull release 0.6 UU, pending lock survives interruption (KF2-D 13-30; IR:3561 `UpdateSlideLocks`, IR:3662 `ReleaseSlideLock`) | `locksOpenOnEmpty` catalog flag only, unverified (`src/games/bc2/Bc2MagazineEquipmentProfile.h:20-21`, `Bc2MagazineActionCatalog.h`) | No lock state, no release gesture or button, no lock pose | Lock pose needs bone control (none mapped); release button needs an input mapping | P2 |
| C4 | **Retained chamber shot** | Magazine-out gun fires its chambered round | `1 / CHAMBERED` then `0 / MAGAZINE OUT` (KF2-D 10-55) | Model ready (`DisplayAmmo`); fire blocked while state 11 holds | BC2 can't fire in state 11 | Reorder: remove before the native reload, start the hold on replacement | P3 |
| C5 | **Open bolt / notch lock** | Uzi/Thompson lock (AS2-R:139-141) | `ActionKind` 3 notch, open-bolt cock (IR:2602 `CockOpenBolt`) | `EmptyMagazineState` for open bolts in `Bc2MagazineChamber`; catalog rows for UZI/Thompson/UMP/G3 | No gesture; unverified rows | As C2 | P2 |

## 3. Shells and pump (SPAS-12)

| # | Feature | AS2 (Mossberg 500, id 108) | KF2 VR (MB500) | BC2 now | Gap | BC2 engine mapping | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| S1 | **Shell-by-shell loading** | One shell per insert; pool 30 shells (AS2-H:282) | One stock reload step per seated shell (KF2-D 709-714) | Native 12 → 11 loop per shell; `Bc2PhysicalReload` seats one shell per request with a native receipt (`src/games/bc2/Bc2PhysicalReload.cpp:316-366`) | — | Done natively | done |
| S2 | **Shell insertion feel** | `releaseMagHandleOnInsert=true`, `magInsertingHandPoseId=186` (AS2-R:66-75); auto-insert 0.04-0.25 s on the same tween (AS2-R:31-45); `InsertAmmo` 0.1 s/0.5 (AS2-R:160-163) | A captured shell a quarter into the port **slides home on its own and frees the hand**, even with the grip held (IR:1438-1446; KF2-D 342-346); settle `clamp(0.25·(1−f),0.04,0.25)` (IG:142-160) | SPAS profile is `RailContact` + `Legacy`: 120 ms alignment, 60 ms dwell at depth, release cancels (`src/games/bc2/Bc2ReloadInteraction.cpp:3-36`) | No release-on-insert, no settle, dwell makes a fast push miss | **Portable** (this branch: `ReloadInsertionSliderTuning::releaseItemOnInsert` and `SpasShellFeel::As2`); live use needs the SPAS consumer to keep the shell claim through the settle | P0 |
| S3 | **Pump action** | Hand line 9.0 cm vs art 7.6 cm (1.2×) (AS2-H:244); `slidePulledHaptics=21` (0.1 s/1.0) (AS2-R:72) | Rear stop at 95% (`RearStop=0.95`), forward lock 5% (MP:516-530, MP:591-593); rear knock 0.7/45 ms + gun hand 0.3/30 ms (MP:525-526); forward 1.0/80 ms both hands (MP:545); 2× throw (KF2-D 333-335) | `WeaponCycle` (pump family) and SPAS fore-end stroke 0.0950 m measured (`src/games/bc2/Bc2PumpPart.h:7`); **not wired** | No live pump; native cycle is 5/6 → 7 → 8 automatic | Firing states 7/8 hold, like the reload hold; part bone `jntWpn_4` palette write | P1 |
| S4 | **Pending pump survives interruption** | — | Pump owed belongs to the weapon (KF2-D 311-315) | None | Missing | Needs S3 | P2 |
| S5 | **Pouch shell draw** | One shell per grab (AS2-H:276-283) | Pouch grab starts the stock reload and hands over the shell (IR:2392-2428) | `AmmoSupply` with `SingleShell`, 1 unit (`AmmoSupply.cpp:33`); belt display (`src/games/bc2/Bc2BeltAmmo.h:8`) | Done with M2 (same `AmmoSupply` hover on the pouch and shell-belt contacts) | As M2 | P1 |

## 4. Support, feedback and presentation

| # | Feature | AS2 | KF2 VR | BC2 now | Gap | BC2 engine mapping | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| F1 | **Two-hand support during reload** | Engage slerp 25 ms, release 100 ms; primary hand owns roll (AS2-H:110-133) | Acquiring an action grip releases support so aim cannot fight it (KF2-D 261-266); pump stroke owns the support hand (KF2-D 448-458) | `SupportGrip` (`include/fvr/interaction/SupportGrip.h:13`); magazine and shell claims are exclusive per hand (`HandInteraction`) | No slerp on engage/release; rack takes no Mechanism claim yet | Portable | P2 |
| F2 | **Haptics** | GrabAmmo 0.01 s/0.3, InsertAmmo 0.1 s/0.5 (controller override 0.05/0.75), slide 0.1 s/1.0, failed id 11 (AS2-R:160-167) | Grab 0.3/10 ms (IR:2379), capture 0.18/15 ms (IR:1436), seat 0.5/0.1 s + gun hand 0.35/30 ms (IR:2500-2501), rack 1.0/0.1 s (IR:2575-2576) | Capture 0.55/35 ms, seated 0.7/45 ms, applied 0.9/75 ms (`include/fvr/interaction/Feedback.h:44-52`); drained at `Bc2Gameplay.cpp:1233`, `:1273` | No grab, failed, rack or gun-hand pulse; values are not AS2's | New `FeedbackKind` values (IPC; host and DLL ship together) | P1 |
| F3 | **Sounds** | Per-gun handling events | `VRReloadSoundMap` from stock motion; eject/contact/seat/rear/return cues once per clip (KF2-D 245-296; `VRReloadAudio.uc:233` `EmitCue`) | None of BC2's own; the native reload clip plays its sounds on the native clock | On proto/reload: the reload clip's sound is withheld and replayed at the magazine pull, then stopped after 4 s (`Bc2ReloadSoundGate.h`). The replay runs right after the game's listener dispatch `0xf4fb50`, for the ant that owns the listener, under that dispatch's lock (CS `0x18a4bc0` plus TLS chain `[0x1502e04]`) | Engine hook, verified by signature | P1 |
| F4 | **Ammo display** | Tips only (no count) | `1 / CHAMBERED`, `0 / MAGAZINE OUT`, `RACK` (`VRSpatialHUD.uc:938-939`; IR:1749 `UpdateAmmoDisplay`) | `MagazineFeedState::DisplayAmmo` (`MagazineFeedState.h:42`), chamber `display_ammo` in the gun-event `chamber` row (`Bc2GunEventLog.cpp:75`) | No label; the BC2 HUD shows the native count | **Portable label** (this branch: `MagazineDisplayLabel`, `label` key on `chamber` rows); drawing it needs a HUD surface | P1 |
| F5 | **Reload tips (part glow)** | Magazine / magwell / slide glow after 0.75 s, 0.3 s pulse to 0.6 alpha, amber (AS2-R:107-137) | Part shells, per-part clocks (KF2-D 579-647; IR:3273 `UpdateHints`, IR:3368 `UpdateParts`) | None | Missing | Per-part meshes or a material tint; BC2 renderer work | P2 |
| F6 | **Visual magazine props** | Real magazine meshes | Props cut from the gun's own rig (`VRAmmo_<rig>`); spare bones hidden (KF2-D 744-760) | Magazine and shell props from the authored mesh pipeline (`Bc2MagazinePresentation.cpp`, `Bc2BodyAmmoRenderer.cpp`); spare-visibility work in `docs/OWNED-SHELL-VISIBILITY-20261001.md` | Empty-magazine cut (no rounds) missing | Offline mesh pipeline | P2 |
| F7 | **Hand poses** | Separate hold and insertion poses (`magHoldHandPoseId`/`magInsertingHandPoseId`) (AS2-R:75) | Stock finger motion scrubbed with insertion (KF2-D 244-260) | Authored grip poses (`Bc2HandPose.h`); one pose through insertion | No insertion pose blend | Offline pose data | P2 |

## 5. Failure, cancel and tuning

| # | Feature | AS2 | KF2 VR | BC2 now | Gap | BC2 engine mapping | Pri |
| --- | --- | --- | --- | --- | --- | --- | --- |
| X1 | **Tracking loss / gap** | — | Motion guard: gap > 150 ms, implausible step or backwards time cancels silently (`VRReloadMotionGuard.uc`; KF2-D 339-347) | `MotionGuard` (`include/fvr/interaction/MotionGuard.h`); recognizer reasons `StaleInput`, `MotionRejected`, `TrackingLost` (`ReloadInsertion.h:115-120`) | — | — | done |
| X2 | **Cancel back to stock** | Timed mode | X/A stock reload stays the fallback (KF2-D 665-668) | Cancel releases the native hold (`freeze_stop reason=cancel`); fire during reload cancels (`docs/RELOAD-FIRE-CANCEL-20261002.md`) | — | — | done |
| X3 | **Interrupted reload** | — | Pending lock and pending pump survive swaps (KF2-D 23-30, 311-315) | Chamber `Reset` on owner/weapon change (`Bc2MagazineChamber.h`) | Nothing pending survives a swap (by design today) | Needs C3/S3 | P2 |
| X4 | **Per-weapon tuning** | `reloadFeatureData` per item (AS2-R:61-84) | `VRReloadCatalog` / `VRPumpCatalog` / `VRBreakCatalog` rows | `MagazineEquipmentProfile` with action kind and lock flag (`Bc2MagazineEquipmentProfile.h:20-35`), `Bc2MagazineActionCatalog.h`, per-profile `ReloadInsertionFeel` | No per-weapon throw scale, settle or haptic override | Portable fields; values need per-gun review | P2 |
| X5 | **Accessibility / off switch** | Quick, Advanced, Timed (AS2-R:107-110) | Interactive reloads OFF by default (KF2-D 1-8) | `-MagazineReload` / `-PhysicalReload` session switches | No in-game toggle | Menu binding | P3 |

## Ranked backlog

Ranked by feel gain against risk. "Done here" items are on this branch.

0. **Last round / slide lock row (C3a): done here.** `slide_lock`
   (`locked`, `state`, `loaded`, `last_round`) from the existing copy reads,
   per the static RE. Next: feed it to `Bc2MagazineChamber` for
   `locksOpenOnEmpty` rows and to the lock pose (C3).
1. **SPAS AS2 shell feel (S2), portable half: done here.** `releaseItemOnInsert`
   (AS2 `releaseMagHandleOnInsert`) on the slider: a captured shell a quarter
   in, after its real post-capture stroke, settles home on its own while the
   grip is still held, and `handFreed` tells the adapter it may let the hand
   go. `SpasShellFeel::As2` selects it; the default stays `Legacy`, so the
   live consumer and the `feat/spas-sim` work are unchanged.
   **DLL half: wired on `proto/reload`, As2 is the live default.**
   `Bc2PhysicalReload::LatchesLoadingHand()` holds the shell's claim while it
   settles, while a release would settle it, and while its seat waits for
   submission; Gameplay keeps `released[0]` false from it (as for the XM8
   slider) and Tick passes that grip to AmmoSupply, the deferred seat and
   `ManualReload`'s neutral test. A settle start emits `release` with
   `to: "port"`; the report carries `shell_feel`, `settles` and
   `latched_ticks`. `SetShellFeel(SpasShellFeel::Legacy)` restores the old
   feel. CPU-tested (47 consumer cases, including a release on a waiting
   seat); not yet run in game.
2. **Ammo display label (F4), portable half: done here.**
   `MagazineDisplayLabel()` gives KF2's `CHAMBERED` / `MAGAZINE OUT` / `RACK`
   (or none) from the chamber view; `chamber` gun-event rows carry it as
   `label`. **Still owed:** a HUD or wrist surface to draw it.
3. **Pump action (S3), P1, engine. Hold and stroke wired on `proto/reload`.**
   `PumpHoldGate` (`src/games/bc2/Bc2PumpHold.h`) owes a pump on each SPAS
   shot (6 -> 7 with rounds left) and holds each copy at state 7 once its
   BoltActionDelay is nearly spent, with the reload hold's exact delta
   transaction (`PumpUpdate` in `Bc2ReloadFlowRuntime.cpp`). Rack, a reload
   press, a copy leaving 7/8 or an 8 s safety timeout releases it; the game
   then plays its own pump (+0x30) and returns to ready. `PumpStroke` is
   KF2's 95%/5% stroke on a 6 cm support-hand travel along the barrel. On by
   default in `-PhysicalReload` sessions with magazine request cycles; gun
   events show `freeze_start`/`freeze_stop` with reason `pump` and a `rack`
   row. Still owed: the `jntWpn_4` fore-end visual and pump haptics.
4. **Haptic set (F2, M7), P1, IPC.** Add `ReloadGrab` (0.3/10 ms),
   `ReloadFailed` (two 0.3/30 ms peaks 60 ms apart), `ReloadRack` (1.0/100 ms)
   and gun-hand seat (0.35/30 ms). Host and DLL change together.
5. **Pouch hover tick and hysteresis (M2, S5), P1, portable.** Enter at r,
   leave at 1.25 r, 0.1/12 ms tick once per entry.
6. **Rack live (C2), P1, engine.** Map charging-handle poses for XM8 and AEK,
   add throw scale (3.5×/2×/1× by stroke), take a Mechanism claim against
   support grip, then turn `EnableChamberRack()` on per verified row.
7. **Dropped magazines (M6), P1.** Falling prop with a fixed floor from the
   play space, 12 s lifetime, three slots.
8. **Sounds on physical events (F3), P1, engine.** Needs BC2 sound event
   mapping.
8a. **Dry-fire and abort rows, P1.** `dry_fire` (listener `+0x1c` or gather
   derivation), abort reasons for 10/11 → 1 and the Deactivate state diff.
9. **Magazine eject button (M1), P1.** Release input routed into the existing
   pull/drop path.
10. **Hand-to-mag ease (M3), P2.** AS2 0.05 s + 0.10 s/m pickup curve on the
    held prop.
11. **Reload tips (F5), P2.** AS2 per-part clocks; render path first.
12. **Slide lock and release (C3, C5, X3), P2.** After C2.
13. **Two-hand slerp (F1), P2.** 25 ms engage / 100 ms release.
14. **Retained chamber shot (C4), P3.** Adapter reorder.

## What needs live mapping or an in-game check

- Every row above: no headset or Windows build has run.
- S2 live: the SPAS consumer latch (item 1), then a headset check that a fast
  push seats and a held shell leaves the fingers without a double credit.
- C2/S3: charging-handle and fore-end poses, firing states 7/8 hold.
- F3: BC2 sound events. F4/F5: a HUD surface or material tint.
- `actionVerified` rows in `Bc2MagazineActionCatalog.h` against the Granny
  empty-reload clips.
