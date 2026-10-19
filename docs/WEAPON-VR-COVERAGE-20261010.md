> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# Weapon VR coverage and all-gun arsenal — 2026-10-10
## Current integration coverage — October 11

This table describes the integration candidate `0524f7c`, not the older selected
package. Counts are base guns unless the asset count is explicitly given.

| Capability | Coverage | What the count establishes |
|---|---|---|
| Basic controller-held weapon route | Generic native-attachment fallback | No verified all-gun VR compatibility count. The 305-asset load/equip sweep below includes gadgets and alternate modes; it did not fire/test every gun. |
| Gun content catalog | 205 give variants / 64 main body meshes | Available content, including Vietnam and distinct skins; not 64 verified VR weapons. |
| Exact grip/support/translated-muzzle profiles | 2 guns: scoped XM8 and SPAS-12 | Exact profiles; broader native-attachment handling is separate. The XM320 profile lacks verified muzzle placement. |
| Native-strength backward kick and weighted haptics | 5 guns / 6 exact assets, conditionally | XM8_sp_s, SPAS12_sp, F2000_sp, AEK971_sp, M9, M9-3; requires admitted physical authority, a confirmed shot and verified positive native strength. |
| Complete physical mechanisms implemented | 4 guns / 5 assets | XM8 magazine/handle/bolt; SPAS shells/pump; F2000 magazine/handle; M9 and M9-3 magazine/slide/lock-open. This is code coverage, not certification of every complete headset reload flow. |
| Partial physical reload | AEK-971 | Magazine handling only; no charging-handle profile. |

The current integration cache includes the M9/M9-3 native and pistol mechanism
headers. The older table/build caveats below describe the October 10 baseline.
The new feel path uses configured BC2 recoil amplitude and KF2's kick envelope;
actual support ownership halves translation and permits the softer offhand pulse.
Native angular recoil retains the game's own hip/zoom selection and modifiers.

The live feel evidence and current packaged candidate are recorded in
[the current handoff](HANDOFF-RELOAD-GUIDANCE-20261011.md). Basic fallback, exact
profiles, mechanism implementation and live validation are separate coverage
levels; none of these numbers should be promoted to an all-gun acceptance claim.

What it takes to make every BC2 gun workable in VR, which guns have physical
reloads, and how the player can carry everything. Inventory data:
`profiles/weapon-inventory-20261010.csv` (one row per give name per source
bundle: category, async bundle, 1P mesh, material paths, shader coverage).

## 1. Loading: every non-Vietnam gun can draw in SP_02

A gun draws in first person only when all three of these hold:
- its `SoldierWeaponData` resolves from a mounted bundle;
- its `async/weapon/<name>` bundle exists;
- every `<meshset>/<material>` key of its 1P mesh is in a loaded Dx11 shader
  database (see `WEAPON-LOADING-AND-DEBUG-RECOVERY.md` 1.6).

The current `sp02-m9-pistol.cfg` mounts are `mp2_common`, `mp_common` and the
`mp_009sdm` patch bundle (shaders). The SP_02 and MP_009SDM databases together
cover **all 279 non-Vietnam give names**:

| Covered by | Give names |
|---|---|
| both databases | 117 |
| MP_009SDM only (all MP-only guns) | 155 |
| SP_02 only (TOZ194 family, SP_MortarStrike) | 6 |
| neither | 1 (Binoculars, which has no 1P mesh) |

No single Package level covers everything; mp_009sdm, mp_004sdm, mp_003 and
others tie at 444 of 457 rows.

Other loading notes:
- `MEC_shg_TOZ194*` has no async bundle of its own; its mesh is in
  `async/weapon/sp_shg_toz194`. Give the `SP_shg_TOZ194` variant instead.
- **Vietnam** (32 rows, `nam_common`) needs both of these:
  - a mount of `Package/levels/nam_common` (295 MB; the knife, grenade and
    mine meshes live there);
  - a Vietnam shader bundle: `nam_mp_002sdm` (9.4 MB) covers 31 of the 32.
