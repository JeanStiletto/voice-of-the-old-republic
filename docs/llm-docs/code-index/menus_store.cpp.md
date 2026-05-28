# menus_store.cpp (559 lines)

Store (merchant screen) accessibility implementation. Detects buy/sell mode from visible listbox bits, announces price/stock on chain step, watches trade outcomes via list-size delta, and handles Esc/G hotkeys.

## Declarations (in source order)

- L25 — `enum class Mode` — Unknown, Buy, Sell (anonymous ns)
- L31 — `uint32_t ReadListBoxControlBitFlags(void* panel, size_t listOffset)` — reads CSWGuiListBox.bit_flags from a panel-relative list offset (anonymous ns)
- L46 — `Mode ResolveMode(void* panel)` — determines Buy/Sell by which of the two listboxes has the visible bit set (anonymous ns)
- L59 — `uint32_t ReadRowObjId(void* row)` — reads the obj_id handle from a store item row at +0x1c4 (anonymous ns)
- L73 — `bool IsStoreItemEntry(void* control)` — true when the control's vtable matches CSWGuiInventoryItemEntry (anonymous ns)
- L87 — `int ReadListBoxSize(void* panel, size_t listOffset)` — reads controls.size from the listbox at the given panel-relative offset (anonymous ns)
- L102 — `inline void* ResolveItemFromHandle(uint32_t clientHandle)` — resolves a client object handle to a CSWSItem* (anonymous ns)
- L108 — `typedef uint32_t (__thiscall* PFN_GetItemValue)(void* this_, void* item)` (anonymous ns)
- L110 — `uint32_t CallGetItemValue(uintptr_t fnAddr, void* storePanel, void* item)` — thiscall to engine's buy or sell value accessor (anonymous ns)
- L126 — `int ReadItemStock(void* item, bool& outFinite)` — reads the item's stack count / merchant stock; sets outFinite (anonymous ns)
- L149 — `void* g_lastSeenStorePanel`, `Mode g_lastSeenMode`, `int g_lastSeenActiveListBoxSize`
- L165 — `bool g_tradeWatchArmed`, `Mode g_tradeWatchMode`, `int g_tradeWatchSizeAtArm`, `int g_tradeWatchTicksRemaining`, `uint32_t g_tradeWatchPrice`
- L166 — `constexpr int kTradeWatchTicks = 4`
- L175 — `uint32_t ReadStorePlayerGold(void* panel)` — reads the player's current gold from the store panel's cached value (anonymous ns)
- L187 — `bool acc::menus::store::IsStorePanel(void* panel)`
- L198 — `bool acc::menus::store::IsHiddenStoreListBox(void* panel, void* listBox)`
- L214 — `void acc::menus::store::AnnounceChainStepSuffix(void* panel, void* control)` — speaks "Price: N credits, Stock: N" (or "unlimited") after the item name
- L265 — `void acc::menus::store::TickMonitorMode()` — polls StoreModeToggle hotkey; detects first-sighting, mode changes (speaks mode word + force chain rebind), and trade outcomes via list-size delta
- L410 — `bool acc::menus::store::IsStoreItemRow(void* control)` — delegates to IsStoreItemEntry
- L414 — `typedef void (__thiscall* PFN_StoreOnControlButton)(void* this_, void* param_1)` (anonymous ns)
- L416 — `void acc::menus::store::DispatchTradeAction(void* panel, void* row)` — pre-checks gold for buy; arms trade watcher; calls OnControlStoreAButton or OnControlInvAButton
- L511 — `bool acc::menus::store::ToggleModeFromHotkey()`
- L535 — `bool acc::menus::store::CloseFromEsc()`
