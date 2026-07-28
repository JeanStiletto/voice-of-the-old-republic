# menus_internal.h (177 lines)

Private cross-TU seam between menus.cpp and menus_extract.cpp (and, for the listbox-drive helpers, menus_listbox.cpp / menus_keymap.cpp). Declares `acc::menus::detail::*` — predicates and helpers both TUs need but that aren't public API — plus the equip-panel .gui control-ID constants (`kEquipBtn*`, `kEquipLb*`) shared between extract's per-kind label resolution and menus.cpp's slot-detection/dispatch. Documents which globals/functions are defined in which TU (most in menus.cpp; `ParkCursorToCorner` in menus_monitors.cpp).

## Declarations (in source order)

- L46 — `bool acc::menus::detail::IsChainNavigable(void* control)`
- L47 — `bool acc::menus::detail::IsClassSelectionIcon(void* panel, void* control)`
- L48 — `const char* acc::menus::detail::ClassLabelCacheLookup(void* panel, void* icon)` / L49 `void ClassLabelCacheStore(...)`
- L54 — `bool acc::menus::detail::GetControlCenter(void* control, int& outCx, int& outCy)`
- L60 — `void* acc::menus::detail::FindControlById(void* panel, int id)` — locates a child control by +0x50 ID field, defined in menus.cpp
- L65 — `void* acc::menus::detail::FindListBoxChild(void* panel)`
- L70 — `bool acc::menus::detail::IsSaveLoadPanel(void* panel)`
- L75 — `const char* acc::menus::detail::ReadSaveLoadEntryString(void* entry, size_t fieldOffset)`
- L82 — `struct acc::menus::detail::ListBoxNavResult { oldSel, newSel, rowCount, row }`
- L95 — `enum class acc::menus::detail::ListBoxNavOp { StepUp, StepDown, JumpFirst, JumpLast }`
- L104 — `bool acc::menus::detail::DriveListBoxSelection(void* listbox, ListBoxNavOp op, short minSel, ListBoxNavResult& out)` — raw selection_index write, no engine callback fired
- L117 — `bool acc::menus::detail::DriveListBoxSelectionEngine(listbox, op, minSel, out)` — drives real SetSelectedControl (native highlight/sound/scroll); requires the cursor parked off the list
- L130 — `bool acc::menus::detail::ParkCursorToCorner(const char* tag)` — must be called from a per-tick monitor, never the input hook; defined in menus_monitors.cpp
- L137 — `bool acc::menus::detail::QueueButtonByIdActivate(void* panel, int buttonId, const char* logPrefix)`
- L143 — `extern void* g_currentPanel` — file-scope global from OnSetActiveControl in menus.cpp; read-only from extract
- L151-166 — `constexpr int kEquipBtnHeadId..kEquipBtnHandsId, kEquipLbItemsId, kWorkbenchUpgradeLbItemsId, kEquipBtnBackId, kEquipBtnEquipId` — equip.gui / upgrade.gui control IDs
- L168 — `constexpr size_t kEquipItemEntryFlagsOffset = 0x394`, `constexpr uint32_t kEquipItemEntryEquippedBit = 0x2` — bit tagging the currently-equipped LB_ITEMS row (set by OnEnterSlot's SetItem)
