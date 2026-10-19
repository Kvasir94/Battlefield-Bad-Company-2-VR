> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Headset frame delivery: telemetry and findings (2026-10-11)

Symptom: the headset looks low-FPS and blurry, while the PC mirror looks smooth.
A smooth mirror does not show that the headset is getting frames on time. The
desktop window presents every game frame, uncapped (about 45 to 50 per second,
including mono frames that never reach the headset). The headset needs one fresh
stereo pair per display refresh (72 to 120 Hz).

This work reviewed the frame path statically and read the logs of real headset
sessions that were already on disk. It also added per-second delivery telemetry
to the XR host (x64) and to the native DLL (x86). There was no game launch, no
GPU workload and no headset run.

> **Update:** the stereo-every-frame changes below were made on
> `perf/stereo-every-frame`, which is cut from `codex/bc2-resume-20261011`
> (`3be9c94`) plus this telemetry. They apply to the installed producer.
>
> **Branch caution (original telemetry commit).** This branch is cut from local `main` (401b456). The
> playtest builds that are actually installed come from
> `codex/bc2-resume-20261011` (`b74560c`, `a0fa4fe`). That branch has a different
> legacy-sharing producer: two fenced pairs with deferred GPU export (see H2b).
> The host-side changes here apply to it nearly unchanged. Port the DLL-side
> changes by hand (see "Porting").

## Evidence from existing sessions (read-only)

| Session (runtime, eye size) | XR frames | Fresh pairs | Reused | Fresh share | Req to completion (mean) | Game world frames | Stereo begun | No lease |
|---|---|---|---|---|---|---|---|---|
| `build/playtest-b74560c/.../native-xr-session-20261012-020553-094` (SteamVR/oculus, **1832x1920**, ~120 Hz) | 43 261 | 12 339 (~35/s) | 29 311 (~83/s) | 28.5 % | 49.9 ms (49 % > 50 ms) | 16 182 | 12 352 | ~3 800 (24 %) |
| `build/playtest-20261011-055537-4f94cac/.../native-xr-session-20261011-060330-535` (SteamVR/oculus, **1920x1080**) | 89 121 | 22 131 | 61 133 | 24.8 % | 47.8 ms | 47 825 | 23 110 | 24 709 (52 %) |
| `build/five-distinct-04d8da8/.../native-trace-20261012-005105-487` (native only) | | | | | | 30 666 | 14 658 | 16 002 (52 %) |

None of these timings is labelled cold or warm in its session. They are
whole-session aggregates that include the cold start and loading. They show the
order of magnitude only and cannot be used for an A/B comparison.

Other facts from the same reports:

- `sharing_mode` is 3 in every BC2 native report. That is fenced legacy sharing:
  BC2's device rejects keyed-mutex textures with `E_INVALIDARG`, see
  D3D11_BRIDGE.md.
- `depth_layers.active=true`, but `pairs=0` and `frames=0`. Depth never reaches
  the runtime, because `DepthWanted` requires keyed sharing (mode 1).
- `frame_gap_buckets` puts 99.6 % of XR frames at 5 to 11 ms. The host keeps up
  with the runtime.
- xrEndFrame takes 2.5 ms on average (maximum 18 ms). Async copy completion is
  under 2 ms for 84 % of copies and under 11 ms for almost all of them.
- `pair_reject_reasons` is all zero.

## Ranked hypotheses

**H1 — The game cannot produce enough stereo pairs, and the runtime is never
told. (Certain; measured.)** About 35 fresh pairs/s reach a headset that runs at
~120 Hz. The host keeps calling xrEndFrame at the full rate and re-submits the
last pair with its original pose. 70 % of submitted frames are re-submissions.
Because the app appears to run at full rate, SteamVR Motion Smoothing or ASW
never engages. The runtime only re-projects the rotation of each stale image, 3
or 4 times in an uneven 3,4,3,4 pattern. As a result, anything that moves (the
world while you strafe, the gun, animation) judders and doubles. That reads as
low FPS and blur. The cost comes from two full scene renders per pair at the
runtime's recommended 1832x1920 per eye: 7.0 MP per pair, about 3.4 times a
1080p desktop frame.

