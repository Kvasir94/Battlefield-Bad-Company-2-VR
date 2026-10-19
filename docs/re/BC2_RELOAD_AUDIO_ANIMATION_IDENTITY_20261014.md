# Native reload sound GUID identity boundary

The canonical GUIDs in the installed SP_02 animation sound map are exact:

| Role | Animation instance GUID | Sound template GUID |
| --- | --- | --- |
| XM8 reload | `34687db2-3aae-479c-8fce-eb2d41b65a5d` | `0f419de5-e13d-420e-bcd5-3ba9f7b5ecd7` |
| SPAS pump | `2d9cf41b-3aba-4a22-82a8-64051c180432` | `9515f482-793e-420d-b107-234f1c7ece0d` |

The map hash is recorded in `BC2_PHYSICAL_RELOAD_AUDIO_20261014.md`. These are
canonical DBX identities, not established native memory byte arrays. Earlier
private profile scripts used Python `UUID.bytes_le`, but the saved historical
correlation reported no exact map matches. That conversion alone is not proof.
The October 14 protected candidate's available sound trace contains eight idle
animation identities and does not corroborate these two target animations.
Do not claim verified memory arrays from that evidence.

## Read the native identity instead of guessing its encoding

The existing `Bc2ReloadAudioCue.h` resolver and cached disassembly establish
that the native GUID is sixteen bytes through the pointer at AnimationAsset+4.
The map constructor at VA `0x006f0a30` keys its animation-to-sound map with that
identity. No new call or hook is required to read it.

For the exact current owned weapon, walk its reflected data with the existing
bounded/type-checked reader:

1. Confirm ClientSoldierWeapon+4 equals the verified owned SoldierWeaponData.
2. Read its single WeaponStates entry (+0x88), then AnimTree1p at state+0xb8.
3. Require SpecificAnimTreeData and its single ResultAnimTrees entry (+0x20).
4. Require ResultAnimTreeData; its AnimationList is at +0x74.
5. Read the exact documented role index, require AnimationAsset, and copy the
   sixteen bytes at the pointer stored at animation+4.
6. Join the active AnimationSoundMapData category/pair arrays by animation
   object pointer equality. Require one matching pair with a SoundTemplate.
7. Re-read and require the same owner/tree/result/animation/map/sound/GUID before
   caching. Invalidate the cache on owned weapon or space generation changes.

The cached installed-source joins show both lists contain 57 animations:

| Exact weapon source | Result tree GUID | Role index |
| --- | --- | ---: |
| `US_rgl_XM8/SP_rgl_XM8_Scoped.dbx` | `229E72F4-155C-11DF-9CA7-E8C6EDFFA3B9` | reload 25 |
| `UL_shg_SPAS12/SP_shg_SPAS12_Slug.dbx` | `C6F253B5-155B-11DF-9CA7-E8C6EDFFA3B9` | pump 30 |

The weapon DBX SHA-256 values are respectively
`23a7261d6cbaffe7c642af167edfca1a8ac818ebef551538b01a5784ef8f472f` and
`d63b873bc98ae672b9fd6176894329c691a7d3172585eb8090e7020a7c9739eb`.
The source joins are in the private
`tmp-bc2-audio-20261011/weapon-audio-coverage.private.json`.
The XM8 list also contains the SPAS pump at index 30 as a generic fallback,
so the caller must retain its exact current-weapon/role eligibility check.

This supports a bounded native-object resolver; it does not make unchecked
index access or suppression of every animation safe. Suppression still belongs
only to the exact freshly owned soldier, the matching current weapon role, a
ready replacement audio path, and the resolved animation identity.

## Implemented loaded-object resolver

`src/games/bc2/Bc2OwnedReloadAudio.h` adds `ResolveOwnedReloadAudio`. It uses
the existing `ReloadAudioMemory`/bounded `Reader` and never converts canonical
GUID text. The result's `binding.guid` is copied from the actual selected
AnimationAsset's native sixteen bytes. `role` is `Xm8Reload` or `SpasPump`.
No native function is called, no new hook is introduced, and no memory is written.

