// internal seam between menus.cpp and menus_extract.cpp.
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
#include <cstdint>

namespace acc::menus::detail {

bool IsChainNavigable(void* control);
bool IsClassSelectionIcon(void* panel, void* control);
const char* ClassLabelCacheLookup(void* panel, void* icon);
void ClassLabelCacheStore(void* panel, void* icon, const char* text);

// Screen-center of a control's extent, false on degenerate extents.
// Used by FindSiblingLabel / IsCycleFlankerArrow in menus_extract.cpp
// and by FindAdjacentArrow / AppendChain* in menus.cpp.
bool GetControlCenter(void* control, int& outCx, int& outCy);

// Locate a child control on `panel` by its +0x50 ID field. Stable across
// localizations and panel.controls reordering.
// Defined in menus.cpp; used there + in menus_listbox.cpp (Step 4 spec
// callbacks for Container/SaveLoad/EquipPicker).
void* FindControlById(void* panel, int id);

// Find the first CSWGuiListBox among panel.controls. Container's input
// handler + the dialog/container monitors use it; the spec dispatcher in
// menus_listbox.cpp's Container entry calls it on every Up/Down step.
void* FindListBoxChild(void* panel);

// Detect the CSWGuiSaveLoad panel by structural signature (saveload.gui
// IDs are baked into the resource at build time, language-independent).
// Used by the SaveLoad spec entry's `matches` callback.
bool IsSaveLoadPanel(void* panel);

// Read a CExoString-style field on a control by byte offset. Used by the
// SaveLoad announce callback to pull planet (lastmodule) + area (areaname)
// from each save-row entry. Returns nullptr on empty field.
const char* ReadSaveLoadEntryString(void* entry, size_t fieldOffset);

// Result of a single arrow-key step on a listbox cursor. Filled by
// DriveListBoxSelection; caller reads `row` to announce, `newSel`/`rowCount`
// for index reporting, and `oldSel == newSel` to detect a boundary clamp
// (per-tick monitors that watch selection_index changes won't fire on a
// no-op step, so callers using a monitor announce inline only on clamp).
struct ListBoxNavResult {
    short oldSel;     // selection_index before the step
    short newSel;     // selection_index after the step
    int   rowCount;   // listbox.controls.size
    void* row;        // row pointer at newSel (nullptr iff rowCount == 0)
};

// Direction of cursor motion driven by DriveListBoxSelection.
//   * StepUp / StepDown: ±1 with boundary clamp (Nav-Up / Nav-Down).
//   * JumpFirst / JumpLast: absolute jump to minSel / rowCount-1
//     (Home / End). Caller still gets a `ListBoxNavResult` populated the
//     same way as a step; oldSel == newSel still indicates "already at
//     the boundary" so the inline announce-on-clamp branch fires.
enum class ListBoxNavOp { StepUp, StepDown, JumpFirst, JumpLast };

// Drive a CSWGuiListBox cursor in response to Nav-Up / Nav-Down / Home / End.
// Pure listbox-side effect: writes selection_index + scrolls
// top_visible_index to keep the new selection visible. Does NOT call
// SetSelectedControl, so the engine's onSelectionChanged callback won't
// run. minSel = first valid selection (0 normally, 1 for equip-picker
// LB_ITEMS to skip the protoitem template at row 0). Returns false iff
// listbox is null or rowCount == 0.
bool DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel,
                           ListBoxNavResult& out);

// Queue activation of the chain-navigable button child of `panel` whose
// +0x50 ID matches `buttonId`. Reserved for select-then-confirm panels
// (Container / SaveLoad spec entries). Returns false on debounce or
// missing target — caller still consumes the keypress so the engine's
// stale activeControl can't take over.
bool QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix);

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

// CSWGuiInGameItemEntry (LB_ITEMS row) — field6_0x394 bit-field after the
// embedded CSWGuiButton + item id + borders + text. OnEnterSlot tags the
// currently-equipped row via SetItem(id, /*param_2=*/1, 0); SetItem packs
// (param_2 & 1) into bit 1 (0x2) of field6_0x394 — the same flag that makes
// the engine append the " (Ausgew.)" suffix. So bit 0x2 set ⇔ "this row is
// the item currently equipped in the selected slot". Used by EquipPickerOnEnter
// to route Enter on the equipped row to an unequip (commit the row-0
// 0x7f000000 "empty" entry) instead of a no-op re-equip.
constexpr size_t kEquipItemEntryFlagsOffset  = 0x394;
constexpr uint32_t kEquipItemEntryEquippedBit = 0x2;
