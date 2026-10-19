> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).
> Follow-up: [later recoil basis research](re/BC2_RECOIL_AIM_BASIS_20261012.md) and [current source](../src/games/bc2/Bc2NativeRecoilTransfer.cpp); the observer-only status below is historical.

# Native rotational recoil on tracked weapons

The goal is to transfer BC2's own rotational recoil to the firing wrist, weapon
and attached support hand while keeping the HMD stable. Preserve each weapon's
native magnitude, accumulation, recovery and aim/spread behavior. Do not replace
that response with generic shot impulses or an extra animation kick.

## Current status

KF2's implemented approach has been inspected. BC2's transfer is **not
implemented**. The 2026-10-11 static pass identified separate FireLogic and
GunSway recoil lanes, including GunSway's native recovery and camera transform.
The sampling seam is after the original GunSway tick, using the exact
copy selected by the native camera. A source observer is now wired behind
`FVR_BC2_NATIVE_RECOIL_OBSERVE=1`; it transfers no pose or firing rotation and
has not been accepted in game. The remaining observation is selected-copy tick order relative to the
rig's publication epoch; total aiming angles cannot establish that relationship.
GunSway's camera response and FireLogic's separate rotational camera additions
must both remain explicit in the eventual full transfer design.
See [the native recoil contract](re/BC2_NATIVE_RECOIL.md) for addresses, evidence,
and the bounded next step.

The new observer records normal selected firing A/B ticks, the actual native
camera output at the inspected return site, and every valid or invalidated rig
publication route in `GatherHook`. It preserves original native call counts,
absolute response matrices and original tick QPC. Its input label is the latest
published tracking context, not a claim that the tick consumed that input.
Alternate selected objects are explicitly unsupported, and no native zoom
context reader is invented. See the contract's source-observer section for the
local report schema and interpretation limits. The diagnostic can be included
in an integrated build before a future coordinated capture; including it does
not enable native rotational recoil on the hands or weapon.

KF2 advances the native weapon recoil integrator once per game tick for each
managed item. It applies that item's output to the solved weapon bore and hands,
keeps it out of the HMD, and compensates the firing input so native aim adds the
response once. Its presenter does not advance recoil again. Relevant reference
source is `VRWeaponRuntime.uc` (`AdvanceRecoil`), `VRHandInventory.uc`,
`VRHandsBridge.uc`, and native `WeaponRuntimeDispatch.cpp` / `WeaponAimStack.h`.
The audited KF2 source was `9ac1c654394756ad63c2e33603de81422fc644d9`.
KF2's rotator units, coordinate conversions and gameplay multipliers are not
BC2 bindings and must not be copied blindly.

The reference implementation calls `Item.WeaponProcessViewRotation` from
`VRWeaponRuntime.AdvanceRecoil`, saves the resulting controller buffer on the
item, then restores the shared buffer. `VRHandInventory` gates this call with
`RecoilFrame` / `LastRecoilFrame`; native `SuppressManagedRecoil` admits that
scheduled call and suppresses duplicate managed-item integration. Presentation
adds `ItemRecoil` to the solved bore, then subtracts it from `FireRotation`
because stock adjusted aim adds the same buffer. These are separate integration,
presentation and firing responsibilities. KF2 also has a separate authored
translation kick; copying that kick would not implement BC2 rotational recoil.

BC2's admitted HaloAimer setters use radians. The unused
`MakeLhAbsoluteGripWeaponRecoilDelta` helper assumes BFSoldier degree angles;
it is not connected to BC2 gameplay and supplies no native sampling proof.
`AnchorView` already supplies the stable HMD base. `PlaceFireAtMuzzle` preserves
the native firing rotation/spread and changes only shot origin. The recoil
transfer must not add a second rotation to that firing matrix.

## Bounded total-angle diagnostic

Set `FVR_BC2_AIM_ANGLE_TRACE=1` in the native module's process environment before
installation to opt in. The flag is read once; the normal path adds no native
angle reads. This does not deploy or launch a game.

The existing gameplay report includes `total_angles_radians`. It keeps the latest
4096 completed local on-foot update transactions in chronological order with an
overwrite count. Each transaction may contain:

1. `before_update`: before the original player input update.
2. `after_gather_original`: after the original input gather for the exact admitted
   router/cache.
3. `before_vr_baseline`: immediately before the controller yaw/pitch writes.
4. `after_vr_baseline`: after the admitted setters and simulation-angle mirror.
5. `after_update`: after the original player input update returns.

