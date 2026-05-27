// store / trading panel (CSWGuiStore).
//
// Two-mode trading screen. Same panel kind for both modes; the engine
// flips one bit on the shop / inv listbox's CSWGuiControl.bit_flags and
// swaps which listbox is visible. The price displayed in the static
// cost_value label is whichever value function fits the active mode:
//   * Buy mode  (shopitems_listbox.bit_flags & 2 != 0)
//       → GetItemBuyValue  (what the merchant charges).
//   * Sell mode (invitems_listbox.bit_flags & 2 != 0)
//       → GetItemSellValue (what the merchant pays).
//
// Both checks are language-agnostic. We don't read the title label or
// button text. See engine_offsets.h kStore* for the field map.
//
// What we add for screen readers:
//   * Chain-step suffix per item row — price + stock count.
//   * Mode-change announcement when the user flips Buy↔Sell.
//
// We don't reshape the chain itself even though it carries rows from
// both lists (the engine packs both listboxes' rows into panel.controls
// regardless of which is visible). Filtering chain rows by mode is a
// separate follow-up.

#pragma once

namespace acc::menus::store {

// True iff `panel` is a CSWGuiStore (vtable equality).
bool IsStorePanel(void* panel);

// True iff `panel` is a Store AND `listBox` is one of its two item
// listboxes (shopitems or invitems) and that listbox is currently
// HIDDEN — i.e. its visibility bit (0x02 on its CSWGuiControl bit_flags)
// is clear. RebindChain calls this when recursing into a listbox's
// children: if it returns true, skip the listbox entirely so chain
// navigation can't land on rows the user can't act on.
//
// description_listbox is not filtered here (always visible; only carries
// a single descriptive blob which the chain promotes only on modal-text
// panels anyway). Non-Store panels return false unconditionally.
bool IsHiddenStoreListBox(void* panel, void* listBox);

// Speak the per-row price + stock suffix from the chain-step input
// handler, right after AnnounceControl spoke the item name. Reads the
// active listbox's visibility bit to pick Buy vs. Sell pricing, resolves
// the row's obj_id → CSWSItem*, and calls the matching engine value
// function via thiscall. No-op on non-Store panels and on chain rows
// that aren't CSWGuiStoreItemEntry (i.e. the three action buttons at the
// bottom).
void AnnounceChainStepSuffix(void* panel, void* control);

// Per-tick hook. Watches the foreground store panel's mode bit and
// announces "Buy mode" / "Sell mode" when the user flips it via the
// examine_button. Quietly tracks per-panel state so re-opening the store
// doesn't re-announce the initial mode.
//
// Also tracks the active listbox's size between ticks. After a trade
// completes the engine re-runs Populate*ListBox, which preserves row
// pointers but rebinds their obj_id to whatever item now sits at that
// slot. The chain's row pointers stay valid but the items they refer to
// changed — so the chain needs a rebind. We detect this by polling the
// listbox's controls.size and firing RebindChain on any delta.
void TickMonitorMode();

// True iff `control` is a CSWGuiStoreItemEntry — a row of either the
// shop or inv listbox. False on the action buttons (cancel / mode-
// toggle / accept) and anything else. Used by chain Enter dispatch to
// route store rows to the proper trade-action path instead of the
// generic FireActivate.
bool IsStoreItemRow(void* control);

// Drain handler for the Kind::StoreItemActivate pending op. Re-resolves
// the current mode (Buy / Sell) at drain time and calls the engine's
// matching click handler with `row` as param_1:
//   * Buy mode  → CSWGuiStore::OnControlStoreAButton(panel, row)
//   * Sell mode → CSWGuiStore::OnControlInvAButton(panel, row)
// Either function reads the row's obj_id at +0x1c4 to resolve the
// CSWSItem*, then opens a confirmation MessageBox or commits the trade.
// SEH-wrapped; logs the dispatch outcome.
//
// Also arms the trade-outcome watcher (see TickMonitorMode): if the
// active listbox shrinks/grows within ~3 ticks, speaks "Verkauft" /
// "Gekauft"; if it doesn't, speaks "Kann nicht verkauft werden" /
// "Kann nicht gekauft werden". Without this every refused-by-engine
// sell looks identical to a successful one from the user's perspective.
void DispatchTradeAction(void* panel, void* row);

// Synthesize a click on the store's examine_button (mode toggle). The
// engine handles the actual buy↔sell flip; our per-tick monitor sees
// the bit_flags change and announces "Modus Kaufen" / "Modus Verkaufen"
// plus rebinds the chain. Returns false (and logs) if no Store panel is
// in the foreground or an op is already pending.
bool ToggleModeFromHotkey();

// Synthesize a click on the store's cancel_button (Schliess.). Closes
// the store and returns the user to the dialog. Returns false (and
// logs) if no Store panel is foreground or an op is already pending.
bool CloseFromEsc();

}  // namespace acc::menus::store
