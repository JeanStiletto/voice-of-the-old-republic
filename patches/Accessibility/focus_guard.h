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

namespace acc::focus_guard {

// Log a single line describing the calling thread's COM apartment.
// `tag` is appended so the same probe can fire at multiple sites
// without ambiguity (e.g. "post_prism_init", "first_menu_tick").
void LogComApartment(const char* tag);

// Start the focus-probe polling thread. Idempotent. First call spawns
// the worker; subsequent calls return immediately.
void StartFocusProbe();

// Arm the cold-start foreground guard. For a bounded window after this
// call, the focus-probe poll thread watches for a NON-game window grabbing
// the foreground (the Xbox Game Bar launch popup and similar overlays do
// this), and pulls the game's render window back to the foreground when it
// happens. Reclaiming foreground makes the engine's own activation handler
// (FUN_005f6b10 -> CExoInput::SetActive(1)) re-Acquire the DirectInput
// keyboard, which is otherwise dead until the user alt-tabs.
//
// Deliberately bounded in time and reclaim count: once the window elapses
// (or the cap is hit) the guard disarms and never fights for focus again,
// so a user who genuinely opens Game Bar later is left alone. Requires the
// poll thread (StartFocusProbe) to be running. Call at MainMenu first-sight.
void ArmStartupForegroundGuard();

// True when the foreground window belongs to our process — i.e. the game owns
// the foreground. Used to gate the cold-start input acquire: input must mirror
// foreground, so we only acquire the keyboard at main-menu first-sight if we
// actually own the foreground (acquiring while an overlay/other window is in
// front would bleed keys into the background game). Cheap; no allocation.
bool GameOwnsForeground();

// Drain a pending "input blocked by Steam Big Picture" warning and speak it
// (no-op if none queued). The focus-probe poll thread flags this when the user
// presses a key while Steam Big Picture Mode holds the foreground — in
// windowed mode those keystrokes go to Big Picture, not the game, so menus
// look dead. Edge- and cooldown-throttled by the poll thread. Call once per
// tick on the engine's main thread (speech path is COM-safe there).
void DrainInputBlockedWarning();

// Fix + diagnostic for "stuck on the loading screen" (KOTOR 2 tester,
// patch-20261124-174110.log: Ebon Hawk scene -> movie -> 907MAL, loading
// screen never closed, repeatable; an Alt+Tab away and back un-sticks it).
// Both games keep their own "render window active" flag, cleared by
// DeactivateRenderWindow (which also pauses and turns the engine's input off)
// and set by ActivateRenderWindow. KOTOR 2's post-load step hides the loading
// screen only while that flag is set, and its movie-end handler re-activates
// the window only in full screen — so a stray deactivate at a movie's end in
// windowed mode leaves the loading screen up for good.
//
// The fix: when the flag stays 0 for 1.5 s while the game's render window
// holds the foreground and no movie is on screen (or ended within 2 s), call
// the engine's own ActivateRenderWindow — what Alt+Tab back does. Cooldown 5 s.
// The diagnostic: every flip of the flag is logged (with who owns the
// foreground), and the relevant state is dumped when a modal panel is still up
// 5 s after an area loaded. Call once per tick on the main thread.
void TickEngineActive();

}  // namespace acc::focus_guard
