> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 context retarget and first-person world depth (lane R2, 2026-10-10)

Build 795745 (`BFBC2Game.exe`, x86, preferred base 0x400000). Evidence labels:
**DIS** = read from disassembly/decompilation (REA Ghidra provider or the
read-only Ghidra copy), **INF** = inferred, not yet confirmed at runtime.

## Constant bindings

| VA | Signature | Role | Label |
|---|---|---|---|
| `0x5c7f20` | `__thiscall(ctx, const float* m)`, `ret 4` | Projection setter. Sets ctx+8 \|= 0x8000, ctx+7++, optionally scales by `[0x1412cb4]` when renderer vf+0xb4, then `ctx+0x20 = 0x5c0ec0(ctx, "shaderState/projectionMatrix", m, 1)`. | DIS |
| `0x5c0ec0` | `__thiscall(ctx, name, src, n)` | Per-context lock-free frame-arena alloc + memcpy of n x 64 bytes. | DIS |
| `0x5c0e10` | same shape, 16-byte payload | Same for `cameraPos`. | DIS |
| `0xb92b40` | `__thiscall(ctx, renderBlock primary, cullingBlock secondary)`, `ret 8` | Context camera: cameraPos = primary+0x80, viewMatrix = primary+0x220 (rebuild `0x96f260` if dirty&1), projection via `0x5c7f20(primary+0x2e0)` at `0xb92be4` (rebuild `0x973800` if dirty&2), frustum `0x965bf0` if secondary dirty&8, `0x9a13e0(secondary+0x90)`. | DIS |

- There is no shared per-view constant block. Each shaderState context holds
  pointers to its own arena copies: +0x1c viewMatrix, +0x20 projectionMatrix,
  +0x24 cameraPos. A rebinding allocates new memory; it never writes in place.
  So the setters are the choke point, and an exact-write scope over the arena
  copies is the least invasive left-eye correction. (DIS)
- `0xb92be4` is `E8 37 53 A3 FF` -> `0x5c7f20`; return `0xb92be9` = entry+0xa9.
  The mod pins this via the existing context-camera body hash (0xce bytes). (DIS)
- `0xb92b40` callers: `0xb9c990` (5x, per-view visibility, only from
  `0xbadff0` = prepareVisibility) and `0xbdfd80` (from `0xbe0900`). (DIS)

### `0x5c7f20` callers (12)

| Call | Function | Notes | Label |
|---|---|---|---|
| `0xc446e3` | `0xc44520` | mesh decal depth bias (strict path) | DIS |
| `0xa13b38`, `0xa13e22` | `0xa138d0` | terrain; job DATA `0x144fd20` (strict path) | DIS |
| `0xb92be4` | `0xb92b40` | context camera | DIS |
| `0xb9d0ab` | `0xb9c990` | identity view `0x14f2570` + projection ESI+0x3c0 | DIS |
| `0x5cbb06` | `0x5cb6b0` | | DIS |
| `0x5d766f` | `0x5d73d0` | first-person object prepare = `viewAnchor.projectionCaller` (return `0x5d7674`) | DIS |
| `0x5d7f23` | `0x5d7ad0` | | DIS |
| `0xc460e0`, `0xc4db39`, `0xbbfa4c`, `0xbdf1c0` | | | DIS |

`0x5d73d0` runs from job function `0x5dd7f0` (DATA ref `0x141ba2c`), so
projection bindings are made on Frostbite job threads, not the render thread. (DIS)

## General left-eye retarget (implemented)

The previous code retargeted only the context-camera bindings gathered for each
eye, the first-person weapon, and the two depth-bias callers. Now:

- **Strict** (unchanged, any mismatch fails the frame): gathered context
  cameras, weapon contexts, mesh `0xc446e3` and terrain `0xa13b38`.
- **Soft** (new, `FVR_BC2_GENERAL_RETARGET=0` disables): any other `0x5c7f20`
  binding made while a tracked frame is live, outside native Draw and Prepare,
  and not from the weapon caller or `0xb92be9`. If its matrix is
  `fvr::math::RetargetCameraBinding(values, right block, left block)`:
  (a) a bitwise copy of a 16-byte-aligned matrix of the right eye's 0x460-byte
  render block maps to the left block's matrix at the same offset; (b) otherwise
  a depth-biased copy of the right projection keeps its clip Z and takes the
  left X/Y/W. Left-eye, shadow and unrelated bindings do not match.
