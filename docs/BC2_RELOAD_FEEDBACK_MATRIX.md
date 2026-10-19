> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 physical reload sound and haptic matrix

Haptics source checkpoint: `4b157a5`, compared with local AS2 extraction and the
working KF2-VR source on 2026-10-11. Integration reported an x86 compile and
four passing focused checks; the new completion check exposed a backwards-clock
fixture corrected in `ceb3fa1`. Integration later reported the audio ownership
checkpoint `e5278ef` passing
four focused checks. Integration then compiled the original explicit cue
checkpoint `3aa8c4f` and passed both cue/sound-gate checks. The broader profile-list
extension `6013762` was integrated as `9e3fe64`: the affected native compile and
four focused checks passed at 07:35:10 UTC. Those runs were performed by the
integration owner, not this worker. Audible action-phase acceptance is pending.
No audio sampling, game run or headset validation was performed by this worker.

## Evidence and limits

AS2 reference: `<kf2-repo>\build\re\as2-reloads-20260926\reload-settings.json`
and `settings\HapticsProfile.txt` (decoded data; the corresponding `.json` stores
typed wrappers). The extraction identifies `resources.assets` by SHA-256
`192738787bb4a95f6da7674a3eafe47531cd7316370256e08211c8ce12a76c57`.
Profiles 7, 8, 9, 11 and 21 are tuning evidence, not a fresh AS2 runtime test.
The local `<kf2-repo>\docs\re\ARIZONA_SUNSHINE_2_RELOADS.md` explains their
relationship to acquisition, insertion and action movement.

KF2 reference: `<kf2-repo>\script\KF2VR\Classes\VRInteractiveReload.uc`
(`TakeAmmo`, `Seat`, failed insertion, `Rack`, `ReleaseSlideLock` and magazine
ejection); `VRReloadAudio.uc` (`EmitCue`, `PlayCues`, `FlushPending`) and
`VRReloadSoundMap.uc` (generated event mapping from stock reload clips).
These are current source requests, not fresh hardware measurements. Older
comparison notes are not authoritative for current KF2 duration clamps.

## Event matrix

Amplitude is normalized; duration is the requested pulse length. Left is BC2's
loading hand and right is its gun hand. All events still require the producer's
owned action plus fresh input; delivery retains focus, tracking, epoch, expiry
and consumed-event checks.

| Physical event | AS2 extracted profile | KF2 source request / context | BC2 next-build behavior | Sound status |
|---|---|---|---|---|
| Magazine release / unseat | No separate release profile established by this extraction | Eject: gun hand 0.12 / 18 ms; release-click sound followed by eject cue | Existing removal ownership retained; no new release pulse added | Whole native reload clip replayed on unseat; separate click unproven |
| Ammo or removed-magazine grab | 7: 0.3 / 10 ms | `TakeAmmo`: grabbing hand 0.3 / 10 ms; pouch-handling native cue | Existing `ReloadGrab`: loading hand 0.3 / 10 ms | No independent BC2 pouch cue established |
| Approach / capture | 9: duration 0, intensity 0.1; does not prove a discrete pulse | Pouch hover and insertion guidance are separate from seating | Existing hover 0.1 / 12 ms; existing shell capture 0.55 / 35 ms retained | No new sound on mere proximity |
| Magazine latched in well | 8: 0.5 / 100 ms | `Seat`: loading hand 0.5 / 100 ms plus gun hand 0.35 / 30 ms | `ReloadSeated` changed from 0.7 / 45 ms to 0.5 / 100 ms; gun-hand knock retained | Baked clip's seat noise still follows clip time, not physical latch |
| Failed guided insertion | 11: 100 ms curve, peaks 0.5 at normalized times 0.25 and 0.75 | Loading hand 0.3 / 30 ms twice, second start after 60 ms | Existing KF2-style two pulses retained; not an exact AS2 curve reproduction | No invented failure sound |
| Handle rear stop | Slide profile 0 in AS2 is a valid full-power shot profile; 21 is also 1.0 / 100 ms action tuning | Rear stop 1.0 / 100 ms in handling hand | No new rear-stop event; BC2 completion remains the admitted event | Separate rear-stop clip/cue not established for BC2 magazine reloads |
| Successful physical rack / chamber return | 21: 1.0 / 100 ms default | `Rack` / physical `ReleaseSlideLock`: both hands 1.0 / 100 ms | Existing `ReloadRack` now targets both hands after successful chamber transition | Explicit private-profile native cue audition only; audible return phase unproved |
| Button bolt release with seated loaded magazine | Slide-release behavior supported; default action profile above | `ReleaseSlideLock(bButton)`: gun hand 1.0 / 100 ms | Existing `ReloadRack` now targets gun hand only after success, replacing loading-hand routing | KF2 uses its own empty-reload bolt-forward event; BC2 mapping unproven |
| Native ammo receipt | No corresponding extracted profile assigned here | Distinct from physical contact | Existing receipt feedback retained; never substitutes for latch or rack | No extra replay |

