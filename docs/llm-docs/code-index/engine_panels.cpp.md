# engine_panels.cpp (772 lines)

Implementation of engine_panels.h. No leading comment block. Contains the PanelKind slot offset table and all structural panel detectors.

## Declarations (in source order)

- L12 — `namespace acc::engine`
- L24 — `namespace { ... }` (anonymous, structural detectors)
- L26 — `void* FindControlByGuiId(void* panel, int id)`
  note: scans panel.controls[] up to 64 entries for a child with .gui-id == id
- L47 — `bool ControlHasVtable(void* control, uintptr_t expected)`
  note: used by structural detectors to disambiguate panels sharing .gui-time IDs by control type
- L57 — `bool IsSaveLoadStructural(void* panel)`
  note: requires listbox at ID 0 + buttons at IDs 11/12/14 to distinguish from Workbench upgrade (ID 11 = LabelHilight there)
- L99 — `bool IsWorkbenchUpgradeStructural(void* panel)`
- L126 — `bool IsWorkbenchItemsStructural(void* panel)`
- L158 — `bool IsWorkbenchSelectStructural(void* panel)`
  note: ID 0 is a Button on upgradesel.gui (BTN_RANGED); on other workbench panels ID 0 is a ListBox
- L180 — `bool IsLevelUpStructural(void* panel)`
  note: CSWGuiLevelUpPanel identified by vtable equality at 0x00759568
- L197 — `bool IsMainMenuOptionsStructural(void* panel)`
  note: CSWGuiOptions vtable 0x00758838; pre-game so CGuiInGame not resolvable
- L217 — `bool IsPowersLevelUpStructural(void* panel)`
  note: listboxes at IDs 6+7 uniquely identify pwrlvlup.gui; backs both chargen and level-up flows
- L234 — `namespace { ... }` (CGuiInGame resolution constants)
- L252 — `struct PanelKindOffset`
- L258 — `static const PanelKindOffset kPanelKindOffsets[]`
  note: 40-row table mapping CGuiInGame field offsets to PanelKind values; kNoSlotOffset marks heap-allocated kinds
- L319 — `const char* PanelKindName(PanelKind k)`
- L327 — `void* ResolveGuiInGame()`
- L362 — `namespace { ... }` (PanelKindCacheEntry + unknown-panel probe)
- L368 — `bool IsVtableAlreadyDumped(uintptr_t vt)`
- L374 — `void RememberDumpedVtable(uintptr_t vt)`
- L386 — `bool ProbeReadGuiString(void* control, size_t guiStringPtrOffset, char* outBuf, size_t bufSize)`
  note: inline minimal gui_string reader; avoids circular dep with menus layer
- L413 — `void LogUnknownPanelDiagnostics(void* panel)`
  note: first-sight dump per unique panel vtable; captures per-control {vtable, id, text} for writing new structural detectors
- L476 — `PanelKind IdentifyPanel(void* panel)`
- L552 — `bool IsPanelKindInGameMenu(void* panel)`
- L556 — `bool HasActiveDialogPanel()`
- L580 — `typedef void (__thiscall* PFN_PrevSWInGameGui)(void*)`
- L585 — `typedef int (__thiscall* PFN_HideSWInGameGui)(void*, int)`
- L587 — `bool CallPrevSWInGameGui()`
- L599 — `bool CallHideSWInGameGui(int param_1)`
- L632 — `bool HasActiveMapPanel(void** outPanel)`
- L658 — `bool IsInGameOptionsSubScreen(void* panel)`
- L683 — `bool HasActiveSubScreen()`
- L711 — `bool IsForegroundUiBlocking(UiBlockState* outState)`
