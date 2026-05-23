// KOTOR Accessibility — virtual stat-row anchors for the Equip panel.
//
// Sighted players see four computed stat values rendered at the bottom of
// the equipment screen (CSWGuiInGameEquip): Vitality (HP), Defense (AC),
// Attack to-hit, and Damage range. Each is drawn into an inline CSWGuiLabel
// inside the panel struct. The labels aren't IsChainNavigable, so the
// chain walker would skip them — we surface them as virtual text-only
// chain entries the same way menus_credits does for the credits readout
// and menus_charsheet does for the Charakterblatt attribute block.
//
// Ordering: stats anchor at sortCy values higher than every real button
// on the panel so they read AFTER the 9 slot buttons and the Back /
// Change-character buttons.
//
// Read path: inline CSWGuiLabel.gui_string holds the engine's actually-
// rendered text. Attack splits into single-weapon (LBL_TOHIT) vs dual-
// wield (LBL_TOHITL + LBL_TOHITR) at render time; we anchor on
// tohit_label and choose the format string by whether the
// left/right tohit labels carry text.
//
// Wired by:
//   * menus_chain.cpp — RebindChain: ForEachEquipStatRowAnchor registers
//     a text-only chain entry per stat value.
//   * menus_extract.cpp — FromControl section 0: IsEquipStatRowAnchor +
//     ExtractEquipStatRow override the bare label text with the
//     localised composed phrase.

#pragma once

#include <cstddef>

namespace acc::menus::equipstats {

// True iff `labelControl` is one of the inline stat-value labels for an
// InGameEquip `panel`. Cheap address comparison; no SEH needed (caller's
// panel pointer is already validated upstream).
bool IsEquipStatRowAnchor(void* panel, void* labelControl);

// Iterate the equip-stat-row anchors for `panel`. For InGameEquip,
// invokes the callback once per anchor with (labelControl, sortCy).
// No-op on other panel kinds. Visiting stops if the callback returns
// false. `sortCy` is the synthetic y coordinate the chain uses when
// inserting the virtual entry — picked above every real button's cy so
// the stats land at the END of the navigable chain.
void ForEachEquipStatRowAnchor(void* panel,
                               bool (*callback)(void* labelControl, int sortCy,
                                                void* userData),
                               void* userData);

// Read the stat label's rendered text and format the localised line.
// Returns true on success with `outBuf` filled. Returns false when
// `labelControl` is not a recognised anchor for `panel`, OR when the
// engine has not yet populated the value (label gui_string empty or
// still showing the .gui-load placeholder).
bool ExtractEquipStatRow(void* panel, void* labelControl,
                         char* outBuf, size_t bufSize);

}  // namespace acc::menus::equipstats
