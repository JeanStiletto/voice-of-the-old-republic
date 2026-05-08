// KOTOR Accessibility — character sheet sub-screen opener announce.
//
// Step 2A of the menus.cpp refactor (pure-function extraction).
// CSWGuiInGameCharacter has 17+ inline labels (class / level / HP / FP
// / XP / 6 attributes + their pre-formatted modifiers / alignment
// slider). This module reads them and composes a single localised
// speech line so the user gets the full status on first open.
//
// Called from menus.cpp's MonitorPanelContents on the first-sight
// branch for InGameCharacter sub-screens — the kind name ("Charakter-
// blatt" / "Character sheet") lands first via the generic sub-screen
// announce, then this opener fills in the actual values. Subsequent
// content changes (e.g. level-up) are caught by the generic content
// fingerprint diff in MonitorPanelContents and don't need a separate
// re-fire here.

#pragma once

namespace acc::menus::charsheet {

// First-sight opener. Self-gates on IdentifyPanel(panel) ==
// PanelKind::InGameCharacter; safe to call with any panel pointer.
// Skips fields the engine renders as empty rather than speaking bare
// labels (e.g. "Stärke ." would happen if the value field hasn't yet
// rendered when we read it).
void MaybeAnnounce(void* panel);

}  // namespace acc::menus::charsheet