Valid snapshots include aimer and simulation angles in yaw/pitch order, QPC time,
input sequence/space and freshness. Transactions identify the actor, equipment
incarnation and requested controller angles. Missing phases are absent; invalid
snapshots provide a reason and omit angle arrays. Recursive native callbacks
cannot append samples to their parent's transaction. Reporting copies the ring
under its mutex before JSON serialization.

These are verified **input-hook boundaries**, not verified recoil/recovery
boundaries. The report explicitly leaves recoil attribution and native recovery
verification false. It can reveal when VR writes replace total angle state and
what happens in the remainder of the native update. It cannot establish the
meaning of a hidden accumulator. The short ring covers only the most recent
updates, and enabling it adds diagnostic reads. Ring storage is allocated only
when the flag is enabled during installation. Actual native traces remain
local; do not upload or print them into provider inputs.

## One short capture after a deliberate combined deployment

This is a prepared protocol, not a request to replace the installed `8614b4e`
candidate or launch a game now. The original diagnostic in `4d80e7f` had 256
transactions. The 4096-entry expansion retains the same circular insertion and
final-report interface; its compile belongs to the coordinated batch. The
isolated `build/rea-refactor-20261010/x86` DLL is compile evidence, not an
automatically selected play build. The launch scripts use
`build/x86/BC2NativeProbe.dll`, `build/x86/BC2NativeTrace.exe` and
`build/x64/BC2XrHost.exe` in the selected launcher root.

The angle history is written **only at the end** of the native probe into
`gameplay.total_angles_radians` in `native-trace.json`. It is not in the one-second
`session-telemetry.jsonl` stream, and no live snapshot command exists. The native
probe disables its hooks, writes the final gameplay report directly to an
`std::ofstream`, flushes and closes it. The bootstrap passes a bounded report
**path**, not a bounded JSON payload. Completion parsing reads the file with
`Get-Content -Raw` / `ConvertFrom-Json`; there is no fixed byte-size transport cap
on the expanded history.

`Start-BC2VRSession.ps1 -ReportFolder` names the XR session directory.
`Start-NativeTrace.ps1` independently creates
`reports/native-trace-<UTC>/native-trace.json`, alongside its own `manifest.json`
and `gun-events.jsonl`. Match the native manifest's `host_pid` and `pid` to the XR
session's `host_pid` and `game_pid`; do not assume the native JSON is inside the
XR directory. A unique newly created native-trace directory is another direct
way to bind this one run.

### Environment and profile preservation

The DLL reads `FVR_BC2_AIM_ANGLE_TRACE` from **BFBC2Game.exe's environment**.
Set it in the shell that starts a fresh game, before invoking `Play-Checkpoint`
or `Play-BC2VR -Flat`. Setting it only before injecting into an already running
game does not change that game's inherited environment. The launch wrappers do
not forward this flag through the probe configuration. `Play-Checkpoint` clears
its `FVR_BC2_PROBE_*` / `FVR_BC2_TEST_*` fixture switches but preserves this
diagnostic flag. Restoring the shell variable after the run does not mutate the
game process's environment.

Preserve the accepted build's compile-time magazine geometry, authored grip and
support header selection and `BC2_AUTHORED_GRIP_ACCEPTED_BASELINES` value during
the deliberate deployment. The inspected refactor and ordinary x86 caches both
have the checkpoint204 magazine geometry, empty authored grip/support headers,
and accepted-baseline replacement OFF; cache settings alone do not certify the
profile compiled into an already deployed DLL.

Preserve `FVR_HANDS_MODE` and the accepted `FVR_AUTHORED_CONTACTS` policy. The latter
loads once from its explicit path, or otherwise looks for
`authored-contacts-v1.txt` **beside the injected DLL**. `Start-NativeTrace` copies
the DLL into its new native-trace folder and optionally copies the body-ammo
cache; it does not copy the authored contact library. If the accepted session
uses that library, bind the same verified file through an absolute
`FVR_AUTHORED_CONTACTS` path before creating the game. Do not turn it off or
silently select a different library for this diagnostic. If the accepted library
path/policy or combined deployment is unresolved, preparation stops there.

### Exact launch and graceful-stop recipe

Use the launcher root where the deliberate combined candidate will be selected.
For the inspected source root this is `<repo>`. The game location is read
from that root's `config/local.json`; no guessed install path is needed.
The existing test-range stage selection remains `-Stage`, the root's stage,
`FVR_BC2_STAGE`, or the existing user stage according to the launcher used.
Keep the accepted stage and firearm configuration; XM8 is the existing default.

Terminal 1, only after the candidate and authored-policy decisions above:

