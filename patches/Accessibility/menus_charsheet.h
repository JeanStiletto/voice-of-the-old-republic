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

#include <cstddef>

namespace acc::menus::charsheet {

// First-sight opener. Self-gates on IdentifyPanel(panel) ==
// PanelKind::InGameCharacter; safe to call with any panel pointer.
// Skips fields the engine renders as empty rather than speaking bare
// labels (e.g. "Stärke ." would happen if the value field hasn't yet
// rendered when we read it).
void MaybeAnnounce(void* panel);

// Per-row formatted speech for one Charakterblatt value label.
// `labelControl` should be a pointer to one of the inline CSWGuiLabel
// fields inside `panel` (e.g. lbl_class at panel+0x2e4, lbl_str at
// panel+0x1d24, …). On match, fills `outBuf` with the localised
// composed phrase ("Stärke 14, +2", "Klasse: Soldat") and returns
// true. Returns false if `labelControl` is not a recognised stat-row
// anchor for InGameCharacter — caller falls through to the standard
// extract ladder.
//
// Drives both ends of the virtual stat chain:
//   * RebindChain (menus_chain.cpp) inserts text-only chain entries
//     anchored on the value labels at their real y coordinates.
//   * FromControl (menus_extract.cpp) routes through this helper
//     before the standard label-text path so the user hears the
//     composed phrase rather than the bare "14" / "Soldat".
//
// Single-shot read; safe to call per chain rebind + per focus tick.
// Re-reads the live label text each invocation so cycling characters
// or the engine rewriting values on level-up reflects immediately.
bool ExtractStatRow(void* panel, void* labelControl,
                    char* outBuf, size_t bufSize);

// True iff `labelControl` is a known stat-row anchor in `panel`.
// Used by RebindChain to know which labels to add as virtual chain
// entries without trying the read first (saves the per-rebind
// label-text reads on non-anchor labels).
bool IsStatRowAnchor(void* panel, void* labelControl);

// Iterate all stat-row anchor pointers for `panel`. The callback is
// invoked once per anchor with the live CSWGuiLabel pointer + the
// synthetic sort-cy that the chain should use to position the virtual
// entry. Visiting stops if the callback returns false. Anchors are
// emitted in spec-table order (Klasse → Stufe → Erfahrung → HP → FP
// → Str → Dex → Con → Int → Wis → Cha); RebindChain uses sortCy as
// the chain entry's cy so the y-sort produces this logical reading
// order regardless of where the engine actually renders each label.
void ForEachStatRowAnchor(void* panel,
                          bool (*callback)(void* labelControl, int sortCy,
                                           void* userData),
                          void* userData);

}  // namespace acc::menus::charsheet