- **Name collisions.** 47 partition names differ between bundles; for
  example, mp2_common carries patched M9-3, M1911A1, G3 and gadgets. Targets
  appended later get lower priority, so mounting mp2_common before mp_common
  lets the patched mp2 copy win, which matches MP levels.

Base guns by category (variants such as `_Kobra`, `_Scoped`, `_Slug` and
`_FRAG` excluded): rifles 46, SMGs 30, LMGs 30, snipers 29, shotguns 39,
pistols 6 (M9, M9-3, M1911, MP412 Rex, MP443 x2), AT launchers 4, rifle
grenade launcher modes 32, underbarrel shotgun 24, underbarrel smoke 24, and
14 gadgets.

## 2. VR support today (default build)

Profiles are keyed by the exact `SoldierWeaponData` asset name
(`owner.equipment.Asset()`), sometimes together with the mesh path. MP
variants of a gun (`US_rif_XM8c` vs `XM8_sp_s`) therefore get no profile.

| Gun (asset) | Grip | Sights | Support hand | Physical reload | 600 ms hold → native reload |
|---|---|---|---|---|---|
| XM8 scoped `XM8_sp_s` | profile | flip sight, ACOG aperture math | yes | **magazine, charging handle, bolt** | no (physical lane) |
| XM320 `40mmgl` (on the XM8) | rejected in headset | adapter only | - | no (launcher deferred) | - |
| SPAS-12 `SPAS12_sp` | profile | none | yes | **tube shells, fore-end pump** | no (physical lane) |
| F2000 `F2000_sp` | native attachment | none | none | **magazine, handle rack** | no |
| AEK-971 `AEK971_sp` | native attachment | GP30 geometry, not accepted | none | **magazine only** | no |
| M9 / M9-3 | native attachment | none | none | no: slide/magazine code is merged (26c7dc7), but the M9 profile header is private and not built | yes |
| Every other gun | native attachment | none | none | **no** | yes |

**Physical reloads exist for 4 guns:** XM8 scoped, SPAS-12, F2000 and AEK-971.
Every other gun reloads natively from a 600 ms hold of the reload button
(`Bc2ReloadButtonGesture.h`, `Bc2Gameplay.cpp` ~2170-2220).

Build caveat: `build/x86` has `BC2_AUTHORED_GRIP_HEADER`,
`BC2_AUTHORED_SUPPORT_HEADER` and `BC2_PISTOL_MECHANISM_HEADER` empty. The
authored grip data in `profiles/checkpoint202` (AEK971_sp, XM8_sp_s grips;
SPAS12_sp support) is not compiled in. Only the checkpoint204 magazine geometry
is.

**What a gun without a profile gets** (`Bc2RigPublication.cpp` ~1026-1075):
- It is held where its native 1P animation puts it in the hand
  (`WeaponOrientationSource::NativeAttachment`, captured about 600 ms after
  equip) and follows the controller.
- It fires natively and gets recoil haptics.
- Aim, support-grip and muzzle-origin gates stay off (`unknown_profile`).

## 3. Gaps, in priority order

1. **Build carries the data.** Wire the checkpoint202 grip and support headers
   and an M9 pistol-mechanism header into the default build, or load them
   from a file.
2. **Aim alignment for unprofiled guns.** Measure each mesh's barrel axis
   (`tools/weapon_profile_pipeline.py`, `tools/check_weapon_alignment.py`) and
   emit a default forward/up per gun, so shots match the visible barrel.
3. **Sights.** A generic iron-sight path (eye to rear/front sight from mesh
   bones) and an optic path for scoped variants. Only the XM8 has any today.
4. **Support hand.** A generic foregrip grab for two-handed guns.
5. **Name aliasing.** Map MP and SP asset names of the same gun and mesh to one
   profile.
6. **Physical reloads (later).** M9 slide data first, then reuse the magazine
   consumer for other rifles, SMGs and LMGs; then tube shotguns, bolt actions
   and belts (`docs/native-weapon-mechanisms-roadmap.md`).
