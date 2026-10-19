# FrostbiteVR (BC2): physical reload system, current state and path to Arizona Sunshine 2 quality

> **Superseded for the KF2 VR and AS2 comparison.** This file was written without access to the KF2 VR code or its extracted AS2 data, so its comparisons to them are from general knowledge only. A side-by-side assessment against the real KF2 VR implementation is in progress and will replace those parts.


Scope: the source snapshot at `/home/claude/src/FrostbiteVR` (checkpoint 202, Oct 7 2026). Everything here comes
from reading source and docs. Nothing was built or run. The KF2 VR mod source is **not** in this repo; the only
mention is a validation-discipline note (`docs/SIMULATED-PLAYER-197.md:7`). I compare against AS2 from public
knowledge of its behavior. When this report says "KF2-style", it means the user's description of that mod, not
code I inspected.

---

## 1. How the current reload system works

### 1.1 Layers

| Layer | Location | Role |
|---|---|---|
| Portable interaction policies | `include/fvr/interaction/*.h`, `src/interaction/*.cpp` (~6.1k LOC) | Pure state machines: hand claims, ammo supply, insertion rail, transaction request/ack, magazine removal, pump/bolt gestures, body inventory. They contain no addresses and no ammo writes. |
| BC2 adapter | `src/games/bc2/Bc2Reload*`, `Bc2Magazine*`, `Bc2PhysicalReload*`, `Bc2Prehold*`, `Bc2Body*`, `Bc2Pump*` (96 files, ~15k LOC) | Hooks the native firing state machine, holds and releases native reload, reconciles three native copies, cancels and aborts, publishes presentation. |
| Gameplay composition | `src/games/bc2/Bc2Gameplay.cpp` (2,317 lines; 182 Reload/Magazine refs) | Ticks consumers, masks actions, selects the native family by asset (`Bc2Gameplay.cpp:1154`, `:1256`). |
| Presentation | `Bc2MagazinePresentation.*`, `Bc2ReloadPresentation.*`, `Bc2PumpPart.*`, `Bc2BodyAmmoRenderer.*`, `src/graphics/D3D11RigidPropRenderer.cpp` | Edits a private copy of the skinning palette for held parts. A separate flat-shaded renderer draws the body/chest props. |
| Offline asset/profile pipeline | `tools/bc2_*.py`, `profiles/` | Granny/mesh decoding, authored geometry/grip/magazine extraction, package index, capability report, generated headers. |

### 1.2 Native synchronization: it gates the game's own reload. It does not bypass it.

This is the most important fact about the design. The mod **never writes ammunition**. It starts BC2's native reload
and **freezes it mid-way** until the physical action completes. It then lets the native state machine perform the
transfer.

Native facts the project recovered (`docs/BC2-PUMP-RELOAD-20261001.md:49-103`):
- Firing update `0x6e9000` → state step `0x6e62d0` → transition commit `0x6dd6a0`. Ammo transfer is `0x6d6f40`:
  one round for `rtSingleBullet`, the missing capacity for `rtMagazine`, written to loaded `+0x7c` and taken from
  reserve `+0x80`.
- Reload states: 2 ready → 10 begin → 11 wait `ReloadTime×Threshold` → 12 transfer + tail → 1 → 2. A bolt or pump
  cycle is states 7 → 8. **There is no separate chamber count** (`BC2-PUMP-RELOAD:77-80`).
- Every weapon has **three firing copies**: two client branches (wrapper `+0x3c`/`+0x40`) and the server item. All
  three must agree (`Bc2ReloadFlowRuntime.cpp:72-84`, `ReloadReserveCopiesAgree`).

Reload sequence (magazine path):
1. Removing the magazine (eject button or a grab and 9 cm pull) emits a short synthetic native **Reload pulse**
   (`reloadHeld`; see `docs/XM8-MAGAZINE-INTERACTION.md` bullet "Only `reloadHeld` emits the short native Reload pulse").
2. The hooked `Update` checks owner, config, timing and the three-copy cohort. While the cycle is Holding, it
   **zeroes the frame delta at `context+0x18`** for one original call and then restores it
   (`Bc2ReloadFlowRuntime.cpp:1045-1052`, `RunReloadDeltaOverride`). The native timer stays in state 11 below
   `reloadTime×threshold` (`Bc2MagazineNativeProfile.h:35-40`, `Wait`).
