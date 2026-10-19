> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Physical reload authority replacement

Source review prompted by a playtest report: XM8 magazine removal/insertion
worked after death/checkpoint, but reload could not finish. That report does
not establish the cause. The following two lifecycle gaps are independently
visible in the source; live reproduction remains separate.

`PlayerInventoryAnchor` follows the client's player, soldier, weak reference
and resolver generation. The verified inventory reader independently resolves
the authoritative server player, server soldier and inventory. Previously,
`InventoryUpdate` replaced that server snapshot and reused a weapon whenever
its slot, item address, serial, data and firing pointers matched. A changed
server pawn/inventory with recycled item identities could therefore retain an
old chamber, shot budget or pending physical command. An unexplained native
loaded-count change suspends the ledger; suspension deliberately cannot be
cleared by interpreting that ammo change as a refill or respawn.

The runtime now compares the coherent native authority identity before
reusing any weapon. Changed anchor, server player, server soldier, inventory
or inventory data retires all previous ledger incarnations and rejects their
queued commands. Newly configured items initialize from the new authority's
native counts with monotonically new weapon incarnations. Selection changes,
client wrapper changes, failed reads, input gaps and ammunition deltas do not
trigger retirement. Existing hook-time structural ownership checks remain.
The existing runtime report counts these events as `authority_retirements`.

Closed-bolt magazines also need a distinction between feed capacity and
total native capacity. The mod owns firing `+0x98` as magazine plus chamber;
native snapshot/restore can retain that override. Using it as a new ledger's
magazine size would add the chamber a second time. The reader now retains the
existing verified authored derivation independently: `FiringFunctionData`
`+0x184` BaseCapacity multiplied by firing `+0x74`. The exact admitted magazine
descriptor's BaseCapacity must match that native field. Closed-bolt ledger
initialization uses the finite integral authored-derived feed size, even when
`+0x98` already contains the mod's total override. Unknown or fractional
derivations reject initialization rather than guessing rounding. Tube and
open-bolt capacity behavior is unchanged. This adds no native function,
signature, hook or new layout assumption; the fields were already read by
`Bc2PlayerInventory`/`Bc2ReloadState`.

No ammunition is granted during retirement or capacity selection. A new
authority admits exactly its observed loaded and reserve counts; prior
incarnations cannot submit commands into it. Native loaded-count mismatches
within the same authority still suspend the existing ledger.

`Bc2PlayerInventoryTests` exercises a verified server-pawn replacement with
unchanged client anchor and recycled complete item slots, a replacement
inventory, non-retiring selection/client-replication changes, and restored
total capacity 31 with authored feed 30. It also checks multiplier handling
and finite integral derivation. These are offline reader/state checks, not a
claim of live checkpoint or headset acceptance.

Further review found a session-wide hand obligation: a cancelled held magazine
kept a cleanup request, while the suspended weapon could never accept that
request. The frontend's single cleanup slot then refused later guns' commands.
The runtime now permits only `RetainMagazine`/`DropMagazine` on a still-owned
suspended incarnation, including an old busy reload. These moves only change
the held magazine's location. A guarded candidate must preserve loaded, feed,
chamber, reserve, live, spent, discarded and admitted counts and conservation.
The suspended update branch performs no capacity/count read or native write,
never clears suspension or busy state, and reports
`suspended_metadata_cleanups`. Other commands remain blocked on that weapon.
The existing interaction regression verifies that cleanup leaves the old
counts and quarantine intact while a healthy second gun can eject and fire.

A prepared automatic reload also needs settlement when cancelled before its
native phase enters. Previously, the fire barrier could mark the request
terminal while the ledger transaction remained active; `AfterUpdate` then
skipped it because the request was no longer busy. Cancellation now leaves a
prepared request busy until the existing owned idle/count check settles it.
Only a not-yet-prepared request becomes terminal immediately. The state
regression verifies pre-entry cancellation preserves original chamber/counts
and restores readiness without a refill.

Managed physical readiness now masks `Fire` and `AlternateFire` before native
input application, after reload-stop replay. The projectile hook remains the
final barrier for scheduled shots. This covers tube weapons owing a pump,
suspended/settling weapons and known physical initialization; unmanaged
weapons retain their existing input path. A blocked attempted trigger press
can still produce the existing dry-fire haptic. No sound emitter is muted.

Pouch display also had missing asset registrations. Its installed-part seeds
covered only XM8, AEK and the SPAS shell. The exact catalog now covers all
13 admitted ammo identities: those three, F2000_sp, M9/M9-3, M416, MG36,
VSS, AKS74u, UZI, XM8C and XM8 LMG. Their 21 new base/scoped/Kobra configuration
bindings share seven exact native asset/mesh/magazine-part keys; independent
geometry extraction matches each pouch source's mesh/LOD hashes and rig.
F2000 without the SP suffix remains holster-only until its separate native
reload descriptor is admitted. No generic replacement geometry is used.

The private installed cache contains 32 parts including the preserved 19
holstered weapons, and remains below the existing 16 MiB limit. Magazine
sections retain their section-local native vertex attributes instead of
duplicating a whole weapon vertex buffer. Indexed fingerprints and triangles
remain unchanged. `Bc2BodyAmmoCacheTests` reconstructed the actual complete
cache: 32 parts, 143956 triangles, plus its three deterministic CPU validation
groups. This validates extraction and decoder admission, not texture rendering
or headset visibility; the material path is a separate repair.

The combined x86 DLL built on 2026-10-12. Focused PhysicalWeaponState,
Bc2PlayerInventory and Bc2PhysicalWeaponInteraction checks passed, including
pre-entry reload cancellation and pump-required input suppression. The final
input mask follows reload-stop replay; later Gather stages cannot reassert Fire.
EntryAction8 receives this masked input. Live sound/physical-controller behavior
is not established by these CPU checks.
