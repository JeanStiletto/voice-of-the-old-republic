# menus_chargen_feats.h (54 lines)

Public surface for the chargen Feats panel (CSWGuiFeatsCharGen SkillFlow tree) accessibility module. Declares `namespace acc::menus::chargen_feats`.

## Declarations (in source order)

- L1 — `namespace acc::menus::chargen_feats`
- L10 — `bool IsChargenFeatsPanel(void* panel)`
- L14 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)` — 2D Up/Down/Left/Right nav over the 3-column SkillFlow tree plus Enter/Esc; returns true when consumed