3. An empty-ammo auto-reload is suppressed by a one-byte override at `context+0x28` in the hooked `Step`
   (`Bc2ReloadFlowRuntime.cpp:884-995`). This lets an empty gun wait for a physical reload.
4. A physical seat (`ReloadInsertion` → `ManualReload` request) triggers `SubmitMagazineRequest`. The hold is
   released, native state 12 calls the transfer, and the `Transfer` and `Restore` hooks (`:1116-1137`) build a
   conserved **receipt**. That receipt is the only completion evidence. Client prediction rollback is handled through
   a typed Restore receipt (`docs/RELOAD-20261005-HEADSET183.md`).
5. On cancel, a native abort helper (`AbortFn`, `:230`) returns all three copies to idle. A large "prehold" cleanup
   subsystem handles cancellation **before** the hold is established (`FVR_BC2_PREHOLD_CLEANUP`, 22 `#if` blocks in
   the file).

Consequences:
- Ammo is pooled BC2-style: a partial magazine's rounds stay in the pool (`MAGAZINE-PROFILES-20261003.md`).
- The native reload animation and timer still exist under the VR presentation. The right-hand weapon pose is
  VR-driven. The left hand and magazine are overridden through the palette.
- The native reload begins before the physical hold. This ordering is the documented root of several
  regressions: "Native reload entry precedes the hold… The animation can then continue without insertion"
  (`WEAPON-SYSTEM-PIPELINE.md`, audit item 4). It also caused the SPAS "takeover", where three extra native shell
  transfers happened after cancellation (`MANUAL-RELOAD-20261001.md`, trace013308).

### 1.3 State machines

Portable:
- **HandInteraction** (`HandInteraction.h:19`): claim kinds `GunHold, WeaponSupport, Sight, Mechanism, AmmoObject,
  BodyInventory`. Tokens and generations arbitrate both hands. This is a good foundation.
- **AmmoSupply** (`AmmoSupply.h`): grabbing at the body pouch creates an `AmmoSupplyObject` with a reservation.
  Only a native receipt resolves it. Unresolved reservations are quarantined.
- **ReloadInsertion** (`ReloadInsertion.h:12-95`): `Free → Guided → Seated`. Approaches: `AxialFront`,
  `FrontHemisphere`, `EntrySphere`, `RailContact` (magnetic capture along the whole path, then a required fresh
  inward stroke). Keyed vs `AxialSymmetry` orientation. Tolerances are per profile: capture/release distance and
  angle, `postCaptureTravel`, `seatTolerance`, `alignmentNs`, `seatDwellNs`, step/jump limits.
- **ManualReload** (`ManualReload.h:10-195`): an ordered plan of `UnseatMagazine, SeatMagazine, InsertRound,
  CycleAction, OpenBreech, CloseBreech`. Phases are `Idle → Active → AwaitingAcknowledgement → Complete`, with 15
  cancel reasons.
- **DetachableMagazine** (`DetachableMagazine.h:55-57`): `Attached, PreparingRemoval, Pulling, RemovedHeld,
  WellEmpty, ReplacementHeld, Guided, AwaitingSeat, Complete, Cancelled, AwaitingOriginalReturn`, with 20 reject
  reasons. The hand is fixed to **Left** (`:8`).
- **WeaponCycle** (`WeaponCycle.h:9-10`): pump (rear → forward) and bolt (unlock → rear → forward → lock). **Not
  wired to gameplay.**
- **FeedMechanism** (cover, latch, container, seat, charge) and **WeaponMechanism** (feed × action × chamber plan
  selector). Both have **no production consumer** (`WeaponMechanism.h:6-8`; `WEAPON-SYSTEM-PIPELINE.md`, item 5).
- **BodyInventory** (`Unavailable, Held, Empty, AwaitingHolster, AwaitingDraw`) and Bc2 `BodyHolsterPhase`
  (`Bc2BodyHolster.h:23`). Accepted only for SPAS and scoped XM8 (`Bc2BodyHolster.h:21`, mask 3).

BC2:
- **ReloadRequestCycle** (`Bc2ReloadRequestCycle.h:17-26`): `Disabled, Idle, Arming, Holding, Advancing, Finished,
  Cancelled`. `AdvanceNs` is 1.5 s (`:63`).
