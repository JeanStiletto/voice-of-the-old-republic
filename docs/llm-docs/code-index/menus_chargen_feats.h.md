# menus_chargen_feats.h (55 lines)

Public surface for the chargen "Talente" 2D feat-tree chart handler. Explains
why this panel needs its own dispatcher instead of the listbox-driven
machinery in menus_listbox.cpp: the chart isn't row-shaped
(`DriveListBoxSelection` would manipulate a single-child listbox's
selection_index uselessly). Documents cell status enum values (0 available,
1 existing, 2 granted-this-level, 3 locked, 4 chosen-this-level).

## Declarations (in source order)

- L36 — `namespace acc::menus::chargen_feats`
- L39 — `bool IsChargenFeatsPanel(void* panel)`
- L51 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)`
  note: Up/Down across cells+buttons, Enter activates (OnFeatPicked for a cell), Esc queues BTN_BACK; all else falls through