Options (design choices, not applied):

- Lower the headset refresh rate to 72 or 80 Hz so the cadence is a steady 2:1.
- Lower the SteamVR render resolution, or the per-eye size, for game FPS.
- Add an opt-in "pace to content" host mode that does not re-submit stale pairs,
  so the runtime's own frame synthesis engages.

**H2 — Request starvation wastes game frames. (Very likely; measured.)** Every
world frame that begins with no host request available (`bridge_no_lease`)
renders mono. The main view uses the native camera. That frame is GPU time the
headset never sees: 52 % of world frames on single-pair builds and 24 % on
b74560c.

- Cause: requests from the host to the game are admitted one at a time, and
  only after the previous pair was taken. A completed request takes about
  50 ms (~2.5 game frames): it waits for the next game frame, renders, waits for
  the fenced GPU event (polled at the next pump), waits for the host's next XR
  poll, then for the async-copy acknowledgement. Pipeline depth 2 does not cover
  that.
- On `main` (single legacy pair): when a second request is taken while pair 0
  is still exporting or held by the host, the whole stereo frame renders and
  then `CaptureEye` refuses it (`submitting_`/`waitingLegacyExport_`, or
  `pairs_busy`). One older run shows `tracked_applied` 440 against `consumed`
  220.
- H2b, b74560c only (low probability, now detectable): two legacy pairs share
  one private eye pair, `eyes_`. The copy into pair *i* is queued inside
  `PumpGraphics`, which returns early if the feedback try-lock is busy. If the
  next frame's `CaptureEye(0)` succeeds before that copy is queued, pair *i*
  goes out with the left eye from frame N+1 and the right eye from frame N.

**H3 — Left-eye-only ghosting and fades come from the interleaved mono frames.
(Plausible; not yet measured against the symptom.)** The left eye is the game's
main view. On every starved frame (H2), the main view renders again with the
native camera. Effects that blend the main view with its previous frame
(explosion radial or motion blur, flash or exposure adaptation, screen fades)
then mix a tracked left-eye image with a native-camera image. The extra right
view renders only in stereo frames, so its history stays consistent. That
would give ghosting, LOD or damage-state differences, and fades in the left
eye only.

This is not frame mixing in the transport. On `main`, both eyes come from one
lease by construction, and that is now verified per pair (`eye_frame_mismatch`).
To test it, compare left-eye artefacts against the `bridge_no_lease` rate. They
should disappear if starvation goes to zero.

**H4 — No depth reaches the runtime. (Certain; measured.)** Legacy sharing
disables depth publication, so the depth extension is enabled with nothing
attached. With 70 % reprojected frames, positional movement (strafe, lean)
cannot be corrected and judders more. In host.json, `depth_layers.active` only
means that setup succeeded.

**H5 — FOV overscan costs pixel density. (Small blur; certain by
construction.)** `FVR_XR_OVERSCAN` defaults to 0.08. Tangents are rendered 8 %
wider into the same pixel count, so the visible area gets about 7 % lower linear
density, or about 14 % fewer pixels. To A/B it, set `FVR_XR_OVERSCAN=0`. It is
now in the launcher env list. The trade-off is black edges under rotational
reprojection at 35 fps.

**H6 — The developer launch path renders at 1920x1080 per eye. (Real blur on
that path; measured in a past session.)** `Play-BC2VR.ps1`,
`Start-BC2VRSession.ps1` and the DevLauncher VR tab default to 1920x1080 (16:9)
unless the user changes it. The swapchain is created at that size, which gives
about half the vertical resolution of a Quest recommendation, with anisotropic
pixels. The 2026-10-11 06:03 playtest ran this way: `ready.json`
1920x1080.

The packaged Playtest path, through `BC2EyeRenderProfile.ps1` (present only on
the codex branch and in packages), uses the runtime recommendation. Check
`ready.json` `width`/`height` for every session you judge. Not changed here: it
is a user setting, and the right default trades blur against H1.