- **Bc2ReloadNativePolicy** (`Bc2ReloadNativePolicy.h`): exactly one active family. `ReloadNativeFamily` is
  `{SpasTube, Xm8Magazine}` only (`Bc2ReloadFamily.h:4`).
- **Bc2MagazinePhysicalReload** (`Bc2MagazinePhysicalReload.h:13-16`): 15 cancel causes. `MagazinePhysicalApi` has
  18 function pointers (`:17-35`).
- **Bc2PhysicalReload** handles the SPAS shell path, including haptic emission (`Bc2PhysicalReload.cpp:279,320`).

### 1.4 Rendering of magazines and shells

- **Held, attached or removed magazine**: this is the weapon's **own magazine bone** (XM8 `jntWpn_6`) moved inside
  a private copy of the skinning palette. The swap happens before each native Pack. `HideMagazinePackedPalette`
  collapses the bone. The left wrist and 15 finger transforms come from a captured native reload-animation row
  (XM8 row 57). Left-arm IK solves to that wrist (`Bc2MagazinePresentation.h:62-71`;
  `XM8-MAGAZINE-INTERACTION.md`, "Measured profile").
  - Because only one magazine mesh exists, the old and new magazines can never both be visible. A released old
    magazine **disappears**: `WellEmpty` → `MagazinePropRole::Hidden` (`DetachableMagazine.cpp:253,271,320`).
  - The fallback machinery (`MagazineFallbackReason`, 9 reasons, `Bc2MagazinePresentation.h:73-80`) is what the
    docs mean by "visual fallbacks": when a guard fails, the ordinary native palette is used.
- **SPAS shell in hand**: the weapon's `jntWpn_7` shell bone, with a visibility exception for that natively
  collapsed leaf (`Bc2ReloadPresentation.h:45-59`). Again only one shell can be visible.
- **Chest and shoulder props**: a separate `D3D11RigidPropRenderer` draws geometry extracted from installed mesh
  sections (`Bc2BodyAmmoAssetProfiles.h`). The shader is **untextured flat tint × |normal.y|**
  (`D3D11RigidPropRenderer.cpp:14-18`), so these props are grey silhouettes, not native-material magazines.
- **Pump fore-end**: a private palette edit of `jntWpn_4` (`Bc2PumpPart`). It is groundwork only.

### 1.5 Per-weapon authoring data and how it is generated

- Tools: `bc2_granny_resource.py`, `bc2_granny_curves.py`, `bc2_mesh_geometry.py`, `inspect_bc2_mesh_asset.py`,
  `bc2_authored_magazine_geometry.py`, `bc2_magazine_contact_batch.py`, `bc2_authored_mechanism_geometry.py`,
  `bc2_authored_grip_bindings.py`, `bc2_authored_hand_pose_batch.py`, `bc2_weapon_config_pipeline.py`,
  `bc2_weapon_animation_pipeline.py`, `bc2_weapon_package_index.py`, `bc2_weapon_component_closure.py`,
  `bc2_weapon_capability_report.py`, `bc2_magazine_registry_header.py`, `bc2_body_ammo_assets.py`.
- Flow: the user's installed archives go through DBX config + Granny animation/mesh decoding into an exact
  configuration package (path/hash). Component closure (grip, magazine contact, body) produces generated C++
  headers. Those headers flow into the native registry, which needs a separate "ReviewedNative" admission per
  config (`Bc2MagazineNativeProfile.h:15-27`).
- Geometry per magazine weapon (`Bc2MagazineGeometryProfile`; example in
  `profiles/checkpoint202/Bc2ExperimentalMagazineGeometry.h`): rig fingerprint, magazine/hand bone names, insertion
  profile (`itemFromHand`, `itemFromInsertion`, `weaponFromEntry`, travel and tolerances), attached transform and
  15 finger transforms. The AEK file says "Carry/rail are design estimates".
- Native registry: built-ins are **XM8 and AEK only** (`Bc2MagazineNativeRegistry.cpp:9-11`). Generated rows
  compile but are **disabled** (`MAGAZINE-PIPELINE-COVERAGE.md`: "zero reviewed/enabled rows").
