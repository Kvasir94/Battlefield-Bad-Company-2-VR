> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Recoil pitch and muzzle/ejection effect anchoring — 2026-10-11

Branch `fix/recoil-muzzle`, based on `aaa36be` (`fix/hand-authority-support-grip`,
itself on `codex/bc2-resume-20261011`). Offline source work only: no game or
headset run backs any claim here.

## Codex `recoil-transfer-20261011`

`dd15673` and `23f37a6` are already on this line as `733ae80` / `7f285ae`
(`git cherry` marks both as patch-equivalent). They are the opt-in sway-tick
observer and the recoil ABI fixture. Nothing was cherry-picked.

## Recoil pitching the barrel down

### Already fixed before this branch (b74560c)

`NativeRecoilRotation` used to apply only the coordinate reflection `D*M*D`.
The native camera consumer `0x89c490` also subtracts pi from the heading, so
the native negative-pitch rise became a dive. b74560c applies `X*M*X` with
`X=diag(-1,1,1)` and checks it in `NativeMuzzleRise`
(`docs/re/BC2_RECOIL_AIM_BASIS_20261012.md`). That fix is in the installed
`b74560c` package. Any playtest report of a dive on a build older than
b74560c is explained by it.

### Remaining causes addressed here

1. **Spring overshoot (high confidence for "kick, then dips").** GunSway's
   response is a damped spring (`0x6cc760`) chasing a signed pitch target. On
   recovery it overshoots past zero, and a wide authored heading range can also
   produce a positive (downward) raw pitch. The absolute sample was transferred
   unchanged, so for those ticks the held barrel dropped *below* where the hand
   holds it. In VR this reads as the gun diving, not as the view settling.
2. **Recoil basis was not the drawn barrel (medium confidence).** The pivot is
   correct: the transfer is `inverse(bore) * R * bore`, pivoted on the solved
   firing wrist. The basis, though, came from verified model axes (only for
   profiled or authored guns) or else from the separate OpenXR aim pose. Since
   `aaa36be` the drawn gun follows the controller wrist and the native
   weapon-in-wrist fit, so on unprofiled guns the aim pose can differ from the
   visible barrel in roll. A model frame with a reversed forward axis pitches
   the recoil exactly backwards (down) and mirrors its yaw.
3. **IK composition (low).** With IK hands, the angular recoil set
   `placed = placed * delta` directly. If the arm solver clamped short of the
   recoiled wrist, the gun left the hand. The kick path already re-seated the
   gun on the solved wrist.

### Fix

| File | Change |
|---|---|
| `Bc2NativeRecoilTransfer.h/.cpp` | `NativeRecoilMuzzleUp`: if the converted aim-local rotation takes the bore below the baseline, replace it with the pure heading rotation of that bore. The native yaw/heading is kept and only the descent (and its small accompanying roll) is dropped. A rise is returned unchanged. `NativeRecoilGripDelta(..., muzzleUpOnly, rectified*)` applies it. `MuzzleBoreFrame(weapon, muzzle, upRef, min)` is a rotation-only bore frame: +Z runs from the weapon root to the muzzle, and +Y is the reference up made orthogonal. It rejects a muzzle at the root and a barrel parallel to up. |
| `Bc2RigPublication.cpp` | The recoil basis is the visible barrel. The drawn `jntWpn_Flash` bone (native evaluated relation to the weapon root, applied to the placed gun) gives +Z, and the controller aim's up gives roll. Verified model axes stay first unless they disagree with the drawn barrel by more than 60°; in that case the muzzle frame wins and `model_bore_mismatch` counts it. Without model axes the muzzle frame replaces the XR aim pose. Guns with Flash at the root (M416) keep their model axes. The configured effect offset is intentionally not used as a bore source, because its sign convention is unverified. The muzzle-up clamp is applied to the angular delta. The recoiled gun is re-seated on the solved wrist through `PresentedWeaponFrame`, as the kick already was. The kick uses the same bore. |

Unchanged: native shot orientation and spread, the native getter or ballistic
baseline, `boreInGrip` native aiming, kick envelope magnitudes, and the
absolute non-accumulating transfer.

New report fields (`native_recoil_transfer`): `muzzle_down_rectified` counts
ticks; `muzzle_bore_frames` and `model_bore_mismatch` count frames while a
recoil or kick sample is present.

## Muzzle flash and shell ejection (XM8-FX-001)

### State before this branch