The scope guards require the following exact native strings:

| Weapon label | WeaponAssetName | Required SkinnedMeshAsset Name |
| --- | --- | --- |
| `XM8_sp_s` | `Objects/Weapons/Handheld/US_rgl_XM8/SP_rgl_XM8_Scoped` | `Objects/Weapons/Handheld/US_rgl_XM8/US_rgl_XM8_Mesh` |
| `SPAS12_sp` | `Objects/Weapons/Handheld/UL_shg_SPAS12/SP_shg_SPAS12` | `Objects/Weapons/Handheld/UL_shg_SPAS12/UL_shg_SPAS-12_Mesh` |

The ordinary SPAS DBX (not the slug variant) is also present in the cached
coverage join: SHA-256
`800b46aead4e2704cb771176e5d7ae58f72838beb89fe4cb3f9d7192089f2113`,
same tree/result as the slug row above, 57 animations, pump index 30 and the
same canonical animation/sound pair. The implementation accepts only the
ordinary SPAS configuration, matching the current physical reload descriptor.

Name at SoldierWeaponData+0xc is the established equipment identity read.
WeaponAssetName+0x40 is recorded in `Bc2TestRangeFields.h` (type info
`0x01bfca60`) and used by `ReadReloadState` and `Bc2SelectedMeshes1p.cpp`.
Meshes1p at WeaponStateData+0x80, SkinnedMeshAsset Name+0xc and reflected
types are checked by that existing selected-mesh reader. The resolver repeats
those known contracts. It does **not** assume an unverified AnimationAsset,
SpecificAnimTreeData or SoundTemplate name field.

The callback captures a freshly re-resolved typed owner before and after two
complete reads. `OwnedReloadAudioIdentity` contains the existing five-field
audio owner plus a nonzero equipment generation. The caller must verify its
full local selected-player/inventory/equipment chain on each callback, not
return a cached token. Both passes must agree on owner, weapon data, persistence,
mesh, tree, result, animation, native GUID, map, map data and unique SoundTemplate
pointer. A duplicate AnimationAsset GUID, duplicate matching map pair, unknown
type, missing scope guard, malformed array or bounded-read failure rejects.
This is repeated coherence checking, not an atomic native snapshot.

Fixture source `tests/Bc2OwnedReloadAudioTests.cpp` uses active `CHECK` macros
under release builds. It covers both roles, exact scope rejection, type/count
changes, duplicate mapping, a different animation pointer with the same GUID,
ownership/equipment changes, mid-read GUID mutation, read bounds and oversized
arrays. Compilation/execution belongs to the parent build batch; this addition
has no new in-game validation. Cache only on fresh owned Gather, then invalidate
on owner/equipment/space/focus changes. Suppression additionally requires the
replacement host to be ready, the appropriate physical action to own the sound,
and automatic reload to be inactive. The resolver alone grants none of those
authorities.

Production integration now refreshes this proof in owned Gather (250 ms for a
successful binding; failed resolution retries at most once per second). A
complete owner/equipment/space change invalidates the cache. The existing
AnimSoundStart hook only compares the immutable publication's GUID and verified
listener/soldier; it does not traverse the asset tree or map. Exact event audio
requires the host acknowledgment and proof, with automatic reload inactive.
The private rack audition is canceled and cannot start while event audio owns
the lane. Without readiness or proof, the existing native/learned sound path
remains available.

Audio.v1 now has a 1272-byte shared layout (size mismatch rejects old peers).
The host acknowledges the original owner observation only while its initialized
mixer has no error and current tracked input is focused in the same space.
The producer checks that acknowledgment's original 150 ms owner lease; a new
producer heartbeat cannot extend it. Hook publication expires at the earlier
of that deadline and the original input deadline. Stop/focus loss clears host
readiness, and channel contention cannot manufacture a newer acknowledgment.
`ReloadAudioTests` also checks acknowledgment absence, owner/space mismatch,
producer inability to acknowledge, explicit revocation, ownership change and
unchanged ready expiry after a producer heartbeat. These new integration edits
still require the parent's focused build/test batch; no game was launched.