- Coverage stats: 35 exact configurations indexed, with 11 missing backlinks; 20 grip and 10 magazine-contact
  records joined (`WEAPON-SYSTEM-PIPELINE.md`). 112 AR/SMG/LMG descriptors exist, and 7 names have measured
  magazine contacts (`MAGAZINE-PIPELINE-COVERAGE.md`).

---

## 2. Coverage matrix

Roster source: `profiles/weapon-family-work-plan.json` (44 items) and `docs/LAUNCHER-RELOAD-PLAN.md`.
Legend: **P** = playable behind a flag with native verification; **X** = experimental, offline or partial;
**—** = missing.

| Family / mechanism | BC2 weapons | Feed interaction | Action / chamber | Native binding | Status |
|---|---|---|---|---|---|
| Detachable box magazine, AR/carbine | XM8 (scoped), AEK, F2000, AUG, AN-94, M416, M16A2, 9A-91, SCAR-L, XM8c, G3, M14 | Eject, pull, grab from chest, rail insert, original return | Charging handle: none. Chamber: none | XM8 reviewed and simulated-player verified (cp202); AEK reviewed, no runtime test; others need geometry and admission | **P** XM8 only; AEK **X**; rest **—** |
| SMG box magazine | UMP, PP-2000, UZI, AKS-74U | Same consumer | None | 9A91/AKS74u/UZI deferred over stock bolt fields (`MAGAZINE-PIPELINE-COVERAGE.md`) | **X** (offline mocked) |
| Magazine-fed LMG / drum | XM8 LMG, MG36 | Same consumer; drum geometry agent | None | Mocked offline only | **X** |
| Belt-fed LMG | PKM, M249, Type 88, M60, MG3 | Plan only (`BELT-FED-RELOAD-PLAN.md`) | Cover, latch, belt and charge in `FeedMechanism` with no consumer | None | **—** |
| Shell tube + pump | SPAS-12, 870, NS2000 | SPAS: chest shell, bottom-entry magnetic rail, one-by-one native transfer; headset-accepted once (`MANUAL-RELOAD:…030952`) | Pump: `WeaponCycle` + `PumpPart` + finite state 7/8 hold diagnostic; **not playable** | SPAS only | SPAS feed **P**; pump **X**; 870/NS2000 **—** |
| Semi-auto shotgun | Saiga, USAS-12 | Box magazine (data-assigned) | — | — | **—** |
| Bolt-action sniper | M24, SV98, GOL, M95 | Magazine or clip unknown | `WeaponCycle` bolt policy, unwired | None | **—** |
| Semi-auto marksman | Type 88 sniper, SVU, VSS | Magazine | — | — | **—** |
| Pistols | M9, M1911, MP443, M9-3 | Magazine | Slide rack/release requested (`WEAPON-MECHANISM-NEXT.md`) | — | **—** |
| Revolver | MP412 | Cylinder (`revolver_cylinder`) | — | — | **—** |
| Underbarrel 40 mm | XM8/XM320, GP30, M203 (×3 ammo kinds each) | Plan only; stock reload retained | Breech unmeasured | Zero-loaded ownership unsolved (`LAUNCHER-RELOAD-PLAN.md:12-14`) | **—** (sight interaction **P**) |
| Rockets | RPG-7, Carl Gustaf, M136 | Plan only | — | — | **—** |
| Clip / en-bloc | Garand (excluded), Tracer dart | — | — | — | **—** |
| Cross-cutting | — | Chest pouch (SPAS, XM8): **P**. Shoulder holsters (SPAS, XM8): **P** | Chamber state, physics drop, magazine haptics, sounds, left-handed mode: **—** | — | — |

In short, 1 magazine weapon and 1 tube weapon work end to end, out of roughly 44 roster items. No action mechanism
(pump, bolt, slide or charging handle) is playable. No chamber model exists.

---

## 3. Critique against the AS2-quality goal

### 3.1 What is genuinely good (keep)

1. **The hand-claim arbiter** (`HandInteraction`) and the separation of raw and guided poses. This matches AS2's
   priority between grab and support grip.
2. **The insertion maths** (`ReloadInsertion.h`): RailContact magnetic capture, a required real inward stroke,
   keyed vs symmetric alignment, and per-profile tolerances. This is the core of good-feeling insertion, and
   tolerances are already data.