XM8-FX-001 was reported on build `66fc2bd`, session `053042-223`. That build
predates the FireEffects FOV-compensation bypass (`3d24bce`/`04d8da8`,
`docs/re/BC2_EFFECT_FOV_COMPENSATION_20261011.md`). Compose `0x7f90b0` scales
camera-local X/Y of first-person effect positions by `tan(f8/2)/tan(fc/2)`.
That displaces the flash and shell origin off the barrel, and the displacement
varies with gun rotation, which matches the report. The bypass, plus
re-anchoring onto the drawn root (which includes visual recoil and kick), is
already in b74560c. It has not been headset-checked.

### Gap fixed here

Both the FOV bypass and the re-anchor required a current published
`WeaponShotFrame`. That frame exists only when the rig finds a `jntWpn_Flash`
child of the weapon bone and the pose lease is current. Without it, the
effects kept the warped, world-fixed native spawn, even though
`RetargetWeapon` had written the VR gun root into the very buffer the effects
read. The counter `reanchor_skipped_unwritten` misreported this case as
"unwritten".

Now, if the getter root was written by the VR rig in the same animation
update:

- the compose hook bypasses the FOV warp (`fov_bypass_written_root`), and
- re-anchoring uses that written root (`reanchor_written_root`). This root is
  the drawn gun without private visual recoil. It is never a native root.

When a shot frame is current, the previous path is unchanged: the effects
follow the visual root with recoil and kick, and the per-entry native offsets
keep the flash and the ejection port as separate sockets. No offsets are
authored.

## Kill switches (default = new behaviour)

| Variable | Effect |
|---|---|
| `FVR_BC2_RECOIL_MUZZLE_UP_ONLY=0` | Transfer the raw native recoil, including the downward overshoot |
| `FVR_BC2_RECOIL_MUZZLE_BORE=0` | Recoil basis from model axes or XR aim only (b74560c behaviour) |
| `FVR_BC2_EFFECT_WRITTEN_ROOT=0` | FOV bypass and effect re-anchor need a current shot frame again |
| `FVR_BC2_EFFECT_ANCHOR=0` | (existing) native fire-and-forget effects, no bypass |
| `FVR_BC2_NATIVE_RECOIL_TRANSFER=0` | (existing) no native recoil on the held gun at all |

## Checks

- `Bc2NativeRecoilTransferTests` gained `MuzzleUpOnly` and
  `MuzzleBoreFromDrawnBarrel`:
  - A raw dive is clamped with heading and pivot kept, and a rise is untouched.
  - The muzzle frame raises the drawn muzzle where a reversed model frame
    dives.
  - The degenerate cases (muzzle at root, barrel parallel to up) are rejected.
- `Bc2WeaponEffectsTests` covers the config struct and JSON writer.

## In-game test steps

1. Install the integrated candidate through the launcher. The package hash
   check must show the new DLL.
2. XM8, hip fire, single shots with the gun level: the barrel kicks **up** and
   back, then returns to where the hand holds it. It never dips below. Repeat
   on full auto.
3. Repeat on M416, MG36, VSS and one unprofiled gun (AKS74u or F2000), also
   with the gun rolled about 45° in the hand. The recoil follows the gun's own
   up direction.
4. Hold the support grip and fire: the left hand stays on the handguard while
   it recoils.
5. Muzzle flash: fire while sweeping and rolling the gun. The flash stays at
   the barrel tip and shells leave the ejection port on every gun, including
   guns that publish no shot frame.
6. Trace: check `native_recoil_transfer.muzzle_down_rectified` (above 0 on auto
   fire), `muzzle_bore_frames` and `model_bore_mismatch` (expected 0 on
   profiled guns; above 0 means an authored axis is reversed, so record which
   gun). Under `weapon_effects`, check `fov_compensation_bypassed` (about
   2 × shots), `fov_bypass_written_root` and `reanchor_written_root`.
7. To isolate a regression, retry with the matching kill switch above.

## Open risks

- No in-game confirmation yet. The overshoot cause is inferred from the native
  spring contract and the b74560c report ("kick, then down"). It is not proven
  by a capture of this build.
- `boreInGrip` (native aiming) still uses the model axes. If
  `model_bore_mismatch` fires, the aim is also off for that gun. Fix that
  gun's axes rather than masking it.
- The clamp changes native feel slightly: the settling dip after recovery no
  longer appears on the held gun. The camera is unaffected because the HMD
  never carries recoil.
- A worktree build has no private authored grip, support or contact headers.
  It proves compilation only and is not an installable candidate.
