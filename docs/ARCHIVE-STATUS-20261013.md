> **Historical archive.** Preserved on October 13, 2026. Implementation claims,
> priorities, agent assignments and workflow restrictions below belong to their
> recorded sessions. Use [current status](STATUS.md) and [development rules](../AGENTS.md)
> for new work. Technical evidence retains its original scope.

# Current status — source snapshot 1764186 (October 9, 2026)

Current claims are pinned to pushed source, not a selected local binary or dirty
checkout. The later persistent physical-weapon architecture is implemented:
owned per-weapon state, server-side command transactions, accepted-shot accounting
and independent inventory publication/selection. The adapter blocks native reload
entry for managed weapons and applies a final shot gate while retaining native
shooting and replication. See [native boundaries](../src/games/bc2/Bc2PhysicalWeaponNative.cpp),
[server transaction consumer](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp) and
[inventory selection retry/readback](../src/games/bc2/Bc2Gameplay.cpp).


**Later source changes checked October 9:** main `97a5b71` adds
controller sampling/focus and selected-profile diagnostics after the gameplay
snapshot above. These changes do not establish real-controller reload acceptance.
The separate composed headset mirror is opt-in via `Play-BC2VR.ps1 -Mirror`;
the game's steady eye mirror remains enabled unless `-NoSteadyWindow` is used.
No performance saving is inferred from that default. See
[launcher](../Play-BC2VR.ps1) and
profile diagnostic change.

Main `65957dc` additionally classifies rejected stereo pairs and
preserves transfer errors. This is diagnostic coverage, not a rendering or
physical-reload acceptance result.
Exact change.

Physical-reload sessions install this adapter; `FVR_BC2_PHYSICAL_WEAPON_AUTHORITY=0`
opts out. Configured charging-handle racking defaults on;
`FVR_BC2_CHAMBER_RACK=0` opts out. These are scoped implemented paths, not a
claim of complete weapon-family support or a new headset pass.
[Installation](../src/games/bc2/Bc2Gameplay.cpp),
[authority switch](../src/games/bc2/Bc2PhysicalWeaponRuntime.cpp) and
[rack default](../src/games/bc2/Bc2Gameplay.cpp) are source evidence.

| Area | Current implementation and remaining work |
| --- | --- |
| Native stereo / OpenXR | Existing native producer and x64 presentation; pacing, LOD and lifecycle polish remain. Controller sampling diagnostics/CPU fixtures do not establish real-controller acceptance or crash closure. |
| Hands, support, roomscale, recenter | Existing tracked paths; reconnect/transition and hand alignment/feel still need coverage. |
| Body inventory / chest ammo | Existing scoped paths; unintended draws and repeated pickup/holster/reload transitions require acceptance. |
| Magazine reload | Persistent magazine/chamber state and physical insertion consumer implemented. B can eject a seated magazine or complete an owed rack; it is not universally automatic reload. Broader configurations and real-hand acceptance remain. |
| Shell / SPAS pump | Physical tube consumer and fore-end rear/forward pump path implemented. This corrects the earlier “manual pumping unfinished” absence claim; it does not prove all shotguns or new headset acceptance. |
| Underbarrel / gadgets | Equipment selection and scoped sight/fire/reload paths exist; item-specific secondary, tool and attachment behavior remains unverified. |
| Boat / vehicles | Reviewed PBL driver consumer supports throttle/steer and verified exit, with optional head aim/fire. General seats, vehicle actions and campaign coverage remain open. |
| Optics / reticle | ACOG aperture/eye math exists; selected GPU optic-instance association is missing. Mounted reticle test registration exists, but actual mounted-shot source binding is unadmitted. |
| Full body / multiplayer | Shared groundwork; no complete implementation. Single-player remains the target. |
| Other Frostbite games | Porting plan only; no verified BF3/BF4/Hardline or Battlefront adapter. |

