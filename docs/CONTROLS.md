# Current controller controls

Source checked October 13, 2026 at `1023fd6`: Quest/Touch-style labels.
These are source defaults, not a fresh headset test. The selected package and
launcher overrides determine the controls actually running.

| Input | Action |
| --- | --- |
| Left stick | Head-relative movement |
| Left stick click | Held sprint; both-stick recenter chord suppresses sprint |
| Right stick left/right | Turn; default snap is 30 degrees, with configurable turn policy |
| Right stick up | Jump |
| Right stick down | Other primary weapon |
| Right stick click/release | Next carried item |
| Right stick click held | Previous carried item; default hold is 450 ms |
| Right trigger | Fire; source hysteresis is 0.60 pull / 0.45 release |
| A tap/release | Physical reload action for managed weapons: magazine release or ready/rack action according to weapon state |
| A hold | Automatic native reload request for managed weapons; ordinary native reload remains available for unmanaged weapons |
| B | No gameplay reload/jump action; native-menu back remains |
| X / Y | Use / crouch |
| Grips | Available weapon, support, ammunition and mechanism contacts |
| Left Menu or X+Y held | Menu where supported |
| Both stick clicks held | Recenter; default hold is one second, after neutral arming |
| Left trigger fully pulled + right stick right/left | Next/previous test Arsenal gun, when the test-range fixture supplies Arsenal events |

Return the stick to neutral between discrete selections. Tracking, focus,
ownership and mechanism state can suppress an action. Left trigger alone is not
a general manual ADS binding. Arsenal consumes its stick gesture so it does not
also turn or select an ordinary carried item.

Sources: [ControllerInput.cpp](../src/interaction/ControllerInput.cpp),
[controller settings](../include/fvr/interaction/ControllerInput.h),
[Arsenal chord](../src/games/bc2/Bc2ArsenalChord.h),
[physical interaction](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp) and
[BC2 integration](../src/games/bc2/Bc2Gameplay.cpp).
See [the launcher](LAUNCHER.md) for player settings.

The [September/October controller record](CONTROLS-20260930.md) and
[October 11 correction](CONTROLS-ARSENAL-TRIGGER-FIX-20261011.md) preserve the
history. Their older A-jump/B-reload mappings are superseded by this card.
