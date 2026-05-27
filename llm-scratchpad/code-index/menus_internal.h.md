# menus_internal.h (136 lines)

Cross-TU seam between `menus.cpp` and `menus_extract.cpp`. Declares `namespace acc::menus::detail` plus shared globals and equip-screen constants.

## Declarations (in source order)

- L1 — `namespace acc::menus::detail`
- L10 — `bool IsChainNavigable(void* control)`
- L12 — `bool IsClassSelectionIcon(void* panel, void* control)`
- L14 — `const char* ClassLabelCacheLookup(void* panel, void* icon)`
- L16 — `void ClassLabelCacheStore(void* panel, void* icon, const char* text)`
- L19 — `bool GetControlCenter(void* control, int& outCx, int& outCy)`
- L22 — `void* FindControlById(void* panel, int id)`
- L24 — `void* FindListBoxChild(void* panel)`
- L27 — `bool IsSaveLoadPanel(void* panel)`
- L30 — `const char* ReadSaveLoadEntryString(void* entry, size_t fieldOffset)`
- L35 — `struct ListBoxNavResult`
- L44 — `enum class ListBoxNavOp`
- L54 — `bool DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel, ListBoxNavResult& out)`
- L57 — `bool QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix)`
- L62 — `extern void* g_currentPanel` — last panel SetActiveControl was called on
- L66 — `constexpr int kEquipBtnHeadId`, `kEquipBtnImplantId`, `kEquipBtnBodyId`, `kEquipBtnArmLId`, `kEquipBtnArmRId`, `kEquipBtnWeapLId`, `kEquipBtnWeapRId`, `kEquipBtnBeltId`, `kEquipBtnHandsId`
- L80 — `constexpr int kEquipLbItemsId`
- L83 — `constexpr size_t kEquipPanelHeadIdOffset`, and sibling slot offsets (implant, armor, armband L/R, weapon L/R, belt, gloves)