Ruled out or not supported by the evidence:

- No CPU readback or staging copy on the transport path. Diagnostic captures
  are one-shot or opt-in.
- No keyed-mutex stalls; BC2 uses fenced sharing.
- No synchronous host waits. The async copy is nonblocking, and its poll is
  ≤2 ms.
- The host paces to the runtime (xrWaitFrame), not to the game.
- No downscale step. Eye, shared, scratch and swapchain sizes must all match
  exactly, or the capture is refused.
- No sRGB mismatch: the bytes are copied into an `_SRGB` swapchain from a
  gamma-encoded UNORM back buffer.
- Stale pairs are re-submitted with their original render pose, which is
  correct for rotational reprojection.

## Telemetry added

### Host: `xr-events.jsonl`, `"event":"delivery"`, once per second

Disable it with `FVR_XR_DELIVERY_LOG=0`, also listed in the launcher. It is
active only when the host has an event log. Per XR frame it costs a few integer
operations. Per second it costs ≤512 stored samples per series and one
nth_element. Each fresh pair adds two GPU timestamp queries, read back without
waiting.

Every value covers the last ~1 s window only, except where marked otherwise.

| Field | Meaning |
|---|---|
| `window_ms`, `xr_frames`, `xr_fps` | Window length, xrEndFrame calls that succeeded, and their rate. |
| `fresh`, `fresh_per_s` | Frames whose world layer is a newly uploaded stereo pair. **This is the headset's real content FPS.** |
| `reused`, `reused_per_s` | Frames that re-submitted the retained pair (stale content, reprojected by the runtime). |
| `no_world` | Frames ended without a world layer (menu, gated, nothing retained). |
| `game_frames_per_pair` | Mean step of the native game frame id between consecutive fresh pairs. 1.0 means every game frame reached the headset. 2.0 means half were mono or dropped (H2). |
| `skipped_game_frames`, `max_game_frame_gap` | Sum of (step − 1), and the worst step. |
| `repeated_frame_ids`, `frame_id_regressions` | A fresh pair with the same or an older game frame id than the previous one. Should be 0. |
| `age_at_end_us` {n,mean,p95,max} | Every submitted frame: time from the host sampling the pose that the displayed pair was rendered with (the late-latched sample when relatched) to the xrEndFrame call. This is content age at submission. |
| `fresh_age_at_end_us` | The same, fresh frames only: best-case latency. |
| `sample_to_delivered_us` | Fresh pairs: pose sample to the host taking the completed pair. Covers the game waiting for a frame, rendering, GPU export, and the host poll. |
| `delivered_to_end_us` | Fresh pairs: host take to xrEndFrame (copy, compose, swapchain upload, waits). |
| `host_gpu_us` | GPU time between timestamps placed before the shared-to-scratch copy and before xrEndFrame (copy, props, mirror, swapchain copies). It is GPU **latency on a shared GPU**: it includes time the GPU spent on the game in between. |
| `host_gpu_dropped` | Spans not measured: slot still pending, disjoint, or unresolved. |
| `xr_period_us` | Runtime's `predictedDisplayPeriod`. |
| `display_step_us` | Mean step of `predictedDisplayTime` between frames. If it is greater than the period, the host is missing refreshes. |
| `skipped_refreshes` | Refreshes the runtime skipped for us (steps ≥1.5 periods). |
| `wait_interval_us` {mean,max} | Wall time between xrWaitFrame returns. |

The existing `frame_timing` line is unchanged. Its counters stay cumulative.

### Native DLL: `session-telemetry.jsonl`, `"frame_delivery"`, cumulative, once per second