3. **Native research**: state-machine meaning, transfer function, three-copy model, abort helper,
   capacity/multiplier, listener callbacks (`BC2-PUMP-RELOAD-20261001.md`). It is accurate, and any design needs it.
4. **The asset pipeline**: Granny/mesh decoders, authored geometry extraction, exact-config package index and
   generated headers. Everything is built from the user's installed files, so no game assets are redistributed.
5. **The private palette path** for moving or hiding native bones without touching native memory, plus
   finger-pose capture from native animation rows.
6. **The simulated-player fixture and audit tooling** (`SIMULATED-PLAYER-20x`, `tools/audit_*.py`). These make
   regressions reproducible without a headset.

### 3.2 Architectural problems

**A. Gating the native reload is the root cause of most of the fragility.**
Every physical reload is a negotiation with a timed native state machine running in three copies, across several
threads.
- The native reload must be entered first (the Reload pulse), so a pre-hold window always exists. A whole subsystem
  covers it: `Bc2Prehold*` (6 headers) plus `#ifdef FVR_BC2_PREHOLD_CLEANUP` in `Bc2ReloadFlowRuntime.cpp:1007-1101`.
- The hold must be renewed every native Update under policy-lock contention. One example: a 152 µs lock miss
  enqueued a global cancellation (`WEAPON-SYSTEM-PIPELINE.md`, "Repeat02").
- Cancellation must call a native abort helper that only works with positive loaded ammo. That is why launchers
  are blocked (`LAUNCHER-RELOAD-PLAN.md:12`).
