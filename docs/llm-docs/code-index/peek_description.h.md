# peek_description.h (29 lines)

Shift+Up/Down description peek. Speaks one description block at a time from the
focused panel's description listbox (Inventory, Journal, Abilities, Store), or
from per-item tooltip resolution (Container, Equip picker, Workbench), or focused
control tooltip as a generic fallback. Shift release resets the block cursor.

## Declarations (in source order)

- L14 — `namespace acc::peek`
- L24 — `bool HandleShiftArrow(int param_1, int param_2, void* activePanel, void* focusedControl)`
  note: called from manager-level input dispatch; returns true iff Shift+Up/Down was consumed
- L27 — `void OnShiftReleased()`
