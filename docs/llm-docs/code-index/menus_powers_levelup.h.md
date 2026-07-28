# menus_powers_levelup.h (57 lines)

Public surface for the Force-power picker. Documents that CSWGuiPowersLevelUp
is shared between chargen and in-game level-up, and that it is structurally a
2D skill tree (not a flat listbox) identical in shape to chargen Talente.

## Declarations (in source order)

- L35 — `bool IsPowersLevelUpPanel(void* panel)`
- L42 — `const char* GetTitleOverride(void* panel)` — thread-local buffer; returns nullptr when not a PowersLevelUp screen; returns the panel's own sub_title_label text (not a power-point-budget string)
- L53 — `bool HandleInput(int n, void* thisPtr, void* panel, int param_1, int param_2, int& outRv)` — Up/Down change row+column-snap, Left/Right step filled columns, Enter activate (cell→OnPowerPicked, button→QueueButtonByIdActivate), Esc→BTN_BACK
