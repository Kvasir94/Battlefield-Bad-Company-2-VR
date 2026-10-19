> **Historical RE record.** Preserve the signatures, layouts and evidence below; implementation status and task restrictions are checkpoint-specific.
> Start with [current status](../STATUS.md) and [development rules](../../AGENTS.md).

# Test-range firing hook handoff

The existing arsenal keeps its MinHook detour on firing Update `0x6e9000`
after initial gives. The VR reload runtime verifies that entry against the
executable before installing its own update hook. These two owners conflict;
changing XR runtime cannot resolve the modified entry.

The fixture now exports a versioned interface to the native mod:

- `bool __cdecl FvrTestRangePrepareNative(uint32_t abi)` accepts ABI 1 and
  disables only the fixture's firing Update hook after the initial plan completes.
- `void __cdecl FvrTestRangeFiringPost(uint32_t firing)` receives the native
  reload runtime's post-original boundary. Existing `WatchSelection` and
  `ArsenalStep` still require the local server chain and the currently selected
  firing copy before any selection or give.

The native mod resolves both exports from the already loaded fixture DLL.
It then runs its unchanged native live-code checks against restored bytes.
No trampoline from an unknown detour is accepted. A legacy fixture with neither
export retains its original behavior and remains subject to those checks.

Evidence reused: `tools/testrange/BC2TestRange.cpp` proves the firing Update
prologue and thiscall signature, validates the local `ReadChain`, and gives
only into an unselected primary from that callback. `Bc2ReloadFlowRuntime.cpp`
already owns this Update entry and its post-original policy accounting.
`docs/re/BC2_DISASM_GIVE_PATH.md` records the give/selection native paths.

This checkpoint changes no controller or IPC ABI. Source diff review only was
performed by the implementation worker; compilation and live Arsenal bootstrap
belong to the sole build/session owner. Build the matching native DLL and fixture
DLL together. Headset acceptance and successful runtime handoff remain pending.
