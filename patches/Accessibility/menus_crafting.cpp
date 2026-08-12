// KOTOR 2 workbench / crafting item-list screens.
// See menus_crafting.h for the design rationale + decompile evidence.

#include <windows.h>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "menus_crafting.h"

#include "engine_game.h"      // IsKotor2 — WorkbenchSelect row commit is K2-only
#include "engine_manager.h"   // GetForegroundPanel, kAddrGuiManagerPtr
#include "engine_offsets.h"
#include "engine_panels.h"    // IdentifyPanel, PanelKind
#include "log.h"
#include "menus_chain.h"      // RebindChainPreserveIndex on list changes
#include "menus_extract.h"    // FromControl — title label read
#include "menus_internal.h"   // detail::FindControlById
#include "strings.h"
#include "prism.h"

using acc::engine::IdentifyPanel;
using acc::engine::PanelKind;
using acc::menus::detail::FindControlById;

namespace acc::menus::crafting {

namespace {

// .gui control ids. Baked into the resources, language-independent.
// Mined from K2's own gui.bif (upgradesel_p / component_p / chemical_p —
// the two crafting screens agree on every id we use).
constexpr int kSelUpgradeListId    = 9;   // upgradesel_p LB_UPGRADELIST
constexpr int kSelUpgradeItemsBtn  = 5;   // upgradesel_p BTN_UPGRADEITEMS
constexpr int kSelTitleId          = 4;   // upgradesel_p LBL_TITLE
constexpr int kItemsTitleId        = 3;   // upgradeitems_p LBL_TITLE
constexpr int kUpgradeTitleId      = 12;  // upgrade_p LBL_TITLE (item name)
constexpr int kCraftShopListId     = 4;   // LB_SHOPITEMS (craftable list)
constexpr int kCraftInvListId      = 5;   // LB_INVITEMS (breakdown list)
constexpr int kCraftAcceptBtnId    = 12;  // BTN_Accept ("Objekt erstellen")
constexpr int kCreateItemTitleId   = 19;  // component_p LBL_TITLE
constexpr int kCreateMedTitleId    = 15;  // chemical_p LBL_TITLE

// upgrade_p.gui bakes this literal (not a strref) into LBL_TITLE; the
// engine overwrites it with the item's name when it primes the panel.
// Locale-independent, so equality is a safe "not primed yet" test.
constexpr const char* kUpgradeTitlePlaceholder = "Item Name";

constexpr int kVtableHandleInputEvent = 15;
typedef void (__thiscall* PFN_ControlHandleInputEvent)(void* this_, int code,
                                                       int state);

bool IsCraftingKind(PanelKind k) {
    return k == PanelKind::WorkbenchCreateItem ||
           k == PanelKind::WorkbenchCreateMedical;
}

// The K2 workbench family whose titles Tick's foreground-edge announcer
// owns (see OwnsPanelTitle in the header). Returns the .gui id of the
// panel's title label, or -1 for non-family kinds.
int TitleLabelIdFor(PanelKind k) {
    switch (k) {
        case PanelKind::WorkbenchSelect:        return kSelTitleId;
        case PanelKind::WorkbenchItems:         return kItemsTitleId;
        case PanelKind::WorkbenchUpgrade:       return kUpgradeTitleId;
        case PanelKind::WorkbenchCreateItem:    return kCreateItemTitleId;
        case PanelKind::WorkbenchCreateMedical: return kCreateMedTitleId;
        default:                                return -1;
    }
}

// Speak the family panel's title when it ARRIVES in the foreground.
// Replaces pointer-keyed first-sight for these panels on K2: the stacked
// push spoke four construction-time titles in a row, and the pre-built
// panels are reused so re-entries stayed silent. The upgrade screen
// formats its title as "Aufwerten: <item name>" and retries while the
// engine hasn't yet replaced the .gui placeholder.
void AnnounceFamilyTitleOnFgEdge(void* fg, PanelKind pk) {
    static void* s_lastFg = nullptr;
    static bool  s_retryTitle = false;

    bool edge = fg != s_lastFg;
    s_lastFg = fg;

    int labelId = TitleLabelIdFor(pk);
    if (labelId < 0 || !fg) {
        s_retryTitle = false;
        return;
    }
    if (!edge && !s_retryTitle) return;

    void* label = FindControlById(fg, labelId);
    char text[256];
    if (!label ||
        !acc::menus::extract::FromControl(label, text, sizeof(text), fg) ||
        text[0] == '\0') {
        // Label empty or unreadable — try again next tick (same shape as
        // the placeholder retry; gives the engine a frame to populate).
        s_retryTitle = true;
        return;
    }

    if (pk == PanelKind::WorkbenchUpgrade) {
        if (strcmp(text, kUpgradeTitlePlaceholder) == 0) {
            s_retryTitle = true;
            return;
        }
        char msg[320];
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtWorkbenchUpgradeTitle),
                 text);
        prism::Speak(msg, /*interrupt=*/false);
        acclog::Write("Crafting", "fg-edge title panel=%p kind=%d -> \"%s\"",
                      fg, (int)pk, msg);
    } else {
        prism::Speak(text, /*interrupt=*/false);
        acclog::Write("Crafting", "fg-edge title panel=%p kind=%d -> \"%s\"",
                      fg, (int)pk, text);
    }
    s_retryTitle = false;
}

