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
#include "log.h"
#include "menus_chain.h"      // RebindChain on mode flip
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

// Resolve a row's client-side obj_id to a CSWSItem* via the engine's
// ClientToServerObjectId + GetItemByGameObjectID. SEH-protected at each
// thiscall hop; nullptr on any miss/fault.
typedef uint32_t (__thiscall* PFN_ClientToServer)(void* this_, uint32_t handle);
typedef void*    (__thiscall* PFN_GetItemByHandle)(void* this_, uint32_t handle);

void* ResolveItemFromHandle(uint32_t clientHandle) {
    if (clientHandle == 0 || clientHandle == 0xffffffff) return nullptr;
    void* appMgr = nullptr;
    __try {
        appMgr = *reinterpret_cast<void**>(kAddrAppManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!appMgr) return nullptr;
    void* serverApp = nullptr;
    __try {
        serverApp = *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(appMgr) +
            kAppManagerServerExoAppOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!serverApp) return nullptr;

    uint32_t serverHandle = 0;
    __try {
        auto fn = reinterpret_cast<PFN_ClientToServer>(
            kAddrServerExoAppClientToServerObjectId);
        serverHandle = fn(serverApp, clientHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (serverHandle == 0 || serverHandle == 0xffffffff) return nullptr;

    void* item = nullptr;
    __try {
        auto fn = reinterpret_cast<PFN_GetItemByHandle>(
            kAddrServerExoAppGetItemByGameObjectID);
        item = fn(serverApp, serverHandle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return item;
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

    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!IsStorePanel(fg)) {
        // Drop tracking when the store leaves the foreground so a
        // re-open starts at Unknown and doesn't replay the prior mode.
        if (g_lastSeenStorePanel) {
            g_lastSeenStorePanel = nullptr;
            g_lastSeenMode = Mode::Unknown;
        }
        return;
    }

    Mode current = ResolveMode(fg);
    if (current == Mode::Unknown) return;

    if (fg != g_lastSeenStorePanel) {
        // First sighting of this store panel — record without announcing.
        // The user just opened the store; the panel-title speech already
        // covered the initial mode.
        g_lastSeenStorePanel = fg;
        g_lastSeenMode = current;
        return;
    }
    if (current == g_lastSeenMode) return;

    g_lastSeenMode = current;
    const char* word = acc::strings::Get(current == Mode::Buy
        ? acc::strings::Id::StoreModeBuy
        : acc::strings::Id::StoreModeSell);
    tolk::Speak(word, /*interrupt=*/false);
    acclog::Write("Menus.Store", "mode change panel=%p -> %s",
                  fg, current == Mode::Buy ? "buy" : "sell");

    // Force a chain rebind so the now-hidden listbox's rows drop out and
    // the now-visible listbox's rows take their place. The chain is
    // panel-keyed and the panel pointer doesn't change on a mode flip, so
    // without this the user would keep walking the previous list's rows
    // even though they're invisible and unactionable.
    acc::menus::chain::RebindChain(fg);
    // Anchor on the first item of the visible list (chain index 0 is the
    // first row by visual y after the y-sort; the three action buttons
    // sit at the bottom of the panel so they don't fight for index 0).
    // Doesn't speak — the user will press Up/Down next, which announces.
    acc::menus::chain::g_chainIndex = 0;
}

}  // namespace acc::menus::store
