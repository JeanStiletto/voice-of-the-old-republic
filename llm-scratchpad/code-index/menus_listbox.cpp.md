# menus_listbox.cpp (1779 lines)

Listbox accessibility dispatcher TU. Contains the `ListBoxPanelSpec` spec table, all spec entries (Container, SaveLoad, EquipPicker, SkillInfoBox, InGameMessages, four dialog variants, WorkbenchItems, WorkbenchUpgrade, Examine), the generic dispatcher, and co-located subsystem monitors.

## Declarations (in source order)

- L82 — `bool acc::menus::listbox::IsEquipPickerArmed()`
- L83 — `void* acc::menus::listbox::EquipPickerPanel()`
- L85 — `void acc::menus::listbox::ArmEquipPicker(void* panel)`
- L90 — `void acc::menus::listbox::DisarmEquipPicker()`
- L95 — `bool acc::menus::listbox::IsWorkbenchUpgradePickerArmed()`
- L97 — `void acc::menus::listbox::ArmWorkbenchUpgradePicker(void* panel)`
- L102 — `void acc::menus::listbox::DisarmWorkbenchUpgradePicker()`
- L114 — `struct ListBoxPanelSpec` — spec fields: logTag, matches, armed, resetStale, findListBox, minSel, announce, enrichRow, logExtra, onEnter, onEsc, titleOverride, emptyStateId, alwaysReturnFromHandler (anonymous ns, private to TU)
- L214 — `bool ContainerMatches(void* p)` — Container spec helper (anonymous ns)
- L275 — `constexpr ListBoxPanelSpec kContainerSpec`
- L296 — SaveLoad spec static helper functions (anonymous ns)
- L353 — `constexpr ListBoxPanelSpec kSaveLoadSpec`
- L376 — EquipPicker spec static helper functions: `EquipPickerMatches`, `EquipPickerArmed`, `EquipPickerResetStale`, `EquipPickerFindLb`, `EquipPickerAnnounce`, `EquipPickerEnrichRow`, `EquipPickerOnEnter`, `EquipPickerOnEsc` (anonymous ns)
- L471 — `constexpr ListBoxPanelSpec kEquipPickerSpec`
- L514 — SkillInfoBox spec helper functions (anonymous ns)
- L713 — `constexpr ListBoxPanelSpec kSkillInfoBoxSpec`
- L743 — InGameMessages spec helper functions (anonymous ns)
- L770 — `constexpr ListBoxPanelSpec kInGameMessagesSpec`
- L794 — Dialog spec helper functions (anonymous ns) + `kDialogCinematicSpec`, `kDialogCinematicCopySpec`, `kDialogComputerSpec`, `kDialogComputerCameraSpec`
- L910 — WorkbenchItems spec helper functions: `WorkbenchItemsMatches`, `WorkbenchItemsFindLb`, `WorkbenchItemsAnnounce`, `WorkbenchItemsOnEnter`, `WorkbenchItemsOnEsc` (anonymous ns)
- L960 — `constexpr ListBoxPanelSpec kWorkbenchItemsSpec`
- L1003 — WorkbenchUpgrade spec helper functions: `WorkbenchUpgradeMatches`, `WorkbenchUpgradeArmed`, `WorkbenchUpgradeResetStale`, `WorkbenchUpgradeFindLb`, `WorkbenchUpgradeAnnounce`, `WorkbenchUpgradeOnEnter`, `WorkbenchUpgradeOnEsc` (anonymous ns)
- L1093 — `constexpr ListBoxPanelSpec kWorkbenchUpgradeSpec`
- L1130 — Examine spec helper functions: `ExamineMatches`, `ExamineFindLb`, `ExamineAnnounce` (anonymous ns)
- L1153 — `constexpr ListBoxPanelSpec kExamineSpec`
- L1175 — `constexpr const ListBoxPanelSpec* kSpecs[]` — 12-entry ordered spec table
- L1192 — `constexpr int kNumSpecs`
- L1199 — `bool DispatchKeyDownEdge(const ListBoxPanelSpec& spec, void* panel, int param_1)` — handles Up/Down/Home/End/Enter/Esc for a matched spec (anonymous ns)
- L1255 — `void LogStandard(int n, void* thisPtr, int param_1, int param_2, bool consumed)` — shared log helper (anonymous ns)
- L1274 — `bool acc::menus::listbox::TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)`
- L1329 — `struct ContainerSelState` + `ContainerSelState s_containerSelState` (anonymous ns)
- L1337 — `struct EquipSelState` + `EquipSelState s_equipSelState` (anonymous ns)
- L1343 — `void MonitorContainerSelection()` — per-tick; arms on Container panel entry, announces row text on selection change (anonymous ns)
- L1447 — `void MonitorEquipPickerSelection()` — per-tick; arms on InGameEquip panel entry, announces equipped item on selection change (anonymous ns)
- L1542 — `void MonitorWorkbenchUpgradePicker()` — per-tick; disarms picker when upgrade.gui panel leaves panels[] (anonymous ns)
- L1569 — `void PollContainerGiveModeKey()` — polls ContainerGiveMode hotkey; queues FireActivate on BTN_GIVEITEMS (anonymous ns)
- L1594 — `void acc::menus::listbox::TickListboxMonitors()`
- L1601 — `const char* acc::menus::listbox::GetTitleOverride(void* panel)` — probes spec table for titleOverride callback
- L1616 — `void* s_loggedFeatsPanel` (anonymous ns)
- L1623 — `int FeatNameStrref(unsigned short featId)` — reads feat name strref from engine rules table for diagnostic logging (anonymous ns)
- L1641 — `void DumpUshortListSEH(unsigned char* base, size_t dataOff, size_t sizeOff, const char* tag)` — logs a ushort list field from CSWGuiFeatsCharGen (anonymous ns)
- L1677 — `void DumpChartCells(void* fcp)` — logs the full SkillFlow chart cell grid (anonymous ns)
- L1744 — `void acc::menus::listbox::DumpFeatsCharGenStructureIfNeeded(void* panel)`
