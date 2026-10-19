> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Primary-hand authority and front support grip — 2026-10-11

Source branch `fix/hand-authority-support-grip`, based on
`codex/bc2-resume-20261011` (3be9c94). That branch is the native source of the
installed build; `main` (401b456) does not contain it (see below). Offline
source work only: no game or headset run backs any claim here.

## What the user actually ran

| Item | Value |
|---|---|
| Selected package | `<repo>/build/playtest-b74560c/BC2-VR` (`reports/BC2-Selected.private.json`, selected 2026-10-12T01:59:31Z) |
| Source commit | `b74560c` "Fix playtest weapon readiness, native hand poses and ammo prop coverage" (on `codex/bc2-resume-20261011`, base candidate a0fa4fe) |
| `build/x86/BC2NativeProbe.dll` | SHA-256 `efd3c0d0df6e4a214716eb339f8734c0cca07b24bed13ff8fc0dd99ca3135df4`, equal to `package-manifest.json` and to the copy loaded by session `native-trace-20261012-020559-729` |
| Session | 02:05-02:12 UTC; game exit `-2147483645`, new crash report, **final native report empty** (no `two_hand_support` / `hand_ownership` sections survived) |
| Session gun events | XM8_sp_s, M416, MG36, VSS, 40mmgl; 53 hand-claim rows: gun_hold, ammo_object, mechanism, magazine — **zero `weapon_support` claims** |

So the existing support-acquisition patch (b74560c, `docs/re/support-handoff-20261012.md`:
keep the neutral arm after a rejected trial) **was** in the installed DLL, and
support still never acquired. `main` holds only launcher/handoff commits on top
of the older native tree; it lacks b74560c and ~25 other native commits.

## Root causes

### 1. Support claim used a packet that predates the squeeze (high confidence)

`OwnLeft(WeaponSupport)` proved ownership with the renderer's shot-frame input
generation. That packet trails live input by ~5 generations (the one accepted
grab in run 005105 used evidence 24982 at live 24987). `HandInteraction::Evidence`
rejects evidence older than the hand's release barrier — the last packet with
squeeze <= .35 — so on an ordinary reach-then-squeeze the first trial is always
rejected. Run `native-trace-20261012-005105-487` (prior build): 70 claim
rejections, each an `Action` record at 1-15 cm with squeeze 1.0 and no cancel
flags, followed by `None` (arm consumed). b74560c keeps the arm and retries,
which only succeeds after the renderer packet overtakes the barrier, and any
renderer/gather hiccup, transient Mechanism/magazine hand use, or release
re-arms the race. The required evidence was structurally late.

### 2. Support was gated off for most guns (high confidence)

Gameplay discarded the support contact unless the gun had a `WeaponProfile`
with a verified `SupportGrip` feature (only `SPAS12_sp` and `XM8_sp_s`) or an
authored rifle-support binding. M416, MG36, VSS and other unprofiled guns
therefore published a support contact that gameplay threw away: the grip could
never engage on them. This is a default-off evidence gate (AGENTS.md: keep a
kill switch instead) and contradicts "apply this consistently to every weapon".
The contact itself already comes from the original 1P pose (native left wrist
relative to the weapon bone, or the authored/library support contact).

### 3. Primary hand fed from the weapon / transient native fit (medium confidence)

* `TrackedRig::Update`, profile aim path (any rig without the anatomical
  right-hand binding): after aligning the gun to the aim frame it rewrote
  `out.right = wristFromWeapon * gun` — the visible wrist was derived from the
  gun and that weapon's native weapon-in-wrist relation, so every weapon
  switch rotated the hand. With the anatomical binding (b74560c) this branch is
  skipped, but it remained the fallback.
