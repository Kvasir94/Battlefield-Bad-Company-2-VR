> **Technical reference.** API details and examples below retain their stated checkpoint scope; check current source before reuse.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Native eye resolution and high compatible graphics

The packaged playtest launcher queries the chosen OpenXR runtime before creating
the game. `Resolve-BC2EyeRenderProfile` uses the host probe's primary stereo view
recommendation (the maximum width and height across both views), then passes the
same dimensions to the native game settings and XR host. It does not fit them to
a desktop work area, pin a particular headset resolution or add supersampling.
Both explicit dimensions may be supplied together for a rollback comparison.
Missing readiness and dimensions outside the supported 128–8192 bridge range
fail before game creation. Session creation also checks the runtime view limits.

`HighCompatible` sets the existing native texture, shadow, detail, terrain,
vegetation, static-object, soldier, vehicle, effects and water quality keys to
`high`, with bloom enabled and DX11. The highest native quality tier is High;
texture changes require a fresh game. The [NVIDIA BC2 graphics guide](https://www.nvidia.com/en-us/geforce/news/bfbc2-tweak-guide/)
documents those native tiers, texture restart requirements, bloom/water keys and
the DX11 setting.

`MainQuality=custom` preserves the individually selected High categories; the
native High preset would overwrite MSAA and Aniso with index 2. `Aniso=4`
requests 16x filtering, capped by the device maximum: the native apply function
computes `1 << Aniso`, so writing `16` would be an incorrect enum value.

MSAA selects native option index `0`, not a literal sample count. The native
DX11 device list is sorted by quality/count; its ordinary lowest entry and
explicit fallback are `{1,0}` (no multisampling). Index `1` usually selects 2x
and is not guaranteed single-sample. See the [static settings contract](re/BC2_GRAPHICS_SETTINGS_CONTRACT.md).
The bridge color textures and
XR scratch/depth validation use single-sample resources; higher native AA and
driver-specific CSAA are not admitted by this profile. Ambient occlusion (`HSAO`)
is preserved from the user's existing settings pending stereo validation.
Runtime/driver
refresh, link settings, FOV, audio and render-ahead preferences are preserved.

INI updates keep unrelated sections, comments, duplicate-key consistency,
encoding and a byte-exact backup. The packaged launcher additionally restores
the original settings bytes when its game/session returns. This profile has
PowerShell fixture coverage; actual render dimensions, appearance, filtering
and performance still require the next admitted playtest. No effective 16x
filtering claim is made: the native capability clamp has its own static path,
and the active sampler/resource state has not been measured for this profile.