| Field | Meaning |
|---|---|
| `sharing_mode` | 1 keyed (two pairs, depth possible), 2 helper relay, 3 fenced legacy (BC2 today). |
| `stereo_begin_accepted` | World frames that took a host request and rendered stereo. |
| `bridge_no_lease` | World frames that found no request and rendered **mono** (H2/H3). Rate = Δ/Δ`world_calls`. |
| `begin_active_transaction` | World frames that started while the previous stereo transaction was still open. |
| `eye_frame_mismatch` | Pairs refused because the two private eye images did not both hold the lease's game frame (left and right from different frames). Such a pair is cancelled, never shown. |
| `gpu_export` {n,total_us,max_us} | Fenced or relay export: time from native publication (both eyes captured, state restored) to the GPU copy completing. Mean = total_us/n. Empty with keyed sharing (synchronous). |

These lines are written every second, so they survive the crash that left the
latest `native-trace.json` empty.

## Fix applied

The only behavioural change is `D3D11FrameBridge::PublishRestored`. It now
refuses a pair unless both private eye images were captured for that lease's
native frame (`eye_frame_mismatch`). On `main` this cannot happen by
construction. The check makes any future frame mixing visible and keeps it off
the headset.

No performance fix was applied. The levers for H1 and H2 are design choices:

- Runtime pacing.
- Resolution or refresh rate.
- Producer-driven stereo on every game frame using the late-latch pose lane,
  with no request per frame.
- A third request slot, which is a protocol (ABI) change.

## Stereo every frame, 80 Hz and pace-to-content (`perf/stereo-every-frame`)

This branch acts on H1, H2, H2b and H6. Everything is on by default. Each part
has an environment kill switch. No game launch, no GPU run and no headset run
was made for it. The only checks were incremental compiles and the two channel
unit tests.

### What changed

**Request starvation (H2): pipeline depth 4, ABI change.**

- The control block now has four request slots: wire v9, 13728 bytes, object
  names `Local\FrostbiteVR.Control.v6.*`. It had two slots (v8, `v5` names).
  The DLL and the host must come from the same build. A v8 peer cannot open the
  v9 objects, so a mismatched pair fails to connect instead of misreading the
  block. See `docs/FRAME_CHANNEL.md`.
- Depth 3 and 4 always keep exactly one untaken request queued. A new one is
  admitted even while the host still holds a delivered pair. This now also
  happens at the moment the host takes a pair, not after its copy is
  acknowledged. Each game frame should therefore find a request
  (`bridge_no_lease` should go to about 0), although a request still takes about
  2.5 game frames to complete.
- The producer (`StagedFrameProducer`) can now hold up to three overlapped
  leases besides the one it is rendering. Their outcomes are reaped oldest
  first, because the host takes pairs in request order.
- `FVR_XR_PIPELINE_DEPTH` is read by both processes. The default (unset) is 4.
  Set it to `2` for the previous default and `1` for a single exchange. The
  playtest launcher now passes `4`; before this it forced `2`.

**Per-pair eye textures (H2b).**

- Each fenced legacy pair (sharing mode 3, which is BC2 today) now has its own
  private eye images. They are created when the pair is first used. A later
  frame can no longer overwrite the source of a pair that is still exporting.
  Each pair also records the game frame each of its eyes holds, so
  `eye_frame_mismatch` stays exact.
- The legacy copy into the shared pair is queued at publication, not at the next
  pump.
- If the shared private eye set is used again (`FVR_BC2_PER_PAIR_EYES=0`),
  `CaptureEye` refuses to overwrite it while an export has not queued its copy
  yet. This is the H2b guard, counted as `export_unqueued_refusals`.

**No dropped stereo frames.**

- `CaptureEye` and `PublishRestored` used a zero-wait try-lock on the producer
  gate and dropped a fully rendered stereo frame whenever it was busy. They now
  spin for a bounded time first: 500 µs by default, set by
  `FVR_BC2_BRIDGE_LOCK_SPIN_US` (`0` restores the drop).
- The gate is held only by short CPU work on the visibility thread, never
  across a wait. The results are counted as `gate_retries` and `gate_drops`.
- Pair selection now covers up to four pairs and never picks a pair whose
  export is still active (`pairs_busy` counts the remaining refusals).

