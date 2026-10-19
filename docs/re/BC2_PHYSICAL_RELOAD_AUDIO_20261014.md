# Original BC2 audio for physical reload events

The existing animation sound map plays one template for an entire animation.
The prior native disassembly in `BC2_DISASM_WEAPON_SOUNDS.md` establishes the
map and sound-manager ABIs; this change adds no native calls or hooks. The
one-process rack audition remains a diagnostic and is not the new audio path.

## Verified installed source

`tools/bc2_reload_audio_assets.py` reads the user's installed
`Dist/win32/levels/sp_common/level-00.fbrb`, using the existing bounded archive
reader. The observed index SHA-256 is
`57835d6ace174f83eeb46ec8bbfe3acf8a3e7fde5cb8c8e49f084950e975a842`.
The exact source resources are declared in the tool. Extraction on October 14
decoded all five successfully, without starting the game or changing its files.

| Source | Samples at 48 kHz | Resource SHA-256 |
| --- | ---: | --- |
| XM8 reload 1p | 134252 | `8d855c5845b64c97cc70389f7b42b99bcc1cc4d49732144d4cbdf65aa05d2cd1` |
| SPAS pump 1p | 28864 | `7e3cc4f14e0839f6dce1b3b6d78bf3ef38e82ca056b282557c64a0e3689f3dd5` |
| SPAS shell variant 1 | 29173 | `a535681077aa7c82a193fadb10353ff8701b294691ba152fd9c5e35d08947e6e` |
| SPAS shell variant 2 | 28852 | `4ecaca8d718672fa7d975d59ca2a68c2e95a4132ec036f070771e05d2c47f6eb` |
| SPAS shell variant 3 | 29727 | `77caf2d0c73f25372c5591070f3206075dd95a098276399021d665bed13c2d86` |

The Wave resource contains a 32-byte wrapper, a bounded EA AudioCore stream,
and eight zero footer bytes. Its v1 header is `48 00 00 0c 14 00 bb 80` followed
by the stream flags/sample count. `44` data blocks have an eight-byte header,
then 76-byte XAS1 frames, each producing 128 mono samples. The final `45` block
is four bytes. Wrapper length, sample counts, block bounds, frame alignment,
codec/rate/channel flags and final padding are checked before decoding.

