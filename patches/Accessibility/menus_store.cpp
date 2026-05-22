// KOTOR Accessibility — store / trading panel (CSWGuiStore).
// See menus_store.h for the design rationale.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>

#include "menus_store.h"

#include "engine_manager.h"   // GetForegroundPanel, kAddrGuiManagerPtr
#include "engine_offsets.h"
#include "engine_player.h"    // kAddrAppManagerPtr
#include "engine_reads.h"     // ResolveItemFromClientHandle
#include "hotkeys.h"          // Pressed() for G (StoreModeToggle)
#include "log.h"
#include "menus_chain.h"      // RebindChain on mode flip
#include "menus_pending.h"    // QueueActivate for G / Esc helpers
#include "strings.h"
#include "tolk.h"

namespace acc::menus::store {

namespace {

enum class Mode { Unknown, Buy, Sell };

// Read CSWGuiControl.bit_flags @ +0x44 on the listbox at panel + listOffset.
// SEH-protected: a fault returns 0 which the bit test interprets as "this
// listbox isn't the active one" — never wrong direction even if the read
// fails.
uint32_t ReadListBoxControlBitFlags(void* panel, size_t listOffset) {
    if (!panel) return 0;
    auto* lb = reinterpret_cast<unsigned char*>(panel) + listOffset;
    __try {
        return *reinterpret_cast<uint32_t*>(lb + kControlBitFlagsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Resolve the current Buy/Sell mode from the visibility bit on the two
// item listboxes. The engine sets bit 1 (0x02) on whichever is currently
// shown (see CSWGuiStore::ShowBuyGUI / ShowSellGUI). Returns Unknown if
// neither bit is set — should only happen during the brief construction
// window before InitializeStoreType runs.
Mode ResolveMode(void* panel) {
    if (!IsStorePanel(panel)) return Mode::Unknown;
    uint32_t shopBits = ReadListBoxControlBitFlags(
        panel, kStoreShopItemsListBoxOffset);
    if (shopBits & kStoreListBoxVisibleBit) return Mode::Buy;
    uint32_t invBits = ReadListBoxControlBitFlags(
        panel, kStoreInvItemsListBoxOffset);
    if (invBits & kStoreListBoxVisibleBit) return Mode::Sell;
    return Mode::Unknown;
}

// Read the row's obj_id at +0x1c4. SEH-protected; returns 0 (invalid
// handle) on fault.
uint32_t ReadRowObjId(void* row) {
    if (!row) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(row) +
            kStoreItemEntryObjIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// True iff `control` is a CSWGuiStoreItemEntry (rows of either listbox)
// rather than one of the action buttons (cancel / examine / accept) the
// chain also surfaces.
bool IsStoreItemEntry(void* control) {
    if (!control) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(control);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiStoreItemEntry;
}

// Read the listbox controls.size at panel + listOffset. The size is at
// +0x29c (kListBoxControlsOffset) within the listbox; CExoArrayList
// layout is data@0, size@4. Returns -1 on SEH fault.
int ReadListBoxSize(void* panel, size_t listOffset) {
    if (!panel) return -1;
    __try {
        auto* lb = reinterpret_cast<unsigned char*>(panel) + listOffset;
        int size = *reinterpret_cast<int*>(
            lb + kListBoxControlsOffset + 4);
        if (size < 0 || size > 4096) return -1;
        return size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Thin alias to the shared resolver in engine_reads.h. Kept under the
// existing name so callsites in this file stay short.
inline void* ResolveItemFromHandle(uint32_t clientHandle) {
    return acc::engine::ResolveItemFromClientHandle(clientHandle);
}

// Thiscall to GetItemBuyValue / GetItemSellValue on the live store panel.
// Returns 0 on fault.
typedef uint32_t (__thiscall* PFN_GetItemValue)(void* this_, void* item);

uint32_t CallGetItemValue(uintptr_t fnAddr, void* storePanel, void* item) {
    if (!storePanel || !item) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_GetItemValue>(fnAddr);
        return fn(storePanel, item);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Read CSWSItem.stack_size + check the infinite-stock bit. `outFinite`
// is true when stack_size is meaningful, false when the item flags say
// stock is unlimited (CSWGuiStore::OnControlEntered branches on
// `item->bit_flags & 4`).
int ReadItemStock(void* item, bool& outFinite) {
    outFinite = true;
    if (!item) return 0;
    uint32_t bitFlags = 0;
    int stack = 0;
    __try {
        bitFlags = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemBitFlagsOffset);
        stack = *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(item) + kSwsItemStackSizeOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (bitFlags & kSwsItemInfiniteStockBit) {
        outFinite = false;
        return 0;
    }
    return stack;
}

// Per-store-panel mode tracking. Cleared when the foreground stops being
// a store panel (different pointer or no store visible) so re-opening
// the merchant starts fresh without a phantom "Mode Buy" replay.
void* g_lastSeenStorePanel = nullptr;
Mode  g_lastSeenMode       = Mode::Unknown;

// Active-listbox size snapshot from the previous tick. After a sell or
// buy the engine repopulates Populate*ListBox; the size delta is our
// "trade completed" signal, and we rebind the chain so the row pointers
// it holds reflect the new item bindings (the row pointers themselves
// are reused, but their obj_id and text changed — without a rebind the
// chain's notion of what's at each index is stale).
int g_lastSeenActiveListBoxSize = -1;

// Trade-outcome watcher state. Set when DispatchTradeAction queues an
// engine call; cleared on first size delta (success) or after a few
// ticks with no delta (failure). The mode at queue time decides which
// success/fail phrase fires — if the user toggles mode mid-watch, the
// arm is dropped silently so we don't speak a stale phrase.
bool     g_tradeWatchArmed          = false;
Mode     g_tradeWatchMode           = Mode::Unknown;
int      g_tradeWatchSizeAtArm      = -1;
int      g_tradeWatchTicksRemaining = 0;
// Price of the item at dispatch time. Reused for the success speech so
// the user hears "Verkauft für 16 Credits" instead of plain "Verkauft".
uint32_t g_tradeWatchPrice          = 0;

constexpr int kTradeWatchTicks = 4;  // ~64ms at 60fps — engine commits sync

uint32_t ReadStorePlayerGold(void* panel) {
    if (!panel) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(panel) + kStorePlayerGoldOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

}  // namespace

bool IsStorePanel(void* panel) {
    if (!panel) return false;
    void** vt = nullptr;
    __try {
        vt = *reinterpret_cast<void***>(panel);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiStore;
}

bool IsHiddenStoreListBox(void* panel, void* listBox) {
    if (!IsStorePanel(panel) || !listBox) return false;
    auto* p  = reinterpret_cast<unsigned char*>(panel);
    auto* lb = reinterpret_cast<unsigned char*>(listBox);
    ptrdiff_t off = lb - p;
    if (off != (ptrdiff_t)kStoreShopItemsListBoxOffset &&
        off != (ptrdiff_t)kStoreInvItemsListBoxOffset) {
        // Description listbox or something we don't expect — let the
        // chain handle it normally.
        return false;
    }
    uint32_t bits = ReadListBoxControlBitFlags(panel, (size_t)off);
    return (bits & kStoreListBoxVisibleBit) == 0;
}

void AnnounceChainStepSuffix(void* panel, void* control) {
    if (!IsStorePanel(panel)) return;
    if (!IsStoreItemEntry(control)) return;

    Mode mode = ResolveMode(panel);
    if (mode == Mode::Unknown) return;

    uint32_t handle = ReadRowObjId(control);
    void* item = ResolveItemFromHandle(handle);
    if (!item) {
        acclog::Write("Menus.Store",
                      "chain-step suffix focus=%p handle=0x%x: item resolve failed",
                      control, handle);
        return;
    }

    uintptr_t valueFn = (mode == Mode::Buy)
        ? kAddrCSWGuiStoreGetItemBuyValue
        : kAddrCSWGuiStoreGetItemSellValue;
    uint32_t price = CallGetItemValue(valueFn, panel, item);

    bool finite = true;
    int stock = ReadItemStock(item, finite);

    char msg[160];
    if (mode == Mode::Buy && !finite) {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(
                     acc::strings::Id::FmtStorePriceBuyUnlimited),
                 (int)price);
    } else if (mode == Mode::Buy) {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(
                     acc::strings::Id::FmtStorePriceBuyFinite),
                 (int)price, stock);
    } else {
        // Sell: stock = how many you own; always finite for player inv.
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(
                     acc::strings::Id::FmtStorePriceSell),
                 (int)price, stock);
    }

    tolk::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.Store",
                  "chain-step suffix focus=%p mode=%s price=%u stock=%d finite=%d",
                  control,
                  mode == Mode::Buy ? "buy" : "sell",
                  price, stock, finite ? 1 : 0);
}

void TickMonitorMode() {
    void* mgr = nullptr;
    __try {
        mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    if (!mgr) return;

    // Poll the StoreModeToggle hotkey (G) here so the gate is co-located
    // with the rest of the store state. Mirrors PollContainerGiveModeKey
    // in menus_listbox.cpp — engine eats unbound scancodes before the
    // menu dispatcher sees them, so we read the keyboard ourselves.
    if (acc::hotkeys::Pressed(acc::hotkeys::Action::StoreModeToggle)) {
        ToggleModeFromHotkey();
    }

    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!IsStorePanel(fg)) {
        // Drop tracking when the store leaves the foreground so a
        // re-open starts at Unknown and doesn't replay the prior mode.
        if (g_lastSeenStorePanel) {
            g_lastSeenStorePanel = nullptr;
            g_lastSeenMode = Mode::Unknown;
            g_lastSeenActiveListBoxSize = -1;
            g_tradeWatchArmed = false;
        }
        return;
    }

    Mode current = ResolveMode(fg);
    if (current == Mode::Unknown) return;

    bool firstSighting = (fg != g_lastSeenStorePanel);
    if (firstSighting) {
        // First sighting of this store panel — record without announcing.
        // The user just opened the store; the panel-title speech already
        // covered the initial mode.
        g_lastSeenStorePanel = fg;
        g_lastSeenMode = current;
        g_lastSeenActiveListBoxSize = ReadListBoxSize(fg,
            current == Mode::Buy
                ? kStoreShopItemsListBoxOffset
                : kStoreInvItemsListBoxOffset);
        g_tradeWatchArmed = false;
        return;
    }

    bool modeChanged = (current != g_lastSeenMode);
    if (modeChanged) {
        g_lastSeenMode = current;
        const char* word = acc::strings::Get(current == Mode::Buy
            ? acc::strings::Id::StoreModeBuy
            : acc::strings::Id::StoreModeSell);
        tolk::Speak(word, /*interrupt=*/false);
        acclog::Write("Menus.Store", "mode change panel=%p -> %s",
                      fg, current == Mode::Buy ? "buy" : "sell");

        // Force a chain rebind so the now-hidden listbox's rows drop out
        // and the now-visible listbox's rows take their place. The chain
        // is panel-keyed and the panel pointer doesn't change on a mode
        // flip, so without this the user would keep walking the previous
        // list's rows even though they're invisible and unactionable.
        acc::menus::chain::RebindChain(fg);
        // Anchor on the first item of the visible list. Mode flip is the
        // one rebind path that intentionally resets the cursor — Buy and
        // Sell lists hold different items so the saved index is
        // semantically meaningless.
        acc::menus::chain::g_chainIndex = 0;
        g_lastSeenActiveListBoxSize = ReadListBoxSize(fg,
            current == Mode::Buy
                ? kStoreShopItemsListBoxOffset
                : kStoreInvItemsListBoxOffset);
        // Drop any in-flight trade watch — the user toggled mode rather
        // than waiting for the trade outcome. Speaking "Verkauft" /
        // "Kann nicht…" after a mode flip would be incoherent.
        g_tradeWatchArmed = false;
        return;
    }

    // Same panel + same mode. Detect a trade completing by watching the
    // active listbox's controls.size — Populate*ListBox rebuilds the
    // list (preserving row pointers) on every sell / buy, and the size
    // changes by ±1 because:
    //   * Sell: inv loses the sold item, shop gains it.
    //   * Buy:  inv gains the bought item, shop loses it.
    // We watch the active listbox (the one the user can see and chain-
    // navigate). On any delta, rebind the chain (preserving the cursor
    // position) and resolve the trade watcher.
    int activeOffset = (current == Mode::Buy)
        ? (int)kStoreShopItemsListBoxOffset
        : (int)kStoreInvItemsListBoxOffset;
    int currentSize = ReadListBoxSize(fg, (size_t)activeOffset);
    if (currentSize < 0) return;

    if (currentSize != g_lastSeenActiveListBoxSize) {
        acclog::Write("Menus.Store",
                      "active list size %d -> %d (mode=%s); chain rebind preserving idx",
                      g_lastSeenActiveListBoxSize, currentSize,
                      current == Mode::Buy ? "buy" : "sell");
        g_lastSeenActiveListBoxSize = currentSize;
        // Keep the cursor on the same logical slot — the engine
        // repopulates rows in-place, so chain index N now holds whatever
        // shifted up to fill the sold/bought row's position.
        acc::menus::chain::RebindChainPreserveIndex(fg);

        if (g_tradeWatchArmed && g_tradeWatchMode == current) {
            char msg[96];
            snprintf(msg, sizeof(msg),
                     acc::strings::Get(current == Mode::Buy
                         ? acc::strings::Id::FmtStoreBoughtFor
                         : acc::strings::Id::FmtStoreSoldFor),
                     (int)g_tradeWatchPrice);
            tolk::Speak(msg, /*interrupt=*/false);
            acclog::Write("Menus.Store",
                          "trade success mode=%s (size delta %d, price=%u)",
                          current == Mode::Buy ? "buy" : "sell",
                          currentSize - g_tradeWatchSizeAtArm,
                          g_tradeWatchPrice);
        }
        g_tradeWatchArmed = false;
        return;
    }

    // No size delta. If a trade is being watched, count ticks; after the
    // window expires speak the "cannot" phrase. The engine commits
    // SellItem / BuyItem synchronously from OnControl*AButton — if no
    // delta has shown up after a few ticks, the engine refused (plot
    // item, no funds, equipped, infinite-stock store row, etc.).
    if (g_tradeWatchArmed && g_tradeWatchMode == current) {
        if (g_tradeWatchTicksRemaining > 0) {
            g_tradeWatchTicksRemaining--;
        } else {
            const char* word = acc::strings::Get(current == Mode::Buy
                ? acc::strings::Id::StoreCannotBuy
                : acc::strings::Id::StoreCannotSell);
            tolk::Speak(word, /*interrupt=*/false);
            acclog::Write("Menus.Store",
                          "trade refused mode=%s (no size delta after watch)",
                          current == Mode::Buy ? "buy" : "sell");
            g_tradeWatchArmed = false;
        }
    }
}

bool IsStoreItemRow(void* control) {
    return IsStoreItemEntry(control);
}

typedef void (__thiscall* PFN_StoreOnControlButton)(void* this_, void* param_1);

void DispatchTradeAction(void* panel, void* row) {
    if (!IsStorePanel(panel) || !IsStoreItemRow(row)) {
        acclog::Write("Menus.Store",
                      "DispatchTradeAction: not store/row (panel=%p row=%p) skip",
                      panel, row);
        return;
    }
    Mode mode = ResolveMode(panel);
    if (mode == Mode::Unknown) {
        acclog::Write("Menus.Store",
                      "DispatchTradeAction: unknown mode (panel=%p) skip",
                      panel);
        return;
    }

    // Resolve the item up front so we can both pre-check credits (buy
    // mode) and report the transaction price on success. The engine's
    // OnControl{Inv,Store}AButton does the same resolution internally;
    // doing it here means a single thiscall to GetItem{Buy,Sell}Value
    // gets us the number we need without re-entering the engine's
    // confirmation-vs-direct branching.
    uint32_t handle = ReadRowObjId(row);
    void* item = ResolveItemFromHandle(handle);
    uint32_t price = 0;
    if (item) {
        uintptr_t valueFn = (mode == Mode::Buy)
            ? kAddrCSWGuiStoreGetItemBuyValue
            : kAddrCSWGuiStoreGetItemSellValue;
        price = CallGetItemValue(valueFn, panel, item);
    }

    // Buy mode: pre-check player gold against price. If insufficient,
    // speak our localised line and skip the engine call entirely. The
    // engine's own path here pops a CGuiInGame::ShowExamineBox (strref
    // 0xa3de) which is a visual-only popup not in our chain — letting
    // it fire would leave a stranded modal the user can't easily
    // dismiss. Speaking + skipping is cleaner for keyboard play.
    if (mode == Mode::Buy && item) {
        uint32_t gold = ReadStorePlayerGold(panel);
        if (gold < price) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     acc::strings::Get(
                         acc::strings::Id::FmtStoreNotEnoughCredits),
                     (int)price, (int)gold);
            tolk::Speak(msg, /*interrupt=*/false);
            acclog::Write("Menus.Store",
                          "DispatchTradeAction buy refused (gold=%u price=%u) panel=%p",
                          gold, price, panel);
            // Drop any stale watcher so a previous trade's outcome
            // can't double up with this refusal.
            g_tradeWatchArmed = false;
            return;
        }
    }

    uintptr_t fnAddr = (mode == Mode::Buy)
        ? kAddrCSWGuiStoreOnControlStoreAButton
        : kAddrCSWGuiStoreOnControlInvAButton;

    // Arm the outcome watcher before invoking. TickMonitorMode reads
    // these fields each tick to decide whether to speak success or
    // failure. Doing this BEFORE the engine call lets a synchronous
    // commit (engine path that skips the confirmation MessageBox) clear
    // the watch on the very next tick — same window as deferred commits.
    int activeOffset = (mode == Mode::Buy)
        ? (int)kStoreShopItemsListBoxOffset
        : (int)kStoreInvItemsListBoxOffset;
    g_tradeWatchArmed          = true;
    g_tradeWatchMode           = mode;
    g_tradeWatchSizeAtArm      = ReadListBoxSize(panel, (size_t)activeOffset);
    g_tradeWatchTicksRemaining = kTradeWatchTicks;
    g_tradeWatchPrice          = price;

    __try {
        auto fn = reinterpret_cast<PFN_StoreOnControlButton>(fnAddr);
        acclog::Write("Menus.Store",
                      "DispatchTradeAction panel=%p row=%p mode=%s -> %s "
                      "(watch size=%d, ticks=%d, price=%u)",
                      panel, row,
                      mode == Mode::Buy ? "buy" : "sell",
                      mode == Mode::Buy
                          ? "OnControlStoreAButton" : "OnControlInvAButton",
                      g_tradeWatchSizeAtArm,
                      g_tradeWatchTicksRemaining,
                      price);
        fn(panel, row);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Menus.Store",
                      "DispatchTradeAction SEH fault panel=%p row=%p",
                      panel, row);
        g_tradeWatchArmed = false;
    }
}

bool ToggleModeFromHotkey() {
    void* mgr = nullptr;
    __try {
        mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!mgr) return false;
    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!IsStorePanel(fg)) return false;
    if (acc::menus::pending::IsPending()) {
        acclog::Write("Menus.Store",
                      "G (mode toggle) -- op already pending; ignoring");
        return false;
    }
    void* toggleBtn = reinterpret_cast<unsigned char*>(fg) +
                      kStoreToggleButtonOffset;
    acc::menus::pending::QueueActivate(toggleBtn);
    acclog::Write("Menus.Store",
                  "G (mode toggle) -> FireActivate toggle_button panel=%p target=%p",
                  fg, toggleBtn);
    return true;
}

bool CloseFromEsc() {
    void* mgr = nullptr;
    __try {
        mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!mgr) return false;
    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!IsStorePanel(fg)) return false;
    if (acc::menus::pending::IsPending()) {
        acclog::Write("Menus.Store",
                      "Esc (close) -- op already pending; ignoring");
        return false;
    }
    void* cancelBtn = reinterpret_cast<unsigned char*>(fg) +
                      kStoreCancelButtonOffset;
    acc::menus::pending::QueueActivate(cancelBtn);
    acclog::Write("Menus.Store",
                  "Esc (close) -> FireActivate cancel_button panel=%p target=%p",
                  fg, cancelBtn);
    return true;
}

}  // namespace acc::menus::store
