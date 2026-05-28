# menus_store.h (100 lines)

Public surface for the store (merchant screen) accessibility module. Declares `namespace acc::menus::store`.

## Declarations (in source order)

- L1 — `namespace acc::menus::store`
- L10 — `bool IsStorePanel(void* panel)`
- L12 — `bool IsHiddenStoreListBox(void* panel, void* listBox)` — true when the given listbox is the inactive (hidden) buy/sell list; used by chain builder to skip it
- L15 — `void AnnounceChainStepSuffix(void* panel, void* control)` — speaks price and stock after the item name when the user chains to a store row
- L18 — `void TickMonitorMode()` — detects buy/sell mode changes, trade outcomes (list size delta), and polls the StoreModeToggle hotkey
- L22 — `bool IsStoreItemRow(void* control)` — true when the control is a CSWGuiInventoryItemEntry-style store row
- L25 — `void DispatchTradeAction(void* panel, void* row)` — calls engine's OnControlStoreAButton or OnControlInvAButton; pre-checks gold for buy; arms the trade outcome watcher
- L29 — `bool ToggleModeFromHotkey()` — queues FireActivate on the toggle button when the StoreModeToggle hotkey fires
- L32 — `bool CloseFromEsc()` — queues FireActivate on the cancel button when Esc is pressed on the store panel
