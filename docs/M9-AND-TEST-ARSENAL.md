> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# M9 and test arsenal — 2026-10-10

## Requested behavior

Spawn with access to all guns and switch between them without restarting the
mission. Guns with complete physical interaction profiles use physical reloads.
Other guns reload as soon as the reload button is pressed, through the normal
engine reload command and authored animation (one pulse per press; holding does
not repeat it). Weapon changes require a neutral packet before another press can
reload. `FVR_BC2_NATIVE_RELOAD_HOLD=1` restores the old 600 ms hold on those guns.

## Implemented in the isolated candidate

- The native reload input lane emits one engine reload pulse on the press edge
  (no hold). Physical short presses and existing automatic assistance
  remain available on physically managed guns.
- Pistol slide interaction separates opening from closing. Opening ejects a
  chambered round once; feeding waits for closing. Holding the slide back blocks
  firing. An inserted empty magazine locks the slide after the last shot; a
  magazine-out chambered round can still fire.
- Ordered action intents survive quick back/forward/back gestures without
  collapsing transitions. Failed claims do not release the slide. Ownership,
  tracking loss and native reload handoffs clear unsubmitted gestures.
- Slide visuals use absolute authored closed transforms plus measured travel,
  including moving descendants, avoiding addition over an already-open native
  pose. Untouched battery leaves native weapon animation available.
- Exact private M9 and M9-3 profiles join configuration, authored grip,
  magazine geometry and slide motion. They do not claim compatibility with the
  older native held-reload adapter.

The candidate is not installed or selected. Selected source remains 95556fe.
No game or headset capture was started for this work.

## Verification and limits

Five focused C++ checks passed: physical weapon state, handle/slide gestures,
physical interaction, shot ordering and native adapter separation. Controller
input checks passed after the 600 ms fallback change. The geometry generator's
31 Python checks passed. The compiled capability probe resolved both exact
pistol profiles with magazine, slide and lock-open support.

These are offline checks, not proof of M9 spawning, first-person animation
quality or the live single-player server/client replication path. Full authored
hand animation during unrigged reloads still needs an engine check against the
tracked-hand pose override. No full M9 engine input replay has run.

## All-gun spawning remains incomplete

The existing test-range helper gives weapons already loaded by the mission;
it does not load missing weapon bundles. Its native inventory has nine exposed
slots with slot-specific behavior, not an arbitrary all-gun collection. Its
startup selection watcher must also finish before user-controlled cycling.

The local catalog combines existing SP/MP configuration extracts, including
variants. It is a catalog, not a runtime inventory. M9 configuration was found
in MP common resources; availability in the chosen SP test mission remains
unproven. A pending M9 range configuration is deliberately not selected.

A subsequent index-only scan checked 61 SP archives, with one additional archive
error. Four archives contained M9 references; bounded parsing of their four DBX
candidates found zero SoldierWeaponData definitions. SP_02 had no matching M9
index reference. These references therefore do not establish a campaign spawn
route. The scan did not extract whole archive payloads or launch the game;
selected DBX resources were cached locally for the follow-up parse.

Resource-loading investigation continued beyond the mission scan. The existing
annotated Ghidra project was opened with `-readOnly -noanalysis`, verifying its
executable digest against the known local target. A bounded loader-symbol and
resource-string-reference pass saved eight function dossiers successfully in
2.8 seconds of script time. No import, whole-program reanalysis, game launch or
native loader call was performed. The dossiers are candidates for local review,
not proof of a callable resource-loading interface. Calling convention, thread
context, asynchronous completion and retain/release ownership remain unresolved.

Next implementation work:

1. Resolve the native weapon bundle/resource loading and retention path locally.
2. Implement an arsenal above native slots, with verified give/replace lifetime
   handling and persistence of ammo, magazine and chamber state per gun.
3. Bind cycling to that arsenal and retire the startup selection watcher.
4. Replay M9 firing, empty lock, magazine removal/insertion, slide closure,
   interrupted reload and swapping through the actual SP server/client copies.
5. Check unrigged reload animation with tracked/floating hands, then select a
   combined playable build while retaining the current rollback.

Vehicle and dummy spawning are not implemented by the existing helper.

## Local evidence

Private generated profiles, asset review and build logs remain under
`%USERPROFILE%\Documents\Codex\2026-10-09\task-3\rea-review\m9-implementation`.
Build receipts: `build-20261010T094908Z/status.json` (five focused checks) and
`build-20261010T095543Z/status.json` (reload routing and compiled capabilities).
The latter's `private_m9_profiles: false` records that configuration was reused,
not that the already-configured private profiles were absent.
Further receipts: `mission-index-status.json` and
`mission-candidates-status.json`; full matches remain in the corresponding
private `.local.json` files.
The `resource-loading/research-receipt.local.json` receipt binds the read-only
loader investigation and its private dossiers. There is no new authorization
blocker for source research; game execution remains deferred during KF2 testing.

Do not publish generated game profiles, local catalogs, extracted assets,
native analysis or their excerpts. Public source contains only the interaction
implementation and synthetic checks.
