> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 first-person effect FOV compensation

## Evidence and target

Target: build 795745, preferred image base `0x400000`, SHA-256
`c603f686b2ea9cc7ec84e8548fddf3b2e8e0be5a62f3f4860148e543906b0162`.
REA responded but rejected the existing `.gpr` target as an unsupported binary
header. Earlier fresh imports could not complete. The permitted fallback used
Ghidra 12.1.3 on the existing private project copy with `-readOnly -noanalysis`;
no game process was attached or modified. Retained private packets:
`tmp-recoil-contract-20261011/effects-compose-20261011-2340` and
`effects-fov-20261011-2343`. File-byte hashes below were independently checked
against the current c603 executable in `D:/bc2-re/BFBC2Game_795745.exe`.

## Exact native contract (static disassembly)

`0x7f90b0` is `void __thiscall(effects, root, camera, offset, rotation,
firstPerson, output)`. ECX holds the effects object; the six arguments occupy
`[EBP+08..1c]`, and the function returns with `C2 18 00` at `0x7f9561`.
The input offset is at `[EBP+10]`, rotation at `[EBP+14]`, the flag at
`[EBP+18]`, and the output at `[EBP+1c]`.

The function copies the root, composes authored degree rotations (Y, X, Z),
rotates the selected offset by the root through `0x403970`, and adds it to
translation. At `0x7f941f` it tests `firstPerson`. If true and effects floats
`+f8` and `+fc` differ, `0x7f945b..0x7f94e6` performs:

```
p = worldPoint * inverse(camera)
p.x *= effects[0x100]
p.y *= effects[0x100]
worldPoint = p * camera
```

Z is retained. After that branch the native particle basis conversion swaps
rows 1/2 and negates the old row 1; translation is preserved. The flag has no
other use anywhere in the complete function.

`0x7eff70` refreshes `+f8` from `0x7af9e0 -> 0x78d520` and `+fc` from its
float argument (falling back to `+f8` if nonpositive). It computes
`+100 = tan(f8 * pi / 180 / 2) / tan(fc * pi / 180 / 2)`.
This proves an FOV projection adjustment, not a time-dependent zoom-transition
blend. It may apply while unzoomed. We do not rely on unproven field names for
the upstream FOV providers.

`0x823b30` selects FireEffects1p versus FireEffects3p and each entry's normal
versus zoom offset/rotation before calling the compose function. Its FireEffects
call is `0x823cd9`, return `0x823cde`, immediately followed by the per-entry
asset spawn at `0x823ceb`. Its other compose call at `0x823b97` belongs to the
persistent overheat path and is outside this change. The selected entry's
transform and particle asset stay separate for flash and shell ejection.

## Binding fingerprints

Compose prefix (unique executable-section signature):

```
55 8B EC 83 E4 F0 81 EC B4 00 00 00 53 8B 5D 08
D9 03 56 8B 75 1C D9 1E 8B 45 14
```

The complete `0x4b4` bytes starting at `0x7f90b0` have FNV-1a64
`134a88c946902ee1`, SHA-256
`891a7bd1a12f317af5012208ccd4aa7e4c86de1f3ce8f5c2c54115b1b541531b`.
The discovery code pins that complete body and the caller argument construction
at Update `+0x18f`, resolves the call at `+0x1a9`, and records return `+0x1ae`.
Installation compares all `0x4b4` live bytes to the verified file before hooking.
The FOV producer `0x7eff70`, length `0xf7`, has FNV-1a64 `275cfa2a65cafc3a`,
SHA-256 `ef8e92c461c199cdd81868873a41e3ff918a756d1c05dab443ff0388d6ab7f07`;
it is evidence only and is not called or hooked by this change.

## Scoped VR behavior

The compose hook calls the original exactly once. Only for the verified
FireEffects call, inside this local player's animation scope, does it pass
`firstPerson=false`. It revalidates current soldier, weak token and weapon;
requires the effects pointer to equal that weapon's verified `+0x38` field;
requires the exact caller-owned root pointer; and requires a current immutable
VR weapon pose with valid identity/equipment/ownership and deadline guards.
The existing `FVR_BC2_EFFECT_ANCHOR=0` kill switch disables the bypass as well
as anchoring. The original still composes each native selected offset,
rotation and particle basis. No effect data, FOV state, shot rotation, native
weapon getter, or remote actor is modified.

The existing anchor then stores the uncompensated per-entry local transform
and moves it onto the visible private weapon root, including visual recoil.
`weapon_effects.fov_compensation_bypassed` counts admitted calls. Flash and
shell effects are not collapsed onto one muzzle transform.

## Observed problem and validation boundary

Before this fix, M416 replay `native-trace-20261011-230730-213` retained two
spawn transforms whose local position was approximately
`(-0.0632165, 0.056..0.057, 0.899427)`, despite the extracted ordinary effect
offset `(0, 0.05, 0.9)`. The lateral displacement was already in the native
spawn before the VR anchor. The newly proven camera-space warp is a concrete
shared mechanism that can produce this displacement. It is not proof that
every prior XM8 visual complaint had the same cause.

The existing file-backed GameplayDiscoveryProbe now mutates the new callsite,
first-person branch, FOV scale/XY multiplies, and stack cleanup, ensuring these
changes reject discovery. Runtime confirmation should show admitted bypasses
and uncompensated native local effect offsets; no post-fix visual or headset
result is claimed in this static evidence note.
