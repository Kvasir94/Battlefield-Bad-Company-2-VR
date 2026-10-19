> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Arsenal chord correction

The 4f94cac playtest reported that left trigger + right-stick left/right did
not cycle the test catalog. Source inspection identified a deterministic cause:
`reloadRequestMode` zeros the semantic-action copy's left trigger, and the
Arsenal chord read that masked copy. Physical reload enables this mode.

The correction reads the validated original input packet while preserving the
reload action mask. It reuses the existing named events and verified native
post-firing fixture handoff; it changes no hook, input/menu ABI or catalog.

## Exact controls

| Input | Action |
| --- | --- |
| A tap/release | Existing physical magazine release or chamber/rack fallback, according to gun state |
| A hold 600 ms | Existing automatic native reload fallback for physically managed guns |
| A press on an unrigged gun | Existing ordinary native reload pulse |
| Right stick UP, LT released | Jump |
| Right stick DOWN, LT released | Switch between carried primary guns |
| Right stick click/release | Next actual carried weapon/item |
| Right stick click held 450 ms | Previous actual carried weapon/item |
| LT fully pulled + right-stick RIGHT | Next gun in the test Arsenal catalog |
| LT fully pulled + right-stick LEFT | Previous gun in the test Arsenal catalog |
| Right trigger | Existing native firing input (.60 pull/.45 release) |
| B | No gameplay reload/jump action; native-menu back remains |
| Left stick / left click alone | Existing locomotion / sprint |
| Left X / Y | Existing use / crouch |
| Both stick clicks held 1 second | Recenter; carried-item cycling is spoiled |
| Left Menu or X+Y menu chord | Existing native VR menu |
| Left grip at left belt | Existing magazine supply interaction |

Catalog cycling replaces the unselected primary slot and selects the new gun;
the previous gun stays carried. It differs from DOWN and right-stick click,
which select items already carried. No radial or floating loadout menu is
installed by this correction.

For a catalog step, hold LT at least .90, release RT to at most .25, release
other buttons, start with the right stick centred, and flick horizontally at
least .75. The horizontal magnitude must exceed vertical magnitude by .15.
Return both stick axes below .35 before another step. The stick is captured
until centre even if LT releases, preventing that flick from also turning,
jumping or switching carried items. Fire/button conflicts, unavailable
tracking/focus, stale input, actor/space changes and busy equipment prevent
catalog requests and require a fresh neutral stick before selection.

The existing fixture has a 2.5-second async-equip cooldown. Wait for each gun
to finish equipping before another flick. Faster requests still accumulate in
the existing fixture and may apply later; request-time input guards do not
provide cancellation of an already queued fixture request. No claim is made
that this existing asynchronous queue has new transactional guarantees.

## Focused verification

`ControllerInputTests` adds a regression using a raw LT value of 1 while the
action copy's LT remains 0, with real `ControllerActions` downstream. It covers
both catalog directions, repeat/neutral arming, LT release without turning,
fire/button conflicts, tracking/admission/focus loss, source gaps, actor/space
changes and unchanged A/UP/DOWN semantics. Existing reload interaction tests
remain relevant to short/long A dispatch.

This checkpoint has source review and `git diff --check` only from its worker.
No compiler or test binary was run during the runtime owner's live lease.
The owner should incrementally build the native DLL and run ControllerInput
and Bc2ReloadInteraction after the playtest ends. Headset verification should
confirm LEFT/RIGHT catalog replacement versus DOWN/click carried selection,
including neutral rearming and blocked fire/menu/recenter combinations.
