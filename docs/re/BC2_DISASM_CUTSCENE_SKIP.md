> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# BC2 (build 795745, x86) in-game cutscene skip - static findings

> Cloud static disassembly of build 795745 ([`tools/re/bc2re.py`](../../tools/re/bc2re.py)), 2026-10-08.
> In this report **SV** means read in the disassembly; it is the **DIS** level in
> [BC2_WEAPON_RUNTIME.md](BC2_WEAPON_RUNTIME.md). Nothing here has run in the game unless marked LO. Script names refer to throwaway analysis scripts that are not in the repo.

SV = read in disassembly; INF = inferred. Signatures are unique over .text+ctr (checked with bc2re.find_bytes).

## 1. UI command skipCutscene (id 0x9a) -> message (cat 0x12, id 0x1b)
* In-game dispatcher fn 0x7764f0 (`81 EC B4 00 00 00 8B 84 24 B8 00 00 00 53 55 56 57 8B 3D ?? ?? ?? ??`), thiscall, ret 8.
  Index table 0x7772f4, jump table 0x777234. id 0x9a -> case 0x776cba: `mov ecx,esi; call 0x73fcd0` (SV).
* **0x73fcd0 UI_SkipCutscene** sig `E8 ?? ?? ?? ?? D9 EE 8B 48 18 8B 01 8B 50 20 51 D9 1C 24 6A 00 6A 1B 6A 12` (1 hit).
  ECX is NOT used; no stack args; plain `ret`. (SV)
  ```c
  void UI_SkipCutscene(void) {
      MessageManager *mm = *(MessageManager**)(Globals_0x7ff060() + 0x18);  // 0x7ff060 returns singleton 0x15713d8
      mm->vtbl[8](mm, 0x12, 0x1b, 0, 0.0f);   // 0x422a40: new IntMessage(0x30), [m+8]=0x12,[m+0x20]=0x1b,[m+0x24]=0; queue with delay 0
  }
  ```
  MessageManager vtable 0x140a288 (ctor string "MessageManager"); slot2 0x426e70 register listener, slot4 0x426f70 unregister,
  slot8 0x422a40 queue int message, slot9 0x41a740 -> 0x414360 queue (takes the lock at mm+0x10; INF thread-safe enqueue).
  Delivery happens later in the message pump (INF: main/game thread).

## 2. Consumer: ClientCutsceneEntity (size 0x10c) message listener
* Primary vtable 0x14366c8 (getType 0x877ef0 -> typeinfo 0x1bf4d30). Listener sub-object at entity+0xdc, vtable 0x1436650.
* **0x895a90 ClientCutsceneEntity::onMessage** (listener this = entity+0xdc), thiscall, 4 stack args, ret 0x10. Sig
  `8B 44 24 04 83 F8 1F 56 8B F1 75 3D 83 7C 24 0C 2F 75 62 E8 ?? ?? ?? ??` (1 hit). (SV)
  ```c
  if (cat == 0x1f && id == 0x2f) {                 // input path
      in = Input_0x7b8cc0()->vtbl[0x38/4]();
      if (!ent->skipRequested && in && in->vtbl[0x14/4](in, 0x39 /*ConceptMenu*/) && ent->data->IsSkippable) Skip(ent);
  } else if (cat == 0x12 && id == 0x1b) {          // UI skipCutscene path
      if (!ent->skipRequested /*ent+0x100*/ && ent->data /*ent+0xc*/ ->IsSkippable /*+0xcd*/) Skip(ent);
  }
  ```
  Gate sig at 0x895af1: `80 BA CD 00 00 00 00 74 0B 8D 8E 24 FF FF FF E8 ?? ?? ?? ?? 5E C2 10 00` (1 hit); the `74 0B` at 0x895af8 is
  the IsSkippable gate for BOTH paths (SV).
* Listener registered for cats 0x1f and 0x12 only while playing: 0x8afc90 (called from update at 0x8b45c1 on entering state 3);
  unregistered in 0x8a3c70 (finish) and dtor 0x8a3400 (SV). So the message is ignored before playback starts / after it ends.
* **0x886210 ClientCutsceneEntity::requestSkip(this=entity)**, thiscall, no args, ret. Unique tail at 0x88622c:
  `8B 50 10 57 6A 1F FF D2 5F C6 86 00 01 00 00 01 5E C3` (1 hit). Unregisters cat 0x1f, sets `entity+0x100 = 1` (SV).
* **0x8b4460 ClientCutsceneEntity::updateState(float dt)** thiscall ret 4 (via 0x8b9490, the update vfunc; sig
  `83 EC 14 55 56 8B F1 8B 6E 0C 57 8B 7E 44 8D 47 FF 83 F8 07 0F 87 8D 06 00 00`). State at entity+0x44 (1..8).
  While playing: if +0x100 set and a screen-fade (entity+0xe0, fade time>0) exists -> fade out (+0x103, timer +0xfc), else
  immediately: `entity+0x101 = 1; 0x878020(entity, 0, 1)` (site 0x8b488a sig
  `38 9E 00 01 00 00 0F 84 70 02 00 00 6A 01 53 8B CE C6 86 01 01 00 00 01`) (SV).
* 0x878020 (sig `57 8B F9 E8 ?? ?? ?? ?? 8B 10 8B C8 8B 02 FF D0 84 C0 74 27 56 E8 ?? ?? ?? ??`), thiscall(a,b) ret 8:
  builds an entity event (0x610f00(this,a,b)) and sends it through singleton+0x58 via 0x5371e0 - INF: client->server
  "cutscene finished/skipped" event; the ServerCutsceneEntity (vtable 0x14235a8) then ends the cutscene and returns control.
  Exact player-control restore not traced (INF).

## DLL recipes (in order)
A. Call `UI_SkipCutscene()` (0x73fcd0; resolve by sig; any ECX) once the cutscene is playing (state 3; e.g. poll each frame for
   ~a few seconds after level start, it is harmless when no listener is registered). Works only if the SP_02 opening
   CutsceneEntityData.IsSkippable(+0xcd) == 1 (data, not checked statically). Prefer calling on the game/main thread (INF).
B. If not skippable: hook 0x895a90 (or the update 0x8b9490) to capture the entity (listener this - 0xdc) and call
   `requestSkip` 0x886210 directly (bypasses IsSkippable and the UI), on the game thread; nothing else to restore
   (native flag path, same as a user skip). Alternative: temporarily NOP the `74 0B` at 0x895af8 around a 0x73fcd0 call and
   restore it after the message is delivered (patch affects both skip paths while active).
C. Native input path: during a skippable cutscene, ConceptMenu (0x39) on input message (0x1f,0x2f) also skips.

## Not done / gaps
* Server.DisableCutscenes / ClientSettings.DisableMovies readers not searched further (item 4).
* Whether SP_02's intro is one CutsceneEntity or several chained (would need repeated skips) - data, unknown. Headset/game test required.
