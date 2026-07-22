// CGuiInGame::SwitchToSWInGameGui detour + MessageBoxModal close cleanup.
//
// SwitchToSWInGameGui is the engine's universal "open sub-screen" entry.
// Without our cleanup, an already-open sub-screen stays alive in
// panels[] when the user switches to a different one (InGameOptions's
// OnQuit reorders panels[] without popping). Stale entries accumulate
// → "menu opens in background" / "Esc fires quit-confirm".
//
// Single-site fix: if a sub-screen is already active when the hook
// fires, call PrevSWInGameGui first, then let the original push onto
// clean panels[]. Covers strip-Enter, M/I/J hotkeys, every other
// invocation.

#pragma once

namespace acc::engine {

// True once the SwitchToSWInGameGui detour has fired at least once.
// Diagnostic only.
extern bool g_switchHookEverFired;

// Edge-triggered on modal_stack non-zero → 0. MessageBoxModal close
// paths (Alt+F4 quit-confirm, save-overwrite, dialog-skip) skip the
// engine's full pause/audio cleanup, so we dispatch:
//   1. CServerExoApp::SetPauseState(server, 2, 0) — clears menu-pause bit.
//   2. CExoSoundInternal::SetSoundMode(ExoSound, 0) — un-mutes mixer
//      (step 1's server message doesn't reach this).
//
// Pause-screen + sub-screens live in panels[] not modal_stack — their
// open/close keeps modal_stack at 0 throughout, so this trigger
// doesn't fire on them; HideSWInGameGui handles their cleanup natively.
//
// Function name is historical (earlier iteration touched input_class).
void TickInputClassReassert();

// In-world overlay pause hold. Our keyboard-driven in-world menus (examine
// view, action queue, the Shift-number action menus) have no engine panel,
// so the engine doesn't pause the world for them like it does for native
// sub-screens. Call BeginOverlayPause(owner) when such an overlay opens and
// EndOverlayPause(owner) when it closes to get the native "menu freezes the
// world" behaviour. Routes through SetPausedByCombat (the same pause the
// pause key uses).
//
// Owner-tracked, because our overlays STACK: the combat queue opens on top of
// the unified action menu (Shift+H from within it), the help list opens over
// the unified menu, etc. A naive unconditional resume-on-close would unpause
// the world when the INNER overlay closes even though the OUTER one is still
// open and still wants the world frozen (the combat-queue-Esc-unpauses bug).
// Each owner sets its bit on Begin and clears it on End; the world only
// actually resumes when the LAST owner releases. Begin re-asserts the engine
// pause every call (idempotent), so the existing "re-assert on resume" call
// sites stay correct.
enum class OverlayPauseOwner : unsigned {
    UnifiedMenu = 1u << 0,
    CombatQueue = 1u << 1,
    ExamineView = 1u << 2,
    Help        = 1u << 3,
};
void BeginOverlayPause(OverlayPauseOwner owner);
void EndOverlayPause(OverlayPauseOwner owner);

// True when the in-world simulation is currently frozen by any pause source
// (manual pause key, combat auto-pause, or one of our overlay holds). Reads the
// live server pause-bit shadow (g_pauseShadow bit 0x02) that OnSetPauseState
// maintains — synchronous with every pause mutation, including our own. The
// unified action menu uses this OUT OF COMBAT to decide fire-and-close (world
// running) vs. queue-and-stay-open / "stack mode" (world paused).
bool WorldIsPaused();

// Resume the real world pause (CClientExoApp::SetPausedByCombat(0,4,0)) IF the
// world is currently paused; no-op (returns false) when already running. Unlike
// EndOverlayPause this call is NOT self-flagged, so the engine's resume cue
// ("Fortgesetzt") speaks — the unified menu's out-of-combat Esc uses that as its
// close announcement, matching how native sub-screens unpause on Esc. Also
// clears our overlay-owner bookkeeping so the mask can't outlive the resume.
// Returns true if it issued a resume.
bool ResumeWorldIfPaused(const char* reason);

}  // namespace acc::engine

// Detour @0x0062cf2d (5-byte cut after EBX = GUI_id is loaded).
// __cdecl from the framework's param table; thisPtr ← ECX, guiId ← EBX.
extern "C" void __cdecl OnSwitchToSWInGameGui(void* thisPtr, int guiId);

// Diagnostic detour at HideSWInGameGui @0x0062cba0. Logs WHO closes
// sub-screens (caller_eip is the engine address that invoked close).
// 9-byte cut: PUSH ESI (1) + MOV ESI,ECX (2) + MOV EAX,[ESI+0x108] (6).
// All register/memory-relative; relocate cleanly.
extern "C" void __cdecl OnHideSWInGameGui(void* thisPtr, void* p1_addr);

// Diagnostic detour at SetSWGuiStatus @0x0062aa00. Status: 1 in-world,
// 2 main menu, 3 sub-screen. Status sometimes flips 3→1 within 1s of
// pause open with no visible cause; hook logs every transition + return
// EIP to find the auto-close path. 5-byte cut at entry:
// `8b 44 24 04 56` (MOV EAX,[ESP+4]; PUSH ESI) — relocates safely.
extern "C" void __cdecl OnSetSWGuiStatus(void* thisPtr,
                                          void* p1_addr,
                                          void* p2_addr);