The decoder is the locally installed FFmpeg, using its documented-in-source
`ea_cdata` demuxer through a temporary wrapper. Container interpretation was
cross-checked against primary source:
[vgmstream EA AudioCore](https://github.com/vgmstream/vgmstream/blob/master/src/meta/ea_eaac.c)
and [FFmpeg cdata](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/eacdata.c).
No decoder implementation or original game audio was added to Git. Generated
source/cue manifests retain per-resource and decoded PCM hashes.

## Cue calibration boundary

The tool declares cuts explicitly, with a one-millisecond edge fade. These
positions are waveform/animation calibration, not verified audible event
attribution. In particular, the XM8 handle split still needs listening and
comparison with its original animation. Its 2.797-second sound source must not
be treated as the 4.133-second rifle animation timeline.

| Cue | Source seconds |
| --- | --- |
| XM8 magazine out | .390â€“.820 |
| XM8 magazine in | 1.400â€“1.900 |
| XM8 handle back | 2.005â€“2.120 |
| XM8 handle forward | 2.120â€“2.360 |
| SPAS pump back | .145 to .355 |
| SPAS pump forward | .355 to .601 |
| SPAS shell insertion, variants 1â€“3 | .275â€“.600 |

The XM8 magazine cuts contain the bursts around native removal at .55â€“.633
and insertion at 1.517â€“1.575. The SPAS pump has two distinct waveform bursts,
around .15â€“.325 and .375â€“.535. Cutting the shell source removes its initial
silence so an accepted physical insertion produces immediate audio.

## Host audio implementation

The production source now uses `ReloadAudioChannel` and `ReloadAudioMixer`.
The private waveOut experiment under `reports/native-reload-audio-20261014`
is superseded and remains outside the production build.

The host creates a separate `Local\FrostbiteVR.Audio.v1.<frame-session-token>`
lane before publishing the frame token. The producer connects with the same
token. `PublishOwner(revision, space, observedNs)` refreshes a 150 ms lease using
the original fresh gameplay observation time; revision zero cancels. Revisions
identify owned weapon lifetimes and must not be recycled pointers.
`Publish(ReloadAudioEvent)` carries monotonic ID, input sequence, owner revision,
space generation, observation/expiry (at most 100 ms), role, gain and emitter
coordinates in raw OpenXR tracking-space meters.

The 16-entry queue uses a zero-wait mutex, version/size/token checks and peer
process creation times. Owner changes discard queued cues. Full queues consume
and drop edges, preventing later retries. Host policy validates the latest owner
lease against current space/input/focus/tracking and suppresses duplicate input
sequences per role. Focus/menu/tracking/session/owner loss and lease expiry stop
voices. No game pointer crosses IPC.

OpenXR's advertised `XR_OCULUS_audio_device_guid` supplies an IMMDevice ID.
`FVR_BC2_RELOAD_AUDIO_DEVICE` overrides it with a launcher-selected endpoint.
Otherwise the host reports `windows-default`, without claiming that it is a
headset. Explicit-device failures fail audio initialization instead of silently
rerouting. Core OpenXR does not supply a universal audio endpoint.

XAudio2 2.9 loads from Windows System32. Eight overlapping mono source voices
feed a two-channel mastering voice. Each voice's output matrix updates relative
to the current tracked head, with equal-power panning and distance gain beyond
.7 m. This is stereo positioning, not HRTF or BC2 mixer/occlusion/reverb. A
critical device error stops voices, is reported, and requires host restart to
reopen the endpoint in this version.

`FVR_BC2_RELOAD_AUDIO=0` disables the default-enabled path.
`FVR_BC2_RELOAD_AUDIO_GAIN` accepts 0–1 (default .8).
`FVR_BC2_RELOAD_AUDIO_DIR` overrides executable-adjacent `assets/reload-audio`
or `../../assets/reload-audio`. All PCM loads before the frame loop. Setup route,
readiness, HRESULT and played/dropped/rejected/stopped counts appear in the host
report and periodic timing log.

`tests/ReloadAudioTests.cpp` covers malformed/stale events, dedupe, focus barriers,
owner expiry/change, and actual IPC queue capacity/contention/close behavior.
The parent integration owns compilation and execution of those checks; this
agent did not build while the parent edited build integration. No game/headset
playback was performed.

Primary API contracts:
[OpenXR endpoint](https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrGetAudioOutputDeviceGuidOculus.html),
[XAudio2 device selection](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2-createmasteringvoice),
[output matrix](https://learn.microsoft.com/en-us/windows/win32/api/xaudio2/nf-xaudio2-ixaudio2voice-setoutputmatrix).

The verified native `0x637d50`/`0x637290` functions accept a registered
SoundAsset, not PCM. Their signatures do not prove resource creation, cloning,
registration or sampler-resource lifetime. Native-mixer PCM ingestion would
need further loader/ownership evidence; no such calls were added here.

## Silent mixer runtime check

The actual packaged nine cues in the `b54ad96` candidate initialized through
the production x64 `ReloadAudioMixer` object and Windows-default XAudio2 device
with HRESULT zero. No source voice was started and the played count stayed
zero. An explicit nonexistent endpoint was rejected with `0x80070057`, rather
than rerouted. The ignored local probe and JSON result are in
`reports/native-reload-audio-20261014/` (`mixer-init-b54ad96.json`). This confirms
cue loading and local mixer/device initialization; it does not establish the
headset endpoint, audible event attribution, spatial feel or in-game timing.

## XM8 graph comparison

The SP and MP copies of `US_rif_XM8_reload_1p.dbx` have identical SHA-256
`35fa8c06cd483900bc9ac841ec90af5005db4b3a35a73cebb17da6a3229328a9`.
The graph contains exactly one monophonic sampler with one group and one wave
map entry. Its only public events are `Start` and `Stop`; public parameters are
empty. Sampler Offset and Delay are unconnected zero, and Pitch is unconnected
one. There are no per-magazine or per-handle cue timestamps in this graph.

The cached `Levels/SP_02/animationsoundmap.dbx` (SHA-256
`f9078ce260589598761cb37c2b65db9b15c722118ab2546c9fc6a25563dec73b`) binds
`US_rgl_XM8/1p/1p_reload` animation GUID
`34687db2-3aae-479c-8fce-eb2d41b65a5d` to template GUID
`0f419de5-e13d-420e-bcd5-3ba9f7b5ecd7`. It has no `US_rif_XM8` animation entry.
The scoped, base and SP XM8 entries in the cached exact-tree joins resolve to
that same RGL animation/template pair. No separate 4.133-second rifle reload
sound was found in the SP/MP common sound inventory. The rifle animation donor's
3.333â€“3.433 pull and 3.6â€“3.7 return therefore do not prove the role boundaries
inside this 2.797-second mapped sound. The current handle cuts remain calibration.

## Refined physical pump cues

The refined SPAS split meets at the quiet `.355 s` valley: the rear cue is
210 ms and the forward cue is 246 ms. This preserves the first stroke's tail
and a small forward onset previously omitted by the `.365 s` start. Cuts are
contiguous without overlap, retain the decoded original amplitude and use the
same 1 ms edge fades. The nine regenerated cues were checked against original
PCM: the other seven remain byte-identical, all bounds/hashes match, endpoints
are zero and interior samples are unchanged. Generated assets remain local.

The production x64 mixer accepted and started all nine regenerated cue buffers
with zero output gain, zero drops and HRESULT zero. That muted check exercises
voice submission as well as loading; it does not verify headset output or
audible attribution. The local probe/result are `MixerCueSmoke.cpp` and
`mixer-cues-split-20261014.json` under `reports/native-reload-audio-20261014/`.

Split playback now proves the selected player's exact native weapon,
configuration, mesh and animation identities with coherent reads and a fresh
owner recapture. It does not require the unrelated level-wide sound-map rows:
PCM playback makes no call through that map. The native sound callback still
requires the matching player/listener, exact proven animation GUID and a fresh
host-ready lease before withholding a whole clip. SPAS covers both the pump
and shell-loading animation GUIDs. The legacy complete map resolver remains
available for audits and native auditions.

Event coverage is the starter pair: XM8 magazine out/in and handle rear/forward,
SPAS pump rear/forward and accepted shell insertion (three original variants).
A pump pause cannot schedule its forward sound; that cue follows the completed
physical cycle's applied receipt. Other weapon assets require their own original
wave cuts and owned animation bindings before receiving these replacements.
New native counters distinguish owner publication, host readiness, animation
proof failures, active replacement samples, native suppression and sends by role.
The previous session's zero sends alone did not identify which gate failed;
this change does not claim that failure has been reproduced in the headset.

The incremental x86 native build and focused `Bc2OwnedReloadAudio` /
`Bc2ReloadAudioCue` checks passed. In-game split-cue playback and headset
timing remain unverified for this replacement candidate.
