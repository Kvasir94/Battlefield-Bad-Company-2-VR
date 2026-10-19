> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Weapon effects, launcher cant and projectile basis — October 9

Scope: the two worn observations in `HANDOFF-2026-10-08-LATE.md` — muzzle
flash/weapon glow floating and rotating sideways with hand rotation, and the
40mm launcher looking strongly canted with grenades leaving at an odd angle.
Installed executable SHA-256 `c603f686…0162`, preferred base `0x400000`.

Labels: **DIS** static disassembly of the installed exe, **SV** checked by
repo discovery (`DiscoverWeaponEffects`, mutation-tested in
`BC2GameplayInspect`), **ASSET** shipped `.dbx`/mesh data from the intake
cache, **CODE** our source, **INF** inferred. Nothing here is headset-accepted.

## Which transform feeds what

| Consumer | Transform | Evidence |
| --- | --- | --- |
| Visible gun (mesh) | Skin palette packed by `PackHook`; weapon subtree retargeted to `placed` | CODE (`Bc2RigPublication.cpp`, HANDS-20260930) |
| Weapon getter output | `0x78b080` → bone `animation+0x938` (= rig weapon bone `jntWpn_1`) world into a caller-owned stack buffer of animation update `0x8ade10`; `RetargetWeapon` overwrites it with `placed` at return `+0x85` when tracking is current | DIS + SV + CODE |
| 1P FireEffects (muzzle flash, shell ejection) | Same buffer: update `+0x140` calls `ClientWeaponFiringEffects::Update` `0x823b30` (thiscall, 6 args, `ret 0x18`) with a pointer to it. Each `FireEffects1p` entry is composed by `0x7f90b0` as root × R(Rotation°) + Offset in the root frame, then spawned through `0x637d50` | DIS + SV |
| First-person attachment root | Same buffer composed by `0x53db40` at update `+0x177`, then root setter `0x875790` | DIS |
| Projectile (server and client shot) | Final composition `0x53db40` at the verified Server/ClientShoot sites; base matrix from the firing-effects object (`+0x30/+0x70/+0x60/+0xa0` via builder `0x6cddf0`). Native orientation is the aimer (controller yaw/pitch via `ControllerAim`), never `placed`; only the 12 origin bytes are replaced for `TranslatedMuzzle` weapons | DIS + CODE (FIRING-ORIGIN-20260930) |

So the visible model and the effect root share one basis by construction
whenever `RetargetWeapon` wrote that frame (CODE). The projectile does not use
that basis (CODE/DIS); for the launcher, `TranslatedMuzzle` is disabled, so
its origin is native too.

### Authored offsets (ASSET, `sp_common/level-00.fbrb`)

| Asset | FireEffects1p | Shot |
| --- | --- | --- |
| `US_rgl_XM8_Firing(_Scoped)` rifle | `fx_muzz_us_rif` Offset (0, 0.05, 0.9), ZoomRotation (10°,0,0) with UseZoomRotation; shell ejection Offset (0.02, 0.035, 0.425) Rotation (0, -45, 25) | ForceSpawnToCamera 0, InitialPosition 0, InitialDirection +Z, 600 m/s |
| `SP_rgl_XM8_GL_Firing` launcher | `fx_muzz_grenadelauncher` Offset (0, 0, 0.75) | **ForceSpawnToCamera 1**, InitialPosition (-0.12, -0.15, 0.5), InitialDirection +Z, 50 m/s, projectile `UL_rgl_HK416_Grenade` |

Blender check against the XM8 rig/mesh in `jntWpn_1` frame (private, not
committed: `reports/effect-anchor-intake-20261009/`): rifle muzzle tip centroid
(0, 0.0365, 0.8465) — the flash offset sits 5.4 cm ahead of it; launcher tube
mouth (0, -0.016, 0.815) — the GL flash offset sits 6.5 cm inside it. Both
offsets are consistent with the weapon-root frame, i.e. the effect basis is the
weapon root, not the camera (ASSET + DIS).

The launcher's ForceSpawnToCamera flag is read by both shoot paths
(`0x57a12e`, `0x81c46c`, DIS) but that branch is not decoded: that the 40mm
spawns camera-relative at (-0.12, -0.15, 0.5) is **INF**.

## Cause of effect lag (A)

- **Checked (DIS):** `0x823b30` spawns the FireEffects only on the animation
  update after a fire request (`+0x10d` set by `0x802520`, cleared at
  `+0x1ce`). The handle returned by `0x637d50` is discarded. The spawn is
  therefore a world-space fire-and-forget at that frame's root.
- **Checked (DIS):** the root it reads is written by our override earlier in
  the same update (`+0x85` < `+0x140`): not one frame stale, not sampled before
  the VR write.
- **Checked (DIS, corrected October 11):** the effect offset is in the
  weapon-root frame. `0x7f90b0` then applies first-person FOV compensation:
  camera-local X/Y are scaled by `tan(f8/2)/tan(fc/2)` when `+0xf8 != +0xfc`.
  This is separate from authored ZoomRotation/ZoomOffset selection. The
  earlier zoom-transition inference was incorrect; see
  [the exact compose contract](re/BC2_EFFECT_FOV_COMPENSATION_20261011.md).
