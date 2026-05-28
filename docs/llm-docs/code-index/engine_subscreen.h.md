# engine_subscreen.h (55 lines)

CGuiInGame::SwitchToSWInGameGui detour and MessageBoxModal close cleanup. Documents the stale-panel accumulation bug (InGameOptions::OnQuit reorders panels[] without popping), the single-site fix via PrevSWInGameGui on redrill, and the two-edge unpause cleanup (modal_stack pop + last sub-screen left panels[]).

## Declarations (in source order)

- L16 — `namespace acc::engine`
- L20 — `extern bool g_switchHookEverFired`
  note: diagnostic; true once the SwitchToSWInGameGui detour has fired at least once
- L34 — `void TickInputClassReassert()`
  note: edge-triggered on modal_stack non-zero → 0 and on has-active-sub-screen true → false; dispatches SetPauseState(server,2,0) + SetSoundMode(exoSound,0) to clean up pause bit and audio mixer that MessageBoxModal close paths skip; historical name (earlier iteration touched input_class)
- L40 — `extern "C" void __cdecl OnSwitchToSWInGameGui(void* thisPtr, int guiId)`
  note: detour at SwitchToSWInGameGui+5 (after EBX=GUI_id loaded); calls PrevSWInGameGui if a sub-screen is already active, then lets the original push the new one onto clean panels[]
- L46 — `extern "C" void __cdecl OnHideSWInGameGui(void* thisPtr, void* p1_addr)`
  note: diagnostic detour at HideSWInGameGui; logs which engine path invokes close via caller_eip
- L53 — `extern "C" void __cdecl OnSetSWGuiStatus(void* thisPtr, void* p1_addr, void* p2_addr)`
  note: diagnostic detour at SetSWGuiStatus; logs every status transition + caller_eip; on status=4 calls InvalidateChain + ClearPendingAnnounce to prevent stale-pointer faults in the next tick's DrainPendingAnnounce
