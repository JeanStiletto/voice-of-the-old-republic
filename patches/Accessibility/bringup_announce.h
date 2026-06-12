// Loading-phase nag: while the engine is between "intros finished"
// and "main menu input pump live", arrow-key presses don't do anything
// because the engine is recreating Render Window instances and shuffling
// focus through them. Silent multi-second windows in this phase feel
// like a hang to a blind user.
//
// This module spins up a polling thread that watches GetForegroundWindow
// to derive the bringup phase, and if it detects an arrow/Enter/Space
// keypress during the Loading phase it speaks "Game is still loading"
// once. Silent during MoviesPlaying (audio would clash with intro
// audio) and during Responsive (no nag needed).
//
// Detection is fully derived from foreground-window class name + the
// input-pump-live signal — no callbacks from diag_focus needed, so the
// two modules stay decoupled.

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

// True iff a SWMovieWindow owned by this process is currently the
// foreground window. Phase-INDEPENDENT (unlike the internal phase
// machine, which latches to Responsive once gameplay starts and then
// ignores movie windows) — so this also detects mid-game cutscene
// movies (e.g. 03.bik at the Leviathan capture), not just the startup
// intro logos. Cheap synchronous GetForegroundWindow + class compare,
// safe to call from any thread.
//
// Speech / input-synthesis paths use it to stay silent and hands-off
// while an engine movie is on screen: KOTOR's movie player aborts its
// play queue if windows/focus churn during playback (cf. the
// Alt+Tab-during-intros queue-restart bug), which surfaces as the game
// closing instead of starting the next queued movie.
bool IsMovieWindowForeground();

}  // namespace acc::bringup_announce