AS2 controller override mask 229390 requests grab 0.5 / 50 ms and insertion
0.75 / 50 ms with frequency constant 1.0. Profile 21 has mask 3 requesting
1.0 / 75 ms. These are controller-specific source values; this patch does not
guess equivalent hardware or select those overrides for BC2.

AS2 default frequency constants are 0 for grab, seat and slide, and 0.5 for
failed insertion. They are not established Hertz values. Current KF2
`native\xr\src\OpenXrD3D11Backend.cpp` also requests unspecified frequency. BC2's
`src/xr/OpenXrInput.cpp` requests `XR_FREQUENCY_UNSPECIFIED`; no frequency field
crosses the feedback wire. This patch changes timing and action-hand context,
without claiming frequency control or a measured controller response.

## Native audio gap

[Native sound evidence](re/BC2_DISASM_WEAPON_SOUNDS.md), section 4, establishes
one `SoundAsset` per animation in the level's `AnimationSoundMap`. That asset
can be a layered `SoundTemplate`, so this does not prove one continuous wave.
Magazine-out and magazine-in can share its timing. `Bc2ReloadSoundGate.h` learns the start
after physical reload admission, withholds it and replays the original call on
unseat. It does not seek, sample, split or independently identify seat/bolt
sounds. The audio patch retains the existing native calls, while restricting
learning and dispatch to the admitted weapon/soldier pair and the exact learned
listener. A foreign soldier or another view cannot consume queued work. A pair
change retires retained pointer work without calling an old listener. Cancel
drops a withheld start and requests a playing replay's stop on the next owned
dispatch. Listener membership and soldier identity are read again under the
existing native dispatch lock before draining work.

### Corrected exact pistol binding (2026-10-11)

**Correction:** the earlier broad `m9` resource filter included the M95 rifle.
Its three reload/three bolt associations, two sampler groups, reverb parameters,
first-person tree/control-node claims and six generated profiles do not prove
anything about the M9 pistol. The six incorrect private profiles were renamed
`m95-invalid-for-m9-*`; do not enable them. This was caught before source
checkpoint, compilation, deployment or playback of the audition.

The corrected investigation decoded 31 exact pistol DBX documents from MP
common, joined the historical weapon label exactly to one SoldierWeaponData,
then followed its sole WeaponStateData.AnimTree1p reference to the actual
SpecificAnimTreeData and its sole ResultAnimTreeData. Its 36-entry animation
list contains a shared pistol bolt animation at index 30. That exact GUID
occurs once in the selected SP_02 AnimationSoundMapData and resolves to an
AnimationAsset and a SoundTemplate with one sampler group, a Start event and
zero public parameters. The map has 193 associations. Resource-name searches
alone miss this shared cue; neither a name nor index 30 selects runtime audio.
Raw resource references, GUIDs, assets and evidence remain local and ignored.

