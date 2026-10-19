> **Technical reference.** API details and examples below retain their stated checkpoint scope; check current source before reuse.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Tracking and stereo frame channel

FvrFrameChannel connects an x64 host and a single x86 render producer using a
fresh GUID-named Local\FrostbiteVR.Control.v6 session. A named mutex protects a
13728-byte shared mapping (header version 9: four request slots, per-slot body
props and depth attachment; v8 had two slots and 7056 bytes); pipeline depth
3..4 keeps one untaken request queued (docs/HEADSET-FRAME-DELIVERY-20261011.md);
an event wakes the host. Process handles and creation
times detect peer exit and PID reuse. Default Windows object security is used;
this is same-user, same-machine development IPC, not a network service.

FrameRequest version 3 is 248 bytes in both architectures. It explicitly encodes reference,
head and eye poses, asymmetric FOV, GPU LUID, dimensions/typed format, tracking
and space generations, predicted OpenXR time, request ID and a separate QPC
completion deadline. Invalid poses, reserved fields, versions and sizes fail.
The OpenXR timestamp is never compared with the QPC clock.

The slot advances through requested, rendering, ready, delivered and complete.
Only a complete matching GPU ticket is delivered. Before delivery, a missed
deadline cancels the request and the producer reclaims or recreates its texture
stream. After delivery, only explicit GPU-consumption feedback or peer exit
permits reclamation. After acknowledgment, one CPU-only successor can queue;
the producer returns the old outcome and releases its GPU lease before taking
the promoted successor. Queue cancellation never erases that old outcome.
This removes the mandatory extra XR-frame gap between requests. The successor
uses the completion frame's current tracking; old images retain their original
render poses. A newer request cannot overwrite an owned slot. An
abandoned CPU mutex poisons the session rather than trusting partially written
state. Resource sharing/key ownership is separately enforced by FvrD3D11.

Pipeline depth 2 (BC2XrHost default; `FVR_XR_PIPELINE_DEPTH=1` in either
process restores depth 1): the host may start a request in the second slot once
every in-flight request has been taken by the producer, so the game renders
frame N+1 into its second shared pair while the host still owns frame N.
Untaken requests never stack; the producer takes the oldest requested slot and
renders one request at a time; the host delivers oldest first and holds at most
one delivered pair awaiting feedback. The library default stays depth 1.

Depth attachment (`XR_KHR_composition_layer_depth`, `FVR_XR_DEPTH_SUBMIT=0` in
either process disables): the host sets `FrameDepthRequested` (flag 4) on its
requests only when its runtime has the extension and a depth swapchain. The
producer may then publish a `graphics::DepthAttachment` with the color pair: a
second named keyed pair in R32_FLOAT holding [0,1] window depth, a ticket with
the color ticket's frame identity, and per-eye near/far in metres. An
inconsistent attachment is dropped; it never fails the color pair.
Depth and body-prop reads require the current delivered lease and stop as soon as
feedback is accepted, even if the acknowledgment is still waiting for the mutex.
Depth metadata stays with its private scratch pair across a delayed color upload;
the host never attaches depth from a different retained frame.

RemoteFrameProvider implements IFrameProvider for the XR host. The XR path
uses zero-wait polling and a 150 ms request deadline (configurable 1..200 ms).
The separate blocking diagnostic rendezvous defaults to 8 ms (1..50 ms).
Neither promises that BC2 will render within that budget. Game-side
RemoteFrameProducer::Pump never waits for the CPU mutex and must run at a
verified native graphics boundary. Its local provider receives feedback for
cancelled or consumed pairs. The native BC2 adapter uses StagedFrameProducer.

BC2XrHost --ipc creates a channel and prints frame_channel=<token> to stderr.
It does not launch, inject, or attach the game. Explicit bootstrap coordination
must pass that token to a producer. The existing --test-scene mode remains a
local synthetic renderer. Controller input and feedback use independent lanes.

## Runtime timing evidence

