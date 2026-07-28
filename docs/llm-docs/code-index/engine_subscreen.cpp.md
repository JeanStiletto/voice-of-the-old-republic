# engine_subscreen.cpp (573 lines)

Implements the CGuiInGame::SwitchToSWInGameGui detour (stale sub-screen
cleanup on redrill) plus the mod's pause/unpause machinery: server pause-bit
toggling via CServerExoApp::SetPauseState, audio-mixer resync via
SetSoundMode, and the "real" world-freeze via CClientExoApp::SetPausedByCombat
(the same path the pause key/combat auto-pause use). Owner-tracked overlay
pause (BeginOverlayPause/EndOverlayPause) lets stacking keyboard-only overlays
(examine view, action queue, help, level-up) share one freeze without an
inner-close prematurely resuming the world. Talks to engine_manager
(GuiManagerPtr/modal-stack), engine_panels (HasActiveSubScreen), menus/
menus_chain (InvalidateChain — stale-pointer teardown guard), and prism
(pause/resume speech). Diagnostic hooks OnSetSWGuiStatus/OnHideSWInGameGui log
callers to find engine auto-close paths; OnSetPauseState maintains a live
pause-bit shadow (g_pauseShadow) since the Ghidra-labelled offset reads 0.

## Declarations (in source order)

- L16 — `namespace acc::engine`
- L18 — `bool g_switchHookEverFired`
- L38 — `kAddrSetPauseState` — CServerExoApp::SetPauseState @0x004ae9a0
  note: first arg is a bit MASK not an index; bit 0x02 = manual/menu pause source.
- L47-48 — `kAddrAppManagerPtrLocal`, `kAppManagerServerOff`
- L56 — `kAddrSetSoundMode` — CExoSoundInternal::SetSoundMode @0x005d5e80
- L61 — `kAddrExoSoundPtr`
- L68 — `kPauseBitManualOrMenu = 0x02`
- L76 — `unsigned char g_pauseShadow` — live-maintained pause-bit shadow
  note: Ghidra's pause_state_ offset reads 0 even when paused; this hook-accumulated shadow IS the live state by construction.
- L82 — `bool g_inOwnPauseCall` — suppresses self-triggered OnSetPauseState speech
- L93 — `void DispatchUnpauseCleanup(const char* trigger)` — idempotent SetPauseState(2,0) + SetSoundMode(0)
- L171 — `kAddrSetPausedByCombat` — CClientExoApp::SetPausedByCombat @0x005edc20
  note: the REAL world-freeze the pause key uses; SetPauseState bit 2 alone does not stop simulation (verified live 2026-06-07).
- L176 — `void DispatchOverlayPause(const char* trigger, int paused)`
- L223 — `unsigned g_overlayPauseOwners` — bitmask of held overlay-pause owners
- L226 — `void BeginOverlayPause(OverlayPauseOwner owner)`
- L231 — `void EndOverlayPause(OverlayPauseOwner owner)` — only resumes world on empty mask
- L243 — `bool WorldIsPaused()`
- L247 — `bool ResumeWorldIfPaused(const char* reason)` — un-flagged so engine's own resume cue speaks
- L284 — `void TickInputClassReassert()` — modal-stack and sub-screen-empty edges both trigger unpause cleanup
- L362 — `extern "C" void OnSwitchToSWInGameGui(void*, int guiId)` — pops stale sub-screen on redrill
- L414 — `extern "C" void OnSetSWGuiStatus(void*, void*, void*)` — diagnostic + status==4 chain/pending-announce invalidation
  note: guards against stale control pointers from crash dumps 20996/2288 (2026-05-22).
- L474 — `extern "C" void OnHideSWInGameGui(void*, void*)` — diagnostic, logs caller EIP
- L506 — `extern "C" void OnSetPauseState(void*, void*, void*)` — maintains g_pauseShadow, speaks Paused/Resumed when no UI is blocking