- Client prediction and rollback must be reconciled (headset183 `magazine-reserve185-02`).
- Fire during a hold must be deferred (`RELOAD-FIRE-CANCEL-20261002.md`).
- After a cancellation, the native loop kept loading shells by itself (`MANUAL-RELOAD-20261001.md`, "REPRODUCED
  THE TAKEOVER").

Result: the latest **human** feedback is still "broken rifle reloads" and "manual reload remains inconsistent"
(`STATUS.md`; `RELOAD-20261005-HEADSET183.md`). AS2 and similar games own weapon state themselves. The physical
action is the reload, and no engine timer exists to race against.

**B. Cancellation-first guard design.**
Every consumer carries leases, deadlines, sequence watermarks and generation checks. Any failed check cancels the
operation:
- 20 reasons in `DetachableMagazine.h:58-63`
- 15 in `MagazineCancelCause`
- 15 in `ManualReloadCancel`
- 19 in `ReloadInsertionReason`

A momentary claim-renewal miss cancelled a magazine before seating (`magazine-prediction186-01/02`: "removal
cancelled with InteractionRejected/ClaimLost"). From the player's side, the magazine vanished from the hand. AS2's
rule is the opposite: an object in your hand stays in your hand, and transient tracking gaps degrade (freeze or
smooth) instead of cancelling.

**C. Complexity relative to delivered coverage.**
About 21k LOC (15k BC2 + 6k portable) delivers two weapons.
- `Bc2ReloadFlowRuntime.cpp` (2,034 lines) mixes four things: hook trampolines, policy decisions, diagnostic
  recording, and the prehold monitor. It uses 48 atomics and 22 preprocessor blocks.
- `Step` alone (`:884-995`) is about 110 lines in which diagnostics are interleaved with the one-byte control write.
- `MagazinePhysicalApi` has 18 callbacks.
- `Bc2Gameplay.cpp` still branches on asset name (`:1154`, `:1256`). Family selection is global, one family at a
  time (`Bc2ReloadNativePolicy.h`, `Family()`).

This is the "per-gun state machine" problem the docs warn against, moved into a native-negotiation layer instead.

**D. Admission gating blocks rollout.**
Each config needs `ReviewedNative` plus `ReviewedReload11Transfer12` cycle admission
(`Bc2MagazineNativeProfile.h:15-27`) and a native test before the shared consumer will run it. Generated rows ship
disabled. That is rigorous, but it means 40+ weapons each need a native audit. A design that does not depend on
per-weapon native timing (see §4) removes most of that.

**E. Missing AS2-level features** (all confirmed absent):

| Feature | Evidence |
|---|---|
| Chamber state / round-in-chamber, slide lock, charging handle, slide rack | `WeaponMechanism.h:81` ("Handle/slide recognizers are deliberately not implemented"); `BC2-PUMP-RELOAD:77-80` (no native chamber) |
| Manual pump / bolt | `WeaponCycle` unwired (`WEAPON-SYSTEM-PIPELINE.md`, item 5) |
| Dropped magazines with physics | Released magazine becomes `Hidden` (`DetachableMagazine.cpp:253`). There is no physics code anywhere (grep for gravity/velocity/rigid body finds nothing) |
| Two visible magazines (old falling, new in hand) | Single-bone presentation (§1.4) |
| Native-looking chest magazines | Flat-tint shader (`D3D11RigidPropRenderer.cpp:17-18`) |
| Magazine haptics | `Bc2MagazinePhysicalReload.cpp:266` forces `haptic=None`. `FeedbackKind` has only `ReloadCapture`/`ReloadApplied` (`Feedback.h:8`), emitted only by the shell path |
| Sounds (mag out/in, rack, shell insert, dry fire) | No audio code in the interaction or BC2 adapter. The native reload sounds follow the frozen native timeline, not the hand |
| Left-handed / either hand | `DetachableMagazineConfig::hand=Left` (`DetachableMagazine.h:8`); `AmmoSupplyConfig::hand=Left` |
| Per-magazine round counts | BC2 pooled model only. Acceptable for BC2, but AS2 tracks per-mag counts (optional, see §4) |
| Tolerance tuning workflow | Tolerances are hard-coded C++ in generated headers and need a rebuild. There is no live tuning and no in-headset authoring |
| Hand poses on chest items, shells or pistol mags | Only XM8/AEK magazine and SPAS shell rows are captured |

### 3.3 Keep, rewrite, add

- **Keep**: HandInteraction, the ReloadInsertion maths and profile schema, AmmoSupply *concept*, BodyAnchors and
  BodyInventory, palette presentation, IK, Granny/mesh tooling, package index, simulated-player fixture, the native
  research, and the existing hook-installation infrastructure (signature-verified prologues).
- **Rewrite**:
  - Replace `Bc2ReloadFlowRuntime` hold/prehold/abort and `Bc2ReloadRequestCycle`/`NativePolicy` with a narrow
    **NativeWeaponBridge** that suppresses the native reload and commits ammo.
  - Replace `DetachableMagazine` + `Bc2MagazinePhysicalReload` + `Bc2PhysicalReload` with data-driven mechanism
    components on a VR-owned weapon state.
  - Cut cancellation reasons down to real ones: owner or weapon change, death, explicit drop.
- **Add**: chamber/bolt/slide model, item instances with physics, multi-instance textured rendering, haptics/audio
  event bus, live tuning, left-hand support.

---

## 4. Recommended design: VR-owned weapon state with native ammo commit

### 4.1 Core decision

Stop holding BC2's reload. Make the VR layer **authoritative for the physical weapon state** (magazine present,
rounds in magazine, chamber, bolt/slide position, tube count, cover). Treat BC2 as an **ammo ledger plus fire
gate**:
- **Native reload never runs** during VR play. Mask the Reload action, as `Bc2Gameplay.cpp:1256` already does for
  SPAS. Keep the existing Step `+0x28` empty-byte override so the empty-auto-reload is suppressed for all weapons.
- **Fire gating comes from VR state.** When the chamber is empty, magazine-out-and-chamber-empty, or the bolt is
  open, mask Fire and play a dry click. Action masking already exists (`blocksWeaponActions`).
- **Ammo commit.** On a physical seat or shell insert, move rounds natively in one atomic step on the game thread.
  Two candidate mechanisms need a spike to choose between them:
  1. Invoke the native transfer function `0x6d6f40` (`TransferFn`, `Bc2ReloadFlowRuntime.cpp:229`). It is already
     signature-verified and hooked, and it conserves reserve by native rules. Call it for the server copy inside
     the hooked Update and let client copies receive it through the normal path, or call it on all three. This is
     still the game's own accounting.
  2. If (1) fails on prediction: run the native reload with **delta scaling instead of zeroing**. When the physical
     seat happens, fast-forward state 10 → 12 in a single Update (large delta) so the transfer happens immediately.
     The hold ends, so there is nothing to cancel. The pre-seat window becomes "no native operation", which removes
     prehold, abort and takeover entirely.
- **Chamber model.** Native loaded `L` = rounds in magazine (or tube) + chamber. The VR state stores
  `chamber ∈ {empty, live, spent}` and enforces conservation: magazine count = `L − chamberLive`.
  - Tactical reload with a live chamber: the native commit transfers to capacity, giving `cap+1` for real AS2
    feel. That needs a capacity-override write (`+0x98` exists, `BC2-PUMP-RELOAD:65`). A simpler option is to
    accept `cap`.
  - Empty reload leaves `chamber=empty` until a rack, slide release or charging handle pull. No native write is
    needed for racking; it only clears the fire gate.
- **Pump and bolt.** Do not hold native states 7/8. Native cycles automatically after each shot (fine; hide its
  fore-end motion with the palette). VR state sets `chamber=spent` after each shot and gates Fire until a
  rear-then-forward gesture (pump) or unlock-rear-forward-lock gesture (bolt). This reuses `WeaponCycle` for
  recognition. Bolt-action native delay may need to be shortened via the `BoltActionTime` config read, or masked by
  presentation.

This keeps the project's main principle: **no ammo is created**, and native ammo changes only through native
transfer (option 1) or the native state machine (option 2). It deletes the timing race.

### 4.2 Components

```
WeaponInstance (portable, per carried item)
 ├─ WeaponProfile (data, generated + tuned JSON)
 ├─ FeedComponent: MagWell | Tube | BeltTray | Breech | Cylinder
 ├─ ActionComponent: Slide | ChargingHandle | Bolt | Pump | Breech | None
 │     linear/rotary constraint, stops, spring return, lock points (slide lock, bolt open)
 ├─ ChamberState {empty, live, spent}; FireGate()
 └─ events → FeedbackBus (haptic + audio)
ItemInstance (Magazine{rounds,profile}, Shell, Grenade, Rocket): pose, owner (hand/well/pouch/world), RigidBody
HandInteraction (keep) + Grabbable registry (any hand, with priority by distance and kind)
NativeWeaponBridge (BC2): ReadCounts, CommitTransfer, SuppressNativeReload, FireMask, VisibilityPalette
Presentation: palette edits for attached parts; DrawCloner for free items; HandPoseLibrary
```

- **MagWell**: wraps `ReloadInsertion` (RailContact, keyed) with per-profile tolerances. Seating triggers a commit,
  a haptic and a "mag in" sound. The release button, or a grab and pull, ejects the magazine into the hand or under
  gravity (AS2 lets it drop free).
- **Grabbables**: a magazine can be grabbed from the well, the chest pouch, the world (dropped) or the other hand.
  Either hand can grab. Support grip yields through the existing claim kinds.
- **Physics drop**: a lightweight mod-side rigid body is enough. Use gravity, linear/angular velocity inherited from
  hand release, and a ground plane from the player's feet height or a native raycast if one is found. Despawn after
  N seconds or N items. No native physics is needed.
- **Rendering (DrawCloner)**: the project already captures native `DrawIndexed` metadata (`Bc2ReloadDrawCapture`)
  and a palette per Pack. Re-issue the native weapon's magazine-section draws K extra times with a palette where only
  the magazine bone is set to each item's transform. That gives native materials and lighting, multiple instances
  (falling old magazine, new magazine, chest magazines, shells), and nothing redistributed. Fallback: extend
  `D3D11RigidPropRenderer` with textures resolved from installed `.itexture` files plus simple lighting.
- **Hand poses**: build a `HandPoseLibrary` per item kind (magazine grip, shell pinch, slide pinch, charging-handle
  hook, pump grip, bolt knob). Seed it from captured native animation rows (`bc2_authored_hand_pose_batch.py`).
  Refine it in **Blender via the official MCP**: import the rig and weapon mesh with the existing Granny decoders,
  pose the hand on each socket, and export JSON. This matches how KF2 VR was built, per the user.
- **FeedbackBus**: typed events (`MagRelease, MagGrab, Capture, Seat, SlideBack, SlideLock, SlideForward,
  ShellInsert, PumpBack, PumpForward, BoltOpen, BoltClose, DryFire`), each mapped to a haptic pulse on the right
  hand and a sound.
  - Audio plan: first try triggering native sound events. The listener forwarders at `0x80f640`/`0x80f6b0` show the
    native event path. Otherwise play sample files decoded at install time from the user's BC2 sound banks through
    XAudio2.
  - Extend `Feedback.h:8` beyond two kinds and remove the magazine haptic zeroing at
    `Bc2MagazinePhysicalReload.cpp:266`.
- **Tuning**: load tolerances, sockets, travel, spring and haptics from JSON at runtime with hot reload, plus an
  in-VR debug overlay showing capture capsules. Generated headers stay as defaults only.

### 4.3 Per-weapon data (generated, then tuned)

Profile = { exact config path/hash, rig fingerprint, feed kind, action kind, magazine bone/sections, magWell entry +
travel + tolerances, action part bone + axis + stops + lock points, ejection port, sockets for each hand pose,
capacity source, chamber policy (`tacticalRetainsChamber`), sound/haptic set }.

Generation path:
1. The existing pipeline produces mesh sections, bones and rig.
2. A Granny animation pass picks the native reload clip frame where the magazine is out, which gives travel
   direction and length.
3. The native bolt/slide clip gives axis and stops (as done for SPAS `jntWpn_4`: 94.96 mm along Z).
4. Hand-pose rows come from the same clips.
5. Blender MCP review for outliers.

Weapon families then become data plus a few component kinds, with no per-weapon native admission. Native validation
is needed **once per mechanism**: commit transfer for `rtMagazine`, commit for `rtSingleBullet`, fire mask, and
empty-reload suppression.

### 4.4 Phased plan

| Phase | Deliverable | Exit test |
|---|---|---|
| **0. Spike (decision gate)** | NativeWeaponBridge prototype on XM8: suppress native reload, fire mask, commit via (1) direct transfer call or (2) single-Update fast-forward. Measure three-copy agreement and prediction behavior with the existing simulated-player fixture | 50 repeated reloads with conserved ammo, zero native reload states entered (option 1) or zero holds (option 2), and no extra transfers |
| **1. Core runtime + rifles** | WeaponInstance, MagWell, ChamberState, ChargingHandle; FeedbackBus (haptics); either hand; JSON tuning; drop-to-hidden is fine for now | XM8 and AEK: tactical and empty reload, charging handle needed only when empty; headset feel pass |
| **2. Items and visuals** | DrawCloner multi-instance magazines; physics drop; textured chest pouches; audio | Old magazine falls visibly while the new one is in hand; both eyes correct; 90 fps budget |
| **3. Pistols and slide** | Slide component (rack, lock back on empty, release); M9/M1911/MP443/M9-3 profiles | Slide locks on empty and releases on seat or rack |
| **4. Shotguns, pump and bolt** | Port SPAS shell feed onto Tube + MagWell code; Pump action gate; 870/NS2000; Bolt for M24/SV98/GOL/M95 (with magazine or clip feed per data) | Pump/bolt required between shots; shell-by-shell; no takeover possible since no native hold |
| **5. Batch rollout** | Run the pipeline over all ~25 magazine weapons; drum/MG36/XM8 LMG; semi-auto shotguns; marksman rifles | A per-family representative headset check plus an automated sweep of the remaining weapons |
| **6. Belt-fed, launchers, revolver** | BeltTray (cover open → box swap → belt seat → close → charge); Breech for 40 mm (zero-loaded case is trivial once there is no native hold); rockets; MP412 cylinder | Family representatives pass |
| **7. Polish** | Holster/pouch ergonomics, comfort options (auto-chamber, assisted insertion strength), death/respawn/vehicle transitions, release packaging | User headset acceptance |

### 4.5 Risks and mitigations

- **Direct transfer call may desync client prediction.** Mitigation: option 2 (fast-forward) keeps all native
  paths. Both reuse the existing Restore receipt logic.
- **The native 1P reload animation will not play.** That is desired, since the VR hands replace it. Native reload
  sounds are lost and must come from the FeedbackBus.
- **`cap+1` requires a capacity override.** Ship `cap` first.
- **DrawCloner depends on stable draw identification.** Section identity is already fingerprinted
  (`SPAS-RELOAD-PRESENTATION`); the fallback is a textured rigid-prop renderer.
- **Losing the "never write native state" rigor.** Keep exact-config identity checks and conservation receipts on
  the commit path only. Drop them from hand-interaction continuity.