Targeted static inspection checked the existing listener start `0x881650`
and stop `0x881690`: x86 thiscall with animation GUID pointer and callback
argument, ret 8. The start ignores the callback argument, checks the
listener/soldier view, then forwards GUID, soldier and listener-local transform
to map start `0x6ee6a0`; stop routes through `0x6eafb0`. Map construction
`0x6f0a30` copies the asset GUID from the pointer at object +4. The singleton
getter return at `0x7ff0d3` is `MOV EAX,object; RET`; the adapter verifies those
opcodes and reads that image-local object's map slot at +0x48 without invoking
the lazy-initializing getter. These are static contracts, not runtime proof.

The graph can be started through the existing wrapper without a seek call.
There is still no verified per-instance pause/resume/seek interface: play
`0x637d50` delegates to parameterized play `0x637290` (ret 8 / ret 0xc), and
handle stop `0x633cb0` (ret 8) has no offset argument. A reflected sampler
Offset port does not establish transport semantics. The shared pistol cue's
audible phase remains unproved: it may represent a full action cycle rather
than the desired final chamber return. It is not assigned to seat or button
release. Native event audio remains preferred; slicing is an unimplemented
fallback and no AS2/KF2 assets are borrowed.

### Fresh exact historical correlation

The matcher was corrected to exact weapon-label equality and the actual shared
pistol animation GUID, then rerun against all three existing logs:

| Trace | Exact pistol physical rack completions | Button completions | Matching shared bolt callbacks |
|---|---:|---:|---:|
| 20261011-053044-326 (66fc2bd session) | 2 | 0 | 0 |
| 20261011-055635-051 (4f94cac IK session) | 0 | 0 | 0 |
| 20261011-060332-953 (4f94cac floating session) | 0 | 0 | 0 |

The earlier broad-label result happened to contain the same two completions,
but its target GUID comparison was wrong and is superseded. Comparing all 193
selected-map entries gives 202 GUID-memory-order matches in the floating log.
These counts establish neither silence nor audible cue suitability. No new
capture was taken. Eight fresh synthetic matcher assertions cover byte order,
exact pistol/rifle label separation, malformed and simulated rows, and exact
weapon-instance correlation. Raw rows and identities remain local.

### Explicit one-rack audition source

`Bc2ReloadAudioCue.h` parses five nonzero, distinct 16-byte identities in this
order: weapon data, first-person specific tree, animation, sound template,
current map data. The profile is exactly 164 ASCII characters: five 32-digit
hex strings separated by colons, each in native GUID memory order (canonical
UUID converted with `bytes_le`). There are no compiled asset IDs or defaults.

Both variables must exist **before game creation**:

- `FVR_BC2_M9_RACK_AUDIO_AUDITION=1`
- `FVR_BC2_M9_RACK_AUDIO_PROFILE=<private 164-character profile>`

The private setup script is
`<repo>\tmp-bc2-audio-20261011\Enable-ExactM9RackAudition.ps1`.
Dot-source it in the integration-owned launch shell; it checks its profile
against the exact private binding record, sets only that shell's environment,
and launches nothing. Its selected file is
`m9-pistol-exact-rack-audition.profile.private.txt` in the same directory;
`m9-pistol-exact-rack-audition.binding.private.json` records the reference chain
and source hashes. Neither file is a public deliverable. The runtime caches
the environment once, so changing it in another shell or after process start
will not enable this audition. Unset both variables for normal subsequent runs.

The sole admission edge is successful `magazineResult.racked` with
`boltReleased == false`, fresh input, an admitted player/weak/soldier/weapon,
and matching exact profile. Seat and button-release events cannot request it.
The loaded weapon/tree/list/map chain is bounded, read twice and compared;
owner resolution brackets it. A duplicate, unreadable, stale or mismatched
binding rejects playback. Admission is at most 250 ms and never beyond the
input deadline. A valid request consumes the one-per-process allowance even
if later cancelled or expired. Missing listener dispatch expires the request.

