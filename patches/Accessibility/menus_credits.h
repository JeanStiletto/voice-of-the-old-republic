// KOTOR Accessibility — virtual credits row for Inventory + Store.
//
// Sighted players see "Credits: 1247" in the top-right of CSWGuiInGameInventory
// and CSWGuiStore (both .gui files have an LBL_CREDITS heading + LBL_CREDITS_
// VALUE label written by the engine at panel-open time). Labels aren't
// IsChainNavigable, so the chain walker would skip them — we surface them as
// virtual text-only chain entries the same way menus_charsheet does for the
// stat block.
//
// Read path: the inline CSWGuiLabel.gui_string holds the engine's actually-
// rendered text. For Store, the same value is also cached on the panel struct
// at +0x2270 (kStorePlayerGoldOffset, written by PopulateStore via
// CSWSCreature::GetGold) and consumed by ReadStorePlayerGold — but for the
// chain row we just read the rendered label, which works for both kinds with
// no per-panel branching.
//
// Wired by:
//   * menus_chain.cpp — RebindChain: ForEachCreditsRowAnchor registers a
//     text-only chain entry on credits_value_label.
//   * menus_extract.cpp — FromControl section 0: IsCreditsRowAnchor +
//     ExtractCreditsRow override the bare label text with the localised
//     "Credits: 1247" phrase.

#pragma once

#include <cstddef>

namespace acc::menus::credits {

// True iff `labelControl` is the credits_value_label inline-struct member of
// `panel`, for a panel kind where we surface the credits row (Inventory /
// Store). Cheap address comparison; no SEH needed (caller's panel pointer is
// already validated upstream).
bool IsCreditsRowAnchor(void* panel, void* labelControl);

// Iterate the credits-row anchors registered for `panel`. For supported panel
// kinds, invokes the callback exactly once with (labelControl, sortCy).
// No-op on other panel kinds. Visiting stops if the callback returns false.
// `sortCy` is the synthetic y coordinate the chain should use when inserting
// the virtual entry — fixed at 1 so credits sorts above every real button
// regardless of where the engine renders the on-screen label.
void ForEachCreditsRowAnchor(void* panel,
                             bool (*callback)(void* labelControl, int sortCy,
                                              void* userData),
                             void* userData);

// Read the credits_value_label rendered text and format the localised line
// ("Credits: 1247"). Returns true on success with `outBuf` filled. Returns
// false when `labelControl` is not a recognised credits anchor for `panel`,
// OR when the engine has not yet populated the value (label gui_string empty
// or still showing the .gui-load placeholder).
bool ExtractCreditsRow(void* panel, void* labelControl,
                       char* outBuf, size_t bufSize);

}  // namespace acc::menus::credits
