# menus_store.h (101 lines)

Public surface for the store/trading panel module. Documents the two-mode
listbox-visibility-bit design and that both buy/sell price checks are
language-agnostic (no title/button text reads). Notes the chain still carries
rows from both lists regardless of visible mode — filtering by mode is called
out as a separate follow-up, not yet done.

## Declarations (in source order)

- L29 — `bool IsStorePanel(void* panel)`
- L41 — `bool IsHiddenStoreListBox(void* panel, void* listBox)` — chain-recursion skip filter for the hidden shop/inv listbox
- L50 — `void AnnounceChainStepSuffix(void* panel, void* control)` — price/stock speech after AnnounceControl; no-op on non-item rows
- L63 — `void TickMonitorMode()` — mode-change speech + chain rebind on trade/mode-flip
- L70 — `bool IsStoreItemRow(void* control)` — routes Enter to trade-action path vs generic FireActivate
- L86 — `void DispatchTradeAction(void* panel, void* row)` — drain handler for Kind::StoreItemActivate; arms/reports the outcome watcher
- L93 — `bool ToggleModeFromHotkey()`
- L98 — `bool CloseFromEsc()`