`xr-events.jsonl` checkpoints cumulative timing and gating counters once per
second. `host.json` contains their final values. `stage_total_us` and
`stage_max_us` use this order: xrWaitFrame, xrBeginFrame, tracking/input/menu,
consumer.Copy, props/mirror, XR image acquire/wait/copy/release, xrEndFrame,
controller sampling alone, asynchronous GPU-copy completion polling. Controller
sampling is also included in tracking/input/menu.
These measure CPU wall duration of calls, not GPU execution duration.
`controller_sampling` counts sampling calls, `xrSyncActions` not-focused results,
four active pose actions, then four successfully tracked poses. Both groups of
four use left grip, left aim, right grip, right aim order. This distinguishes
input focus refusal from inactive actions and active-but-untracked poses.
`controller_current_profiles` records each hand's selected profile at one-second
checkpoints and just before requested shutdown. Empty with a successful
`controller_profile_results` value means OpenXR returned `XR_NULL_PATH`; an API
error is recorded separately and never disables gameplay input. Suggested
profile count is not evidence that a profile was selected or a hand tracked.
`pair_reject_reasons` partitions rejected pairs into invalid metadata/pose,
reference-space mismatch, future pose, expired pose, consumer-open failure,
copy Busy, copy Invalid, copy Unavailable, copy Faulted, and other, in that order.
`last_pair_transfer_error` retains the last failed open/copy HRESULT. These
counters classify failures without changing transfer deadlines or pose limits.

For legacy fenced textures, the host queues shared-to-scratch copies and then
composes/submits those scratch images on the same immediate context. GPU command
order preserves image contents without a CPU completion wait. The exact source
ticket remains owned until a nonblocking completion poll succeeds; only then is
positive acknowledgment sent. A failed or abandoned copy retires the whole
resource generation. Original render poses and the 250 ms presentation limit
remain unchanged. Set `FVR_XR_ASYNC_CONSUMER_COPY=0` for the synchronous fallback.
`async_copy` counts queued transfers, completed acknowledgments, completion
timeouts, other completion failures, retired transfers, and pending polls.
Post-submission completion failures are separate from `pair_reject_reasons`.

A completion timeout means the copy's D3D11 event query was still incomplete
when polled 20 ms or more after queueing, measured with QPC (before 2026-10-09 it
was GetTickCount64, so the real deadline varied from about 15.6 to 31.3 ms). The
pair has normally already been presented once. Its pixels are still valid:
later GPU work runs in order on the same context, and the producer cannot write
the source again (it waits for an ack, and the negative ack retires the source).
Since then, the host keeps that pair retained until the next pair, within the
unchanged 250 ms limit, instead of submitting frames with no world layer. The
negative ack and resource-generation retirement are unchanged.
`FVR_XR_RETAIN_ON_COPY_TIMEOUT=0` restores the older behaviour, which resets the
retained pair. `retain_on_copy_timeout` reports which mode is active.

`copy_timeout_presentation` has three counts:

1. Timeouts that kept the pair.
2. Frames presented after a timeout, before the next pair.
3. Frames in the same interval with no world layer although the session was
   focused, tracked, rendering and showing no menu.

`copy_latency_buckets` sorts acknowledged copies by the time from queueing to
the poll that saw completion. Upper bounds are 2, 5, 11, 17, 20, 25, 33 and
50 ms, then overflow. Polling happens about once per XR frame, so each value is
an upper bound, not the exact time the copy finished.

`late_copy_buckets` is telemetry only and never affects acks. After a timeout,
the host keeps polling the abandoned event to see whether the copy finished
later. Buckets:

- finished within 20, 25, 33, 50, 100 or 250 ms;
- still incomplete, or the query failed, after 250 ms;
- not watched: all four watch slots were full, there was no event, or the copy
  was unresolved at shutdown.
`frame_gap_buckets` and `fresh_age_buckets` have inclusive upper bounds of
5, 11, 22, 50, 100, 150, 250 ms, then overflow. Fresh age compares the original
render pose with the current XR predicted time. `gated_frames` counts
!shouldRender, invalid head, unfocused, and menu; categories can overlap.
No-world-layer counts include menu frames and must not be called black frames.
The original-pose 250 ms presentation limit and request deadline are unchanged.

## Verification