- **Soft context cameras**: a `0xb92b40` call outside the owned gather (e.g.
  `0xbdfd80`) whose view/projection/position equal the tracked right eye's
  (+0x220/+0x2e0/+0x80, 1e-3) gets the left view, position and retargeted projection.
  Lane R4 (`BC2_LATE_LATCH_DEPTH_RE.md`) found `0xb92b40` is only reached from the visibility side, so this path may stay at zero (`general_cameras`); it is kept as a guard for `0xbdfd80`. (INF)
- In `ScopedContextEye::Apply` soft entries are staged last. A rebound context,
  changed bytes or an address overlapping a strict write marks the entry stale
  (`soft_stale`), and the frame continues.
- Soft entries may use at most 384 of the 512 derived slots, leaving room for the strict ones.

The write batch (896 entries, ~120 KB) now lives in a static two-slot pool and
is acquired only when a scope applies.

## First-person world depth (implemented)

- **Prepare `0xbb0ea0`** (only caller `0xbb4d80`, vtable `0x14776a4`). In the
  per-slice loop over view+0x1680, for slice 0, when Settings(+0x94)+0xbd == 0 and
  +0xb8 != 0, the foreground list WR+0x254/+0x258 is merged (`0xb9f850`) into
  the first "worldMainOpaque" list. With +0xb8 && +0xbd a separate
  "worldForegroundDepthClear" (type 0xe) pass is queued instead. (DIS)
- The merge only happens when Settings+0x28 (slice selector) is -1 or 0. The mod
  reports the value as `settings_slice`. (DIS for the condition)
- Draw `0xbb1db0` does not read +0xb8/+0xbd, so the merge is decided entirely in
  Prepare, and a write at Prepare entry is sufficient. (DIS)
- "worldForegroundBlur" depends on +0xb8 && +0xbb only. The Z pass also gets the
  foreground list when +0xb8 && +0xba && !renderer vf+0xb4 && !PostProcess(+0x197). (DIS)
  Blur (+0xbb/+0xbe) and scope filter (+0xbf) are deliberately left native.
- The mod writes +0xbd = 0 at PrepareHook entry for the owned world while a
  tracked frame is live, and restores the original value the next time Prepare runs
  untracked, unless the game rewrote it meanwhile (`Bc2ForegroundDepth.h`).
- The first-person projection binding (`0x5d766f`) now receives the full tracked
  eye projection including Z (`FirstPersonEyeProjection`), for both the right
  matrix passed natively and the staged left copy. `FVR_BC2_FP_WORLD_DEPTH=0`
  restores the previous behaviour (eye X/Y/W, native weapon Z, native depth clear).

## Near plane

- `0x5d73d0` FP branch (obj+0xcc): copies the native RenderView (`0x5cb0c0`,
  including near +0x1c and far +0x20), sets FOV (`0x965970`) and +0xc = 1.0
  (`0x965930`). Near/far are changed (`0x9659b0` near, `0x9659e0` far) only
  for pass types 2/3 with obj+0xcd. (DIS)
- So in the main pass the native weapon clip Z already equals the world's. The
  gun natively shares the world near plane. (INF, from the branch structure)
- The world near itself is data-driven. GameRenderSettings.NearPlane is at
  GameRenderer+0x10 -> +0x10 (registry name string `0x14415ac`), and the value
  reaches the RenderView at +0x1c. No static default was found: `0x9659b0` has
  23 call sites, and the one at `0x5d7030` lies in unanalysed code. (DIS for the
  field, INF for the data path)
- **Decision:** share the world near plane. The tracked eye projection is
  built from the native prototype near (`BuildTrackedViews`), and the gun now
  uses that same matrix. A smaller near plane would cost depth precision for the
  whole world and change culling, so it is not changed. The live value is
  reported as `near_plane`/`far_plane` in `derived-projection-correction.json`
  so the headset session can check whether a close-to-face gun clips.

## Report fields (`derived-projection-correction.json`)

`writes`, `capture_failures` (strict), `general_retarget`, `soft_recorded`,
`soft_writes`, `soft_stale`, `soft_overflow`, `general_cameras`,
`general_camera_overflow`, `write_pool_exhausted`, `fp_world_depth`,
`foreground_writes`, `foreground_restores`, `foreground_invalid`,
`settings_slice`, `near_plane`, `far_plane`.
