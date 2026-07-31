// inventory panel (CSWGuiInGameInventory) helpers.
//
// Sibling of menus_journal.h, and for the same reason: the "Inventar" screen
// has buttons that mutate the item list out from under our cached chain, and
// one button that duplicates what Enter on a row already does.
//
// The engine model behind these three predicates is written up next to
// kInventoryItemListBoxOffset in engine_offsets_fields.h — read that first.
// In short:
//
//   * "Verwenden" (useitem_button) is the gamepad-A shortcut. It dispatches
//     activate to panel.active_control, which our cursor warp routinely fails
//     to update, so it can fire the wrong item's handler. Enter on the row
//     reaches the same per-item handler directly.
//   * "Zeigen: …" (the filter button) repopulates the list lazily in Draw(),
//     leaving the chain with a stale row count and row→item mapping.

#pragma once

namespace acc::menus::inventory {

// True iff `control` is the panel's "Verwenden" button (panel+0x1328).
// IsDecorativeControl uses this to keep it out of the chain.
bool IsUseItemButton(void* panel, void* control);

// True iff `control` is the item-filter cycle button (panel+0x14ec, the one
// captioned "Zeigen: <next filter>"). The chain must be rebuilt after it
// fires.
bool IsFilterButton(void* panel, void* control);

// Point item_listbox.selection_index at `row` before its activate is
// dispatched. MANDATORY for inventory item rows, because the handler
// CreateItemEntry wires to them —
// CSWGuiInGameInventory::OnControlSelected @0x006b25e0 — ignores the control
// that fired it and calls GetSelectedControl(&this->item_listbox) instead. A
// mouse click sets that selection on the way in; our keyboard activate never
// did, so every Enter used whichever row PopulateItemListBox last selected
// (index 0 after a filter change). Verified in patch-20260731-094302.log:
// Enter on "Geschickl.-Enhancer" and on "Hyper-Verfassungsenh." both burned a
// charge off the Mandalorian melee shield sitting at row 0.
//
// Drives the engine's CSWGuiListBox::SetSelectedControl rather than writing
// selection_index raw, so the row highlight, the scroll-follow bit and
// OrganizeControls all run as they would for a real click. SEH-guarded;
// no-ops on a null panel/row, an unresolved address, or a row that is not in
// this listbox.
void SyncListBoxSelection(void* panel, void* row);

// Force a synchronous rebuild of the item list by calling the engine's
// CSWGuiInGameInventory::PopulateItemListBox. SEH-guarded, and a no-op when
// the address is unresolved on the running build. Used after a filter
// activate so the subsequent chain rebind sees the final filtered list
// instead of racing Draw().
void ForceRepopulate(void* panel);

}  // namespace acc::menus::inventory