Playback does **not** depend on another animation-sound start callback. The
existing byte-checked `AnimDispatchHook` at `0xf4fb50` calls the original, then
uses its Ant object's +0xc0/+0xc4 listener list (at most 64 entries) to find the
unique current-soldier listener whose start/stop vtable slots and view match.
Due audition work takes precedence over a retained listener from another
owner. Membership is checked again under the existing native critical-section
and TLS lease. `RackAudioWork` resolves the same owner and full loaded binding
again before calling the existing original listener wrapper once, callback 0.
No new update ABI or native lock is introduced. The same owned listener can
receive one stop after cancellation or four seconds; owner/tree/map changes
retire work without calling retained pointers. Live resolver acceptance and
dispatch cadence still require observation; no sound-callback priming action
is assumed or necessary in the source path.

Only scalar `gameplay.anim_sound.rack_audition` diagnostics were added:
`profile_valid`, `attempts`, `rejected`, `played`, `stopped`. `profile_valid`
means syntax and opt-in only; it does not mean loaded identity acceptance.
`played` counts invoking the existing native wrapper, not measured audibility.
An attempt with zero played can reflect expiry or missing owned dispatch.
There are no added raw trace rows, wire fields or observer requirements.

For an integration-owned audition, first finish the separate hands capture in
its neutral frozen pose, without equip/reload/recenter. Only afterward equip
the exact profiled pistol and do one deliberate empty-magazine reload with
physical rack completion, leaving time between seat and rack and then four
seconds for the bound stop. Keep a local audible recording or human cue note.
Do not substitute button release; do not assign this cue to seating based on
similarity. A fresh process is required for another audition. A completed
start proves neither rack timing quality nor a complete event audio solution.

## Patch and validation

### Broader test-weapon profile list

The follow-up source extension accepts `FVR_BC2_RACK_AUDIO_AUDITION=1` with
`FVR_BC2_RACK_AUDIO_PROFILES`: 1–64 of the exact five-identity profiles above,
separated by semicolons. Every row must be complete and each weapon-data GUID
must appear only once. Invalid or conflicting input rejects the entire list.
The current admitted weapon-data GUID selects its row, then the existing full
tree/map/owner validation runs twice. Names and weapon-family similarity never
select a cue. The generic variables take precedence, including explicit disable
or malformed input; legacy M9 variables apply only when both generic variables
are absent. The one-attempt-per-process limit is unchanged, so selecting several
profiles permits a choice of test weapon, not repeated playback after cycling.

Offline inventory parsed 429 weapon-data configurations from 988 SP/MP common
DBX documents. Exact mesh joins to the checked-in family work plan covered 380
configurations across 39 labels. Their first-person tree references were joined
to the selected map by exact animation identity. After excluding consumers
outside the current self-loading detachable-magazine completion edge and
requiring one unique mapped action candidate, the private catalog contains 199
distinct weapon configurations across 25 labels:

- AEK-971, XM8, F2000, AUG, AN-94, M416, M16A2, 9A-91, SCAR-L and XM8 Compact.
- AKS-74U, UZI, PP-2000, UMP, XM8 LMG and MG36.
- Type 88 sniper, SVU, VSS, M14/Mk14 EBR, M9, M1911 and MP443.
- Saiga and USAS.

These rows ultimately reference two shared action animations and two sound
templates. Follow-up exact WaveAsset metadata gives a 0.601-second non-looping
pistol wave with unwired zero Delay/Offset and unit Pitch. The other graph has a
2.433-second action wave and ten non-looping handling variations of 0.327–0.739
seconds. Its handling Delay is wired to a uniform generator with 1.3–1.6-second
bounds. SP/MP metadata agree, and these authored nominal timings fit inside the
existing four-second audition stop. All inspected waves have Seekable=false.
These are static timing values, not an audible test or transport interface.
Coverage therefore does **not** prove weapon-specific sounds or audible phase
suitability. No weapon is newly admitted for physical reload by
this change. Bolt-action, pump/tube, belt, revolver and other completion consumers
are not added. In the static join, M24/SV98/GOL lack matched bolt candidates;
870 and Neostead lack both matched reload and bolt candidates. Those findings
remain gaps rather than a reason to substitute another gun's audio.