[Current controls and gaps](CONTROLS-20260930.md#current-controller-card--october-9-2026)
distinguish source routing from design proposals. Ordinary grenade/melee routes,
item-specific tool/secondary behavior and campaign scripted actions are not
established by discovering native action names or selecting gadget slots.
[Pump consumer](../src/games/bc2/Bc2Gameplay.cpp) and
[B dispatch](../src/games/bc2/Bc2PhysicalWeaponInteraction.cpp)
are scoped source evidence.

The latest human headset feedback recorded before the checkpoint 202/203 fixes
reported broken rifle reloads and unintended draws. Preserve those failures.
Subsequent scripted native checks and chair/controller diagnostics are separate
evidence classes; they do not establish a comfortable real-hand reload, a new
headset acceptance pass or closure of a reported crash.

## Historical checkpoint 202/203 validation

[Checkpoint 203](RELOAD-TRANSITIONS-203.md) recorded the shared magazine
retirement correction after switching rig/profile, CPU transition coverage and
185 suites per architecture, plus the strict native original-return/replacement
sequence. Combined native cross-profile coverage and new headset acceptance
remained open at that checkpoint.

The counts and run results below belong to those dated builds. This documentation
refresh ran no builds, native checks, game launches or headset tests.

The repeated native magazine audit passed with one original return, one
replacement, zero cancellations, zero magazine visual fallbacks, conserved ammo
(22/191 to 30/183), 240 stereo pairs and clean hook shutdown. Four diagnostic
completions lost under contention in the prior build were retained. The source
composition passed 186 C++ suites per architecture; two private test-only registry
targets were omitted from the public default build. See [202](SIMULATED-PLAYER-202.md).

The initial publication copy was rebuilt using `Build-Checkpoint.ps1`: all 184
public C++ suites passed on both x86 and x64. Python discovery ran 800 tests:
799 passed and one installed-BC2 integration check was intentionally skipped.
PowerShell launcher checks and the source-only packaging check also passed.
These historical publication checks did not start the game or a headset session.

## Current work and priorities

The [prioritized open-work queue](PLAYTEST-HOST-FEEDBACK-FIX-20261009.md#prioritized-open-work--resumed-after-the-october-9-playtest)
consolidates the resumed owners' work: stereo consistency/fades/window-mode
recovery first, then firing/reload/switch/sprint and next-build controls/recovery,
weapon presentation, and the existing hands/vehicle-toolbox lanes. OBS recording
is tracked separately; weapon-wall depth is explicitly later. The same record
separates observed failures, the proven host-feedback cause/source fix with live
acceptance pending, and requested or prepared changes.

Sprint direction (2026-10-10, `c1faeb4`, pushed): the reported bug is that
sprinting toward HMD-forward ran sideways or varied with room rotation, not a
request for multidirectional sprint. Native sprint runs along the aim yaw, which
the mod set from the gun hand; while the sprint button is held and no fire/ADS
is active, the aim yaw now follows the HMD heading. 221/221 CPU checks; the
stock forward-only rule is kept. Headset acceptance pending (rotate the
playspace, hold the gun off-axis, sprint HMD-forward).


[Separate BC2/BF3/BF4 research program and project handoff](FROSTBITE-VR-RESEARCH-PROGRAM.md)
is a dated planning document: verify recent projects and test routes first;
no new port is selected or started.


Live human observations are recorded in the
[October 9 existing-IK worn playtest](PLAYTEST-20261009-EXISTING-IK.md).
Its source/session identity is reported pending owner confirmation; subjective
improvement and observed firing do not establish measured FPS or full acceptance.


The [archived VR quality review and staged acceptance plan](VR-QUALITY-REVIEW.md)
preserves the complete source comparison and its pinned-revision limitations.
Use its Stage 1 XM8 continuity and measured-headset gates to guide the roadmap;
Stages 2–3 are planned polish and broader ambitions, not implemented acceptance.


Use that single queue for immediate integration priorities. Broader weapon-family,
optic/reticle and campaign coverage remain open. Exact-package headset acceptance
and player packaging are still required; no supported installer or player release
is claimed.

Merged PR #9 (`2b77bc1`)
is the historical pump/rack/F2000/switching milestone. The initial stick failure,
fix `87bc623` and scoped scripted runs remain in
[the October 8 ledger](2142-TRANSFER.md#stick-weapon-switching-done-2026-10-08-in-scripted-runs-headset-not-checked).
The frozen playtest tag differs from main; source implementation and scripted
success do not replace real-controller headset acceptance.

[`VehicleReticleNativeShotSourceAdmitted=false`](../src/games/bc2/Bc2VehicleReticleSource.h)
still blocks production reticle admission.
[ACOG observer contract](../src/games/bc2/Bc2ReticleAperture.h)
requires selected mesh, exact GPU geometry, submitted skin transform and rendered-eye
association. Offline math does not supply that proof. Research documents and
`release/` drafts remain dated evidence, not current feature or release claims.
