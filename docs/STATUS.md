# Current development status

Documentation consolidated October 13, 2026 against source checkout `1023fd6`.
This describes development source and recorded evidence, not a new game test or
a statement about the currently selected local package.

BC2 single-player is the active adapter. Native x86 game integration supplies
stereo views to a separate x64 OpenXR host. The unified launcher is the normal
entrypoint. See [controls](CONTROLS.md), [launcher](LAUNCHER.md),
[architecture](ARCHITECTURE.md) and [build instructions](../BUILDING.md).

## Implemented paths and remaining work

| Area | Source and evidence boundary |
| --- | --- |
| Rendering and XR | Native stereo, tracking, roomscale and recentering exist. Rendering, frame delivery and lifecycle repairs have their own dated evidence; broader campaign and headset quality are not established by compilation. |
| Hands and weapons | Native hand replacement, controller-authoritative primary-hand fitting and authored support/action poses exist. Exact per-weapon animation coverage remains incomplete. A rigid attachment or approximate donor pose is not finished sampled-native support. |
| Physical weapon state | Persistent weapon state, owned server transactions, accepted-shot accounting and inventory selection exist. Magazine, tube, SPAS pump and configured rack consumers share the mechanism layer. Coverage is configuration-specific. Suspended weapons permit selection away once pending commands and native reload transactions settle; their firing remains blocked. |
| Controls and launcher | A reload, stick-up jump, carried-item cycling and the test Arsenal chord are in source. The launcher exposes player/test settings and named profiles. Package options depend on the selected build. |
| Recoil and feedback | Native recoil transfer code and later aim-basis repairs exist. Earlier observer-only and "not implemented" notes are historical. Event-specific reload audio and per-weapon feel still have unresolved coverage. |
| Broader scope | Optic/reticle, belt-fed and other mechanism families, campaign/vehicle transitions and all-gun coverage remain incomplete. Full-body IK, multiplayer and other Frostbite adapters are not established as supported features. |

Useful recent records:

- [Primary-hand authority and support](HAND-AUTHORITY-SUPPORT-GRIP-20261011.md),
  [SPAS/native action samples and remaining approximations](SPAS-PUMP-BOLT-GRASPS-20261011.md),
  [native action capture](re/BC2_NATIVE_ACTION_CAPTURE_20261012.md).
- [Hands and mesh coverage](re/BC2_HANDS_READY_20261011.md),
  [five-weapon mechanisms](re/BC2_FIVE_WEAPON_MECHANISMS.md),
  [support acquisition repair](re/support-handoff-20261012.md).
- [Reload lifecycle](re/BC2_PHYSICAL_RELOAD_LIFECYCLE_20261011.md),
  [recoil basis](re/BC2_RECOIL_AIM_BASIS_20261012.md),
  [render/export overlap repair](BC2_LEGACY_EXPORT_PIPELINE_20261012.md).

## Current work and priorities

Choose the next work from the user's current goal, the relevant source and
observed failures. Old agent queues and ordered roadmaps are historical context,
not current assignments. Verify local package selection through the launcher;
paths and hashes in old handoffs do not select or protect a current installation.
Preserve actual active work and installed candidates.

The XM8/SPAS reload rework adds pouch-first magazine exchange, durable command
outcomes, strict owned idle recovery and continuous native sampled reload hands.
SPAS releases the loading hand before its short native shell-settling tail.
See [native reload sampling](re/BC2_NATIVE_RELOAD_SAMPLING.md) for input/animation
calibration and remaining exact XM8 rack and event-audio coverage. Headset feel
has not yet been verified for this rework.

Keep source implementation, automated game evidence and human headset feedback
separate when reporting results. This distinction is not an approval or testing
gate for ordinary development. Follow [AGENTS.md](../AGENTS.md) for the smallest
useful build/check and the native safety requirements.

The [old status ledger](ARCHIVE-STATUS-20261013.md) preserves prior claims and
checkpoint results. See the [documentation index](README.md) for other records.