The ignored local `weapon-audio-catalog.private.json` stores exact identities
and provenance. `Enable-WeaponRackAudition.ps1 -CandidateId <explicit IDs>` in
`<repo>\tmp-bc2-audio-20261011` validates a selected subset and sets only
the launch shell's environment. It has no default candidate and starts nothing.
The private catalog is a set of audition candidates, not a shipping profile or
an audible acceptance record. `weapon-audio-batches.private.json` groups all
199 configurations into four explicit batches (64/64/64/7); each batch retains
its exact candidate IDs and profile path. No batch is automatically enabled.
The source and raw decoded documents stay local.

The follow-up C++ tests cover list capacity, malformed separators, duplicate and
conflicting weapon rows, exact current-data selection and unknown identities.
Integration's 07:35:10 UTC gate passed Bc2ReloadAudioCue, Bc2ReloadSoundGate,
InputBinding and HandCapture (4/4), with the affected native compile. No compiler,
game or capture process was run by this worker. The neutral engine capture kept
audio audition disabled, so it adds no audio acceptance evidence.

`Bc2ReloadFeedback.h` chooses zero targets without completion, the gun hand for
button release, and both hands for physical completion. The magazine consumer
uses this helper at its existing success edge; no native state, ammo ownership,
controls, IPC layout or new event kind changes.

`Bc2ReloadFeedbackTests` covers these masks through feedback encode/decode,
actual haptic policy, deduplication and rejected delivery. `FeedbackTests`
updates the seated duration/amplitude assertion. Integration passed the latter;
the completion fixture correction awaits its focused retry.
The integration owner's later offline gate should build the affected native
producer and XR host plus these focused checks. The seat profile lives in the
host: retaining an older host retains its previous seat pulse even with a new
producer. Magazine physical/probe checks are appropriate regression checks for
the added gun-hand completion event; no headset result is implied.

The audio patch extends `Bc2ReloadSoundGateTests` with foreign-soldier/listener
rejection, pair replacement, owned cancellation stop and superseded pending
starts. Integration reported the ownership checkpoint's affected x86 DLL compile
and four focused checks passing; this worker has not independently rerun them.
No host, IPC, native function ABI, sound asset or selected binary changes are
introduced by audio.

The new `Bc2ReloadAudioCueTests` covers invalid profiles, exact loaded identity
joins, duplicate/unreadable map and animation entries, two-pass mutation,
wrong-owner/proof retirement, physical-edge-only admission, cancellation,
expiry with and without a listener, repeated edges and one attempt per process.
These C++ checks were built and run by integration, including the profile-list
extension described above. The affected `BC2NativeProbe` compile also passed.
The remaining gate is a separately coordinated physical-rack audition after
the hand-draw investigation; seat and button assignments remain unproved.
No host or IPC change is required by this audition.

### Private identity observation for the audition geometry join

`FVR_BC2_AUDIO_IDENTITY_PATH` opts into a new, absolute local JSONL output file.
The parent directory must exist; an existing output file is never overwritten.
At the existing admitted hand-tracking publication seam, the observer retains at
most 16 distinct owner/equipment/space identities. Each row contains the exact
current ClientWeapon and typed SoldierWeaponData, its complete accepted Name,
its instance GUID as 32 hexadecimal characters in native memory byte order,
input generation, actor/equipment/render/hand generations, space and QPC time.
The existing equipment reader rejects names exceeding its terminated 64-byte
bound rather than truncating them. Owner/equipment and GUID are read twice and
must agree; stale input is rejected. No transform or cue assignment is inferred.

This file stays private. Join it locally to the existing hand evidence and input
tape by weapon, owner generation and space, respecting equipment generations;
the observation's input generation is a timestamped identity sample, not proof
that a hand-pose record exists for that exact input frame. Compare the full
instance GUID to the private profile before selecting the M9 action geometry.
GUID text converts to canonical UUID notation with `UUID(bytes_le=bytes.fromhex(...))`.
The writer flushes complete rows and keeps its handle until DLL unload; a live
.NET reader must share read and write access. No identities go to stdout or the
aggregate report. This metadata adds no native calls, audio dispatch, state
writes, IPC fields, or default-enabled feature. Compilation and live observation
of this seam are left to the integration owner; no runtime acceptance is claimed.
