# peek_description.cpp (470 lines)

Implementation of the Shift+Up/Down description peek. Three dispatch paths:
equip-slot tooltip (9 slots by control ID), item-tooltip (listbox-driven panels),
and description-listbox (Inventory/Journal/Abilities/Store). Generic fallback reads
focused control tooltip strref.

## Declarations (in source order)

- L19 — `namespace acc::peek`
- L21 — `namespace` (anonymous)
- L23 — `constexpr int kCursorReset`
- L25 — `int g_blockIdx`
- L27 — `typedef void (__thiscall* PFN_PanelOnControl)(void* panel, void* control)`
- L33 — `static void CallOnControlEnteredWithActive(std::uintptr_t addr, void* panel, void* focused)`
  note: saves is_active, forces to 1, calls OnControlEntered, restores — works around equipped-row is_active=0 gate
- L48 — `constexpr std::uintptr_t kAddrInventoryOnControlEntered`
- L50 — `static void RefreshInventory(void* panel, void* focused)`
- L57 — `constexpr std::uintptr_t kAddrStoreOnControlEntered`
- L59 — `static void RefreshStore(void* panel, void* focused)`
- L66 — `constexpr std::uintptr_t kAddrJournalOnControlEntered`
- L68 — `static void RefreshJournal(void* panel, void* focused)`
  note: no is_active gate on Journal's OnControlEntered — direct call without the save/force/restore wrapper
- L85 — `struct PanelPeekInfo`
  note: maps PanelKind to description listbox offset + optional refresh adapter; add one row per new panel
- L91 — `constexpr PanelPeekInfo kPanels[]`
- L103 — `const PanelPeekInfo* LookupPanel(acc::engine::PanelKind k)`
- L122 — `struct ItemTooltipPanelInfo`
  note: for listbox-driven panels with no inline description_listbox; findLb callback + minSel skip protoitem row
- L128 — `void* ContainerFindLb(void* panel)`
- L133 — `void* InGameEquipFindLb(void* panel)`
- L138 — `void* WorkbenchItemsFindLb(void* panel)`
- L142 — `void* WorkbenchUpgradeFindLb(void* panel)`
- L146 — `constexpr ItemTooltipPanelInfo kItemTooltipPanels[]`
- L156 — `struct EquipSlotPeekInfo`
  note: maps locale-stable .gui control ID to panel-cached client item handle offset
- L161 — `constexpr EquipSlotPeekInfo kEquipSlotPeek[]`
- L173 — `const EquipSlotPeekInfo* FindEquipSlotByControl(void* control)`
- L191 — `bool HandleEquipSlotTooltip(void* panel, const EquipSlotPeekInfo& info)`
  note: consumes key regardless of return (predictable-behaviour rule); silent on empty/unresolvable slot
- L235 — `const ItemTooltipPanelInfo* LookupItemTooltipPanel(acc::engine::PanelKind k)`
- L242 — `constexpr std::size_t kItemEntryGameObjectIdOffset`
- L245 — `bool HandleItemTooltip(acc::engine::PanelKind kind, const ItemTooltipPanelInfo& info, void* activePanel)`
  note: caller consumes key regardless of return
- L322 — `const char* ReadRowText(void* row, char* outBuf, std::size_t bufSize)`
  note: tries all text paths (label-gui, button-gui, label-cexo, button-cexo); rows may be either control type
- L346 — `bool ShiftHeld()`
- L354 — `void OnShiftReleased()`
- L362 — `bool HandleShiftArrow(int param_1, int param_2, void* activePanel, void* focusedControl)`
