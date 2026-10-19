> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Effective native action pose capture

`FVR_BC2_NATIVE_ACTION_CAPTURE=1` extends the existing private
`gameplay.rig_publication.hand_evidence` records. It does not change animation,
ammunition, controls or native dispatch. The intended use is an isolated ordinary
native reload, with physical reload authority disabled, to inspect composed
animation-tree output where a raw Reload clip does not move the handle.

The immutable `RigSnapshot::evaluatedWorld` supplies every pose before VR edits.
The row is retained only after successful rig publication and an unchanged,
current authored selected-configuration lease. This proves the configured mesh
and actor rig; it does not prove the submitted graphics mesh or active clip.

Each non-null `hand_evidence[].native_action` contains:

- Exact asset, configured mesh, rig fingerprint, equipment generation, capture
  time, and original animation/pose object identities.
- Original weapon and left wrist, left wrist relative to weapon, and 15 fingers
  relative to wrist in Thumb/Index/Middle/Ring/Pinky order, three joints each.
- Named, nonhidden weapon descendants relative to the original weapon. Hidden
  leaves are omitted explicitly; no bind pose substitutes for missing data.
- The existing read-only reload observer's two firing branches: wrapper offset,
  address, phase, current/previous/next state, timer, loaded and reserve counts.

All action matrices use canonical row vectors and translations in metres.
The firing observation has its own source sequence/time and may precede the pose
by less than 200 ms. Its owner, selected weapon data, space and sequence are
checked at both publication and capture. The diagnostic keeps the existing
rate-limited observer active beyond its usual initial 20-second window; it adds
no new native address, function call or state write. Repeated reads do not prove
an atomic simulation snapshot or an authoritative firing branch.

Clip GUID, normalized clip progress and authoritative native reload phase remain
null. The actual branch phases/timers are evidence, not inferred animation time.
A later private sampler must select an observed action window from these records
and verify contact/motion; it must not label the whole Reload animation as a rack.

The existing hand evidence ring retains at most 512 frames at a minimum 20 ms
interval (approximately a 10.24-second tail at full rate), 32 part transforms and
15 fingers per row. Fixed records plus at most 512 retained firing snapshots are
compile-time bounded below 4 MiB; bounded JSON output is below 16 MiB. The report's
`native_action_capture` object records the enabled state and limits. Without the
opt-in, action fields stay null and the ordinary hand evidence rate stays 100 ms.
No captured game matrices are committed to public source.