7. **In-game check.** Confirm the 600 ms native hold on non-managed guns
   (start with the M9).

## 4. Spawning with all guns

Cycling in the headset: hold the left trigger fully and flick the right stick right (next) or left (previous). The native DLL signals the test-range events `Local\BC2TestRange.ArsenalNext`/`ArsenalPrev`; nothing happens when the test-range DLL is not injected. Kill switch `FVR_BC2_ARSENAL_INPUT=0`. See docs/CONTROLS-20260930.md.

Findings from Ghidra (BFBC2Game 795745):
- **Slots.** `WeaponSwitchingEnum` slots are 0 Primary1, 1 Primary2, 2 GL1,
  3 GL2, 4 StaticGadget, 5 DynGadget1, 6 DynGadget2, 7 Melee, 8 HandGrenade.
  Real slot 9 is a special carried item.
- **Storage.** Server slots (`soldier+0x2b8`) and client slots (`+0x260`) are
  vectors that the give grows. The ghost replicates at most 15 slots.
- **Switching.** The inventory input tables cover only slots 0..9; a selected
  slot of 10 or more would index past them. Switching follows
  `WeaponSwitchingData.SwitchMap`, so a rifle in slot 7 or 8 would be selected
  by the melee or grenade input.
- **Give.** Each slot holds one item. Give 0x6bcf80 into an occupied slot
  tears down and replaces the old item.

So the player cannot hold ~280 guns at once. The design is a **mod-owned
arsenal**:
- **Cycling.** An ordered list of give names. A dev action calls
  `give(data, slot 0 or 1, selectNow 1, forceSlot 1)` on the game thread with
  the next entry, then replaces the slot.
- **Net ids.** Register a net id per entry (check 0x5debd0, register 0x5e5d70)
  before giving it.
- **Ammo.** Optionally shadow `{loaded, reserve}` per entry and write them back
  with 0x6cd850 then 0x6cd520, as the pickup does. A gun with empty ammo fails
  the switch validator.
- **Avoid** filling slots 4-8, which steal the gadget, melee and grenade
  inputs, and slots 10 and up.

Status: **implemented** in `BC2TestRange.dll`, with the `Arsenal.*` keys and
`tools/testrange/ranges/sp02-arsenal.cfg`:
- `Arsenal.All 1` loads every resident first-person weapon: 290 on SP_02 with
  all mounts, including Vietnam.
- `Arsenal.Gun <name>` lines add specific guns, in the order given.
- **Physical-reload guns lead the rotation:** XM8 scoped, SPAS-12, F2000, AEK-971.
  The order is matched on the data Name the VR profiles use. `Arsenal.First <Name>` lines replace the list.
- **Skipped automatically:** guns without a 1P mesh, and guns without an `async/weapon/<name>` bundle
  (the MEC TOZ194 variants and Binoculars). `NAM_Handgrenade` is excluded by default; its client handler
  lookup `0x8ac380` reads a null at `0x8ac396`.
- **AI cache:** before each give, the DLL registers the gun in the AI ballistics cache if it is missing.
  This mirrors the level loader and fixes the `0xe93ed7` give fault on G3/Mk14EBR/FRAG variants.
- **Full sweep on `1698da1` (2026-10-10 20:46–21:03):** `tools/testrange/harness/Sweep-Arsenal.ps1 -Steps 310 -DelayMs 3000`.
  All 305 resolved guns swapped once (plus wrap-around), zero `fault:` lines, game alive at the end.
- **Sweep result (2026-10-10):** `tools/testrange/harness/Find-ArsenalCrashers.ps1` swapped every gun at
  3 s spacing. With these fixes no gun faults.
- Step forward with F7 or the headset chord (left trigger held + right-stick
  flick right); step back with F6 or the chord flicked left.
- Each step gives the next gun into the Primary slot that is not selected and
  selects it.

Vietnam mounts: `package:levels/nam_common` (data and meshes) and
`package:levels/nam_mp_002sdm` (shaders), appended after the MP mounts.
