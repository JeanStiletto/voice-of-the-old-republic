// First-sight / focus-capture seam, lifted out of menus.cpp by the Phase-1
// structure pass (refactoring candidate 1).
//
// Everything here is driven by exactly one caller: OnSetActiveControl in
// menus.cpp, which fires on every real focus change. The functions split
// that hook's work into the four things it does on a focus event:
//
//   * PrefillClassIconCacheOnTransition — pump the chargen class-label
//     cache with the OUTGOING icon before the engine overwrites it.
//   * UpdateFocusedPanelState — publish the new panel as g_currentPanel.
//   * WalkAndCaptureOnFirstSight — one-shot per-panel child dump +
//     cycle-category capture.
//   * SpeakPanelTitleOnFirstSight — the entry banner ("<panel title>").
//   * AnnounceNewFocusedControl — queue the focused control into the
//     pending-announce slot for the next tick's drain.
//
// AnnouncePanelTitle stays file-static in menus_focus.cpp: its only caller
// is SpeakPanelTitleOnFirstSight, which moved with it.
//
// Same seam convention as menus_chain.h / menus_monitors.h — menus.cpp
// brings these names back into unqualified scope with using-declarations
// so the call sites in OnSetActiveControl read as they did before.

#pragma once

namespace acc::menus::focus {

void PrefillClassIconCacheOnTransition(void* panel, void* newControl);
void UpdateFocusedPanelState(void* panel);
void WalkAndCaptureOnFirstSight(void* panel);
void SpeakPanelTitleOnFirstSight(void* panel);
void AnnounceNewFocusedControl(int n, void* panel, void* newControl);

}  // namespace acc::menus::focus
