> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# A reload / right-stick up jump

Source checkpoint only; the sole session owner builds and installs the candidate.

- **A tap/release**: the existing physical reload action, including magazine
  release or chamber/rack fallback according to the selected gun's current state.
- **A hold for 600 ms**: the existing automatic native reload request for a
  physically managed gun. Short release is not replayed after that long hold.
- **A press on an unrigged native weapon**: the existing ordinary native reload
  pulse. The native weapon keeps its authored reload behavior and animation.
- **Right stick UP**: jump. It no longer also changes weapons.
- **Right stick DOWN**: the existing other-primary selection. Return to centre
  to rearm either vertical direction.
- **B**: no gameplay action in this checkpoint. It no longer releases magazines,
  reloads or chambers. Native-menu B/back behavior remains as before.

Right-trigger firing, left movement/use/crouch, right-stick click item cycling,
both-stick-click recenter, left Menu/X+Y menu, and left-trigger + horizontal-stick
test arsenal controls keep their existing bindings. No new menu or IPC ABI is
part of this checkpoint.

Controller mapping checks were updated to cover A reload, B inactivity, UP jump
without a weapon-select edge, DOWN selection, held direction/neutral rearm and
diagonal turning. Existing reload routing tests cover short release, 600 ms hold,
native press pulse and cancellation. Compilation/test execution and headset
acceptance belong to the sole build/session owner.

Deferred observations: the owner confirmed the M9 appears to repeat shots while
holding its trigger; cause remains unverified. He tentatively identified the XM8
and could not find a physical bolt/pull; the old B action chambered after magazine
insertion. This checkpoint moves that same fallback to A; it does not change the
bolt interaction or native fire modes. Menu and magazine-pop work is deferred.
