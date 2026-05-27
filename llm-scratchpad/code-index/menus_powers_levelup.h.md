# menus_powers_levelup.h (56 lines)

Public surface for the PowersLevelUp panel (pwrlvlup.gui CSWGuiSkillFlow tree) accessibility module. Declares `namespace acc::menus::powers_levelup`.

## Declarations (in source order)

- L1 — `namespace acc::menus::powers_levelup`
- L10 — `bool IsPowersLevelUpPanel(void* panel)`
- L14 — `const char* GetTitleOverride(void* panel)` — returns a thread_local formatted title string including the current power-point budget
- L18 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)` — 2D Up/Down/Left/Right nav over the 3-column SkillFlow tree; mirrors chargen_feats shape
