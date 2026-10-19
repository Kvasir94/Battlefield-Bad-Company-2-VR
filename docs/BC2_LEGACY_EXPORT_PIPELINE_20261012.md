> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 legacy export overlap repair — 2026-10-12

## Matching user playtest

Installed package: `<repo>\build\five-distinct-04d8da8\BC2-VR`.
Session `native-xr-session-20261012-005103-682`, trace
`native-trace-20261012-005105-487`. Native source `278ba9e`, resolution
helper/launcher source `422960b`, host source `0524f7c`.
User ended this playtest; the repair agent did not interrupt any process.

The black-screen resolution repair held: both native eye rectangles were
1832x1920, matching host allocation, with 29,315 viewport matches and zero
size rejections/camera restoration failures. The user confirmed visible world.

Delivery remained poor: 14,445 pairs in the 608-second telemetry interval
(23.76 pairs/sec), versus 30,565 world calls (50.27/sec). Host ended 71,823 XR
frames, submitted 14,441 fresh pairs, and reused 57,106. Mean request completion
latency was 64,214 us; 13,979 completions exceeded 50 ms. No pair rejection,
GPU copy fault or copy timeout was recorded. This does not establish SteamVR
compositor reprojection or encoder performance; those were not measured.

Native `sharing_mode=3` means legacy fenced IPC. Native progress recorded
16,002 `bridge_no_lease` against 14,658 accepted frames, zero second-pair
captures and 202 pair-busy captures. The PC mirror made 31,525 successful
presents; that does not mean 31,525 distinct stereo pairs reached the headset.

## Cause and implementation

Legacy export waited for its GPU EVENT on the render thread while the staged
channel remained in Rendering. Consequently the next visibility callback
could not obtain a lease. This mode also restricted export storage to one pair,
despite the host asking for pipeline depth two. The named keyed path already
supported two independent pairs. Changing eye resolution or disabling its
mismatch guard would not correct this scheduling barrier.

The repair gives legacy fenced mode two export pairs, each with its existing
independent completion query and exact frame lease. After the source copies
are queued and native state restored, the producer may seal that frame's CPU
metadata and admit the second request. A sealed shared slot stays Rendering;
it is published only after its own GPU query completes. Its late-latched pose,
body props and identity are retained separately from the successor.

Explicit request-ID publication/cancellation operates on the sealed slot without
altering the current render slot. Positive GPU ownership feedback still waits
for the consumer copy. No new waits, game replay, viewport overrides, wire
layout changes, runtime changes or quality reductions were added. Existing
`FVR_XR_PIPELINE_DEPTH=1` disables overlap. The helper-device relay stays serial.

A standalone x86 hardware capability probe on the RTX3080Ti supported keyed
sharing at the game's dimensions/flags, including sRGB. This did NOT reproduce
the documented BC2-process restriction. The repair therefore preserves BC2's
working fallback instead of assuming a driver, format or compatibility fix.

## Validation

- x86 and x64 StagedFrameProducerTests and AsyncFrameChannelTests passed.
- Added deterministic sealed-export tests: no early publication; next-frame
  admission; no third lease; exact pose/props/frame association; stale/mutated
  lease rejection; independent cancellation; reconnect rejection; callbacks
  only on the graphics thread.
- Explicit x86 LegacyFramePipelineTests passed on hardware: 16 pairs at
  1832x1920, eight captures in slot two, 15 full-image exact-pixel comparisons,
  one intentional discard, successful continued reuse, zero bridge failures.
  Both frames are captured before either is acknowledged.
- x86 producer to x64 consumer legacy IPC passed exact eye pixels, tracking
  roundtrip, discard recovery, peer exit and abandoned-mutex handling.
  This existing fixture used FVR_PAIR_REUSE=0 because it explicitly requires
  descriptor recreation; the new overlap fixture tested default reuse.
- All standalone GPU work was coordinated before KF1's runtime window; neither
  BC2 nor a headset session was launched by this repair.

Evidence is preserved at
`%USERPROFILE%\Documents\Codex\2026-10-11\task-10\performance-005103`.
Full gun-event log SHA256:
`5FA74C7AB104588CBFCD6F471F19A9C87DFD8ED003C9EC01BD14203773F2E9EE`.

The transport regression proves safe overlap and correct pixels, not a measured
headset FPS gain. A combined normal-launch test must compare fresh-pair rate,
lease misses, second-pair captures, pair age and host reuse against this session.
Gameplay and original gun-material repairs remain separately owned.
