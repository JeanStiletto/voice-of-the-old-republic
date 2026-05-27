// Shift+Up/Down description peek.
//
// Shift+arrow speaks one description block at a time from the focused
// panel's description listbox (Inventory, Journal, Abilities, Store) or
// from per-item tooltip resolution (Container, Equip picker, Workbench)
// or the focused control's tooltip strref as a generic fallback. Shift
// release resets the block cursor.
//
// Adding a new panel = one entry in the panel registry inside the .cpp
// (offset of the description_listbox member, optional refresh adapter).

#pragma once

namespace acc::peek {

// Called from the manager-level input dispatch. Returns true iff
// Shift+Up/Down was consumed for peek; on false the event passes through
// to plain nav.
//
// focusedControl is the chain target — needed by panels whose
// description listbox is refreshed by re-firing OnControlEntered on
// the focused row.
bool HandleShiftArrow(int param_1, int param_2, void* activePanel,
                      void* focusedControl);

// Resets the block cursor; next Shift+arrow speaks block 0.
void OnShiftReleased();

}  // namespace acc::peek