* The weapon-in-wrist fit is re-sampled from the native equip animation on
  every equip and after every actor/rig reset (cutscene return): it freezes the
  first window that is stable for 150 ms after 600 ms. A raise/equip pause, or a
  cutscene-exit pose, gives a different fit each time. Because native aim is
  derived from the visible bore (`boreInGrip`), that per-equip fit changes aim;
  during the pending window aim also falls back to the XR aim pose and then
  jumps to the bore. Reported as "switching weapons and returning from cutscenes
  rotates the hand and changes aim". Not confirmed in a capture (final report
  of the latest run is empty).

## Fixes

| File | Change |
|---|---|
| `src/games/bc2/Bc2Gameplay.cpp` `OwnLeft` | WeaponSupport acquire/renew uses the CURRENT tracked packet as hand evidence (the one carrying the squeeze). The contact geometry is still the renderer's publication for this exact owner/space and must be unexpired. Other claim kinds are unchanged. |
| `src/games/bc2/Bc2Gameplay.cpp` support gate | Support contact is kept for every gun with a published shot frame. |
| `src/interaction/TrackedRig.cpp/.h` | Aim-aligned gun is anchored on the controller wrist; the wrist is no longer re-derived from the gun. Settled native weapon-in-wrist fits are cached per item key and reused immediately on re-equip and after cutscene/actor resets (no pending window, no re-sample). |
| `src/games/bc2/Bc2RigPublication.cpp` | Supplies the item key (FNV of asset name x rig layout fingerprint, never a native pointer) and the legacy switch. |
| `src/games/bc2/Bc2InteractionZones.h` | Settings + documented env switches. |
| `tests/TrackedRigTests.cpp` | Legacy hand-from-weapon expectation replaced; fit-cache reuse case added. |

Reload-authority code (magazine lane, `MagazineBlocksSupport`, rack/bolt
ownership) is untouched.

## Kill switches (all default ON = new behaviour)

| Variable | Effect |
|---|---|
| `FVR_BC2_SUPPORT_CURRENT_EVIDENCE=0` | Support claim waits for the renderer packet again |
| `FVR_BC2_SUPPORT_ALL_WEAPONS=0` | Support grip only on profile-verified/authored guns again |
| `FVR_BC2_WEAPON_FIT_CACHE=0` | Re-sample the native weapon fit on every equip |
| `FVR_BC2_LEGACY_HAND_FROM_WEAPON=1` | Profile aim frame may rotate the primary wrist again |

## Known remaining limits

* Support is still blocked while the magazine lane blocks weapon actions
  (magazine out, owed rack, reload in flight) or the XM8 bolt/rack owns the
  left hand. That is reload-authority behaviour, intentionally unchanged.
* The first fit for each gun is still sampled from its native animation; if
  that first sample is bad it is now consistently bad (cache is per session).
* Two-hand support still steers the gun-hand orientation (intended two-hand aim).

## In-game test steps

1. Build/install the candidate from this branch through the launcher (package
   hash check must show the new DLL hash, not `efd3c0d0...`).
2. XM8 (magazine seated, no owed rack): put the visible left hand on the
   handguard and squeeze once. Expect an immediate grab (left hand snaps/eases
   onto the fore-end, two-hand aim). Release, re-grab 5 times quickly.
3. Repeat on M416, MG36, VSS and one unprofiled gun (AKS74u/F2000).
4. Reach while already squeezing, then squeeze-then-reach: both must grab.
5. Hold support, fire a burst, flick weapon stick without switching: hold persists.
6. Hand authority: note the right hand/controller angle and where the barrel
   points. Switch to another gun and back three times: the hand must not rotate
   relative to the controller; the returning gun must sit and aim exactly as
   before with no settle delay.
7. Trigger a cutscene and return: hand and aim of the held gun unchanged.
8. Report from the native trace: `gameplay.two_hand_support.grabs` > 0,
   `hand_ownership.claim_rejections` near 0, and gun-events `claim` rows with
   `"kind":"weapon_support"`.
9. If anything regresses, retry with the matching kill switch above to isolate it.
