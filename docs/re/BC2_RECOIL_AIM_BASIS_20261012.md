> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# GunSway camera-local to tracked bore-local recoil

The playtest reported a backward kick followed by downward barrel pitch. The retained VSS replay independently contained a downward barrel: at generation 319, the canonical weapon row 2 Y was +0.0271958709, so its native model -Z bore Y was -0.0271958709. The un-recoiled interaction weapon had approximately zero pitch. This occurs before considering the arm solver.

## Native evidence

Target SHA-256: `c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162` (`BFBC2Game_795745.exe`). REA could not open the saved project (unsupported binary header); prior fresh imports did not finish. Targeted Ghidra fallback used the existing isolated copy of the saved project with `-readOnly -noanalysis`, without attaching to or changing the live game. Retained packets are in `<repo>/tmp-recoil-contract-20261011/recoil-sign-20261012` and `recoil-sign-roll-20261012`.

- `0x6d63c5`: `d9 e0 d9 5e 2c` (`FCHS; FSTP [ESI+0x2c]`) stores negative pitch for upward recoil. The spring target `0x6d5d10` passes this signed pitch through.
- `0x6d5db0` constructs the spring response as native padded Rx(pitch) * Ry(yaw); in adapter row storage, Rx has rows `(1,0,0)`, `(0,cos(p),sin(p))`, `(0,-sin(p),cos(p))`.
- Native camera consumer `0x89c490` does more than copy this local matrix into a tracked aim frame. At `0x89c7a0`, `0x6dd030` extracts `acos(forward.y/length)-pi/2`, then `0x89c7a5` negates it. At `0x89c7af`, `0x6dd190` extracts heading, then **`0x89c7b4` subtracts pi**. `0x89c7bf` obtains roll from `0x6e1890`, then `0x89c7c7` negates it. The consumer rebuilds its rotation, including pitch construction at `0x89ca80` via `0x52ee00`.
- Exact current bytes at `0x89c79c` (52 bytes): `8d 47 20 50 e8 8b 08 e4 ff d9 e0 8d 47 20 d9 5c 24 18 50 e8 dc 09 e4 ff dc 25 40 96 4d 01 57 d9 5c 24 28 e8 cc 50 e4 ff 83 c4 0c d9 e0 f6 86 14 01 00 00 01`.
- Double constants are exactly `3.1415927410125732` at `0x14d9640` (bytes `00 00 00 60 fb 21 09 40`) and `1.5707963705062866` at `0x1419820` (bytes `00 00 00 60 fb 21 f9 3f`). This agrees with the existing adapter's documented native aim versus RenderView heading difference of pi.

## Correction

The existing helper applied only the camera coordinate reflection `D*M*D`, with `D=diag(1,1,-1)`. That is a coordinate conversion, but not the additional camera-local to tracked bore-local heading conversion. Apply the proper half-turn `H=diag(-1,1,-1)` on both sides as well: `H*(D*M*D)*H`. Since D and H commute, this is `X*M*X`, `X=diag(-1,1,1)`.

This preserves the native yaw response and changes pitch and roll together as a basis transformation. A native negative pitch now raises the canonical +Z bore. There is no extra transpose or Euler-angle decomposition, and no change to the native sample, native shot orientation, absolute recovery, strength, or shot-driven translation envelope. World transfer remains `inverse(aim)*localRecoil*aim`, pivoted about the resolved firing wrist.

KF2's reference path applies its native recoil buffer to the desired bore rotation before fitting the model and anchoring the primary grip (`VRHandsBridge.uc`, `DesiredRot += ItemRecoil` and final rotated primary-grip subtraction). BC2 similarly needs an aim-local response rather than directly copying a camera-local rotation. Anatomical primary-hand authority and deriving native base aim from the current physical bore are separate integration work.

## Bounded verification

`Bc2NativeRecoilTransferTests` adds independent bore-direction checks using native negative pitch and nonzero yaw with both level and rolled/yawed aim. It checks upward motion in the hand's up axis, preserved native yaw direction, fixed firing-wrist pivot, complete roll conversion, and backward kick along the raised bore. Existing tests cover absolute nonaccumulation, native identity recovery, preserved item/wrist attachment, and invalid matrices. Compilation/runtime validation is coordinated by the root task; this note makes no new live-playtest claim.

## Controller-authoritative bore follow-through

After the primary wrist becomes controller-authoritative, the physical gun can differ from OpenXR's separate aim pose. The rig now derives its bore from the exact admitted native Flash frame (generated profiles) or the existing verified model axes (baseline profiles). It uses that pre-recoil physical bore for both angular recoil and backward translation.

A separate immutable publication carries only `boreInGrip = boreInModel * weaponInWrist * wristToGrip`. The native input consumer evaluates this fixed relation against the **current** controller grip pose before writing native base yaw/pitch. It does not copy an earlier rendered weapon direction or recoil back into native aiming. Publication is allowed only with a settled attachment and anatomical wrist mapping; readers retain original input/authored source deadlines and check exact soldier, weak handle, actor/equipment generations, equipment identity and tracking space. Missing evidence uses the previous XR aim fallback. The configured native Flash capability now also admits this verified controller-grip path, so M416's native effect offset remains available.

The portable regression compares the resulting bore against the currently fitted physical weapon after moving the controller without republishing calibration. It also checks the identity relation against the existing OpenXR direction convention and rejects invalid matrices. Shared focused compilation remains coordinated by the root task.
