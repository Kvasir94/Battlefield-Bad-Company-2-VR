> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# SPAS pump and XM8 rack grasp: asset evidence (2026-10-11)

Evidence for `docs/SPAS-PUMP-BOLT-GRASPS-20261011.md`. Offline, CPU only. The
data was decoded from the installed game archives with the repo's own
`tools/bc2_weapon_animation_pipeline.py` (`Clip.evaluate`) against
`Characters/Skeletons/ske01.res` (`d1d713cc…`, rig `fnv1a64:a7f219a1426216ab`).
No game process, native call, hook or GPU was used. No new native VA or
signature was needed: the pump's native authority is the existing
`PumpHoldGate`, built on the state-7/8 evidence in `BC2_DISASM_PUMP_AND_DRY_FIRE.md`
and `docs/BC2-PUMP-RELOAD-20261001.md`.

## SPAS pump (existing private profile, re-checked)

`NativeActionPoses-racks.private.h` (the integration recipe's
`BC2_NATIVE_ACTION_POSE_HEADER`) carries `SPAS12_sp` / `UL_shg_SPAS-12_Mesh`
`Pump` from `UL_shg_SPAS12/1P/1P_BoltAction.res`:

- 64 frames at 60 Hz, 1.033 s. Parts `jntWpn_4` (fore-end, driven),
  `jntWpn_3`, `jntWpn_5`. Peak at frame 19 (t ≈ 0.317 s).
- Native fore-end stroke: **94.5 mm**. The wrist moves with it
  (z −0.668 → −0.573 m) and returns by t ≈ 0.55 s; the rest of the clip is
  closed and still.
- Progress is the driven part's displacement over that stroke. Frames after
  the peak are `returning`.

The native pump replay that follows a rack (state 8 → listener `+0x30`) plays
this clip. Its length (1.033 s) plus the up to ~0.7 s early-rack wait in
native state 6 (`PumpHoldGate` comment, run 091024) sets the 1.9 s settle
default.

## XM8_sp_s has no native rack of its own

`Dist/win32/async/weapon/sp_rgl_xm8_scoped-00.fbrb` holds these 1P clips for
`US_rgl_XM8`: Lower, deploy, HandsIkPose, UnDeploy, FireStart, AltDeploy,
ZoomFire, FireRelease, Reload, Fire (plus the scoped zoom clips). The only
reload clip, `US_rgl_XM8/1P/1P_Reload.res` (2.90 s), was sampled at 30 Hz:

- `jntWpn_17` (charging handle) displacement is **0.0 mm for the whole clip**.
- The left pinch (thumb/index tip midpoint) never comes closer than
  **132 mm** to the handle.

The game therefore authors no hand rack for this gun. `1P_FireStart.res`
moves the carrier and handle together (~120 mm, the existing
`FindChargingHandleParts` stroke) with no hand involved.

## XM8 LMG supplies the same receiver's native rack

The rest transforms in each gun's own `1P/HandsIkPose.res`, weapon-relative, in metres:

| Bone | `US_rgl_XM8` (XM8_sp_s) | `US_rif_XM8_LMG` |
|---|---|---|
| `jntWpn_17` handle | (0.0, 0.0697, −0.4967) | (0.0, 0.0697, −0.4967) |
| `jntWpnwpnJnt_16` carrier | (0.0, 0.0697, −0.3831) | (0.0, 0.0697, −0.3831) |
| `jntWpn_6` magazine | (0.0, −0.0624, −0.4402) | (0.0, −0.0624, −0.4402) |

They agree to under 0.1 mm. The XM8 bolt asset's grab point,
(0, 0.069722, −0.496687), is the same handle origin.

The private profile `XM8 LMG` / `US_rif_XM8lmg_Mesh` `Rack` is the window
4.733–5.333 s of `US_rif_XM8_LMG/1P/1P_Reload.res`. It has 37 frames and peaks at
frame 13. The handle stroke is **117.3 mm**, against 120 mm for XM8_sp_s. Its
closed frame-0 handle sits at (0.0, 0.07, −0.497). The spec's
`rack-moving-window-audit` records a native pinch window of 4.60–5.25 s, with
the minimum pinch 22 mm from the handle. That is a real native hand-on-handle
rack.

Other rack candidates (`rack-window-audit.private.json`):

- `VSS` and `MG36` already have exact `Rack` profiles.
- In `XM8C` the handle moves 85 mm, but the hand stays at least 104 mm away,
  so there is no grasp.
- `M416`, `AKS74u` and `UZI` show no handle motion.

## Runtime binding

`FindNativeRackGraspDonor("XM8_sp_s")` → `XM8 LMG`, handle `jntWpn_17`, rest
(0, 0.069722, −0.496687), tolerance 2 mm. `NativeRackGraspDonorPart` admits the
donor only when the profile is a valid `Rack` for that exact asset and mesh,
carries the handle once, and its closed outbound sample lies within that
tolerance of the recipient rest. Otherwise the existing fallback is unchanged.