```powershell
Set-Location <repo>
$repo = (Get-Location).Path
$session = Join-Path $repo ('reports\recoil-angle-session-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
$nativeBefore = @(Get-ChildItem (Join-Path $repo 'reports') -Directory -Filter 'native-trace-*' | ForEach-Object FullName)
$previousTrace = $env:FVR_BC2_AIM_ANGLE_TRACE
$env:FVR_BC2_AIM_ANGLE_TRACE = '1'
try {
    # Existing accepted environment/profile stays in place; the game inherits it.
    .\Play-Checkpoint.ps1 -Flat -Gun XM8
    Write-Host "Graceful-stop session path: $session"
    .\Start-BC2VRSession.ps1 -ReportFolder $session -MagazineReload -GunEvents
} finally {
    $env:FVR_BC2_AIM_ANGLE_TRACE = $previousTrace
}
$nativeNew = @(Get-ChildItem (Join-Path $repo 'reports') -Directory -Filter 'native-trace-*' | Where-Object { $_.FullName -notin $nativeBefore })
if ($nativeNew.Count -ne 1) { throw 'Bind the native trace by session host/game PID instead of guessing a folder.' }
$nativeReport = Join-Path $nativeNew[0].FullName 'native-trace.json'
```

The flat launcher fast-starts campaign gameplay and does not inject the native
VR probe. Startup/loading time is separate from the short firing window. The
session command blocks while the host/probe runs. The real XR stream and
controller input must be ready before starting the timed actions. A session
state of `running` alone is insufficient because it is saved before native
injection completes: wait for native session telemetry and responding tracked
aim. This is the manual observation variant. The authored synthetic variant
below is also available for engine diagnostics; neither is recoil attribution
proof. Keep rig pulses and equip/death/reload action fixtures out of this capture.

The minimum angle-setter path needs a valid tracked stream, Controllers and
MotionAim, an admitted local on-foot firearm, fresh focused/head-valid input,
and a tracked right aim pose. `-MagazineReload` in the recipe preserves the
normal `Play-BC2VR` feature chain: PhysicalReload → TwoHandGrip → MuzzleFire →
Hands → BodyFollow/Roomscale → MotionAim → Controllers. `-GunEvents` additionally
keeps native shot rows for distinguishing live shots from dry pulls and
correlating times; no new diagnostic firing mode is used.

Once ready, use one firearm for roughly **6–10 seconds**:

1. Hold the firing controller steady for about one second with no fire. Avoid
   turning, movement, support changes, ADS/sight changes or intentional wrist
   rotation during this window.
2. Make one brief trigger pull and release. Hold the same pose for roughly
   two–three seconds to observe the resulting total-angle evolution. A brief
   pull is not proof of exactly one shot; confirm native fire rows afterward.
3. Make a short burst, release, and hold steady for another two–three seconds.
   Do not reload, switch weapon or wait through a long unrelated idle interval.
4. Immediately request graceful session stop from terminal 2 using the exact
   session path printed by terminal 1. Keep BC2 alive while it writes the report:

```powershell
Set-Location <repo>
.\Stop-BC2VRSession.ps1 -ReportFolder '<exact session path printed by terminal 1>'
```

That script validates this session's host and writes its `stop.request`. Host
exit ends the native `UntilHostExit` loop, followed by hook shutdown and final
reporting. With GunEvents enabled the native drain loop polls every 10 ms;
shutdown is not guaranteed instantaneous. Do not kill the game, host, bootstrap
or supervisor to obtain the report. The launcher also has an existing 15-second
post-trace stability observation; that wait occurs after the ring freezes and
does not require further active firing or another run.

### Local interpretation and actual coverage

Read the completed native report locally after graceful shutdown. Require an
observed report with hooks disabled, the trace requested, and the expected
capacity. Measure retained time as
`(last stage QPC - first stage QPC) / qpc_frequency`; 4096 updates are not a fixed
duration. Check whether baseline, single pull, recovery interval and burst are
actually inside that span and have valid fresh phases with the same
actor/equipment/space/aimer identity. If overwrite occurred or an early event is
outside the retained span, explicitly mark that phase uncovered. Do not add
runtime passes merely to compensate for the old 256-entry limit.

Compare these values, separately for the aimer and simulation:

- `before_update → after_gather_original`: native work preceding the mod's
  gathered controller processing.
- `before_vr_baseline → after_vr_baseline`: the existing setters/mirror replacing
  total angle state. Compare the actual post-write result with requested yaw and
  pitch, allowing the known native pitch clamp and yaw wrapping.
- `after_vr_baseline → after_update`: the rest of the original update. This is
  not a proven recoil or recovery function boundary.
