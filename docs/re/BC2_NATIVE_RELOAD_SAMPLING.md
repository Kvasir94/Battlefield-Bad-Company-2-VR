# Native XM8 and SPAS physical reload presentation

Physical reloads use the same separation as KF2 VR: raw tracked input controls
progress, original animation samples control the displayed ammunition and hand,
and the owned ammunition transaction decides readiness. Assisted hand poses do
not feed back into contact recognition. The primary hand remains authoritative.

## Native action windows

The private action catalog contains weapon-relative parts, the left wrist and
fifteen wrist-relative finger transforms. Positions interpolate linearly and
rotations use the shortest quaternion arc between original samples.

| Weapon/action | Original clip interval | Physical input |
| --- | --- | --- |
| Scoped XM8 removal | `1P_Reload`, 0.55â€“0.633333 s | 77.008 mm projected withdrawal |
| Scoped XM8 insertion | `1P_Reload`, 1.516667â€“1.575 s | 99.549 mm projected insertion |
| Scoped XM8 carry | `1P_Reload`, 1.5 s | Independent tracked wrist |
| SPAS shell insertion | `1P_Reload`, 0.35â€“0.4 s | 54.375 mm projected insertion |
| SPAS shell carry | `1P_Reload`, 0.25 s | Independent tracked wrist |
| SPAS shell settling | `1P_Reload`, 0.4â€“0.433333 s | Original 33.33 ms part-only continuation |
| SPAS pump | `1P_BoltAction`, back and return branches | Rear stop followed by forward lock |

Insertion windows end before the native hand withdraws. The AS2/KF2 shell
release threshold frees the actual loading-hand claim while the insertion curve
continues, before submitting InsertRound at the terminal depth. This detached
insertion retains its original item token and verified weapon revision, not a
renewed or synthetic hand claim. It expires within 250 ms; ownership, revision,
tracking or focus loss cancels it. The following 33 ms shell settling
continuation also moves only the shell.
It retains its original deadline, command identity and exact current weapon
revision. Cancellation, rejection or ownership loss removes this cosmetic tail
without issuing another ammunition operation.

## Input and presentation calibration

A carry grasp is the original wrist relative to its ammunition part. The raw
input guide uses that same transform; a render-only change of grasp would make
the visible item disagree with insertion detection.

The straight input guide joins the native hand's entry and contact-end poses,
including the item-tip offset. Native frame progress uses that same projection.
The original curved part/wrist/finger path remains the displayed motion. Maximum
sampled deviation from the input guide is 5.26 mm for XM8 insertion and 6.62 mm
for SPAS insertion. XM8 removal deviates by up to 36.35 mm within its contact
release radius. The XM8 trajectory receives one constant transform that seats
the original magazine at the configured attached transform; the same transform
is applied to its sampled wrist.

The KF2 guide uses capture-relative physical progress. Its first 25% aligns the
original carry grasp into the native entry; only the remaining 75% scrubs the
sampled insertion window. Release assistance starts at 25% of that insertion
fraction, or 43.75% of total travel, and uses KF2's quadratic ease-out. Stationary
captured input cannot advance this mechanical alignment. The separate short
cosmetic hand-acquisition/release blend remains, as in KF2 `VRReloadHandPose`.
The input guide's terminal contact follows the original wrist path and need not
be the ammo bone's seated matrix; rendered seating still uses the exact anchored
original ammo bone endpoint. Source-only fallback and calibrated native guides
therefore have separate endpoint checks.
`DetachableMagazineConfig.attachedItem` separates the actual socket transform
from the translation guide's terminal pose. Registered weapon adapters provide
the exact socket; generic fixtures without an override retain the derived
terminal. Captured removal and attachment both use the socket, including the
legacy native-reload adapter when persistent physical authority is disabled.

