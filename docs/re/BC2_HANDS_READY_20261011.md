> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Native hands appearance and variant coverage â€” October 11

The replacement uses each first-person mesh's own glove geometry and vertex
attributes under its existing native material. This fixes the almost-black
Assault glove: the previous cross-variant asset retained Standard UVs while
Assault's shader sampled a different texture atlas region. The third UV channel
feeds a final RGB multiplication. Native frame and shader analysis did not
support changing normal signs or introducing replacement lighting.

Each asset preserves native hand/finger weights and original glove attributes.
The sleeve is cut at the wrist; residual proximal influences are folded into
the wrist. Closed caps use a coherent native cuff UV tuple, recessed centers
and smooth cap normals. Vietnam US retains its native bandage as a separate
material overlay over the closed hand body. No game assets are committed.

## Variant coverage

The installed archive index inventory found ten distinct first-person meshes:

| Family | Mesh variants | Native vertex layouts |
| --- | --- | --- |
| US base | Standard, Assault, Pads, Recon | half positions, 48-byte color / 16-byte depth |
| Russian base | Standard, Pads, Recon | half positions, 48-byte color / 16-byte depth |
| Vietnam | US Assault | half positions, 48-byte color / 16-byte depth |
| Vietnam | NVA Recon | float positions, 64-byte color / 32-byte depth |
| Beach | US Infantry | half positions, 64-byte color / 16-byte depth |

BC2NSK01 remains compatible. BC2NSK02 supports variable section counts and
multiple retained materials. BC2NSK03 explicitly distinguishes Standard (0),
NVA float (1), and Beach half (2); a 64-byte stride alone cannot identify the
layout. Color/depth indexed positions and effective skeleton weights must
match. Each source vertex/index range still requires its exact GPU SHA256
before replacement. The loader admits the complete set atomically and retains
player ownership, graphics-thread resource creation and frame-boundary
activation. Packs are loaded from `native-skin-hands` beside the injected DLL.
`FVR_BC2_NATIVE_SKIN=0` is the kill switch.

## Appearance evidence

The private `reports/native-hands-ready-20261011` evidence includes native shader
contracts, installed archive inventory, reproducible input and all ten v5 pack
hashes. All ten pass writer regeneration, native body attribute preservation,
finite geometry, closed-body/cap winding and indexed color/depth parity checks.
The native open Vietnam bandage is validated as an overlay, not a closed body.

The v5 Assault fixture uses a defined neutral controller pose without a headset
or physical controller producer. Both saved eye images show native fabric detail,
short wrists and smooth recessed caps. It produced 5,496 replacement submissions,
8 exact source hash matches, no source mismatch or ownership rejection. Probe
exit was 0; the game remained responsive for 22.1 seconds after completion with
no new crash report. Settings were restored and all six protected installed or
selected file hashes matched. This appearance run used the previously proved
DLL; final combined-loader validation is recorded below when completed.

Only Assault currently has this live appearance proof. The other nine variants
have source/layout and generated-asset checks; they still need in-game map/class
coverage. Real headset motion, scale, wrist rotation and interaction feel remain
unmeasured by the static fixture.

## Build validation

The x86 native DLL and geometry test target compile with two jobs. The focused
request-registry and geometry tests pass, including atomic set rejection,
multiple materials, malformed format/stride combinations, nonfinite positions,
ambiguous sources and skin parity. The production reader accepts all ten frozen
private packs together (`Bc2NativeSkinGeometryTests --packs ...`).
`Start-NativeTrace.ps1` passes PowerShell parsing and stages the complete directory
beside the injected DLL. Runtime proof of the combined set is recorded in the
final package evidence.

## Final candidate

Compiled source `a0fa4fef10ab974ed937dfe219647b43f566285e` is packaged at
`<repo>/build/native-hands-ready-a0fa4fe`. Native DLL SHA256:
`30DB35E4D0F8E4203F0806EC8F85065857FC4925133331CD0A5015948225BE66`.
The launcher VerifyOnly and independent rehash both pass all 34 payload files.
No install or selection occurred.

The final defined-pose run loaded all ten packs and published 61,968 replacement
GPU bytes for Assault. It recorded 4,992 admitted submissions, 4,957 final native
replacements and eight exact source hashes with zero mismatches. Geometry's 30
fallbacks were 26 graphics-thread cache misses, two pending source checks and two
frame activation waits. Four requests failed the final ownership/admission check;
five accepted geometry results were not submitted after the subsequent ownership/
admission recheck. These retain native fallback; no gate was weakened.

Both final eye images were inspected. Probe exit was zero, no new crash report
appeared, and the game remained responsive for 23.5 seconds after completion.
Settings were restored and all six protected installed/selection hashes matched.
Private final evidence: `reports/native-hands-ready-20261011/ready-proof.private.json`
and `ready-eye-0.png` / `ready-eye-1.png`. These results establish the combined
loader and Assault appearance; they do not establish headset or other-map acceptance.

## Short real-player sequence

Use the prepared candidate's `Start-BC2-Playtest.ps1 -HandsMode ik` with the
connected headset runtime. Keep the existing selected package available.

1. In the XM8 fixture, inspect both hands palm-up/down and rotate wrists through
   the normal range. Check fabric shading, cuff closure and agreement between eyes.
2. Raise/lower and aim the weapon, attach/release the support hand, then switch
   to M9. Check that the original forearms stay hidden and finger poses remain stable.
3. Perform a reload and recover to the grip; check hands, magazine contact and
   weapon alignment together. Check a shadowed area as well as the bright snow.
4. In available base/Vietnam maps, change faction/class to cover the remaining
   native meshes. Record the map/class if fallback arms or a material issue appears.

Selection/install and an interactive headset run require the user's separate
authorization under the original project instruction. Preparing this candidate
does not change the selected build or establish headset acceptance.