- Previous `after_update → next before_update`: angle changes outside the
  enclosed original update, only for unchanged ownership and tracking space.

Track requested-controller changes alongside these comparisons, wrap yaw
differences across `2*pi`, and inspect aimer/simulation divergence. If a
pre-write total angle differs and the post-write angle equals the tracked
command, the diagnostic establishes replacement of that total angle at the
setter seam. It still does not prove that an unseen recoil accumulator was
erased, identify its ABI, or establish recoil-only provenance.

`gun-events.jsonl` uses absolute QPC microseconds. Align its native `fire` rows
with stage QPC using `qpc_us / 1e6` and `qpc / qpc_frequency`. Keep client and
server copies distinct; two copies are not automatically two shots. Dry pulls,
stale input, missing setters, pitch clamping, intentional hand motion and
ownership changes are concrete alternative explanations to resolve. Neither a
small residual nor a flat tail alone proves complete native recovery.

An optional repeat is justified only by a specific unresolved finding or missing
phase; start with the same firearm and condition. A probe instance admits its
entry once and remains resident until game exit, so do not assume reinjection or
changing shell flags can retarget the existing instance. Prefer a fresh game
for any deliberate repeat. The first pass does not require testing every gun.

The task-local `rea-review/Analyze-Bc2TotalAngles.local.ps1` prepares a local-only
coverage/timeline analysis: stage validity, retained QPC span, requested-motion
and setter/native-interval deltas, and fire-row alignment. It writes a new local
JSON file without stdout and makes no recoil/recovery claim. It has not been run
against game evidence. Keep that derived analysis and the native report local;
do not upload or paste their contents into provider input.

In the current the dev PC task directory, invoke it after `$nativeReport` is bound
to this run as above:

```powershell
$analyzer = '%USERPROFILE%\Documents\Codex\2026-10-09\task-3\rea-review\Analyze-Bc2TotalAngles.local.ps1'
& $analyzer -ReportPath $nativeReport -ExpectedCapacity 4096 `
    -OutputPath (Join-Path (Split-Path $nativeReport) 'total-angle-analysis.local.json')
```

The output path must be new. Missing/nonpositive retained QPC bounds, mismatched
capacity, incomplete shutdown or an unrequested trace reject coverage. Parsing
or analysis errors are saved beside the requested output as `.error.txt`, with
no native evidence printed. The analyzer measures retained phases; it does not
assert that the intended baseline/pull/recovery/burst actions all occurred.

## Authored synthetic engine diagnostic

`tools/replay/tapes/xm8-recoil-static.fvrtape` is authored input, not measured
controller motion. It holds head/reference head at the origin, right grip/aim at
`(0.15,-0.10,-0.20)` metres and left grip/aim at `(-0.20,-0.25,-0.45)` metres,
all with identity orientation. Input is head-relative, focused and tracked, with
age zero and one fixed epoch. No movement, squeeze/grab, reload or equipment
commands are authored. Only the right trigger changes.

The sequence is 120 neutral gathers, 3 trigger-held gathers, 180 neutral gathers,
24 trigger-held gathers and 180 neutral gathers: 507 in total. A gather advances
one tape tick; these are not seconds. The nominal 10000-microsecond header step
is provenance, not a playback timer. Session prediction metadata replaces tape
prediction time. `after_end=idle` retains neutral poses after the sequence.
A three-gather pull does not guarantee exactly one shot; measure actual shots
and elapsed phases. A fixed 12-second first attempt can end before all 507 gathers
at a low gather rate; report that as incomplete coverage, not a failed recoil
response or an automatic reason to extend the run.

The tape retains real coherent `loaded>=12` and `reserve>=0` preconditions; it
does not edit native ammunition or bypass the precondition hold. The current
gather observer may supply an empty weapon asset, in which case the tape reader
skips its `weapon=XM8_sp_s` comparison. Bind the actual selected weapon from local
gun-event/native evidence; that header alone does not certify weapon identity.

Use the existing neutral publisher, not the current `run.py` sink-only route.
`NativeProbe.cpp` replay gather requires fresh focused/head-valid transport input
and stable nonzero space metadata. `--sink-seconds` publishes no input and rejects
controls. `--scene-controls-neutral` supplies this metadata while maintaining a
static HMD frame and neutral controls; tape poses/actions remain authoritative.
Do not run a real XR host against the same channel during this diagnostic.

After the deliberate candidate/environment setup above, start this receiver in
one terminal using the coordinated IPC helper, with a new output directory:

```powershell
& $ipcProbe --output $newOutputFolder --pairs 240 --static-pose --async --scene-controls-neutral
```

In the other terminal, as soon as `channel.txt` exists (the receiver's connection
wait is 15 seconds), run the first bounded capture:

```powershell
$token = (Get-Content -LiteralPath (Join-Path $newOutputFolder 'channel.txt') -Raw).Trim()
.\Start-NativeTrace.ps1 -FrameChannel $token -Seconds 12 `
    -InputTape .\tools\replay\tapes\xm8-recoil-static.fvrtape -MagazineReload -GunEvents
```