`BC2_NATIVE_ACTION_POSE_HEADER` supplies the private sampled catalog.
`BC2_NATIVE_RELOAD_CALIBRATION_HEADER` supplies the corresponding carry and
input-guide calibration. Both must describe the same original action windows.
The ordinary source-only fallback does not establish sampled-native coverage.
Original animation payloads and generated trajectory headers remain outside Git.

The resource reader also handles the observed SPAS reload wrapper with flags 1,
extension tag 40 and bounded float pairs preceding the normal string table and
Granny payload. It retains these pairs as provenance; their engine semantics
are not inferred. Unsupported flags, tags, bounds and nonfinite values reject.

## Native XM8 rack integration

The scoped XM8's own decoded reload does not provide a charging-handle grasp.
The original rifle-XM8 reload supplies a compatible receiver/handle rig with
full wrist, finger and handle trajectories, including its preparation swing.
Production integration at `5f018a1` uses those original phases through
`Bc2NativeRack.h`, rather than the previous linear handle/LMG hand donor.
The helper implements guarded pose-curve projection and Prep/Pull/Rear/Return
states separately from the legacy linear rack path.
It snapshots profile data, combines tracked wrist translation and rotation,
bounds cursor travel using actual motion and elapsed time, releases hand
ownership immediately on grip release, and retains sampled part-only return.
Focused checks include roundtripping the original private Prep/Pull samples.
`BC2_NATIVE_RACK_PHASE_HEADER` supplies both the production adapter and focused
checks. `FVR_BC2_NATIVE_XM8_RACK=0` retains a runtime fallback switch. Original
samples stay outside Git. The adapter retains the hand through current tracked
ownership heartbeats, including bounded recovery from expired leases after a
render hitch. Actual tracking/owner changes cancel. A completed original return
after the real rear stop submits one physical CycleAction; partial returns do not.

As in KF2 `VRInteractiveReload`, a held forward stroke after the rear stop also
closes the action at 35% remaining axial pull. Until that threshold, the native
Pull samples reverse with fresh raw motion and retain the hand. Release or the
threshold joins the closest original Return part pose instead of restarting its
rear dwell. The original curves are not identical: focused native-data checks
bound the handoff below 1 mm/1 degree for gradual return and 3 mm/2 degrees for a
fast overshoot. Angular continuity includes elapsed time, so a plausible wrist
twist across a longer tracked frame does not hit a fixed per-frame angle cliff.
SPAS separately checks full raw 3D controller motion in the gun frame before
interpreting axial pump travel; sideways tracking jumps abandon the stroke.

## Remaining validation

Production per-event reload audio is integrated in the XR host, with original
locally extracted PCM cuts and exact native GUID suppression only while the
replacement mixer acknowledges readiness. The cuts, especially XM8 handle
attribution, still need audible comparison. See
[audio implementation and limits](BC2_PHYSICAL_RELOAD_AUDIO_20261014.md).
Compilation and data validation do not establish headset feel or finished
sampled-pose support for weapons outside this starter set. The October 14
grip/glow candidate is ready for a new headset test; the preceding test's
reported hand and combat-presentation failures remain the acceptance evidence
until superseded by observations of the corrected candidate.

## Package IPC compatibility

The first reload candidate paired the newly built native DLL's Control.v6
protocol with an older host's Control.v5 protocol. Both processes stayed alive,
but no image or controller input crossed the channel. The failed session
`native-xr-session-20261014-060055-080` ended with 20,522 blank frames and zero
submitted pairs; its native trace likewise recorded zero published pairs/input.

The corrected `playtest-reloads-20261014-ipcfix` package rebuilds the x64 host
against the current protocol. Live native telemetry from
`native-trace-20261014-060711-399` at elapsed 80 seconds recorded 3,368 published
pairs, 3,364 consumed pairs and 4,029 input samples. This verifies image/input
transport recovery, not final reload feel. The package tool now validates the
actual UTF-16 IPC namespace markers in both PE binaries (Control, Feedback,
Menu, Pose and Texture) before writing the archive and records them in its
manifest. Compatible different source commits remain allowed.
