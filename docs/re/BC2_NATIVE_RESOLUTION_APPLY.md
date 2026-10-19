> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Native launch resolution application (build 795745)

The normal playtest launcher chooses dimensions from the selected OpenXR runtime
(or the user's explicit pair) and passes them to the host and to the game-start
helper as FVR_BC2_RENDER_WIDTH/HEIGHT. SteamVR's recommendation already includes
its configured resolution scaling. No desktop-fit scale is applied to rendering.

A live preflight without NativeProbe or an XR host observed both the D3D11
renderer and the sole main world view at the requested 1832x1920, then both at
1024x768 after startup. The INI also changed to 1024x768. The subsystem main
pointer matched the request's sole view. Thus the fault precedes stereo view
selection and cannot be repaired by disabling stereo size checks.

## Verified native contract

Offline Ghidra on a read-only project copy and executable disassembly establish:

- 0x7e44b0 constructs the graphics options object, sets vtable 0x142e570 and
  publishes it to singleton 0x1570ea0 (store at 0x7e45b3).
- 0x7dbd60 reads WindowSettings width and height into float members +0xa0/+0xa4;
  fullscreen is byte +0xac. The graphics-apply dirty byte is +0xc0.
- 0x7de030 is a no-stack-argument thiscall, returning with RET at 0x7df1a8.
  It writes these dimensions through DX11DisplaySettings.WindowSize and invokes
  the native renderer's application path. It also updates PostProcess sizing.
- GameRenderer::Update 0x5d1670 loads that singleton at 0x5d1685 and calls
  graphics apply at 0x5d168f before subsequent view construction.
- The native world build 0x5de070 reads the renderer dimension getters and calls
  the main view's viewport setter at 0x5de229. No viewport is forced by this fix.

The normal-start helper checks the exact supported executable and apply signature
before installing a detour. It changes only the identified singleton with the
verified vtable, updates dimensions/windowed state when different, marks native
settings dirty, then calls the original once. Native code retains resource/reset
ownership. Matching options do not continuously trigger resets. The kill switch
is FVR_BC2_RENDER_RESOLUTION=0; absent dimension variables preserve legacy behavior.

The package also includes tools/read_bc2.py, required by game_window.py. Desktop
mirror placement fills the monitor independently of the native eye render targets;
Windows may clamp a headset-tall client window, so that rectangle is not used as
renderer evidence. Native renderer readiness and stereo mismatch guards remain.

Validation: affected x86 helper compiled; isolated x86 ownership/dimension checks
passed (bounds, foreign owner/vtable pass-through, original forwarding, unchanged
unrelated options, no redundant reset). The latest user instruction requested
installation for manual testing, so no post-patch XR session was started. Runtime
proof still requires published stereo pairs, matching native/host dimensions,
and non-black headset/mirror output on the next normal launch.
