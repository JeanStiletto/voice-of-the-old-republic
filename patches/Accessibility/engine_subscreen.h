// CGuiInGame::SwitchToSWInGameGui detour — close-on-redrill cleanup.
//
// Layer: engine/ (engine-side hook handler; depends on engine_panels for
// PrevSWInGameGui dispatch and the panels[] state read).
//
// Why this hook exists: SwitchToSWInGameGui is the engine's universal
// "open sub-screen" primitive — the strip's icon onClicks call it, and
// so do the in-game keymap hotkeys (M for Map, I for Inventory, ...) and
// any future engine path that opens a sub-screen. Without our cleanup, an
// already-open sub-screen stays alive in panels[] when the user switches
// to a different one: visible specifically with InGameOptions, whose
// OnQuit handler reorders panels[] without actually popping. Stale
// sub-screen entries then accumulate, dominate fg routing through our
// Fade-skip, and surface as "the menu opens in the background" / "Esc
// fires the quit confirm instead of closing".
//
// Single-site fix: detour SwitchToSWInGameGui itself. If a sub-screen is
// already active (HasActiveSubScreen), call PrevSWInGameGui first, then
// let the original SwitchToSWInGameGui push the requested new sub-screen
// on a clean panels[]. One hook covers strip-Enter + hotkeys + every
// other invocation path.

#pragma once

namespace acc::engine {

// True once the OnSwitchToSWInGameGui detour has fired at least once.
// Diagnostic only — used by no production code, just lets logs distinguish
// "redrill cleanup never armed" from "armed but didn't fire on this event".
extern bool g_switchHookEverFired;

// Per-frame monitor: when modal_stack pops to empty, send
// CSWCMessage::SendPlayerToServerInput_TogglePauseRequest to unpause
// the world. The engine's HideSWInGameGui calls TogglePauseRequest on
// its pause-screen branch (sub-screen close), but the MessageBoxModal
// close path (Alt+F4 quit-confirm, save-overwrite, dialog-skip, …)
// has no equivalent — leaving the world paused after popup-dismiss.
// Walking gates on world ticking, so it stays frozen.
//
// Pause-screen lives in panels[] not modal_stack — its open/close
// cycle keeps modal_stack at 0 throughout, so this trigger doesn't
// fire on it (engine's HideSWInGameGui correctly toggles pause for
// pause-screen). Only modal popups put items in modal_stack.
//
// Edge-triggered on modal_stack non-zero → 0 transition. Steady-
// state does nothing.
//
// Called once per frame from core_tick::Dispatch.
void TickInputClassReassert();

}  // namespace acc::engine

// Detour handler for CGuiInGame::SwitchToSWInGameGui at 0x0062cf2d (5-byte
// cut after the engine has loaded the GUI_id parameter into EBX). Runs
// BEFORE the original function continues — pops any active sub-screen so
// the upcoming push lands on a clean panels[].
//
// Calling convention: __cdecl, args from the hook framework's parameter
// table. `thisPtr` ← ECX (CGuiInGame*); `guiId` ← EBX (int, the requested
// sub-screen GUI_id).
extern "C" void __cdecl OnSwitchToSWInGameGui(void* thisPtr, int guiId);

// Diagnostic detour for CGuiInGame::HideSWInGameGui @ 0x0062cba0. Pause
// flicker investigation: SetSWGuiStatus diagnostic identified that pause
// auto-close goes through HideSWInGameGui (caller EIP 0x0062cbe2 lands
// inside it). Hooking entry here lets us see WHO calls HideSWInGameGui
// — the caller_eip in our handler is the engine address that invoked
// the close. Read-only, passes through.
//
// Cut: 9 bytes covering PUSH ESI (1) + MOV ESI,ECX (2) + MOV EAX,[ESI+0x108]
// (6). All register/memory-relative; relocate cleanly. ECX still holds
// `this` after our handler returns.
//
// Calling convention: __thiscall, ECX = this (CGuiInGame*),
// [esp+4] = param_1 (int — close-mode flag, exact semantics TBD).
extern "C" void __cdecl OnHideSWInGameGui(void* thisPtr, void* p1_addr);

// Diagnostic detour for CGuiInGame::SetSWGuiStatus @ 0x0062aa00. The
// engine flips sw_gui_status via this single setter — 1 (in-world), 2
// (main menu), 3 (sub-screen showing). Pause-flicker investigation:
// the status sometimes goes 3→1 within 1 second of pause opening
// without any visible cause in our log. Hooking at function entry logs
// every transition with the calling instruction's return EIP so we can
// identify the engine path that's auto-closing pause.
//
// Pure read-only diagnostic — does not consume, does not modify any
// arguments. Cut at function entry, 5 bytes `8b 44 24 04 56`
// (MOV EAX,[ESP+4]; PUSH ESI). Stack-relative MOV + register-only PUSH;
// safe to relocate. Params: this (ECX), new_status (esp+4 LEA), param_2
// (esp+8 LEA).
extern "C" void __cdecl OnSetSWGuiStatus(void* thisPtr,
                                          void* p1_addr,
                                          void* p2_addr);
