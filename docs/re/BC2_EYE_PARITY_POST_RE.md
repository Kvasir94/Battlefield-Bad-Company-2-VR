> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 eye-view parity and per-eye post-process (lane R1)

Build 795745 (`BFBC2Game.exe`, SHA-256 `c603f686…0162`), preferred base 0x400000.
All addresses are VAs. **DIS** means read in disassembly or decompile (REA/Ghidra,
or exact file bytes). **INF** means inferred and not directly proven.
Code: `src/games/bc2/Bc2EyeViewParity.*`, `src/games/bc2/Bc2PerEyePostProcess.*`,
and the shared scanner `Bc2PatternScan.h`.

## 1. Where the main view gets its effect state

- **DIS** `0x5de070` (subsystem build) fills `subsystem+0x34` (the main
  WorldRenderView) every frame. It does this through view vtable `0x14776f8`
  slots, plus raw stores. Then it calls `(subsystem+0x2c)->vf+0xc(subsystem+0x30, …)`,
  which is WorldUpdate `0xbb4980` (already hooked as `UpdateHook`).
  So "after the build, before visibility" is the entry of `UpdateHook`, before
  `updateOriginal`.
- **DIS** The vtable offsets that 0x5de070 calls are 0x3c (slot 15, viewport),
  0x14/0x1c (5/7, camera-cache jumps), 0x5c, 0x6c, 0x74, 0x7c, 0x94, 0xa4, 0xb0,
  0xb8 (46, camera +0x30, not mirrored), 0xc0, 0xc8, 0xcc, 0xd4, 0xdc, 0xe4, 0x64
  and 0x54. If DynamicPostProcess exists, it also calls 0x84 (+0x1600 from DPP+0x40)
  and 0x8c (+0x1610 from DPP+0x30).
- **DIS** Raw stores: `+4`, `+8` (dwords), `+0xc` (byte), `+0x10..+0x1c`
  (dwords). The base ctor `0xbab650` zeroes `+0x1540`/`+0x1548`
  (`89 9E 40 15 00 00`, `89 9E 48 15 00 00` at 0xbab758/0xbab7a7).

## 2. Mirrored accessors (all `__thiscall`; setters `ret 4`)

Each body is verified byte-exact in the file and in the live image (`same()`).
The getter and setter pairs are copied main→eye in the order below.

| set/get slot | set VA | get VA | field | kind |
|---|---|---|---|---|
| 21/22 | b864b0 | b864e0 | +0x1530 vec3 (fade) | ptr (get returns &field) |
| 23/24 | b8f420 | b8f460 | +0x1540 object | ref (Release old vt+4, AddRef new vt+0) |
| 25/26 | b86350 | b86360 | +0x1544 dword | value |
| 27/28 | b8f470 | b8f4b0 | +0x1548 object | ref |
| 29/30 | b86390 | b863a0 | +0x154c dword | value |
| 31/32 | b863b0 | b863d0 | +0x1550 float | value (get = FLD/ST0) |
| 33/34 | b863e0 | b86420 | +0x1600 vec4 (DynamicPostProcess) | out pointer |
| 35/36 | b86450 | b86470 | +0x1610 8 bytes (DynamicPostProcess) | out pointer |
| 37/38 | b86370 | b86380 | +0x1560 colour block (0x70) | ptr; set = `add ecx,0x1560; jmp 0x5bea00` |
| 41/42 | b864f0 | b86500 | +0x1648 byte | value |
| 44/43 | b86540 | b86510 | +0x1620 vec4 | out (slot 43 is the getter) |
| 48/47 | b86590 | b86580 | +0x1640 float | value |
| 50/49 | b865e0 | b865b0 | +0x1630 vec3 | out |
| 51/52 | b86610 | b86630 | +0x1644 float | value |
| 53/54 | b86640 | b86680 | +0x1650 vec4 | out |
| 55/56 | b8f4c0 | b866b0 | +0x15d0 block (+0,+4,+0x10..+0x20, bytes +0x24/+0x25) | ptr |
| 57/58 | b866c0 | b866e0 | +0x1660 float (LOD multiplier) | value |

- **DIS** `0x5bea00` copies the colour block: +0,+4,+8 and +0x10..+0x20, then
  calls `0x403df0` (`ret 4`, a 16-float x87 copy) for +0x30. Neither function
  uses SSE, so the copies have no alignment requirement.
- **DIS** The view dtor `0xba48b0` releases +0x1548 and then +0x1540
  (vt+4). The ref setters therefore balance refcounts with no explicit detach
  before the eye view is released.
- **DIS** Draw `0xbb1db0` does not read +0x1600/+0x1610. The ctor never
  initialises them. The eye's copies are zeroed once through slots 33/35 before
  the first mirror.
- Not mirrored: slots 5/7/15/45/46 (camera caches, viewport, camera +0x30).
  Lane R2 and the stereo path own those.

## 3. PostProcess and PostProcessView