`-MagazineReload` enables the existing managed lane; the tape sends no reload
input. The receiver has its own approximately 55-second pair pacing and bounded
70-second loop budget; trace duration is independently 12 seconds. Coordinate
receiver cleanup after the trace. This protocol does not deploy, modify game
state fixtures or establish native recoil semantics. Keep native outputs local.
Inspect tape progress, real shot events, ownership and completed angle phases
before drawing conclusions. Balance, comfort and headset feel remain separate.

The authored desktop route produced a usable native-engine capture on
2026-10-10: replay completed, native shots and fresh setter phases were observed,
and early/late authored inputs were retained. The local analyzer completed.
Protected settings, stage files, selected binaries and accepted contacts were
restored/verified; the test processes closed. This validates harness access to
the engine path, not recoil-only semantics or headset feel. Native evidence and
derived values remain in the local task's `capture-20261010T085212Z` directory
and the bound `reports/native-trace-20261010-085248-046` directory.

## Transfer contract and next work

The current BC2 ordering is important: `GatherHook` calls the original gather,
then writes the tracked total yaw/pitch and publishes `rigPublication::Tracking`.
Only afterward does the enclosing `UpdateHook` finish its original update and
record `after_update`. Therefore an after-update total-angle residual is neither
a proven recoil sample nor automatically coherent with the already published
rig or firing calls. The verified `void (__thiscall*)(void*, float)` setters and
positive-up pitch convention describe total HaloAimer input, not the missing
recoil function or its output sign. The existing degree-based math helper does
not close this gap.

The local native data-flow pass used Ghidra headless on a read-only copy of
`D:\bc2-re\proj`, reusing analysis because REA was unavailable. It separated the
per-shot FireLogic ramp from GunSway's accumulating and recovering recoil, and
identified GunSway's absolute camera transform as the candidate visual sample.
The native tick must continue to own integration. Do not sum that absolute
transform, advance the integrator again, or substitute the raw recoil pair for
its spring-filtered output. The reviewed client/server shot paths consume a
separate dispersion output; they do not justify copying KF2's recoil subtraction
or adding another firing-matrix rotation. Targeted static decompilation excerpts
needed for this investigation may be inspected through local tool responses.
The provider-input prohibition above applies to actual native runtime traces
and the specifically restricted total-angle report and its derived analysis;
it is not a blanket prohibition on static decompilation. Keep complete native
dumps, binaries and assets local and unpublished. No new native binding or
runtime capture is claimed by the source review.

The remaining check needs an owner-scoped observation of the selected GunSway
tick, camera read, and rig publication with the same equipment/input epochs.
The existing total-angle and shot-matrix reports do not emit GunSway identity or
tick serials, so repeating those captures cannot close the gap. The linked
contract specifies the smallest additional observation. It is a follow-up
design, not a request to build, deploy, or capture during this static pass.
Residual accumulation or invented recovery would not preserve native recoil.

- Establish a recoil-only native value, units, sign, owner and update order before
  integration. Taking the difference between the camera and controller commands
  can include intentional turning, clamps, animation or hit shake.
- Observe the native response after its original tick, then publish an immutable
  snapshot keyed to the exact actor, equipment incarnation and tracking space.
  Reject stale or mismatched samples; do not invent recovery on reload or switching.
- Preserve raw tracked contacts. Apply the admitted rotation once about the
  firing grip to the weapon and firing wrist. Attached support follows the same
  item; free or loading hands retain their existing ownership.
- Use the same final item pose for authored hands, muzzle/effects and shot-latched
  origin. Keep native firing direction/spread and HMD pose unchanged. Solve IK
  against the final wrist targets; floating hands use their direct targets.

The diagnostic's affected x86 `BC2NativeProbe` target compiled successfully in the
existing isolated cache on 2026-10-10. No game/headset acceptance or recoil
transfer is claimed. The installed candidate remains unchanged. Once native
sampling is verified, implement the transfer and choose focused checks for
radians/sign/pivot, ownership/reset, attached versus free hands, and unchanged
native shot orientation. Headset firing/recovery acceptance follows a deliberate
combined deployment; it is not a gate on ordinary source development.
