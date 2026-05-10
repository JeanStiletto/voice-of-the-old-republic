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
