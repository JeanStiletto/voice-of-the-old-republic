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

// Force a synchronous rebuild of the item list by calling the engine's
// CSWGuiInGameInventory::PopulateItemListBox. SEH-guarded, and a no-op when
// the address is unresolved on the running build. Used after a filter
// activate so the subsequent chain rebind sees the final filtered list
// instead of racing Draw().
void ForceRepopulate(void* panel);

}  // namespace acc::menus::inventory
