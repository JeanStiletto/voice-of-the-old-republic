# menus_listbox.h (91 lines)

Header for the spec-table-driven "listbox-arrow-nav + Enter-confirm + Esc-back" dispatcher shared by Container/SaveLoad/EquipPicker-shaped panels. Explains the design rationale: divergence between panels isn't decorative (different Enter targets), so a spec struct with onEnter/onEsc callbacks keeps the dispatcher generic. EquipPicker's armed-flag + bound-panel state lives in the .cpp because its input handler is the primary mutator; two outside touch sites in menus.cpp use the accessors declared here.

## Declarations (in source order)

- L46 — `bool acc::menus::listbox::TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)`
- L60 — `const char* acc::menus::listbox::GetTitleOverride(void* panel)`
- L69-72 — `bool IsEquipPickerArmed()`, `void* EquipPickerPanel()`, `void ArmEquipPicker(void* panel)`, `void DisarmEquipPicker()`
- L79-81 — `bool IsWorkbenchUpgradePickerArmed()`, `void ArmWorkbenchUpgradePicker(void* panel)`, `void DisarmWorkbenchUpgradePicker()`
- L89 — `void acc::menus::listbox::TickListboxMonitors()` — fans out MonitorContainerSelection, MonitorEquipPickerSelection, MonitorWorkbenchUpgradePicker, PollContainerGiveModeKey