- **Inferred:** "floats/rotates sideways with hand rotation" is the world-fixed
  spawn staying put while the tracked gun moves/rolls for the effect's lifetime.
  A spawn on a frame where `RetargetWeapon` did not write (native camera-relative
  root) would add a sideways offset; telemetry now counts these.

## Fix (A): engine-supported re-anchoring

The engine already moves persistent effects: its overheat path (`0x81cd10`,
called by `0x823b30 +0x7a`) stores a `0x637290` create handle in `+0x628` and
moves it each update with `0x629ae0` thiscall(manager, handle, const
LinearTransform*) `ret 8` (DIS + SV). Handles are a monotonic atomic counter;
the setter writes a bounded (<300) pending slot or resolves live handles
through a validated lookup that returns null for dead effects (no-op).

`EffectSpawnHook` (MinHook on `0x637d50`) observes only the call returning to
`0x823b30 +0x1c0` inside the owned soldier's animation update, keeps the handle
and the exact relation `local = spawn × root⁻¹`, and `WeaponWorldHook`
re-applies `local × root` on each later update in which the VR rig wrote the
root (same thread and phase as the engine's own setter call). No offsets or
angles are authored; the relation is the engine's own. Expired handles are
dropped; owner/weapon changes clear the table (16 entries, oldest evicted).
On a frame where the rig published nothing (native camera-relative root) the
render side still draws the held VR pose for up to 25 ms (`PoseHoldNs`); an
effect spawned then is moved immediately onto that last written root, so it
starts on the drawn gun (`spawns_native_root_corrected`).

Env: `FVR_BC2_EFFECT_ANCHOR=0` restores native fire-and-forget (spawn
observation remains); `FVR_BC2_EFFECT_ANCHOR_MS` (0–3000, default 400) is how
long a spawned effect follows the weapon. A discovery or live-byte mismatch
leaves native behaviour untouched.

## Launcher cant (B)

- **Checked (CODE):** with admitted axes (`40mmgl` profile, -Z/+Y, same as
  XM8), the visible launcher is oriented by the controller aim; the
  launcher pilot measured barrel vs native aim within 0.0003°.
- **Checked (CODE):** without admission, `TrackedRig` orients the gun as
  `weaponAttachment × calibratedWrist`, where `weaponAttachment` is the native
  weapon-in-wrist relation captured 600 ms after each equip and the wrist
  calibration is captured once per session/recenter. Any native launcher-mode
  hold that differs from the calibration pose is inherited as a fixed cant.
- **Checked (CODE):** c2083ba admitted the launcher axes only when the full
  native mode map resolved; cfc62a5 admits on exact selected-mesh/configuration
  evidence that must be fresh every gather (250 ms snapshot deadline).
- **Checked (CODE):** the effect root is the same `placed` basis, so it cannot
  by itself cant the visual model; the projectile is aim-based and independent
  of both. A canted visual therefore also means the flash is canted and the
  grenade leaves along the controller aim, not the drawn barrel.
- **Not proven:** which source oriented the launcher in the worn session (no
  pose/admission rows were retained). No launcher behaviour was changed and no
  roll constant was added.

## Telemetry added (session-telemetry.jsonl `weapon_effects`, once per second)

Counters: spawns seen/tracked/native-root/rejected, re-anchor calls and frames,
frames skipped because the root was native, weapon outputs written vs native,
launcher admitted/rejected frames and admission flips, shots.

Rows (≤32 per second drained, 128 retained, keyed by gather `tick` = gun-event
tick and weapon pointer):

- `admission` on every change (and every carried-item change): candidate
  profile, native mode map, fresh selected meshes, visual family, admitted,
  selected secondary, rig epoch.
- `placement` ×8 at ≥100 ms after any item/equipment/orientation-source change,
  then 1 Hz while the launcher is carried: source
  (`profile_axes`/`authored_axes`/`native_attachment`), native weapon, placed
  weapon, controller aim frame, body/eye base, and `forward_deg`/`roll_deg` of
  the drawn barrel against the aim.
- `spawn` for each 1P effect spawn: effect root as read, spawn transform,
  manager/handle, whether the root was the VR one, whether it is anchored.
- `shot` for each server and client final shot composition: native shot matrix,
  after origin write, placed weapon and orientation source of the same-pose shot
  frame, event muzzle, and the shot forward/roll against the drawn barrel.

Every launcher row is kept; other weapons are sampled at two rows per kind per
second. The final native report also carries the remaining rows.

## Next worn session

1. Select the launcher and fire at least two grenades with the gun upright.
2. In `session-telemetry.jsonl`, read `weapon_effects.rows` around the selection
   tick: `admission.admitted`, then `placement.source` and `roll_deg`.
   `native_attachment` with a large `roll_deg` confirms the cant source;
   `profile_axes` with `roll_deg ≈ 0` and a canted view would rule it out.
3. For each launcher `shot`, `forward_deg` is the barrel-vs-launch angle; the
   `native_shot` origin against `placed_weapon` shows the camera-relative spawn.
4. `spawns_native_root` > 0 or `reanchor_skipped_unwritten` growth points at
   frames where the effect root fell back to native.
5. Compare flash stability with `FVR_BC2_EFFECT_ANCHOR=0` if needed.
