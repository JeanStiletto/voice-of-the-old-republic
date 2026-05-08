// KOTOR Accessibility — internal seam between menus.cpp and menus_extract.cpp.
//
// This is NOT public API; it's a private contract between the two TUs that
// were one TU before Step 2B of the refactor. Module names declared here
// live in `acc::menus::detail::` to signal "internal-but-exposed-across-
// TUs."
//
// What the seam covers, and why each thing is here:
//
//   * g_currentPanel — the focused-panel pointer maintained by
//     OnSetActiveControl (in menus.cpp) and read by ExtractAnnounceableText
//     (in menus_extract.cpp) for the slider sibling-label / cycle-flanker
//     gates and the per-kind owner-panel fallback.
//
//   * IsChainNavigable — predicate for "can the user focus this control
//     with arrow keys?" Used by chain code in menus.cpp (RebindChain,
//     AppendChainEntry, FindCloseButton/CancelButton, FindAdjacentArrow)
//     AND by the sibling-label fallback in extract.
//
//   * IsClassSelectionIcon — positional detector for chargen class-icon
//     buttons. Used by extract's perkind-classsel branch AND by menus.cpp
//     chain code (cursor x-offset compensation, RebindChain pitch capture,
//     OnSetActiveControl prefill, AnnounceControl cache-aware skip).
//
//   * ClassLabelCacheLookup / Store — lazily-populated (icon → class_text)
//     cache for the chargen class-selection panel. The cache state lives
//     in menus.cpp (file-static); the extract path reads (lookup), the
//     OnSetActiveControl prefill writes (store). Lookup-then-store happens
//     inside extract step 9c too, so both directions cross the seam.
//
//   * kEquipBtn* / kEquipLb* — control IDs from equip.gui. Used by extract's
//     perkind-InGameEquip branch (slot-button label resolution) AND by the
//     OnHandleInputEvent equip-slot detector / equip-picker handler in
//     menus.cpp.
//
// `kSaveLoad*` and the in-game-menu-icon name table stay private to their
// owning TU because only one side uses them.

#pragma once

#include <cstddef>

namespace acc::menus::detail {

bool IsChainNavigable(void* control);
bool IsClassSelectionIcon(void* panel, void* control);
const char* ClassLabelCacheLookup(void* panel, void* icon);
void ClassLabelCacheStore(void* panel, void* icon, const char* text);

// Screen-center of a control's extent, false on degenerate extents.
// Used by FindSiblingLabel / IsCycleFlankerArrow in menus_extract.cpp
// and by FindAdjacentArrow / AppendChain* in menus.cpp.
bool GetControlCenter(void* control, int& outCx, int& outCy);

}  // namespace acc::menus::detail

// File-scope global maintained by OnSetActiveControl in menus.cpp.
// Read-only from menus_extract.cpp; never assign here.
extern void* g_currentPanel;

// Equipment screen control IDs from equip.gui (extracted via xoreos-tools
// gff2xml). The 9 BTN_INV_* slot buttons + the picker listbox + the two
// action buttons. Both menus.cpp and menus_extract.cpp need these — the
// former for input handling (slot detection, picker zone, equip dispatch),
// the latter for per-kind label resolution (slot button → "Kopf" /
// "Implantat" / etc.).
constexpr int kEquipBtnHeadId    =  7;  // BTN_INV_HEAD     (TLK 31375)
constexpr int kEquipBtnImplantId =  9;  // BTN_INV_IMPLANT  (literal — no TLK)
constexpr int kEquipBtnBodyId    = 11;  // BTN_INV_BODY     (TLK 31380)
constexpr int kEquipBtnArmLId    = 13;  // BTN_INV_ARM_L    (TLK 31376)
constexpr int kEquipBtnWeapLId   = 15;  // BTN_INV_WEAP_L   (TLK 31378)
constexpr int kEquipBtnBeltId    = 17;  // BTN_INV_BELT     (TLK 31382)
constexpr int kEquipBtnWeapRId   = 19;  // BTN_INV_WEAP_R   (TLK 31379)
constexpr int kEquipBtnArmRId    = 21;  // BTN_INV_ARM_R    (TLK 31377)
constexpr int kEquipBtnHandsId   = 23;  // BTN_INV_HANDS    (TLK 31383)
constexpr int kEquipLbItemsId    =  5;  // LB_ITEMS
constexpr int kEquipBtnBackId    = 36;  // BTN_BACK         (TLK 1582 = Schliess.)
constexpr int kEquipBtnEquipId   = 37;  // BTN_EQUIP        (TLK 1580 = OK)