- **DIS** `WorldRenderer+0x70` = PostProcess, vtable `0x1447ff8`. This is the
  only data dword equal to the factory VA. Entries:
  - [0] addRef `0x9a1b70`: refcount at +4, returns the new count.
  - [1] release `0x9a1b80`: vt+0x38(1) at zero.
  - [2] factory `0x9ca800`.
  - [3] update `0x9d0e80`.
  - [6] process `0x9b37f0`.
- **DIS** Factory `0x9ca800`: `thiscall(PostProcess*, const NativeStringRange* name,
  int width, int height)`, `ret 0xc`. It does `new(0x1b4)` and then calls ctor
  `0x9c9e40(mem, owner, name, w, h)`. It returns a PPV with refcount 0.
- **DIS** WR ctor site `0xba820b`: `WR+0x7c = WR+0x70->vf8("Shared", WR+0xbc, WR+0xc0)`,
  with Release old / AddRef new.
- **DIS** PPV vtable `0x1447380` (ctor imm at ctor+41):
  - [0] addRef `0x9a1ba0`.
  - [1] release `0x9a1bb0`: vt+0xc(1) at zero.
  - [3] deleting dtor `0x9c7830`, which calls dtor `0x9c1250`.
- **DIS** The PPV ctor stores the owner at +8 (no AddRef), the width/height at
  +0x18/+0x1c, and pushes itself onto the owner's vector +0x1c..+0x24. It sets
  reset byte +0x1ac = 1.
- **DIS** The PPV dtor `0x9c1250` removes itself from `owner+0x1c..+0x20`. **The
  owner must be alive when the PPV dies.**
- **DIS** Update `0x9d0e80` adds dt to `+0x1b0` of **every registered view**.
  WorldUpdate calls it at `0xbb4a88` (`0xbb4980+0x108`).
- **DIS** The draw reads `ECX=[WR+0x7c]` at `0xbb2fd5` (drawView+0x121d) and
  stores it into block[0] at `0xbb3007` (+0x1257). It then calls
  `PostProcess vf+0x18(&block)` at `0xbb3844` (+0x1a94). Process takes
  `EDI=block[0]` as its PPV. No other `WR+0x7c` read was found in the draw.
- **DIS** Process state per PPV:
  - ring index +0x1a4, `(+1)&7` per call.
  - adapted exposure +0x1a8, which lerps by +0x1b0/tau.
  - reset +0x1ac.
  - luminance readback from slot `(idx+1)&7`.

  With one shared PPV, the index advances twice per frame and each eye reads
  the other eye's luminance.
- **INF** PostProcess-level targets (LdrFull, QuarterHdr, blur/float mips) are
  written before they are read within one process call, so they are scratch.

## 4. Implementation

- **Eye parity** (`FVR_BC2_EYE_PARITY`, default on, `=0` disables).
  `ApplyEyeRenderState` runs in `UpdateHook` right after `AdvanceStereo` and
  before `updateOriginal`. It mirrors only when all of these hold:
  - the stereo stage is Armed;
  - world and request match the experiment;
  - both views have the WorldRenderView vtable and the owning request;
  - `request+8 == world`.
- **Per-eye post** (`FVR_BC2_PER_EYE_POST`, default on, `=0` disables).
  - **Create (main thread):** AddRef the PostProcess, then call
    `factory(pp, "Fvr.EyePostProcessView", WR+0xbc, WR+0xc0)` and AddRef the
    result to 1.
  - **Recreate** on a size mismatch (PPV +0x18/+0x1c vs WR +0xbc/+0xc0) or a
    world/PostProcess change.
  - **Draw scope (render thread, DrawHook):** applies only to the eye view of the
    stereo world. It raises a seq_cst `drawing` counter, loads the published PPV,
    swaps `WR+0x7c` for the draw, and restores it only if the value is still ours.
  - **Retire:** clears the published PPV, then frees it only when `drawing==0`
    (a Dekker pair) and otherwise defers to the next update. It releases the PPV
    (whose dtor unregisters it) and then the held PostProcess reference.
  - **Retire triggers:** the stage leaving Armed, and the start of
    `RecoverStereoOwner` (old world still held; no render callback in flight).
- Discovery failure or a modified live byte soft-disables only that feature. The
  stereo probe still runs.
- The report gains `stereo_probe.eye_parity` and `stereo_probe.per_eye_post`
  counters.

## 5. Risks and open points

- **INF** The PPV is created on the main thread while the render thread may be
  in `process` for the main view. The native precedent is the WR ctor. The
  factory also allocates GPU targets through the renderer at `[0x1572a60]`
  vf+100.
- Exposure adapts independently per eye. Different measured luminance per eye
  can produce a small brightness mismatch.
- If the WR dies without `RecoverStereoOwner` or a stage change, the held
  PostProcess reference keeps the owner alive until the next `Update`. A
  deferred release after the hooks are disabled is leaked deliberately.
- Mirroring uses the main view's values. Anything the main view takes from the
  player's eye (fade, DOF focus) is reused verbatim for the eye, which is the
  intended native-parity behaviour.
