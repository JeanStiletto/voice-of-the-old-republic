# menus_listbox.h (103 lines)

Public surface for the listbox accessibility dispatcher. Declares `namespace acc::menus::listbox`.

## Declarations (in source order)

- L1 — `namespace acc::menus::listbox`
- L10 — `bool TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)` — probes the spec table; returns true when consumed
- L16 — `const char* GetTitleOverride(void* panel)` — returns spec-driven panel title or nullptr
- L20 — `void DumpFeatsCharGenStructureIfNeeded(void* panel)` — one-shot diagnostic dump of CSWGuiFeatsCharGen internals to log
- L24 — `bool IsEquipPickerArmed()`
- L25 — `void* EquipPickerPanel()`
- L27 — `void ArmEquipPicker(void* panel)`
- L29 — `void DisarmEquipPicker()`
- L32 — `bool IsWorkbenchUpgradePickerArmed()`
- L34 — `void ArmWorkbenchUpgradePicker(void* panel)`
- L36 — `void DisarmWorkbenchUpgradePicker()`
- L40 — `void TickListboxMonitors()` — fans out to MonitorContainerSelection, MonitorEquipPickerSelection, MonitorWorkbenchUpgradePicker, PollContainerGiveModeKey
