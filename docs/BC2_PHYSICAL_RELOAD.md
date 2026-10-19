> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).
> Follow-up: [current architecture](ARCHITECTURE.md) and [status](STATUS.md).

# BC2 physical reloads: chamber, magwell slider and motion guard

Status: **wired on `feat/feel-wiring`, offline only.** `feel/chamber-and-insert`
added three portable policies under `include/fvr/interaction`; `feat/feel-wiring`
connects them to the BC2 magazine adapter (see
[Adapter wiring](#adapter-wiring-featfeel-wiring)). The XM8 profile and the
generated experimental rows use the slider; SPAS shells stay `Legacy`. No BC2
native address, hook, reload start or ammunition write changed. Nothing has been
played in a headset or built on Windows; the numbers below are design defaults,
not BC2 acceptance.

The three pieces come from the KF2 VR mod (`/home/claude/kf2-vr`, commit
`43452e24`), which took its feel from shipped Arizona Sunshine 2 data:

| FrostbiteVR | KF2 VR source | AS2 source |
| --- | --- | --- |
| `MagazineFeedState.h` (chamber + magazine presence, open-bolt exception) | `native/adapter/MagazineFeedState.h`, `EmptyMagazineState.h`, `tests/MagazineFeedStateTest.cpp` | Slide-lock and chamber rules, KF2 `docs/re/ARIZONA_SUNSHINE_2_RELOADS.md` (slide lock section) |
| `ReloadInsertion.h` `ReloadInsertionFeel::Slider` | `script/KF2VR/Classes/VRReloadInsertGuide.uc` | `GameSettings` magazine assistance fields |
| `MotionGuard.h` | `script/KF2VR/Classes/VRReloadMotionGuard.uc` | (KF2's own tuning) |

## What BC2 changes about the chamber

KF2 has a stock weapon class per gun and a script-visible `AmmoCount`. BC2 has
neither a chamber count nor a separate magazine count. Its firing state keeps
one loaded count per copy (`+0x7c`), three copies per weapon (two client
branches and the server item), and a reload runs 2 ready → 10 begin → 11 wait →
12 transfer, where state 12 moves the missing capacity from reserve `+0x80`
([pump notes](BC2-PUMP-RELOAD-20261001.md)). The mod already holds state 11
until a physical seat and treats the transfer receipt as the only proof of
completion (`Bc2ReloadFlowRuntime.cpp`, `RunReloadDeltaOverride` and the Transfer/Restore receipts).

So the chamber is VR-side metadata, exactly as in KF2, and the native loaded
count is the snapshot `MagazineFeedState` observes:

- `Observe(loaded)` once per **fresh** three-copy lease (`Bc2AmmoReserveLease`
  from `reloadFlowRuntime::ReadReserve`). A drop confirms a real shot; an
  increase with the magazine in and no rack pending is ordinary native feeding.
- `Eject` when the magazine is physically out, `Seat` on the physical seat,
  `Rack` on a completed charging-handle/slide/bolt stroke, `Reset` when the
  physical feature lets go of the weapon (owner, equip generation or family
  change, explicit cancel back to stock reload).
- `EmptyChamber` when the native count reaches zero inside a hold, `Shot` when
  an ammo-neutral shot is confirmed with the magazine out, `ShotFlushed` after
  the adapter has stopped the auto-refire.

Nothing returns a round. Seating never chambers; only `Rack` does. A rack with
the magazine out cannot feed. Duplicate events are idempotent. These are the
392 KF2 checks, ported unchanged in `tests/MagazineFeedStateTests.cpp` (417 with
the typed action-kind checks).

**BC2 caveat, not solved here:** KF2 lets the stock gun fire its retained
round with the magazine out. In BC2 the magazine-out window is native state 11
(reload held), during which the weapon cannot fire and the mod masks
`Fire` (`Bc2MagazinePhysicalReload.h:65-74`). A retained-chamber shot therefore
needs the adapter to remove the magazine *before* entering the native reload
and start the hold only when the replacement is offered, or to abort and
re-enter. Until that ordering exists, use `MagazineFeedState` for the rack gate
and the HUD (`DisplayAmmo`), and keep magazine-out fire blocked.

## Per-weapon action defaults (BC2 roster)

Weapon names and families are from `profiles/weapon-family-work-plan.json`.
The action kind is the per-weapon flag `MagazineActionKind`, numbered as KF2's
`VRReloadCatalog.ActionKind`. "Locks open" is the real firearm's last-round
hold-open, which decides whether an empty reload ends on a slide/bolt release
(AS2 `slideLockOnEmptyMag`) or a full rack. Both columns are real-mechanism
defaults: BC2's native has no lock state, and whether each gun's Granny empty
reload clip shows a lock, a release or a charging-handle pull still needs
checking against the decoded clips before a row is admitted.

| Family (work plan) | BC2 weapons | `MagazineActionKind` | Locks open on empty | Empty reload ends with |
| --- | --- | --- | --- | --- |
| Pistols | M9, M9-3, M1911, MP443 | `ClosedBolt` | Yes (all four) | Slide release or rack |
| AR / carbine, AR-15 pattern | M16A2, M416, SCAR-L | `ClosedBolt` | Yes | Bolt release or charging handle |
| AR / carbine, other hold-open | XM8, XM8 Compact, F2000, AUG | `ClosedBolt` | Yes (G36-pattern catch on XM8; AUG and F2000 hold open) | Charging handle |
| AK pattern | AEK-971, AN-94, AKS-74U, 9A-91, VSS | `ClosedBolt` | No | Charging handle (rack) |
| Battle rifles | M14 / Mk14 EBR | `ClosedBolt` | Yes | Op-rod handle |
| Battle rifles, roller-delayed | G3 | `NotchLock` | No (handle notched by hand) | Slap the handle out of the notch |
| SMG, closed bolt | PP-2000 | `ClosedBolt` | No | Charging handle |
| SMG, closed bolt with notch | UMP | `NotchLock` (as KF2's UMP) | No | Handle out of the notch |
| SMG, **open bolt** | UZI, Thompson | `OpenBolt` | Bolt stays back after each shot | Nothing if already cocked; cock before or after seating |
| LMG, box/drum | XM8 LMG, MG36 | `ClosedBolt` | Yes (G36 family) | Charging handle |
| DMR / semi sniper | Type 88 sniper, SVU | `ClosedBolt` | No | Charging handle |
| Shotgun, box magazine | Saiga, USAS | `ClosedBolt` | Saiga no; USAS yes | Charging handle |
| Belt-fed (`FeedMechanism`, not this policy) | PKM, M249, Type 88 LMG, M60, MG3 | `OpenBolt` (for reference) | n/a | Cover, belt, charge |
| Bolt-action sniper (`WeaponCycle`) | M24, SV98, GOL, M95 | n/a: every shot needs a bolt cycle | Bolt left open is a player choice | Bolt cycle |
| Pump tube (`WeaponCycle` + shell insertion) | 870, SPAS, NS2000 | n/a: no detachable magazine | n/a | Pump |
| Revolver | MP412 | `NoAction` if ever modelled | n/a | Closing the cylinder |

Open-bolt rules, from KF2 and unchanged: an open bolt may remove a loaded
magazine and shows zero (no chamber round exists); it may be cocked without a
magazine; cock-then-seat and seat-then-cock both end ready without a second
cock; removing an interrupted, never-cocked load cannot cock it. Every other
kind refuses a loaded no-chamber removal and keeps stock policy.

## Magwell slider (insertion feel)

`ReloadInsertionProfile::feel = ReloadInsertionFeel::Slider` turns the insertion
recognizer into AS2's magazine slider as KF2 decoded it (AS2 `MovableHandle`,
`OnFullyClosedEvent`, `forceReleaseOnDistanceFromHandle=0`):

- **Depth follows the hand** along the profile's travel axis, both ways,
  clamped to `[0, travel]`.
- **Locks at depth.** The seat event fires on the first sample at
  `travel − seatTolerance`. Legacy also requires full alignment (XM8: 120 ms)
  and a dwell (XM8: 60 ms); the slider does not.
- **Overshoot seats.** Past the latch is full depth. Legacy cancels
  (`ContactLost` past `travel + releaseDistance`, and `PoseJump` for a fast
  step); a slam now seats.
- **Release ≥ 25% finishes.** Letting go after a deliberate partial insertion
  starts a timed settle from exactly the last guided pose, then emits the
  ordinary seat (and `ReloadInsertionHaptic::Seated`). Letting go earlier still
  cancels `Released`, so a magazine dropped near the well falls.
- **Hand rotation is free** once captured. Radial breakaway
  (`releaseDistanceMeters`) and withdrawal still release it.
- `RailContact` profiles (SPAS shells) must first make their real
  post-capture stroke; a mouth capture alone cannot auto-finish.

| Tuning (`ReloadInsertionSliderTuning`) | Default | Source |
| --- | --- | --- |
| `releaseFinishProgress` | 0.25 of insertion depth | AS2 `GameSettings.magInsertOnGrabReleaseThreshold` |
| `minAutoInsertNs` / `maxAutoInsertNs` | 40 / 250 ms | AS2 `minMagAutoInsertTime` / `maxMagAutoInsertTime` |
| `autoInsertNsPerRemaining` | 250 ms × remaining depth, clamped to min/max | KF2 `VRReloadInsertGuide.Release` |
| `curveOutTangent` / `curveInTangent` | 2.77873087 / 0.0617494546 | AS2 `magFromWorldAutoInsertTweenCurve` keys (0,0),(1,1), `weightedMode=0` |
| `motion` | 10 m/s + 12 cm slack, 150 ms gap | KF2 insert guide `FMax(12, 1000·DT)` UU, DT ≤ 0.15 s (UU = cm) |

The curve is evaluated as Unity's cubic Hermite segment (fast start, soft
stop: 54% at a quarter of the time, 84% at half). With those tangents it is
monotonic; validation keeps any retune monotonic (both tangents in [0, 3]).
Settle times: release at 25% → 187.5 ms, 50% → 125 ms, ≥ 84% → 40 ms.

During the settle the result carries `settling=true`, `rawItem == guidedItem`
(the hand no longer drives it), and the recognizer ignores grip, hand pose and
hand tracking. Focus, gun tracking, eligibility, explicit cancel, both claims,
their leases and a frame gap over 150 ms still cancel it. A whole-insertion
`maxGuidedNs` timeout does not apply to the bounded settle.

## Motion guard

`MotionGuard` is KF2's continuity check in metres and nanoseconds: a sample is
refused if it is non-finite or out of bounds, goes back in time, arrives more
than 150 ms after the last, repeats a timestamp at a new position, or moves
further than `slack + speed × dt`. Any refusal resets it; only a fresh `Begin`
resumes, so a tracking jump can never become insertion progress.

| Config | Speed | Slack | Gap | KF2 |
| --- | --- | --- | --- | --- |
| default | 4 m/s | 2 cm | 150 ms | `VRReloadMotionGuard` defaults (400 UU/s, 2 UU) |
| `Insertion()` | 10 m/s | 12 cm | 150 ms | insert guide envelope |
| `Rack()` | 15 m/s | 2 cm | 150 ms | `RackMotion`/`LeverMotion`/`LockMotion` 1500 UU/s |

In a guided slider the guard replaces the per-sample translation cap
(`maxStepMeters`, XM8 7 cm), which a slam at 90 Hz exceeds; the rotation cap
stays. A refusal cancels with `ReloadInsertionReason::MotionRejected`.
`TrackedSampleGapNs` names the 150 ms rule that `ActionPolicy`, `AmmoSupply`,
`TemporaryHeldFire` and `GroundPickup` still spell as a literal.

## What the BC2 adapter must wire

The list as written for `feel/chamber-and-insert` (line numbers at checkpoint
202). Each item is now done on `feat/feel-wiring`; see the next section.

1. **Opt a profile in.** Set `p.feel=ReloadInsertionFeel::Slider` for XM8 in
   `profiles/checkpoint202/Bc2ExperimentalMagazineGeometry.h:28` (beside
   `seatDwellNs`) and in the shared magazine default
   `src/games/bc2/Bc2MagazineGeometryProfile.cpp:20`. Leave SPAS
   (`src/games/bc2/Bc2ReloadInteraction.cpp:33`) Legacy until its RailContact
   path is replayed. The generator
   `tools/bc2_authored_magazine_geometry.py:530` and
   `tests/test_bc2_authored_magazine_geometry.py` must emit/accept the field or
   generated headers drift.
2. **Latch the hand through a settle.** Releasing the grip drops claims in two
   places before `ReloadInsertion` sees the release:
   `HandInteraction` via `released[0]` from squeeze ≤ 0.35
   (`src/games/bc2/Bc2Gameplay.cpp:615`, `:621`), and `AmmoSupply::Update`
   (`src/interaction/AmmoSupply.cpp:99-100`) via the grip passed at
   `src/games/bc2/Bc2MagazinePhysicalReload.cpp:354` and `:310`. While
   `last_.interaction.insertion.settling` (or the insertion's
   `FinishesOnRelease()` on the release frame) is true, keep `released[0]`
   false and pass the AmmoSupply grip as held, but keep
   `DetachableMagazineSample::gripPressed` (`:314`, `:364`) the **physical**
   grip so the recognizer sees the release. The presentation check
   `src/games/bc2/Bc2MagazinePresentation.cpp:54` (`!released[0]`) then keeps
   the replacement drawn through the settle. `ReloadGripActive`
   (`Bc2Gameplay.cpp:1192`, `:1223`) needs no change.
   `DetachableMagazine` already keeps the original magazine's claim
   (`src/interaction/DetachableMagazine.cpp`, `originalSettling`).
3. **Reserve after a settled seat.** `AmmoSupply::ReserveFrom` needs the held
   object and current claims, which step 2 preserves; the seat arrives on a
   frame where the grip is physically open, so check the existing
   `last_.seat` handling (`Bc2MagazinePhysicalReload.cpp:379`) does not also
   require `s.gripPressed`.
4. **Chamber state.** Own one `MagazineFeedState` and, for open bolts and
   notch locks, one `EmptyMagazineState` per weapon identity in
   `Bc2MagazinePhysicalReload`. Observe the fresh lease at `Tick`
   (`Bc2MagazinePhysicalReload.cpp:92`, `reserve->loaded`); Eject on
   `last_.physicallyRemoved` (`:377`); Seat on the native receipt (`:342`,
   `++completed_`), not on the geometric seat; Reset on `cancel(...)` and on
   owner/weapon change. Add a `blockFire` verdict next to
   `blocksWeaponActions` (`Bc2MagazinePhysicalReload.h:46`) and apply it only
   in `ApplyMagazinePhysicalActions` (`:65-74`), for loaded > 0 with
   `NeedsRack`, never on the native empty path the mod already controls.
   `Rack` needs a consumer for the charging handle; `WeaponCycle` exists but is
   not wired.
5. **Per-weapon catalog.** Add `MagazineActionKind` and a lock-open flag to
   the magazine equipment profile (`MagazineEquipmentProfile`, used at
   `Bc2MagazinePhysicalReload.h:65`) from the table above, admitted per weapon
   with the rest of the reviewed native row.
6. **Haptics.** `ReloadInsertionHaptic::Seated` is produced but nothing
   consumes it on the magazine path; `MagazinePhysicalResult` has no feedback
   array (drain beside `Bc2Gameplay.cpp:1252`, as the SPAS path does).

## Adapter wiring (`feat/feel-wiring`)

1. **Slider on.** `Bc2MagazineGeometryProfile.cpp` (XM8) and the committed
   generated header set `p.feel=Slider`. The generator emits the line from
   `INSERTION_FEEL` (kept outside `UX`, so existing source receipts stay valid).
   The committed header's only row is **AEK971_sp**, not XM8, so the generated
   AEK row moves to the slider as well; set `INSERTION_FEEL='Legacy'` and
   regenerate to undo that. SPAS (`Bc2ReloadInteraction.cpp`) stays Legacy.
2. **Hand latch.** `Bc2MagazinePhysicalReload::LatchesLoadingHand()` is true
   while `DetachableMagazine::HoldsLoadingHandThroughRelease()` (slider settling,
   or a release on the next fresh sample would settle; replacement or original).
   `Bc2Gameplay::ObserveHandOwnership` reads it before building the hand sample
   and `LatchLoadingHand` keeps `released[0]` false on fresh and duplicate
   packets. `Tick` takes the same decision from the same state and passes the
   AmmoSupply grip as `gripPressed||latched` on both the deferred and current
   paths. `DetachableMagazineSample::gripPressed` stays `ReloadGripActive` on
   the raw squeeze, so the recognizer sees the release. `tracked[0]` needed no
   change: `active&Squeeze` is the capability bit, not the squeeze value.
3. **Seat with the grip open.** The seat request at the `last_.seat` handling
   does not test `s.gripPressed`; `ReserveFrom` needs the held object and claims,
   which the latch keeps. The closed-loop test proves it: the settle seats on an
   open-grip frame, the request reserves and the receipt credits once. The
   frame after the seat drops the latch; AmmoSupply then releases the held
   object as it does after a Legacy seat, and the reservation stays pending.
4. **Chamber.** `Bc2MagazineChamber` (one per owner + weapon identity) wraps
   `MagazineFeedState` (`EmptyMagazineState` for open bolts): `Observe` on each
   new reserve sequence, `Eject` on `physicallyRemoved`, `Seat` on the native
   receipt and on the original magazine's return, `Reset` on a cancel of active
   work or an identity change. `MagazinePhysicalResult::blockFire` (loaded > 0,
   seated, NeedsRack, rack input wired) is applied only in
   `ApplyMagazinePhysicalActions` and masks Fire only. The native reload start
   is unchanged. Without a rack input the seat also racks (BC2's own empty-reload
   clip works the action), so nothing blocks Fire by default; the chamber feeds
   `MagazinePhysicalResult::chamber` (display ammo) and the gun-event log.
5. **Rack input.** `ChargingHandleRack` (grab on a fresh grip edge, pull to the
   rear stop, `MotionGuard::Rack()`). `EnableChamberRack()` runs it from the raw
   wrist in weapon space while no magazine work owns the hand and the chamber
   needs a rack. It takes no claim. **Off by default**: it needs a mapped
   `MagazineEquipmentProfile::chargingHandle` per weapon, and none is admitted.
   Before enabling it live, map the handle pose and decide whether it should
   take a Mechanism claim against support grip. BC2 has no pump, bolt or charge
   input action (only `EiaReload`, 29), so a rack that must affect the game
   would have to drive firing states 7/8 rather than send an input; today it
   only clears the VR `NeedsRack` flag.
6. **Catalog.** `Bc2MagazineActionCatalog.h` holds the table above keyed by the
   work plan's mesh name; `MagazineEquipmentProfile` gains `actionKind`,
   `locksOpenOnEmpty` and `actionVerified` (false for every row until checked
   against the Granny empty-reload clips). G3, Thompson and M9-3 have no exact
   mesh and are not listed. The fields never gate `Ready()`. Engine firing data
   comes first where it has an answer (see [Engine sources](#engine-sources-offline-mapping-unverified-live)):
   `ResolveMagazineAction` takes `ReadFiringFunctionFields` (optional
   `MagazinePhysicalApi::firing`, read once per weapon identity) and falls back
   to the catalog row for open bolt, notch lock and hold-open, which the engine
   does not encode. A bolt/pump gun (`FireLogicType` 1) resolves to `NoAction`
   for this chamber. The DLL leaves `api.firing` null until the offsets are
   checked live.
7. **Haptics.** `FeedbackKind::ReloadSeated` (0.7, 45 ms) is emitted for
   `ReloadInsertionHaptic::Seated` into `MagazinePhysicalResult::feedback` and
   drained to `writeFeedback` in Gameplay. The OpenXR host counts it as
   `seated_feedback_applied`. Host and DLL must come from the same build.
8. **Gun events.** `GunFvrTracker` writes `release` (`to: "well"`) when a
   settle starts, `haptic` (`kind: "reload_seated"`), `rack`, and a `chamber`
   row (mod model, ignored by the analyzer) on each chamber change. The sim uses
   the same row builders.

Offline: `fvr_replay_sim synth-xm8 --slider-release` records
`tools/replay/tapes/xm8-slider-release.fvrtape`; ctest
`ReplayScenarioXm8SliderSim` replays it (release at 53% → settle 120 ms → seat →
transfer → credit) through `replay_assert.py` and `analyze_gun_events.py`.

### In the game without a headset: magazine session 7

The tape above was authored against the simulator's mock geometry, so it may
miss the real XM8's magazine well when replayed live. Session 7 runs the same
`Bc2MagazinePhysicalProbe` closed loop that passed the actual-game magazine
sequence at checkpoint 202 (session 4), steering the private left hand from the
live gun and wrist poses, with one change: `ReleaseMidStroke()`. The grip opens
part-way up the replacement stroke, past the 25% threshold, and the hand stays
still. The consumer's AS2 settle must then seat the magazine, submit the seat
and receive one native transfer on its own. A settle that runs over 1 s fails
the fixture (reason 21). There is no original-magazine return and no fast-carry
challenge in this mode, so a failure points at the slider, the hand latch or the
seat-with-open-grip path (items 2 and 3 above).

Preflight is session 4's: `XM8_sp_s` in hand on foot in a campaign level,
`0 < loaded < capacity`, reserve above zero, the reload ready, and no other BC2
test running. The reserve-lease gate must verify (see `docs/WORK_LOG.md`); until
it does, the fixture waits and then fails without touching the game.

```powershell
# Receiver first, in its own window; it writes the frame-channel token to <new-folder>\channel.txt.
build\x64\BC2NativeIpcProbe.exe --output <new-folder> --pairs 240 --static-pose --async --magazine-slider-release-probe
# Then the trace, from the same build.
.\Start-NativeTrace.ps1 -Seconds 30 -FrameChannel <token> -MagazineSliderReleaseProbe -GunEvents
# Judge the run.
python tools/replay/replay_assert.py <trace>/gun-events.jsonl --scenario tools/replay/scenarios/xm8-slider-release.json
python tools/analyze_gun_events.py <trace>/gun-events.jsonl
```

The `xm8-slider-release` scenario's rules are the ones the simulator run of the
same fixture passes: `release[to=well]` once, then `seat`, the
`reload_seated` haptic, one server `transfer` and one `credit`, settle to seat
within 300 ms, seat to credit within 1 s, ammunition conserved, and no
`cancel`, `rack`, unsuppressed auto-reload or fire with the magazine out. The
trace report's `magazine_physical_probe` object holds the fixture's own phase
rows, including the `SettleRelease` phase and the depth at which it let go.

## Engine sources (offline mapping, unverified live)

From the offline engine mapping. None of this is hooked or read in the DLL yet.

- **No chamber state.** The engine has no chamber, slide-lock or
  magazine-seated data, so all three stay VR-side. Empty means loaded
  (`+0x7c`) == 0 in state 2.
- **Firing data.** `SoldierWeaponData+0x98` → `WeaponFiringData`, `+0x40` →
  `FiringFunctionData`:

  | Field | Offset | Meaning |
  | --- | --- | --- |
  | `ReloadTime` | `+0x18` | seconds |
  | `ReloadType` | `+0x20` | 0 single round, 1 magazine |
  | `FireLogicType` | `+0x24` | 0 single, 1 bolt/pump, 2 auto, 3 hold-release |
  | `BoltActionTime` | `+0x50` | seconds |
  | `BoltActionDelay` | `+0x54` | seconds |
  | `MagazineCapacity` | `+0x184` | rounds |

  `WeaponStateData.IsPumpAction` is at `+0xc0`. `Bc2FiringFunctionFields.h`
  reads the chain read-only through `WeaponModeMemory` (with optional type-name
  checks `WeaponFiringData`/`FiringFunctionData`, also unverified) and rejects
  implausible values. Next step: verify the chain and type names live on XM8,
  AEK, SPAS and one bolt rifle, then set `api.firing` in `Bc2Gameplay::Install`.
- **Input.** Only `EiaReload` (29) exists; there is no pump, bolt or charge
  action. A physical rack would drive firing states 7/8.
- **Unhooked native event sources** (do not hook yet):
  pump/cycle forwarders `0x8024a0`, `0x80f640`, `0x80f6b0`; reload begin/end
  `0x6ddb30`, `0x6ddb60`; shot listener `0x6dd960`.

## Checks on `feel/chamber-and-insert`

Linux, GCC 13, Ninja: all suites build except the four that hit the
`wchar_t` size `static_assert` (`src/games/bc2/NativeProbeConfig.h:64`); those
four pass when built with `-fshort-wchar`. New suites: `MagazineFeedState`
(417 checks), `MotionGuard`; slider cases added to `ReloadInsertion` and
`DetachableMagazine`. No game, replay or headset run.
