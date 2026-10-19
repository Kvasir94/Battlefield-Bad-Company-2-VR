> **Historical development record.** The status, priorities, assignments and workflow restrictions below describe the recorded work, not current instructions.
> Start with [current status](STATUS.md) and [development rules](../AGENTS.md).

# BC2 reflection TypeInfo: static dump tool and layout reference

Written 2026-10-08. Offline work only. The real executable was **not** available in
this container, so every layout below comes from FrostbiteVR code that already
reads it. Each layout is cited to file:line in
`/home/claude/src/FrostbiteVR` (commit `a820f76b`). The tool has only been run
against a synthetic PE that encodes these same assumptions. Treat the first run on
the real exe as the actual verification (see §6).

**Target, pinned.** Every repo address quoted here is an RVA against image base
`0x400000`, and is valid **only** for:

```
BFBC2Game.exe  build 795745  PE32 i386
SHA256 3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258   (BUILDING.md:47)
Sections: .text ctr CONST .rdata .data typeinfo fieldinf .tls .rsrc   SizeOfImage 0x1913000
```

The tool hashes its input and prints a note when the hash differs from this one.
In that case the anchor RVAs in §5 do not apply, but the scan itself still does.

---

## 1. What the tool does

```
python bc2_typeinfo_dump.py "C:\Games\BFBC2\BFBC2Game.exe" -o out\
python -m unittest discover -s tests -v        # 21 tests, synthetic PE, no game needed
```

You need Python 3.10 or later and nothing else (no `pefile`, no capstone). It reads
the file and never runs or touches the game.

| Output | Contents |
|---|---|
| `bc2_types.json` | exe identity (sha256, image base, COFF timestamp, sections). For each class and value type: name, size, flags, parent, TypeInfo/meta VAs, raw meta bytes, registration site/ctor, and fields `{name, offset, typeName, kind, elementTypeName, raw record flags/extra}`. Also enums `{name, values}`, primitives, summary (rejections, warnings, ctor histogram, which sections each record lives in) and the repo-anchor check. |
| `bc2_types.h` | `#pragma pack(1)` C++ structs. Enums come first, then structs in topological order (parent and inline value types before their users). Gaps are filled with explicit `_pad_XXXX` bytes. Each struct gets `static_assert(sizeof)` and `static_assert(offsetof)` per member. Pointers are `bc2_ptr32` (uint32), so the header compiles on 32-bit and 64-bit hosts. |
| `ghidra_import_types.py` | Ghidra script that runs under Jython 2.7 or PyGhidra, with the data embedded as base64 JSON. It creates `/BC2_TypeInfo` enums and structs (including pointers to struct) and the labels `TypeInfo_X`, `TypeMeta_X`, `TypeFields_X` and `RegisterType_X_<va>`. It rebases if Ghidra loaded the image at a different base. |
| `bc2_types.rcnet` | ReClass.NET project: a zip holding `Data.xml`. Every class is covered by contiguous nodes that sum to its size. Best effort, see Q9. |
| `bc2_types.csv` | Flat class/offset/size/field/type/kind/element table. Always reliable. |

Flags: `--strict` (exit 3 if any repo anchor fails), `--static-scan` (also accept
TypeInfo that is pre-initialized in `.data`; noisy), `--no-store-scan`, `--prefix`.
Exit codes: 0 ok, 1 unreadable or not PE32, 2 no types found, 3 strict anchor failure.

## 2. Method

1. **Parse the PE myself.** Read the DOS, COFF and optional headers (magic `0x10b`
   is required) and the section table. A VA counts as "file-backed" only if it
   falls within `min(SizeOfRawData, VirtualSize)` of a single section.
2. **Find registration sites.** In every executable section (and in `ctr` even
   without the X bit), find every `B9 imm32 E8 rel32` (`mov ecx, info; call ctor`)
   whose `info` lands in TypeInfo storage. From there, walk backwards over
   contiguous `68 imm32` / `6A imm8` pushes. The nearest push is arg1.
3. **Classify each site by its meta.** arg1 must be a valid meta (§3.2).
   - `flags == 0x35` (class): exactly the 4-push form from the repo. arg2 is the
     parent, arg3 is an unknown pointer, arg4 is the field array.
   - Value types and enums: fields come from `meta+0x18`.
   - Any other flags are recorded as "primitive/other" with no fields.
   - Sites whose class ctor body matches the repo's ctor bytes are tagged
     `ctorVerified`.
4. **Fallback forms.** `C7 05 [info+4], meta` store form (on by default; rejects
   classes). Pre-initialized `.data` (opt-in).
