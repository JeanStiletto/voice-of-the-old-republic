// In-game "Fähigkeiten" screen (CSWGuiInGameAbilities, abilities.gui).
//
// A view-only character screen shaped like the settings menu: three tab
// buttons (Powers / Skills / Talents) switch which list the single
// LB_ABILITY listbox shows, and selecting a row repaints a detail area
// (name + rank/bonus/total labels + the LB_DESC description box).
//
// It is NOT the chargen/level-up button-grid shape — it rides the shared
// ListBoxPanelSpec machinery in menus_listbox.cpp (Up/Down browse LB_ABILITY)
// and the description-peek registry in peek_description.cpp (Shift+Up/Down
// pages LB_DESC). The detail labels and LB_DESC are repainted by calling the
// engine's CSWGuiInGameAbilities::OnAbilitySelectionChanged after we drive the
// listbox cursor (DriveListBoxSelection bypasses the engine's own
// onSelectionChanged), mirroring the chargen-skills "drive cursor → call
// engine handler → read the repainted detail" pattern.

#pragma once

#include "menus_internal.h"  // acc::menus::detail::ListBoxNavResult

namespace acc::menus::abilities {

// True iff `panel` is the in-game Fähigkeiten screen.
bool IsAbilitiesPanel(void* panel);

// Repaint the detail area (name/rank/bonus/total labels + LB_DESC) for the
// listbox's current selection. SEH-guarded. Used as the peek-refresh hook so
// Shift+Up/Down reads a description that matches the focused entry.
void RefreshDetail(void* panel);

// Dedicated input handler for the Fähigkeiten screen — a two-level submenu so
// the engine's mouse-only / crash-prone paths never see the keys:
//   * Tab level (where you land): Up/Down move between the available tabs
//     (Skills / Powers-if-Jedi / Talente), clamped; Enter drills into the tab's
//     list; Esc falls through to close the screen.
//   * List level (after Enter): Up/Down browse entries, clamped (no wrap) —
//     Skills are driven by us via OnEnterSkill (name + rank/bonus/total),
//     Feats/Powers forward to the engine's chart nav (name + fresh description,
//     no stats); Esc returns to the tab level.
// Returns true and sets outRv when it consumed the event; mirrors the
// TryHandleInput contract.
bool HandleInput(int n, void* thisPtr, void* activePanel,
                 int param_1, int param_2, int& outRv);

}  // namespace acc::menus::abilities
