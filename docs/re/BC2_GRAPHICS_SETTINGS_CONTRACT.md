> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 graphics settings contract (build 795745)

Curated static findings from the native x86 executable, inspected with Ghidra on
a read-only project copy. Addresses below are source executable VAs. No native
assets, decompiler dumps, runtime traces, or installed settings are included.
This establishes parsing and application behavior; it does not establish the
active renderer, device capabilities, effective filtering, or accepted resource
sample dimensions in a running game.

## INI values and quality presets

The loader at `0x007dbd60` reads `[Graphics]`. `MSAA` is parsed as a decimal
integer into the options object at `+0x28`; `Aniso` goes to `+0x2c`. The writer at
`0x007dd1f0` serializes those integers back to the INI. Neither operation converts
an INI number into a literal sample count or a literal anisotropy factor.

The quality-string parser at `0x007cc980` recognizes these canonical tokens:

| Token | Internal value |
| --- | ---: |
| `low` | 0 |
| `medium` | 1 |
| `high` | 2 |
| `custom` | 3 |

Use the canonical lowercase spelling. Unknown or missing `MainQuality` is a
different path from valid `MainQuality=custom`: valid parsing stores the value at
options `+0x9c` and clears the fallback flag at `+0xae` (`0x007dcb13` and
`0x007dcb19`). Failed parsing sets that flag; the loader then copies `Detail`
into Soldiers, Vehicles, Overgrowth, Undergrowth, StaticObjects, and Terrain.

Native application at `0x007de030` applies the low/medium/high presets. Its high
branch (`0x007de12c` through `0x007de158`) overwrites the category levels with 2,
enables Bloom and HSAO, and also writes `MSAA=2` and `Aniso=2` into the options.
Valid custom value 3 skips these preset writes, preserving explicitly loaded
category levels, Bloom, HSAO, MSAA, and Aniso. Consequently, custom is required
when keeping high category settings while independently choosing AF and AA.

The recognized category keys for high detail are:

```ini
[Graphics]
MainQuality=custom
Effects=high
Soldiers=high
Vehicles=high
Overgrowth=high
Undergrowth=high
StaticObjects=high
Terrain=high
Shadows=high
Water=high
Texture=high
Detail=high
Aniso=4
MSAA=0
```

This fragment is a settings contract, not a full replacement INI. Preserve the
other explicitly loaded fields, including the existing Bloom and HSAO choices.
The loader stores Bloom and HSAO independently at options `+0x24` and `+0x25`;
custom does not disable them. Application copies them to world settings at
`+0x1a1` and `+0x197`, respectively.

## Aniso is an exponent before a native clamp

At `0x007df04a` through `0x007df052`, native application loads options `+0x2c`
into the shift count, initializes the requested value to 1, then shifts left.

| `Aniso` INI value | Requested anisotropy before clamp |
| ---: | ---: |
| 0 | 1 |
| 1 | 2 |
| 2 | 4 |
| 3 | 8 |
| 4 | 16 |

Therefore `Aniso=4` requests 16x. `Aniso=16` is not the encoding of a 16x
request. The resulting value is bounded before being written to
`ShaderSystemSettings.MaxAnisotropy` at `+0x40` (`0x007df0ce`).

The bound getter at `0x007bc140` initializes a factor to 16, checks the renderer
kind through virtual slot `+0x0c`, and obtains a device-specific factor via
`0x009d61c0` for kind 0. It then returns the bit-scan index of the factor. The
non-kind-0 path therefore returns 4, and the application compares the requested
factor against that returned value. This is an observed static contract; it
does not support claiming effective 16x filtering.

The DX11 constructor at `0x009f3d50` uses the named `Dx11DisplaySettings` and
installs vtable `0x014497a0`. That vtable's `+0x0c` function, `0x009d8110`, returns
kind 2, which follows the static non-kind-0 bound of 4. No active runtime
renderer selection or effective sampler state was inspected for this finding.

## MSAA is a renderer-option index

At `0x007de27a` through `0x007de296`, application obtains the renderer's sample
option vector through virtual slot `+0xa8`, computes its length from 16-byte
entries, bounds options `+0x28` to the last entry, then copies the selected
entry's byte 0 to `GameRenderSettings.MultisampleCount` at `+0x64` and byte 1 to
`MultisampleQuality` at `+0x50`. These fields subsequently feed world sample
count/quality at `+0x3c` and `+0x44`.

For the DX11 vtable, `+0xa8` points to `0x009f4040`, which returns the vector at
renderer `+0x30`. The vector builder at `0x009fb850` clears this vector and:

- In its fallback feature-level branch, inserts exactly `{count=1, quality=0}`
  (`0x009fb8c6`, `0x009fb8cb`). Its only option index is 0.
- In the ordinary branch, calls the device's
  `ID3D11Device::CheckMultisampleQualityLevels` slot `+0x78`, using format `0x2d`
  and candidate counts from 0 through 31. It admits only successful responses
  with a positive quality-level count, storing the candidate count and quality
  0 (`0x009fb991`, `0x009fb995`). The NVIDIA ordinary pass stops above count 4,
  then separately adds supported vendor modes.
- Sorts the vector through `0x009fae90`. The comparator in `0x009eb110`
  (`0x009eb160` through `0x009eb171`) orders entries by ascending quality first,
  then ascending sample count.

Thus the single-sample option is index 0 for the conforming DX11 list starting
with `{1,0}`, and is explicitly index 0 in the fallback branch. `MSAA=1` selects
the next supported option when present; it is not a universal encoding of
single-sample rendering. Higher INI values cannot be interpreted as fixed
sample counts across devices. The actual populated list and accepted resource
dimensions remain runtime verification items.

Microsoft documents disabled AA as Count 1, Quality 0 in
[DXGI_SAMPLE_DESC](https://learn.microsoft.com/en-us/windows/win32/api/dxgicommon/ns-dxgicommon-dxgi_sample_desc),
and describes device-dependent acceptance in
[CheckMultisampleQualityLevels](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkmultisamplequalitylevels).