// CSWGuiControl.bit_flags read, SEH-guarded. 0 on fault — callers treat
// that as "not visible / not usable", never the wrong direction.
uint32_t ReadControlBitFlags(void* control) {
    if (!control) return 0;
    __try {
        return *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(control) + kControlBitFlagsOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

bool IsListBoxVisible(void* listBox) {
    return (ReadControlBitFlags(listBox) & kStoreListBoxVisibleBit) != 0;
}

// The item list the user can currently act on: LB_SHOPITEMS in create
// view, LB_INVITEMS in breakdown view. Null when neither is visible
// (construction window) or the panel isn't a crafting screen.
void* FindActiveCraftList(void* panel) {
    void* shop = FindControlById(panel, kCraftShopListId);
    if (shop && IsListBoxVisible(shop)) return shop;
    void* inv = FindControlById(panel, kCraftInvListId);
    if (inv && IsListBoxVisible(inv)) return inv;
    return nullptr;
}

// Index of `row` in the listbox's controls array, or -1. SEH-guarded
// pointer-equality scan — no reads through the rows themselves.
int RowIndexIn(void* listBox, void* row) {
    if (!listBox || !row) return -1;
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
        if (!list->data || list->size <= 0) return -1;
        int n = list->size > 4096 ? 4096 : list->size;
        for (int i = 0; i < n; ++i) {
            if (list->data[i] == row) return i;
        }
        return -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Row count of a listbox, -1 on fault.
int ReadListBoxRowCount(void* listBox) {
    if (!listBox) return -1;
    __try {
        auto* list = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(listBox) + kListBoxControlsOffset);
        int size = list->size;
        if (size < 0 || size > 4096) return -1;
        return size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

bool WriteListBoxSelection(void* listBox, int index) {
    if (!listBox || index < 0) return false;
    __try {
        *reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(listBox) +
            kListBoxSelectionIndexOffset) = (short)index;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool FireActivate(void* control) {
    if (!control) return false;
    __try {
        void** vt = *reinterpret_cast<void***>(control);
        auto fn = reinterpret_cast<PFN_ControlHandleInputEvent>(
            vt[kVtableHandleInputEvent]);
        fn(control, 0x27, 1);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

bool ResolveRowCommit(void* panel, void* control,
                      void** outListBox, int* outButtonId) {
    if (!panel || !control || !outListBox || !outButtonId) return false;
    PanelKind pk = IdentifyPanel(panel);
    void* lb = nullptr;
    int   btnId = -1;
    if (pk == PanelKind::WorkbenchSelect && acc::game::IsKotor2()) {
        // K1's upgradesel has no listbox at all — this path is K2 shape.
        lb = FindControlById(panel, kSelUpgradeListId);
        btnId = kSelUpgradeItemsBtn;
    } else if (IsCraftingKind(pk)) {
        lb = FindActiveCraftList(panel);
        btnId = kCraftAcceptBtnId;
    } else {
        return false;
    }
    if (!lb || RowIndexIn(lb, control) < 0) return false;
    *outListBox = lb;
    *outButtonId = btnId;
    return true;
}

void DispatchRowCommit(void* panel, void* listBox, void* row, int buttonId) {
    // Re-verify at drain time — a tick has passed since the input event
    // and the engine may have repopulated the list (category switch racing
    // the Enter). A vanished row is a logged no-op, never a stale commit.
    int idx = RowIndexIn(listBox, row);
    if (idx < 0) {
        acclog::Write("Crafting",
                      "row-commit dropped: row=%p no longer in lb=%p (panel=%p)",
                      row, listBox, panel);
        return;
    }
    if (!WriteListBoxSelection(listBox, idx)) {
        acclog::Write("Crafting",
                      "row-commit dropped: selection write failed lb=%p idx=%d",
                      listBox, idx);
        return;
    }
    void* btn = FindControlById(panel, buttonId);
    if (!btn) {
        acclog::Write("Crafting",
                      "row-commit: commit button id=%d not found on panel=%p",
                      buttonId, panel);
        return;
    }
    bool ok = FireActivate(btn);
    acclog::Write("Crafting",
                  "row-commit panel=%p lb=%p row=%p idx=%d btn(id=%d)=%p fired=%d",
                  panel, listBox, row, idx, buttonId, btn, ok ? 1 : 0);
}

bool IsHiddenCraftingListBox(void* panel, void* listBox) {
    if (!panel || !listBox) return false;
    if (!IsCraftingKind(IdentifyPanel(panel))) return false;
    if (listBox != FindControlById(panel, kCraftShopListId) &&
        listBox != FindControlById(panel, kCraftInvListId)) {
        // Description listboxes etc. — chain handles them normally.
        return false;
    }
    return !IsListBoxVisible(listBox);
}

bool OwnsPanelTitle(void* panel) {
    if (!acc::game::IsKotor2()) return false;
    return TitleLabelIdFor(IdentifyPanel(panel)) >= 0;
}

void Tick() {
    // Latched foreground state. Panel pointer distinguishes re-opens (a
    // fresh open must not announce the initial view as a "flip").
    static void* s_lastPanel   = nullptr;
    static int   s_lastShopVis = -1;   // 1 = create view, 0 = breakdown view
    static int   s_lastCount   = -1;   // active-list row count

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    void* fg = mgr ? acc::engine::GetForegroundPanel(mgr) : nullptr;
    PanelKind pk = fg ? IdentifyPanel(fg) : PanelKind::Unknown;

    if (acc::game::IsKotor2()) {
        AnnounceFamilyTitleOnFgEdge(fg, pk);
    }

    bool isCraft = IsCraftingKind(pk);
    bool isSel = pk == PanelKind::WorkbenchSelect && acc::game::IsKotor2();
    if (!isCraft && !isSel) {
        s_lastPanel = nullptr;
        return;
    }

    void* lb = nullptr;
    int shopVis = -1;
    if (isCraft) {
        void* shop = FindControlById(fg, kCraftShopListId);
        shopVis = (shop && IsListBoxVisible(shop)) ? 1 : 0;
        lb = FindActiveCraftList(fg);
    } else {
        lb = FindControlById(fg, kSelUpgradeListId);
    }
    int count = ReadListBoxRowCount(lb);

    if (fg != s_lastPanel) {
        // Fresh open (or panel swap): latch silently. The first-sight
        // title + focused-control announce cover orientation.
        s_lastPanel   = fg;
        s_lastShopVis = shopVis;
        s_lastCount   = count;
        return;
    }

    bool rebind = false;
    if (isCraft && shopVis != -1 && s_lastShopVis != -1 &&
        shopVis != s_lastShopVis) {
        // BTN_Examine (or the engine itself) flipped create ↔ breakdown.
        prism::Speak(acc::strings::Get(
                         shopVis ? acc::strings::Id::WorkbenchModeCreate
                                 : acc::strings::Id::WorkbenchModeBreakdown),
                     /*interrupt=*/false);
        acclog::Write("Crafting", "view flip panel=%p shopVis=%d -> rebind",
                      fg, shopVis);
        rebind = true;
    } else if (count != s_lastCount) {
        // Category filter switch or a create/breakdown changed the list.
        acclog::Write("Crafting",
                      "list count %d -> %d panel=%p lb=%p -> rebind",
                      s_lastCount, count, fg, lb);
        rebind = true;
    }
    if (rebind) {
        acc::menus::chain::RebindChainPreserveIndex(fg);
    }

    s_lastShopVis = shopVis;
    s_lastCount   = count;
}

}  // namespace acc::menus::crafting
