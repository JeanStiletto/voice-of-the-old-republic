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

}  // namespace acc::bringup_announce