5. **Read the field tables.** Validate every 24-byte record: name, offset < size,
   and a type pointer that lands in TypeInfo storage. If one record fails, the whole
   table is dropped and the reason is logged. This is fail-closed, the same way the
   repo readers behave.
6. **Resolve types.** Field and element TypeInfo VAs resolve to names through the
   registration map. Anything unmatched goes to `summary.unresolvedTypeRefs` with a
   reference count.
7. **Plan the layout once per struct**, then emit it four ways (header, Ghidra,
   ReClass, CSV). A layout conflict (field overlaps the base, type size larger than
   the gap, unknown size) never stops the run. It turns into raw bytes plus a
   `layoutNotes` entry.

Validation rules: pointers must land in a non-executable, file-backed section
(`.rdata`, `CONST`, `fieldinf`, `.data`). TypeInfo storage must be a writable
section, or `typeinfo`, checked by virtual range because the storage may be BSS.
Names must be printable ASCII, 1 to 191 bytes, NUL-terminated within the section.
Class and value-type sizes must be non-zero. Every rejection is listed with its
site in `summary.rejected` and in the console summary.

## 3. Exact layouts (with evidence)

### 3.1 Registration site (static). Confidence: **high** for classes, **low** for other kinds

```
68 <fields>   push arg4  field record array          Bc2OpticDiscovery.cpp:34-35 (U(at+1))
68 <arg3>     push arg3  unknown, stored at TI+0x10  (ctor decode below)
68 <parent>   push arg2  parent TypeInfo             Bc2OpticDiscovery.cpp:35 (U(at+11))
68 <meta>     push arg1  meta                        Bc2OpticDiscovery.cpp:35 (U(at+16))
B9 <info>     mov ecx, TypeInfo storage              Bc2OpticDiscovery.cpp:35 (U(at+21))
E8 <ctor>     call class ctor, ret 0x10              Bc2OpticDiscovery.cpp:36-39
```

The engine-survey summary lists the pushes as "fields; parent; meta". The bytes
show four pushes, with an unknown argument between fields and parent.

Decoding the ctor head (`Bc2OpticDiscovery.cpp:38`) and the parent ctor from
`tools/capture_reload_state.py:214` (`… 89 4E 10 … 89 46 14 … 89 56 24`):
`TI+0x10 = arg3`, `TI+0x14 = arg2 (parent)`, `TI+0x24 = arg4 (fields)`. The base
`TypeInfo(meta)` ctor sets `TI+4 = meta`.

**Value types, enums and primitives:** the repo never decodes their registration.
The 1-push `push meta; mov ecx, info; call` form in this tool is an **assumption**.
The console summary prints the ctor histogram (sites, kinds, push counts) so the
real shape is visible on the first run.

### 3.2 TypeInfo (runtime object, in `typeinfo` storage). Confidence: **high** for +4/+0x14/+0x24

| Off | Meaning | Evidence |
|---|---|---|
| +0x00 | not decoded (probably a vtable) | none |
| +0x04 | meta pointer | Bc2ReloadState.cpp:51, Bc2SelectedMeshes1p.cpp:66, Bc2OpticFilter.cpp:46 |
| +0x10 | ctor arg3 (unknown) | capture_reload_state.py:214 ctor bytes |
| +0x14 | parent TypeInfo; **root has parent == self** | Bc2BoatAim.cpp:35, Bc2SelectedMeshes1p.cpp:107-108, capture_reload_state.py:226-227 |
| +0x24 | field array (classes) | Bc2BoatAim.cpp:30, Bc2SelectedMeshes1p.cpp:68, capture_reload_state.py:127 |

