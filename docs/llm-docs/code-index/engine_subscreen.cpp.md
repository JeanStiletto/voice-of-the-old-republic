# engine_subscreen.cpp (338 lines)

Implementation of engine_subscreen.h. No leading comment block.

## Declarations (in source order)

- L13 — `namespace acc::engine`
- L15 — `bool g_switchHookEverFired = false`
- L17 — `namespace { ... }` (anonymous; PFN typedefs + local address constants)
  note: kAddrSetPauseState, kAddrSetSoundMode, kAddrExoSoundPtr, kAddrAppManagerPtrLocal; CServerExoApp::SetPauseState @0x004ae9a0 is idempotent (short-circuits if bit already at requested value); prefers SetPauseState over TogglePauseState because Toggle XOR-alternates state
- L62 — `namespace { DispatchUnpauseCleanup }` (anonymous)
- L69 — `void DispatchUnpauseCleanup(const char* trigger)`
  note: calls SetPauseState(server,2,0) then SetSoundMode(ExoSound,0); both SEH-guarded; logs trigger label for diagnosis; idempotent on already-clean state
- L129 — `void TickInputClassReassert()`
  note: two edges: (1) modal_stack > 0 → 0 when no sub-screen still in panels[] — popup closed, unpause; (2) hasSubScreen true → false — last sub-screen left panels[]; suppresses modal edge when a sub-screen is still alive (popup-on-top-of-panel pattern)
- L207 — `extern "C" void __cdecl OnSwitchToSWInGameGui(void* thisPtr, int guiId)`
  note: first-fire diagnostic; only acts when HasActiveSubScreen() is true (warm path); calls CallPrevSWInGameGui to pop prior sub-screen; cold path (first sub-screen open) returns immediately without intervention
- L259 — `extern "C" void __cdecl OnSetSWGuiStatus(void* thisPtr, void* p1_addr, void* p2_addr)`
  note: logs new_status, param_2, caller_eip; on new_status=4 calls InvalidateChain + ClearPendingAnnounce; crash dumps 20996/2288 identified stale Hinzuf. button pointer surviving into the next tick's DrainPendingAnnounce
- L319 — `extern "C" void __cdecl OnHideSWInGameGui(void* thisPtr, void* p1_addr)`
  note: diagnostic only; logs param_1 and caller_eip; identifies which engine path auto-closes pause
