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
#include "engine_reads.h"     // ResolveItemFromClientHandle — row -> CSWSItem*
#include "log.h"
#include "menus_chain.h"      // RebindChainPreserveIndex on list changes
#include "menus_pending.h"    // QueueActivate — auto-open redirect (BTN_BACK)
#include "menus_extract.h"    // FromControl — title label read
#include "hotkeys.h"          // Pressed() for Q/E (CraftViewToggle)
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
constexpr int kUpgradeBackBtnId    = 13;  // upgrade_p BTN_BACK ("Abbrechen")
constexpr int kCraftShopListId     = 4;   // LB_SHOPITEMS (craftable list)
constexpr int kCraftInvListId      = 5;   // LB_INVITEMS (breakdown list)
constexpr int kCraftAcceptBtnId    = 12;  // BTN_Accept ("Objekt erstellen")
// BTN_Examine ("Inventar betrachten") — the create/breakdown view flip. The
// two .gui files disagree on its id: component_p numbers it 13 and puts
// BTN_Cancel at 27, chemical_p has BTN_Cancel at 13 and Examine at 14. Firing
// id 13 blind on a lab station would close the screen instead of flipping it.
constexpr int kCraftExamineBtnComponentId = 13;
constexpr int kCraftExamineBtnChemicalId  = 14;
// The screen's own per-selection number labels, LBL_COST_VALUE (id 8) and
// LBL_STOCK_VALUE (id 11), are deliberately NOT read. Live evidence
// (patch-20260813-132834.log): they held "12" / "0" across every keyboard
// selection change and only moved when a craft committed — the engine
// refreshes them from the row's own mouse-enter event, which the keyboard
// path never fires. Quoting them per row would have announced a stale price.
// AnnounceChainStepSuffix calls the engine's cost function on the focused
// row's item instead, which cannot go stale. The pool label (id 6) IS
// reliable — it is written whenever the pool changes — and is surfaced as a
// virtual chain row by menus_credits.
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

bool IsWorkbenchFamilyKind(PanelKind k) {
    return k == PanelKind::WorkbenchSelect || k == PanelKind::WorkbenchItems ||
           k == PanelKind::WorkbenchUpgrade || IsCraftingKind(k);
}

