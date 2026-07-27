# menus_chargen_layout.h (34 lines)

Shared layout-helper declarations used by both `menus_chargen_attr` and
`menus_chargen_skills`, which have near-identical N-button/N-label struct
shapes and the same cursor-warp Y-shift hit-test quirk.

## Declarations (in source order)

- L16 — `namespace acc::menus::chargen_layout`
- L19 — `bool IsPanelOfVtable(void* panel, uintptr_t expectedVtable)`
- L24 — `int IndexFromButton(void* panel, void* control, size_t buttonsArrayOffset, int maxCount)`
- L31 — `int RowPitchFromButtonExtents(void* panel, size_t buttonsArrayOffset)`
  note: used by chain-step click-sim to compensate for the engine's hit-test shift on these panels