The storage is **uninitialized on disk** ("TypeInfo storage is initialized by native
registration", Bc2OpticFilter.cpp:44-45; release/AUTOMATIC-STOCK-BOLT-DISPATCH.md:13).
So the static dump reads the pushed immediates, never the storage itself.

Live objects reach their TypeInfo through vtable slot 2, a 6-byte getter
`B8 <TypeInfo> C3` (Bc2BoatAim.cpp:18-21, capture_reload_state.py:143-147). That
getter offers an alternative static route, not implemented here (Q7).

### 3.3 Meta (immutable, `.rdata`/`CONST`). Confidence: **high** for fields used by the repo

| Off | Type | Meaning | Evidence |
|---|---|---|---|
| +0x00 | ptr | name (ASCII) | Bc2OpticDiscovery.cpp:37, Bc2ReloadState.cpp:51 |
| +0x04 | u16 | flags: `0x35` class, `0x29` value type, `0x179` enum | Bc2BoatAim.cpp:27-29, Bc2SelectedMeshes1p.cpp:68-70, Bc2BodyInventory.cpp:46 |
| +0x06 | u16 | size | Bc2OpticDiscovery.cpp:40, Bc2ReloadState.cpp:52 |
| +0x0D | u8 | field / enumerator count | Bc2OpticDiscovery.cpp:40, Bc2BodyInventory.cpp:31 |
| +0x18 | ptr | field array, **value types and enums only** | Bc2SelectedMeshes1p.cpp:69, Bc2BodyInventory.cpp:34, Bc2ReloadState.cpp:105 |
| +0x08..+0x0C, +0x0E..+0x17 | | not decoded; exported raw as `metaRaw` | none |

The repo reads at least 28 bytes (`Image(meta,28)`, Bc2SelectedMeshes1p.cpp:66). The
tool uses the same minimum.

### 3.4 Field record (24 bytes, `fieldinf`). Confidence: **high**

| Off | Meaning | Evidence |
|---|---|---|
| +0x00 | name ptr | Bc2OpticDiscovery.cpp:48 |
| +0x04 | record flags (undecoded, exported) | capture_reload_state.py:132 |
| +0x08 | **type** TypeInfo | Bc2BoatAim.cpp:32 (`row[2]`), Bc2BodyInventory.cpp:45 (`f[2]`), Bc2ReloadState.cpp:112 |
| +0x0C | **element** TypeInfo for arrays (`ArrayBase`) | Bc2OpticDiscovery.cpp:49, Bc2SelectedMeshes1p.cpp:98-99 (`words[3]`) |
| +0x10 | offset in the owner; **for enums, the value** | Bc2OpticDiscovery.cpp:48, Bc2BodyInventory.cpp:51 |
| +0x14 | unknown (exported as `recordExtra`) | capture_reload_state.py:132 |

The task brief put "type at +12". That is the array element slot. The type is at +8.

### 3.5 Semantics. Confidence: **medium**

- **Field tables list only the class's own fields.** Inherited lookups walk the
  parent chain (Bc2BoatAim.cpp:24-35). So the header embeds the parent as a `base`
  member and expects own fields at or after `parent.size`. Any violation is logged
  as a layout note.
- **Fields whose type is a class are 32-bit pointers.** `VehicleEntityData.Mesh@500`
  is dereferenced as a word (Bc2BoatProfile.cpp:52-55), and so is `GameObjectData.Name`
  (`String`).
- **Value-type fields are inline**, and their size is the meta size (`WeaponStateData`
  0xc8 elements, Bc2ReloadState.cpp:115-117).
- **`ArrayBase` field size** comes from ArrayBase's own meta if it is registered;
  otherwise from the gap to the next field. Observed runtime contents disagree:
  `WeaponStates` is read as 16 bytes `{?, elemTI, begin, end}` (Bc2ReloadState.cpp:89),
  while `Mappings` reads count at +12 and data at +16 (Bc2BoatProfile.cpp:56-58).
- **Enum size** is the meta size (`WeaponClassEnum` = 4, Bc2BodyInventory.cpp:46).
  Values are treated as signed int32.

## 4. Testing done here

`tests/test_bc2_typeinfo_dump.py` builds a synthetic PE32 with the real section
names. TypeInfo storage sits in a BSS-like `typeinfo` section that has **no** write
bit, field records live in `fieldinf`, and the class ctor carries the repo's exact
ctor bytes. It registers 5 primitives, an enum with a negative value, a `Vec3` value
type, a 3-deep class chain whose root has parent == self, and 4 malformed
registrations. The tests check:

- the PE parse
- parents and fields, including the array element
- enum values
- each rejection reason
- section statistics
- header compiles under g++ and clang++, and a deliberately wrong offset makes the `static_assert` fail
- the Ghidra script parses, and its labels and member sizes are correct
- the ReClass node sizes sum to each class size
- the CSV contents
- CLI exit codes
- the store form
- 150 random byte-corruption runs neither crash nor emit a struct whose member sizes miss its size

Result: **21/21 pass** (Python 3.13). The Ghidra script and the `.rcnet` were
**not** loaded into Ghidra or ReClass.NET here.

## 5. Built-in anchor check (run on the real exe)

The tool compares its output against facts the repo already proved. Results go to
`anchors` in the JSON, and `--strict` turns any failure into exit code 3.

| Type | Expectation | Source |
|---|---|---|
| ZoomLevelData | size 0x74, TI RVA 0x17fc7fc, FieldOfView@0x48 | Bc2OpticDiscovery.cpp:55-58, tests/Bc2OpticDiscoveryTests.cpp:18 |
| SoldierAimingSimulationData | 0x7c, TI 0x17fc898, ZoomLevels@0x4c elem ZoomLevelData, ZoomType@0x48 | same |
| ScopeFilterData / SniperLensScopeFilterData | 0x30 / 0xd0, TI 0x17f6b48, parent = ScopeFilterData, BlurScale@0xc0, BlurCenter@0x60, ScissorRegion@0x20 | same |
| SoldierWeaponData | class 0x130, WeaponClass@0x84 : WeaponClassEnum, WeaponStates@0x88 ArrayBase<WeaponStateData> | Bc2BodyInventory.cpp:39-46, Bc2SelectedMeshes1p.cpp:97 |
| WeaponClassEnum | enum, size 4, wcAssault..wcUgl = 0..5 | Bc2BodyInventory.cpp:46-51 |
| WeaponStateData | value type 0xc8, IsPumpAction@0xc0 Boolean, Meshes1p@0x80 ArrayBase<SkinnedMeshAsset> | Bc2ReloadState.cpp:104-112, Bc2SelectedMeshes1p.cpp:98-99 |
| SkinnedMeshAsset, Boolean | class 0x44; size 1 | Bc2SelectedMeshes1p.cpp:100, Bc2ReloadState.cpp:112 |
| GameObjectData, EntryComponentData, VehicleEntityData, InputActionMappingsData | Name@12 String; EntryOrderNumber@176, EntryClass@184, InputMapping@192; Mesh@500; Mappings@12 | Bc2BoatProfile.cpp:50-56 |
| RotationBodyData / ChildRotationBodyData | RotationAxis@28, AngularConstraintMin@20, AngularMomentumMultiplier@44; RotationInput@56, UseLinearInput@72 | Bc2BoatAim.cpp:65-70 |

## 6. Open questions to verify on the real exe

1. **Registration form for non-classes.** Does every value type, enum and primitive
   go through `push meta; mov ecx, info; call`? Check the ctor histogram. If
   `WeaponStateData`, `WeaponClassEnum` or `Boolean` come back absent, the form
   differs, and Q7 is the fallback.
2. **Is `typeinfo` really the TypeInfo storage, with `fieldinf` holding the records?**
   Check `summary.sectionsUsed`. Repo RVAs `0x17f6b48` to `0x17fc898` should fall
   in `typeinfo`.
3. **Is `ctr` code?** If registrations live in static-initializer thunks there,
   check `sectionsUsed.site`.
4. **Primitive flags.** Only 0x35, 0x29 and 0x179 are observed. Every other flag
   value is exported under `summary.otherFlags`. Map them, especially ArrayBase,
   String and Boolean.
5. **Meta bytes 8 to 23.** Is the count really a u8 at +13 (byte +12 could be part
   of a u16)? Check for classes with more than 255 fields. `metaRaw` is exported
   for this.
6. **Field record +4 and +20, and TypeInfo +0x10 (ctor arg3).** All undecoded.
7. **Alternative static route.** The vtable-slot-2 getters `B8 <TI> C3` link each
   runtime class's vtable to its TypeInfo. Not used here. It would also give
   class-to-vtable mapping, which is useful for hooking.
8. **Inherited and own field tables.** Confirm that no own field sits below
   `parent.size`, and that `layoutNotes` is empty for most classes. Confirm
   `ArrayBase`'s size and the field kinds of `String` and `CString`.
9. **ReClass.NET format.** The format is written from memory of ReClassNetFile:
   root `reclass` version 65537, base64 GUID `uuid`, node types `Hex32Node`,
   `Int32Node`, `ClassInstanceNode` with `reference` set to the uuid. Open the
   `.rcnet` once. If it fails to load, use the CSV.
10. **Ghidra.** Run the script once and check how many structs it reports and how
    many errors. Enum negative values may fail on older Ghidra versions; those are
    reported per value.
11. **Duplicate names.** Multiple TypeInfos with the same name are possible
    (`Bc2OpticDiscovery.cpp:41` requires uniqueness for its four). The tool keeps
    them all and suffixes `_2`, `_3` in C++.

## 7. Files

- `bc2_typeinfo_dump.py`: the tool (standard library only)
- `tests/test_bc2_typeinfo_dump.py`: synthetic-PE builder and 21 unit tests
- `README.md`: this document
