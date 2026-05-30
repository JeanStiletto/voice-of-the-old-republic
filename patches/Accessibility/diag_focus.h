// Diagnostics for the "menu loaded but unresponsive" / "intro movie plays
// twice" / "alt-tab needed to wake the engine" class of reports.
//
// LogComApartment reports the COM apartment model the calling thread is
// committed to. Called once after EnsurePrismInitialized so we can see
// whether the SAPI backend inside prism.dll has CoInitialize'd the
// engine's main thread into MTA (which can conflict with Bink / other
// engine COM paths that expect STA, producing exactly the "fine until
// the first focus loss, then dead" symptom).
//
// StartFocusProbe spins up a background polling thread that periodically
// re-enumerates the process's top-level windows and subclasses every
// game window it hasn't seen before. Subclassed WindowProc logs every
// WM_ACTIVATE / WM_ACTIVATEAPP / WM_SETFOCUS / WM_KILLFOCUS with the
// source HWND + class name tag. Polls every 100ms so transient windows
// (SWMovieWindow recreated between intros) get caught.
//
// Idempotent — repeat calls are no-ops. Safe to call from OnRulesInit
// (early, before windows exist) and from MainMenu first-sight (late,
// belt-and-braces) without worrying about double-subclassing.

#pragma once

namespace acc::diag::focus {

// Log a single line describing the calling thread's COM apartment.
// `tag` is appended so the same probe can fire at multiple sites
// without ambiguity (e.g. "post_prism_init", "first_menu_tick").
void LogComApartment(const char* tag);

// Start the focus-probe polling thread. Idempotent. First call spawns
// the worker; subsequent calls return immediately.
void StartFocusProbe();

}  // namespace acc::diag::focus
