# menus_store.cpp (632 lines)

CSWGuiStore (merchant buy/sell screen) accessibility. Resolves the current
Buy/Sell mode from the visibility bit on the two item listboxes, speaks a
per-row price+stock chain-step suffix, and runs a per-tick trade-outcome
watcher keyed primarily off the player's cached gold field (listbox size is
an unreliable trade signal for multi-stock buys / partial-stack sells).
Talks to `engine_manager` (GetForegroundPanel), `engine_reads`
(ResolveItemFromClientHandle), `menus_chain` (RebindChain/
RebindChainPreserveIndex), `menus_pending` (QueueActivate for G/Esc), and
`hotkeys` (StoreModeToggle poll).

## Declarations (in source order)

- L26 — `enum class Mode { Unknown, Buy, Sell }`
- L32-L40 — `uint32_t ReadListBoxControlBitFlags(panel, listOffset)` — SEH-guarded; fault reads as "not the active listbox"
- L47-L56 — `Mode ResolveMode(void* panel)` — bit 0x02 on shop vs inv listbox
- L60-L69 — `uint32_t ReadRowObjId(void* row)` — +0x1c4, SEH-guarded
- L74-L83 — `bool IsStoreItemEntry(void* control)` — vtable check vs the 3 action buttons
  note: vtable is `kVtableCSWGuiStoreItemEntry` (store-specific), not a generic inventory-entry vtable
- L88-L99 — `int ReadListBoxSize(panel, listOffset)`
- L103-L105 — `ResolveItemFromHandle` — thin alias to `engine_reads`
- L109-L119 — `PFN_GetItemValue`, `CallGetItemValue` — thiscall to GetItemBuyValue/GetItemSellValue
- L127-L145 — `int ReadItemStock(void* item, bool& outFinite)` — checks the infinite-stock bit
- L150-L178 — trade-watch statics: `g_lastSeenStorePanel`/`g_lastSeenMode`, `g_lastSeenActiveListBoxSize`, `g_tradeWatchArmed`/`Mode`/`SizeAtArm`/`TicksRemaining`/`Price`/`GoldAtArm`, `kTradeWatchTicks=4`
  note: gold-at-arm is the primary success signal — BuyItem/SellItem move gold synchronously even when the listbox size doesn't (multi-stock buy, partial-stack sell)
- L182-L190 — `uint32_t ReadStorePlayerGold(void* panel)`
- L194-L203 — `bool IsStorePanel(void* panel)`
- L205-L218 — `bool IsHiddenStoreListBox(void* panel, void* listBox)` — RebindChain recursion filter
- L220-L281 — `void AnnounceChainStepSuffix(void* panel, void* control)` — price/stock + charge-count speech per chain step
- L283-L452 — `void TickMonitorMode()` — polls StoreModeToggle hotkey, mode-change announce + chain rebind + cursor reset, trade-outcome detection (gold move OR size delta) with the ~4-tick refusal window
- L454-L456 — `bool IsStoreItemRow(void* control)`
- L458-L581 — `void DispatchTradeAction(void* panel, void* row)`: pre-checks buy-mode gold vs price (skips the engine's ShowExamineBox popup on insufficient funds), arms the trade watcher, conditionally raises row.is_active, calls OnControlStoreAButton/OnControlInvAButton
- L583-L605 — `bool ToggleModeFromHotkey()` — synthesizes a click on examine_button via QueueActivate
- L607-L629 — `bool CloseFromEsc()` — synthesizes a click on cancel_button via QueueActivate
