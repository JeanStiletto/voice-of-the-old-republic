// Loading-phase nag: while the engine is between "intros finished"
// and "main menu input pump live", arrow-key presses don't do anything
// because the engine is recreating Render Window instances and shuffling
// focus through them. Silent multi-second windows in this phase feel
// like a hang to a blind user.
//
// This module spins up a polling thread that derives the bringup phase
// from the game's own windows, and if it detects an arrow/Enter/Space
// keypress during the Loading phase it speaks "Game is still loading"
// once. Silent during MoviesPlaying (audio would clash with intro
// audio) and during Responsive (no nag needed).
//
// Detection is fully derived from the game's window state + the
// input-pump-live signal — no callbacks from diag_focus needed, so the
// two modules stay decoupled. Foreground class name identifies the
// Render Window; movie playback is keyed on the movie window EXISTING,
// because it is ghosted out of the foreground once it stops pumping
// (see IsMoviePlaying below).

#pragma once

namespace acc::bringup_announce {

// Spin up the polling thread. Idempotent. Call once from OnRulesInit
// after Prism is initialised so the announce path is ready.
void Start();

// Called from menus.cpp the moment we detect the user's first arrow-key
// nav on the MainMenu panel (= engine input pump is live). Transitions
// the phase to Responsive and stops the announce nag from firing again.
void NotifyInputPumpLive();

// True once NotifyInputPumpLive has fired — i.e. the engine's input pump
// has provably delivered an event all the way to a panel (2nd MainMenu
// SetActiveControl or first manager-side consume). The cold-start
// DirectInput reacquire retry in core_tick uses this as its stop signal:
// it keeps re-driving the SetActive(0)->(1) edge until input is confirmed
// live, then never runs again. Immune to the mislabeled cursor-position
// channel that floods the input hook — those events don't drive
// SetActiveControl, so they never trip the latch. Cheap atomic read,
// safe from any thread.
bool IsInputPumpLive();

// True iff an engine movie is on screen — i.e. this process owns a visible
// SWMovieWindow. Phase-INDEPENDENT (unlike the internal phase machine, which
// latches to Responsive once gameplay starts and then ignores movie windows),
// so this also detects mid-game cutscene movies (e.g. 03.bik at the Leviathan
// capture), not just the startup intro logos. Cached + rate-limited, safe to
// call from any thread and from a per-frame path.
//
// Presence, not foreground: the Bink player stops pumping its message queue
// during playback, so Windows ghosts the window after ~5 s and GetForegroundWindow
// starts returning a "Ghost"-class window owned by another process. Testing
// the foreground therefore reported "no movie" for exactly the long movies —
// see the implementation note in bringup_announce.cpp.
//
// Speech / input-synthesis paths use it to stay silent and hands-off
// while an engine movie is on screen: KOTOR's movie player aborts its
// play queue if windows/focus churn during playback (cf. the
// Alt+Tab-during-intros queue-restart bug), which surfaces as the game
// closing instead of starting the next queued movie.
bool IsMoviePlaying();

// True while a movie is on screen, and for `windowMs` after the most recent
// one disappears. The tail exists because the engine plays some cutscenes (the
// Peragus docking stunt) as several movie segments, destroying and recreating
// the movie window between them — so IsMoviePlaying() blinks false in the
// sub-second gaps even though the cutscene is still running. A caller that must
// stay silent across the whole cutscene (the menu SetActive rebuild burst)
// keys off this. Calling it also refreshes the movie scan, so the end-timestamp
// it reads is current. Cheap, safe from any thread and per-frame paths.
bool MovieRecentlyPlaying(unsigned int windowMs);

}  // namespace acc::bringup_announce
