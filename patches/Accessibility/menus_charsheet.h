// character sheet stat rows.
//
// CSWGuiInGameCharacter has 17+ inline labels (class / level / HP / FP
// / XP / 6 attributes + their pre-formatted modifiers / alignment
// slider). None of them is IsChainNavigable, so this module surfaces
// them as virtual, text-only chain entries the user arrows through —
// each read as a composed localised phrase ("Stärke 14, +2").
//
// Opening the panel speaks its name only ("Charakterblatt"), from the
// generic sub-screen announce in menus_monitors.cpp. There is
// deliberately NO read-everything-on-open path: an earlier opener that
// composed all 12 rows into one line was removed once the rows became
// arrow-reachable, and the generic content-fingerprint monitor is
// silent on this kind for the same reason (it spoke the value labels
// as bare context-free numbers — "14", "120000"). The fingerprint is
// still TRACKED for this kind, because a content change is what
// rebinds the chain when Tab swaps the displayed party member and the
// Force-points row appears or disappears.

#pragma once

#include <cstddef>

namespace acc::menus::charsheet {

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
