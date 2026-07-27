# peek_description.cpp (797 lines)

Implements Shift+Up/Down "peek description" across every panel kind that has
readable item/control descriptions. Two main strategies: (1) a per-panel
registry (`kPanels`) reading an inline `description_listbox` offset with an
optional refresh callback that re-fires the engine's own `OnControlEntered`
(forcing `is_active=1` first, since keyboard nav never sets it — see
`CallOnControlEnteredWithActive`); (2) an item-tooltip path (`kItemTooltipPanels`)
for listbox-driven panels with no inline description (Container, Equip
picker, Workbench, Quest items) that resolves the focused row's item handle
and calls `SpeakItemBlocks`, which navigates an item's four categorised
description blocks (Tags/Values/Properties/Description) via a rebuildable
cache keyed by item pointer. Special-cases: equip-slot peek (9 fixed slots),
workbench upgrade-slot peek, workbench saber picker (mirrors the engine's own
hover handler for the keyed-bonus line `GetPropertyDescription` drops for
crystals), galaxy-map planet description. Falls back to a generic focused-
control tooltip strref. Talks to `engine_area`, `engine_panels`,
`engine_reads`, `menus_abilities`, `menus_galaxymap`, `menus_internal`,
`menus_listbox`, `strings`.

## Declarations (in source order)

- L29-31 — `constexpr int kCursorReset = -1; int g_blockIdx`
  note: shared block-navigation cursor across both the item-block path and the panel-registry listbox path.
- L37-40 — `void* g_blockCacheItem; acc::engine::ItemDescriptionBlocks g_blocks; const char* g_blockPtrs[4]; int g_blockCount`
- L42 — `bool IsBlank(const char* s)`
- L51 — `void TrimCopy(char* dst, size_t cap, const char* src)`
  note: strips leading/trailing whitespace — engine block builders pad with trailing "\n\n".
- L63 — `void* ResolveRowItem(void* row)` (forward decl; defined L385)
- L71 — `bool SpeakItemBlocks(void* item, bool down)`
  note: rebuilds the 4-block cache on item change, clamps cursor at both ends (repeated boundary block is the "end of list" cue).
- L122 — `static void CallOnControlEnteredWithActive(uintptr_t addr, void* panel, void* focused)`
  note: save/force-1/call/restore around control->is_active — several OnControlEntered overrides early-out on is_active==0, which keyboard nav never sets.
- L137-171 — `constexpr uintptr_t kAddrInventoryOnControlEntered=0x006b3d10; kAddrStoreOnControlEntered=0x006c0aa0; kAddrJournalOnControlEntered=0x00645100` + `RefreshInventory/RefreshStore/RefreshAbilities`
  note: Inventory/Store need the is_active workaround; Journal calls directly; Abilities routes through menus_abilities::RefreshDetail (NOT OnAbilitySelectionChanged, which is mouse-driven and corrupts the stack via a ret-4 mismatch).
- L183 — `struct PanelPeekInfo { PanelKind kind; size_t descListBoxOffset; void(*refresh)(...) }` + `constexpr PanelPeekInfo kPanels[]`
  note: Journal's slot is misnamed item_description_label in SARIF but is a CSWGuiListBox.
- L201 — `const PanelPeekInfo* LookupPanel(PanelKind k)`
- L220 — `struct ItemTooltipPanelInfo { PanelKind kind; void*(*findLb)(void*); int minSel }` + `constexpr ItemTooltipPanelInfo kItemTooltipPanels[]`
  note: minSel=1 skips the Equip-picker's protoitem template row; Container uses 0.
- L226-247 — `ContainerFindLb, InGameEquipFindLb, WorkbenchItemsFindLb, WorkbenchUpgradeFindLb, QuestItemFindLb`
  note: fixed inline offsets for type-known panels (e.g. Container +0x07f0, InGameEquip +0x30d8, QuestItem LB_ITEMS +0x488); FindControlById for heap-allocated workbench listboxes.
- L260-275 — `struct EquipSlotPeekInfo { int cid; size_t itemIdOffset }` + `constexpr EquipSlotPeekInfo kEquipSlotPeek[9]`
  note: itemIdOffset is the panel-cached client handle rewritten on every party-cycle.
- L277 — `const EquipSlotPeekInfo* FindEquipSlotByControl(void* control)`
- L294 — `bool HandleEquipSlotTooltip(void* panel, const EquipSlotPeekInfo& info, bool down)`
  note: 0x7f000000 = kInvalidObjectId "slot empty" sentinel.
- L336 — `bool HandleWorkbenchSlotTooltip(void* panel, void* control, bool down)`
  note: falls back to the mod's name when occupied but no property text, so peek never goes silent on an installed mod.
- L373 — `const ItemTooltipPanelInfo* LookupItemTooltipPanel(PanelKind k)`
- L380 — `constexpr size_t kItemEntryGameObjectIdOffset = 0x1c4`
- L385 — `void* ResolveRowItem(void* row)`
  note: shared by Inventory/Store focused-row path and SpeakItemRowDescription.
- L400 — `bool TryReadWorkbenchSaberDescription(...)` (forward decl; defined L540)
- L404 — `bool HandleItemTooltip(PanelKind kind, const ItemTooltipPanelInfo& info, void* activePanel, bool down)`
  note: for WorkbenchUpgrade, prefers the saber-picker engine-hover description and prepends an "installed" marker when the peeked row is the currently-installed crystal.
- L503 — `const char* ReadRowText(void* row, char* outBuf, size_t bufSize)`
  note: rows can be CSWGuiLabel or CSWGuiButton; tries gui-string then CExoString+strref paths for both.
- L527 — `bool ShiftHeld()`
- L540 — `bool TryReadWorkbenchSaberDescription(void* panel, void* row, char* outBuf, size_t bufSize)`
  note: category must be read as uint8_t (a 4-byte int read pulls in neighbouring panel bytes); saber category only (field25==1), returns false for non-saber items to let the richer GetPropertyDescription path run.
- L588 — `void OnShiftReleased()`
  note: resets block cursor and drops the item-block cache.
- L600 — `bool SpeakItemRowDescription(void* row)`
  note: used for Enter on quest-item rows (no meaningful activate action).
- L629 — `bool HandleShiftArrow(int param_1, int param_2, void* activePanel, void* focusedControl)`
  note: dispatch order — InGameEquip slot (skipped while picker armed) -> WorkbenchUpgrade slot (skipped while picker armed) -> GalaxyMap -> item-tooltip panels -> Inventory/Store block-nav -> panel registry -> generic tooltip fallback.