// The K2 workbench dialog reply pushes CSWGuiUpgrade (the slot detail for
// the FIRST upgradeable item) on TOP of the item list, so the user lands
// inside "Aufwerten: <first item>" and has to Escape to reach the list —
// the engine's own flow, but the wrong entry point for keyboard users, who
// want the list first (reported 2026-08-12; verified engine-driven, no mod
// action precedes the push in patch-20260812-144927.log).
//
// We can't stop the push, so we bounce it: when CSWGuiUpgrade arrives in the
// foreground and the user did NOT open it themselves (no craft-row-commit
// from the list just fired), fire its BTN_BACK to drop to the list. This
// counter is set when the chain routes Enter on a list row to the upgrade
// flow; it survives a few ticks (the push lands a frame or two later) and is
// consumed by the arriving upgrade edge.
int  g_userOpenedUpgradeTtl = 0;
// One redirect per workbench-open. Reset when the whole family leaves the
// foreground, so the next visit redirects again but a within-visit reopen
// (user Enters another item) never does.
bool g_redirectedThisOpen = false;

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
void AnnounceFamilyTitleOnFgEdge(void* fg, PanelKind pk, bool edge,
                                 bool suppress) {
    static bool s_retryTitle = false;

    int labelId = TitleLabelIdFor(pk);
    if (labelId < 0 || !fg) {
        s_retryTitle = false;
        return;
    }
    if (suppress) {
        // We're bouncing this panel back to the list; don't announce its
        // title, but clear any pending retry so the list's title speaks
        // cleanly on the next edge.
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

// The engine's own selected-row index for a listbox, -1 on fault.
int ReadListBoxSelection(void* listBox) {
    if (!listBox) return -1;
    __try {
        return *reinterpret_cast<short*>(
            reinterpret_cast<unsigned char*>(listBox) +
            kListBoxSelectionIndexOffset);
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

// Make `row` the engine's selected row of `listBox`: what a mouse click on
// the row leaves behind, minus the click.
//
// Both halves are load-bearing, and the second one is what the screen was
// missing (2026-08-13, kotor2 Ghidra):
//
//   * selection_index — BTN_Accept's handler (component 0x008D2150 /
//     chemical 0x008D7A80) fetches the row to act on with
//     CSWGuiListBox::GetSelectedRow (0x0041FFB0), which is literally
//     `GetRow(this->selection_index)`; GetRow (0x0041FF70) indexes the same
//     controls array RowIndexIn scans, so our index IS the engine's index.
//   * is_active — every one of the four worker functions the Accept handler
//     dispatches into (component create 0x008D4AC0 / breakdown 0x008D4680,
//     chemical create 0x008D98F0 / breakdown 0x008D95E0) opens with
//     `if (row->is_active == 0) return;` and returns SILENTLY. Normally the
//     listbox raises it by firing the row's HandleInputEvent(0) when the
//     mouse enters it; our keyboard path never generates that event (the
//     cursor warp's hit-test does not land on these rows), so the flag stayed
//     zero, Accept fired, and the engine declined without a sound. That is
//     the whole "nothing can be crafted" bug.
//
// Returns false only if the selection write itself faulted.
bool SelectRow(void* listBox, void* row, int index) {
    if (!WriteListBoxSelection(listBox, index)) return false;
    __try {
        acc::menus::pending::RaiseIsActiveIfZero(row);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// The CSWSItem* behind a crafting row, or null. Same two hops the engine's
// own create/breakdown workers take on the row's object id at +0x1d0.
void* ResolveRowItem(void* row) {
    if (!row) return nullptr;
    uint32_t handle = 0;
    __try {
        handle = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(row) +
            kStoreItemEntryObjIdOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    if (!handle) return nullptr;
    return acc::engine::ResolveItemFromClientHandle(handle);
}

// Can the character actually make (or break down) this row? Reads the
// engine's own gate; true on fault, so a bad read never invents a refusal.
bool IsRowCraftable(void* row) {
    if (!row || !acc::off::Ok(kCraftRowCraftableFlagOffset)) return true;
    __try {
        return *reinterpret_cast<int*>(
            reinterpret_cast<unsigned char*>(row) +
            kCraftRowCraftableFlagOffset) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}

// Price quote from the engine's own cost function. The two crafting screens
// use different ones (see kAddrCraftComponentCost). 0 on fault.
typedef unsigned int (__thiscall* PFN_CraftCost)(void* this_, void* item);

unsigned int CallCraftCost(uintptr_t fnAddr, void* panel, void* item) {
    if (!fnAddr || !panel || !item) return 0;
    __try {
        auto fn = reinterpret_cast<PFN_CraftCost>(fnAddr);
        return fn(panel, item);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// The panel's ctor-cached skill factor. 0.0f on fault or a nonsense value —
// callers stay silent rather than quote a yield they can't trust.
float ReadSkillFactor(void* panel, bool medical) {
    size_t off = medical ? kCraftChemicalSkillFactorOffset
                         : kCraftComponentSkillFactorOffset;
    if (!acc::off::Ok(off)) return 0.0f;
    float v = 0.0f;
    __try {
        v = *reinterpret_cast<float*>(
            reinterpret_cast<unsigned char*>(panel) + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0.0f;
    }
    if (!(v > 0.0f) || v > 1.0f) return 0.0f;
    return v;
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
    if (!SelectRow(listBox, row, idx)) {
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
    // The commit button gates the same way its row does; every other
    // hand-fired dispatch in the mod raises both (see EquipCommit /
    // WorkbenchUpgradeCommit in menus_pending.cpp).
    __try {
        acc::menus::pending::RaiseIsActiveIfZero(btn);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    bool ok = FireActivate(btn);
    acclog::Write("Crafting",
                  "row-commit panel=%p lb=%p row=%p idx=%d btn(id=%d)=%p fired=%d",
                  panel, listBox, row, idx, buttonId, btn, ok ? 1 : 0);
    // Opening the upgrade flow from the list is user-initiated — mark it so
    // the arriving CSWGuiUpgrade edge is NOT bounced back to the list (see
    // the auto-open redirect in Tick). ~20 ticks covers the engine's
    // one-to-two-frame delay before the panel actually fronts.
    if (ok && buttonId == kSelUpgradeItemsBtn) {
        g_userOpenedUpgradeTtl = 20;
    }
}

void AnnounceChainStepSuffix(void* panel, void* control) {
    if (!panel || !control || !acc::game::IsKotor2()) return;
    PanelKind pk = IdentifyPanel(panel);
    if (!IsCraftingKind(pk)) return;

    // Only rows of the list the user can act on. Buttons and the resource
    // row fall out here.
    void* lb = FindActiveCraftList(panel);
    if (!lb || RowIndexIn(lb, control) < 0) return;

    void* item = ResolveRowItem(control);
    if (!item) {
        acclog::Write("Crafting",
                      "chain-step suffix row=%p: item resolve failed", control);
        return;
    }

    bool medical = (pk == PanelKind::WorkbenchCreateMedical);
    unsigned int cost = CallCraftCost(
        medical ? kAddrCraftChemicalCost : kAddrCraftComponentCost,
        panel, item);
    if (cost == 0) return;  // fault, or an item the engine prices at nothing

    // Which list is showing decides which number is the useful one: what it
    // costs to build, or what breaking it down hands back. They are not the
    // same — the return is the cost scaled by the bench skill, min 1, which
    // is why a low-skill character loses material on every teardown.
    bool breakdown = (FindControlById(panel, kCraftShopListId) != lb);
    char msg[96];
    if (breakdown) {
        float factor = ReadSkillFactor(panel, medical);
        if (factor <= 0.0f) return;
        int yield = (int)((float)cost * factor);
        if (yield < 1) yield = 1;
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtCraftYield), yield);
    } else {
        snprintf(msg, sizeof(msg),
                 acc::strings::Get(acc::strings::Id::FmtCraftCost), (int)cost);
    }
    // The list shows recipes up to eight skill ranks above the character, so
    // "you can't make this one yet" is ordinary and has to be audible — the
    // engine answers a press on such a row with a message box and nothing
    // else. Same wording the mod already uses for unavailable buttons.
    bool craftable = IsRowCraftable(control);
    if (!craftable) {
        size_t used = strlen(msg);
        snprintf(msg + used, sizeof(msg) - used, "%s",
                 acc::strings::Get(acc::strings::Id::DisabledSuffix));
    }
    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Crafting",
                  "chain-step suffix row=%p item=%p %s cost=%u craftable=%d "
                  "-> \"%s\"",
                  control, item, breakdown ? "breakdown" : "create", cost,
                  craftable ? 1 : 0, msg);
}

void* ViewToggleButton(void* panel) {
    if (!panel || !acc::game::IsKotor2()) return nullptr;
    PanelKind pk = IdentifyPanel(panel);
    if (pk == PanelKind::WorkbenchCreateItem) {
        return FindControlById(panel, kCraftExamineBtnComponentId);
    }
    if (pk == PanelKind::WorkbenchCreateMedical) {
        return FindControlById(panel, kCraftExamineBtnChemicalId);
    }
    return nullptr;
}

void* CommitButton(void* panel) {
    if (!panel || !acc::game::IsKotor2()) return nullptr;
    if (!IsCraftingKind(IdentifyPanel(panel))) return nullptr;
    return FindControlById(panel, kCraftAcceptBtnId);
}

// Q / E — flip create <-> break down. Mirrors store::ToggleModeFromHotkey:
// the button is the engine's own, we just fire it from a key instead of
// leaving it in the chain. The resulting view flip is announced by Tick's
// existing shopVis edge, so nothing is spoken here.
bool ToggleViewFromHotkey() {
    void* mgr = nullptr;
    __try {
        mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    if (!mgr) return false;
    void* fg = acc::engine::GetForegroundPanel(mgr);
    void* btn = ViewToggleButton(fg);
    if (!btn) return false;
    if (acc::menus::pending::IsPending()) {
        acclog::Write("Crafting",
                      "Q/E (view toggle) -- op already pending; ignoring");
        return false;
    }
    acc::menus::pending::QueueActivate(btn);
    acclog::Write("Crafting",
                  "Q/E (view toggle) -> FireActivate BTN_Examine panel=%p "
                  "target=%p", fg, btn);
    return true;
}

void SyncSelectedRowFromChainFocus() {
    if (!acc::game::IsKotor2()) return;
    void* panel = acc::menus::chain::g_chainPanel;
    if (!panel || !IsCraftingKind(IdentifyPanel(panel))) return;
    if (acc::menus::chain::g_chainIndex < 0 ||
        acc::menus::chain::g_chainIndex >= acc::menus::chain::g_chainCount) {
        return;
    }
    void* focused =
        acc::menus::chain::g_chain[acc::menus::chain::g_chainIndex].control;
    void* lb = FindActiveCraftList(panel);
    if (!lb || !focused) return;
    int idx = RowIndexIn(lb, focused);
    if (idx < 0) return;  // focus is on a button / category, not an item row

    static void* s_lastRow = nullptr;
    if (!SelectRow(lb, focused, idx)) return;
    if (focused != s_lastRow) {
        s_lastRow = focused;
        acclog::Write("Crafting",
                      "chain focus -> engine selection panel=%p lb=%p row=%p "
                      "idx=%d", panel, lb, focused, idx);
    }
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

    // Foreground-edge tracker shared by the title announcer and the
    // auto-open redirect (single s_lastFg so both see the same edge).
    static void* s_lastFg = nullptr;

    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    void* fg = mgr ? acc::engine::GetForegroundPanel(mgr) : nullptr;
    PanelKind pk = fg ? IdentifyPanel(fg) : PanelKind::Unknown;

    // Q/E view toggle. Polled here for the same reason the store polls its
    // own: the engine eats these scancodes before the menu dispatcher sees
    // them, so the keyboard has to be read directly. ToggleViewFromHotkey
    // re-checks the foreground itself, which is what keeps this key from
    // colliding with the container and store handlers on the same keys.
    if (acc::hotkeys::Pressed(acc::hotkeys::Action::CraftViewToggle)) {
        ToggleViewFromHotkey();
    }

    bool edge = fg != s_lastFg;
    s_lastFg = fg;

    if (acc::game::IsKotor2()) {
        if (g_userOpenedUpgradeTtl > 0) --g_userOpenedUpgradeTtl;
        // Reset the once-per-open redirect latch when the whole workbench
        // family has left the foreground.
        if (!IsWorkbenchFamilyKind(pk)) g_redirectedThisOpen = false;

        // Redirect the engine's auto-open of the first item's upgrade screen
        // back to the item list, unless the user opened it themselves.
        bool suppressTitle = false;
        if (edge && pk == PanelKind::WorkbenchUpgrade &&
            !g_redirectedThisOpen && g_userOpenedUpgradeTtl == 0) {
            void* back = FindControlById(fg, kUpgradeBackBtnId);
            if (back && !acc::menus::pending::IsPending()) {
                acc::menus::pending::QueueActivate(back);
                g_redirectedThisOpen = true;
                suppressTitle = true;
                acclog::Write("Crafting",
                              "engine auto-opened Upgrade for first item; "
                              "redirecting to item list (BTN_BACK id=%d panel=%p)",
                              kUpgradeBackBtnId, fg);
            }
        }
        if (edge && pk == PanelKind::WorkbenchUpgrade && g_userOpenedUpgradeTtl > 0) {
            // User drove this open from the list — consume the marker and
            // latch so nothing bounces it.
            g_userOpenedUpgradeTtl = 0;
            g_redirectedThisOpen = true;
        }
        AnnounceFamilyTitleOnFgEdge(fg, pk, edge, suppressTitle);
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
