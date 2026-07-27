# menus_chargen_layout.cpp (60 lines)

Implements the three layout helpers shared by the chargen Attributes and
Skills panels (same N-button/N-label/N-plus/N-minus struct shape, differing
only in N). Consolidates near-byte-identical logic that used to be duplicated
in both per-panel `.cpp` files.

## Declarations (in source order)

- L10 — `namespace acc::menus::chargen_layout`
- L12 — `bool IsPanelOfVtable(void* panel, uintptr_t expectedVtable)`
  note: SEH-guarded vtable-pointer compare
- L23 — `int IndexFromButton(void* panel, void* control, size_t buttonsArrayOffset, int maxCount)`
  note: validates stride alignment (kCSWGuiButtonSize) and bounds before returning an index
- L37 — `int RowPitchFromButtonExtents(void* panel, size_t buttonsArrayOffset)`
  note: derives row pitch from buttons[0]/[1] extent.top delta; returns 0 if outside [1,100] (sanity bound)