**Eye size (H6).** The developer path now defaults to the runtime-recommended
per-eye size, the same rule the packaged playtest path already used
(`tools/BC2EyeRenderProfile.ps1`, from `BC2XrHost --probe`). This covers:

- `Play-BC2VR.ps1`: `-RenderWidth`/`-RenderHeight` default to `0`, which means
  the headset recommendation. Flat play keeps 1920x1080.
- `Start-BC2VRSession.ps1`: same default.
- The DevLauncher "Developer launch" tab: 0 x 0 is the default; any non-zero
  size is passed through.

Saved launcher profiles keep the size they stored. Check `ready.json`
`width`/`height` for every session.

**72 Hz (default changed from 80 on 2026-10-11 at user request).**

- The XR host enables `XR_FB_display_refresh_rate` when the runtime offers it.
  It enumerates the supported rates and requests the one nearest
  `FVR_XR_REFRESH_HZ` (default `72`; `0` keeps the runtime's setting).
- The request is never fatal. The result goes to `host.json`
  `display_refresh` {target, initial, requested, current, result, rates}, to an
  `xr-events.jsonl` `display_refresh` line at session start, and to
  `display_refresh_changed` lines when the runtime switches. Every
  `frame_timing` line carries `refresh_hz`.
- Meta's OpenXR runtime (Link/Air Link) and Virtual Desktop's VDXR expose the
  extension. **SteamVR does not.** On SteamVR, set the refresh rate in SteamVR →
  Settings → Video → per-application or headset refresh rate. For Quest through
  SteamVR Link or Air Link, use the Meta Quest Link app → Devices → Graphics
  Preferences → Refresh Rate. For Virtual Desktop on SteamVR, use its Streaming
  tab → Frame rate. The host then reports `extension:false` and leaves the rate
  alone.

**Pace-to-content (H1).**

- Before `xrWaitFrame`, the host checks two things: whether the last frame
  presented the world while focused, tracked and menu-free, and whether a copy
  or upload is still pending. If the last frame qualified, nothing is pending,
  and the game has no finished pair waiting, the host waits for the game's next
  publication instead of re-submitting the stale pair at the full refresh rate.
- The wait is bounded to one display period (13.9 ms at 72 Hz), or
  `FVR_XR_PACE_MAX_WAIT_MS` (1..50). At the limit, the frame proceeds and
  re-submits exactly as it did before.
- The runtime then sees the app at the real content rate, so ASW, SteamVR
  Motion Smoothing or the VDXR equivalent can synthesise the missing frames
  instead of reprojecting stale images in an uneven cadence.
- Counters go to `host.json` `pace_to_content` and to `frame_timing` `pace`
  (cumulative): waits, satisfied, timeouts, wait_total_us and wait_max_us.
- Kill switch: `FVR_XR_PACE_TO_CONTENT=0`.

### Kill switches and launcher settings

| Switch | Default | Effect when changed |
|---|---|---|
| `FVR_XR_PIPELINE_DEPTH` | 4 (unset) | `2` previous default, `1` single exchange (host **and** game) |
| `FVR_BC2_PER_PAIR_EYES` | on | `0` shares one private eye set again (H2b guard active) |
| `FVR_BC2_EXPORT_QUEUE_AT_PUBLISH` | on | `0` queues the legacy copy at the next pump |
| `FVR_BC2_BRIDGE_LOCK_SPIN_US` | 500 | `0` drops a rendered frame on a busy gate (old behaviour) |
| `FVR_XR_REFRESH_HZ` | 72 | `0` keeps the runtime's refresh; other values pick the nearest supported rate |
| `FVR_XR_PACE_TO_CONTENT` | on | `0` re-submits the retained pair every refresh (old behaviour) |
| `FVR_XR_PACE_MAX_WAIT_MS` | one display period | 1..50 ms |

The launcher has these settings:

- **Playtest tab:** "Headset refresh (Hz)", default 72 (0 keeps the runtime's
  rate), and "Pace headset to game frames", default checked. They are passed to
  `tools/Start-BC2Playtest.ps1` as `-RefreshHz` and `-NoPaceToContent`, and
  saved in `%LOCALAPPDATA%\BC2VR\playtest-settings.json`. The single-frame
  troubleshooting box now says "normal is four frames".
- **Developer launch tab:** every switch in the table above is in the env list.
  The render size defaults to 0 x 0, which means the headset recommendation.

### New telemetry

DLL `frame_delivery` (in `session-telemetry.jsonl`) gains these fields:

- `pair_limit`
- `per_pair_eyes`
- `export_queue_at_publish`
- `export_unqueued_refusals`
- `gate_spin_us`
- `gate_retries`
- `gate_drops`

The host's `pipeline` gains `queued_while_delivered`.

### Costs and risks

- **VRAM.** In mode 3 at depth 4, the game holds up to 4 shared pairs plus 4
  private eye sets: 16 eye-size RGBA8 images, about 225 MB at 1832x1920. The
  host keeps up to 8 opened pairs (its LRU grew from 4). On a 12 GB card that
  is shared with ComfyUI and the other services, check `nvidia-smi` first.
- **Pose and input rate during pacing.** While the host waits, it publishes no
  new late-latch pose or input frame. The rate of both drops toward the content
  rate, and a relatched pose can be up to one period older. Compare
  `fresh_age_at_end_us` with pacing on and off.
- **Engagement differs by runtime.** Each runtime decides by its own rules
  whether to engage synthesis for a late app. If `xr_fps` falls but
  `reused_per_s` does not, the runtime is reprojecting rather than
  synthesising. Try the runtime's own Motion Smoothing or ASW setting.
- **The ABI changed** (v9). Host and DLL must come from the same build.
- **Not fixed: H4 (depth).** Depth is still off with legacy sharing.

## Headset test plan: stereo every frame, 72 Hz, pace-to-content

1. **Check the machine first** (global rule). Run `nvidia-smi` and note what
   holds VRAM: ComfyUI, songd worker, ACE, browser, game. Note free system RAM
   and whether another project's job is queued. Free the card, or record what
   was there.
2. Launch from the BC2 VR Launcher Playtest tab with the defaults: refresh 80,
   pacing checked. In `host.json` and in the `xr-events.jsonl` `display_refresh`
   line, confirm `current_hz` = 80. If the line shows `extension:false`, set
   72 Hz in the runtime's own UI (see above) and note it.
3. **Label every number cold or warm.** The first 60 to 90 s after the level
   loads is **cold**. Then play 60 s standing and looking around, and 60 s
   strafing and firing. Those minutes are **warm**.
4. Read the per-second delivery lines:
   `Get-Content xr-events.jsonl | Select-String '"delivery"' | % { $_.Line | ConvertFrom-Json } | ft wall_ms,xr_fps,fresh_per_s,reused_per_s,game_frames_per_pair,@{n='age_p95';e={$_.age_at_end_us.p95}},@{n='fresh_age_p95';e={$_.fresh_age_at_end_us.p95}},skipped_refreshes`
   Then read the DLL `frame_delivery` deltas over the same warm minute:
   - `stereo_begin_accepted`, and `bridge_no_lease` (the mono frame count);
   - `eye_frame_mismatch`, which must stay 0;
   - `gate_drops`, `pairs_busy` and `export_unqueued_refusals`.
5. Expected with this build:
   - `bridge_no_lease` grows by about 0 per second;
   - `game_frames_per_pair` is about 1.0;
   - `eye_frame_mismatch` = 0;
   - `fresh_per_s` is about the game's own frame rate.
6. **A/B pacing.** Run A (pacing on, default) and B (pacing off: uncheck
   "Pace headset to game frames", or `FVR_XR_PACE_TO_CONTENT=0`). Compare both
   **warm**, with the card in the same state, over the same activity. For each
   arm, record:
   - `xr_fps`, `fresh_per_s` and `reused_per_s`;
   - age p95 and fresh age p95;
   - the mono frame count per minute and `eye_frame_mismatch`;
   - `frame_timing` `pace` waits, satisfied and timeouts (deltas);
   - the subjective judder and blur when strafing.

   Expected for A: `reused_per_s` much lower, `xr_fps` near `fresh_per_s`, the
   runtime's synthesis indicator active, and a smoother strafe. Possible cost:
   a slightly higher age p95.

   If one arm is cold and the other warm, the comparison measures load state,
   not pacing. Re-run it, or say plainly that the arms are not comparable.
7. Optional single-variable A/Bs, all warm:
   - `FVR_XR_PIPELINE_DEPTH=2` vs 4 (starvation);
   - `FVR_BC2_PER_PAIR_EYES=0` (H2b guard instead of separate eyes);
   - refresh 72 vs 80 vs 90.

## Test procedure for the user

1. **Before launching**, follow the global rule. Run `nvidia-smi` and note what
   holds VRAM (ComfyUI, songd worker, ACE, browser). Check the system RAM and
   any queued jobs from other projects. Free the card, or record what was
   present. A run started on a nearly full 12 GB card measures the spill, not
   the mod.
2. Launch as usual from the BC2 VR Launcher. Note the refresh rate and the
   SteamVR resolution percentage that the headset runtime reports.
3. **Cold period:** the first 60 to 90 s after the level loads (shader and
   texture streaming). Label any number from it **cold**. Then play 60 s
   standing and looking around, and 60 s strafing and firing. These are **warm**.
4. Open the session's `reports/native-xr-session-*/xr-events.jsonl`. Print the
   delivery lines:
   `Get-Content xr-events.jsonl | Select-String '"delivery"' | % { $_.Line | ConvertFrom-Json } | ft wall_ms,xr_fps,fresh_per_s,reused_per_s,game_frames_per_pair,@{n='age_p95';e={$_.age_at_end_us.p95}},@{n='gpu_mean';e={$_.host_gpu_us.mean}},skipped_refreshes`
5. Read the result:
   - Low `fresh_per_s` together with `xr_fps` near the refresh rate confirms H1.
   - `game_frames_per_pair` well above 1, or a growing `bridge_no_lease` in
     `session-telemetry.jsonl`, confirms H2.
   - `skipped_refreshes` > 0 points at the host or runtime instead.
   - `eye_frame_mismatch` > 0 means the eyes came from different frames.
6. A/B one change at a time, with both arms **warm** and the card in the same
   state:
   - refresh 120, then 90, then 72 Hz;
   - SteamVR resolution 100 %, then 70 %;
   - `FVR_XR_OVERSCAN=0`;
   - `FVR_XR_PIPELINE_DEPTH=1`.

   If one arm is cold and the other warm, the comparison measures load state.
   Re-run it, or say plainly that the numbers are not comparable.
7. For left-eye-only artefacts (H3), note when they happen, and check the
   `bridge_no_lease` rate in the same seconds.

## Porting to `codex/bc2-resume-20261011`

- Host (`src/xr/XrDeliveryTelemetry.h`, the `OpenXrHost.cpp` hooks): these
  apply as they are. That branch differs from `main` here by one line, the
  `bodyProps` condition.
- DLL: add the `eyeFrame_` recording in `CaptureEye` the same way. On that
  branch the legacy copy is queued later, inside `PumpGraphics`, so the check
  belongs where each `legacyExports_[i]` first calls `producer.Publish`:
  compare both `eyeFrame_` values with `item.lease.native.frameId`.
  `CaptureEye` should also refuse while any active legacy export has not yet
  queued its copy; that closes H2b.
- `ExportStarted`/`ExportFinished` need one timestamp per `legacyExports_`
  slot.

Files: `src/xr/XrDeliveryTelemetry.h`, `src/xr/OpenXrHost.cpp`,
`include/fvr/graphics/D3D11FrameBridge.h`,
`src/graphics/d3d11/D3D11FrameBridge.cpp`,
`src/games/bc2/Bc2StereoProgressEvidence.h`, `src/games/bc2/NativeProbe.cpp`,
`DevLauncher.ps1`.