Test-FrameChannel.ps1 runs a real x86 renderer against an x64 receiver with no
headset/runtime. Four GPU pairs passed scene geometry, stereo parallax and head
translation checks; a deliberately unconsumed pair forced a new texture session
and rendering recovered. Tracking/space IDs exceed 32 bits. Separate child
processes tested process exit and abandoned mutex handling. A second producer
and malformed token were rejected. Logs include exact binary hashes.

FrameProtocolTests separately exercises deadline races before/during/after
render, stale publication, invalid data, one-time delivery, and the rule that a
timeout cannot reclaim a pair already owned by the GPU consumer.

Windows contracts: [WaitForSingleObject](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject)
and [CreateFileMapping](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-createfilemappinga).


## Staged native callbacks

StagedFrameProducer separates TryBegin (visibility thread) from Submit and
PumpGraphics (graphics thread). TryBegin copies the exact decoded poses/FOVs
into a FrameLease tagged with channel generation, request ID and the adapter's
owner/frame/device key. It calls no renderer or simulation. A taken native
frame cannot be repeated after failure, cancellation or reconnection. A new
world/device generation must be supplied by the adapter when identities change.

Submit transfers a complete ticket only when its native frame and original
tracking/time/space/GPU requirements match. Call it only after restoring native
state. Busy/Invalid retain caller ownership; Cancel abandons an unfinished
lease without inventing a GPU ticket. PumpGraphics publishes nonblockingly and
returns consumed/discarded feedback exactly once on the graphics thread.
Both native callback threads must be quiescent for Connect/CloseGraphics and
destruction. The feedback provider must outlive the staged producer.

Visibility can reap a completed CPU outcome and admit the successor without
waiting for the next draw callback. It defers up to two GPU receipts (one per
in-flight pair at pipeline depth 2). The
graphics callback drains that receipt before any texture capture, publication,
or reset; a busy drain gate refuses resource access. Close/reconnect drains the
receipt on the graphics thread. This avoids a lost native frame without moving
immediate-context work onto the visibility thread.

BC2 also polls its export queries at the existing verified DXGI Present hook,
only for the owned swapchain, on the observed DrawHook thread, after camera
restoration, outside test Presents. A changed draw thread disables this path.
The polls never wait or invoke native rendering. `FVR_BC2_PRESENT_BRIDGE_PUMP=0`
disables them; `bridge_present_pump` records the guard and publication counters.

Game operations use a local atomic try-lock and zero-time interprocess mutex
polls. The host uses a bounded one-millisecond mutex wait during rendezvous to
avoid starvation, and retries promptly if a response signal arrives while the
producer holds the mutex. Without this, an auto-reset event could be consumed
before the receiver could read the ready pair. Feedback can be deferred under
contention; FlushFeedback lets the host drain it without requesting another
render, which matters when rendering pauses or a test ends.

StagedFrameProducerTests covers separate callback threads, changed owner/device,
wrong frame/tracking tickets, cancellation, duplicate frames, late completion,
reconnection with repeated request IDs, and delivered GPU ownership beyond the
CPU deadline. A deliberate recursive mutex hold reproduces the wakeup race.
After the fixes, 20 repeated runs passed in each architecture.

Test-FrameChannel.ps1 -Staged uses separate visibility/graphics threads in the
x86 GPU fixture and the x64 receiver. Four pairs pass exact tracking identity,
eye parallax, translated head movement and discarded-stream recovery; peer exit
and abandoned mutex checks also pass. Final report is
reports/ipc-staged-cross-architecture.json (2026-09-25 11:53 UTC). The original
single-thread path was rechecked as well. No headset or native BC2 rendering
was used for either hardware fixture.


## September 27 asynchronous host path

BeginRequest and PollRequest use zero-wait mutex attempts. A pending request retains
its original tracking sample and bounded deadline across runtime frames. Suspend
cancels pending production, retrying if the mutex is busy. A Delivered pair is
never revoked by the deadline or cancellation; GPU feedback must complete first.
RemoteFrameProvider returns the original rendered tracking separately from current
tracking. Recenter, incompatible output requirements and time reversal cancel the
pending sample. Protocol v2 and synchronous RequestPair are unchanged.

Native probe `unavailable_attempts` counts unsuccessful polls (including pending
work), not deadline expirations. Historical `timeouts` fields also counted polls.
