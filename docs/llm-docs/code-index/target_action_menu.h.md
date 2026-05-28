# target_action_menu.h (34 lines)

Target-action submenu header (Shift+1..Shift+3) — keyboard exploration of CSWGuiTargetActionMenu rows 0..2. Sibling to actionbar_menu (Shift+4..7). While active: Up/Down cycles, Shift+arrow speaks tooltip, Enter fires, Esc disarms.

## Declarations (in source order)

- L18 — `namespace acc::target_action_menu`
- L21 — `bool Open(int row);`
  note: row 0..2; false on empty row (speaks "Spalte ist leer", stays disarmed); fires PrepareBareDispatch to refresh action_lists before reading
- L23 — `bool IsActive();`
- L25 — `bool HandleInputEvent(int code, int value);`
- L27 — `void ForceDisarm(const char* reason);`
- L31 — `int CurrentSelection(int row);`
  note: used by input_pipeline to re-stamp the persistent per-row index after PopulateMenus invalidates field1[]
