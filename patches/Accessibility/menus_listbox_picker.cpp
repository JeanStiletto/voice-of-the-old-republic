// Armed-picker state and per-tick monitors for the two "select a row, then
// commit it into a slot" panels: the equipment picker (equip.gui LB_ITEMS)
// and the workbench upgrade picker (upgrade.gui LB_ITEMS).
//
// Why these two are a pair, and why they are not in menus_listbox.cpp
// -------------------------------------------------------------------
// Thirteen panels are driven by the spec table in menus_listbox.cpp. Eleven
// of them are stateless: the spec matches a panel, arrows drive its listbox,
// Enter commits, done. These two are the exception — they carry a mode. A
// picker ARMS when the user activates a slot button, and while armed it
// steals the arrow keys away from the panel's own button chain; when it
// disarms, arrows go back to the chain. That armed flag, the panel pointer
// it is bound to, and the one-shot cursor-park latch are the only mutable
// state the whole listbox subsystem owns.
//
// State wants an owner, so it lives here with the two monitors that watch it
// per tick. The specs themselves stay in menus_listbox.cpp next to the other
// eleven — splitting two of thirteen specs out would have forced the private
// ListBoxPanelSpec struct into a shared header and left the dispatcher's
// probe table pointing at entries in another file. The spec callbacks reach
// this state through the accessors in menus_listbox.h instead, which is what
// menus.cpp already did from the outside.
//
// The cursor-park mechanism
// -------------------------
// Both pickers set useEngineSelect, i.e. arrows drive the engine's own
// CSWGuiListBox::SetSelectedControl rather than a raw selection_index write,
// so the user gets a real row highlight and native multipage scrolling. The
// catch is that the engine re-selects whatever row sits under the mouse on
// every frame (HandleMouseMove -> SetSelectedControl), which silently reverts
// our writes. Parking the OS cursor on the panel's BTN_BACK — clear of the
// list — makes that hover-select inert. The park is deferred to the monitor
// rather than done at arm time because MoveMouseToPosition recurses back
// through the hover pipeline and must stay off the input-dispatch stack.

#include <cstdio>

#include "menus_listbox.h"

#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "log.h"
#include "menus_extract.h"
#include "menus_internal.h"
#include "prism.h"
#include "strings.h"

using namespace acc::engine;  // PanelKind, FindPanelByKind, kAddr*/kMgr*

using acc::menus::detail::FindControlById;
using acc::menus::detail::GetControlCenter;

namespace acc::menus::listbox {

// ============================================================================
// Picker state. Both pickers follow the same shape: an armed flag, the panel
// pointer the arming was bound to (re-opening a panel allocates a fresh
// instance, so a pointer mismatch means "stale, disarm"), and a park-pending
// latch consumed once by the monitor.
// ============================================================================

namespace {
bool  s_equipPickerActive = false;
void* s_equipPickerPanel  = nullptr;
bool  s_equipParkPending  = false;

bool  s_workbenchUpgradePickerActive = false;
void* s_workbenchUpgradePickerPanel  = nullptr;
bool  s_workbenchUpgradeParkPending  = false;
}  // namespace

bool  IsEquipPickerArmed() { return s_equipPickerActive; }
void* EquipPickerPanel()   { return s_equipPickerPanel; }

void ArmEquipPicker(void* panel) {
    s_equipPickerActive = true;
    s_equipPickerPanel  = panel;
    s_equipParkPending  = true;
}

void DisarmEquipPicker() {
    s_equipPickerActive = false;
    s_equipPickerPanel  = nullptr;
    s_equipParkPending  = false;
}

bool  IsWorkbenchUpgradePickerArmed() { return s_workbenchUpgradePickerActive; }
void* WorkbenchUpgradePickerPanel()   { return s_workbenchUpgradePickerPanel; }

void ArmWorkbenchUpgradePicker(void* panel) {
    s_workbenchUpgradePickerActive = true;
    s_workbenchUpgradePickerPanel  = panel;
    s_workbenchUpgradeParkPending  = true;
}

void DisarmWorkbenchUpgradePicker() {
    s_workbenchUpgradePickerActive = false;
    s_workbenchUpgradePickerPanel  = nullptr;
    s_workbenchUpgradeParkPending  = false;
}

// ============================================================================
// Per-tick monitors.
// ============================================================================

namespace {

struct EquipSelState {
    void* listBox;
    short lastSelection;
};
EquipSelState s_equipSelState = { nullptr, -1 };

// Warp the OS cursor onto `panel`'s BTN_BACK so it sits OFF the LB_ITEMS list
// while a picker is armed — see the cursor-park note in the file header.
// BTN_BACK is a plain button (safe for MoveMouseToPosition's hover->active
// promotion, unlike a label) and a harmless parking spot: we never synthesise
// a click, and Enter/Esc are dispatched explicitly by the picker handlers
// regardless of hover.
//
// Returns true once the warp is issued (caller clears its park-pending latch).
bool ParkPickerCursorOffList(void* panel, int backBtnId, const char* tag) {
    if (!panel) return false;
    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) return false;
    void* backBtn = FindControlById(panel, backBtnId);
    if (!backBtn) return false;
    int cx = 0, cy = 0;
    if (!GetControlCenter(backBtn, cx, cy)) return false;
    auto move = reinterpret_cast<PFN_MoveMouseToPosition>(
        kAddrMoveMouseToPosition);
    move(gm, cx, cy);
    acclog::Write(tag, "park cursor off LB_ITEMS -> BTN_BACK id=%d at (%d,%d) "
                  "(neutralises hover-select)", backBtnId, cx, cy);
    return true;
}

// Equip picker: disarm when the panel leaves panels[], run the one-shot
// cursor park, and speak row changes. The announce lives here rather than in
// the spec's `announce` callback because the engine also moves the selection
// on its own (scroll, hover, the commit itself) — watching the selection
// index per tick catches every move, where the callback would only catch the
// ones we drove.
void MonitorEquipPickerSelection() {
    void* equipPanel = FindPanelByKind(PanelKind::InGameEquip);

    if (!equipPanel) {
        if (s_equipSelState.listBox) {
            acclog::Write("Menus.EquipPicker",
                          "monitor disarmed: no InGameEquip panel in stack");
            s_equipSelState.listBox       = nullptr;
            s_equipSelState.lastSelection = -1;
        }
        if (s_equipPickerActive) {
            acclog::Write("EquipPicker", "disarm - panel gone from panels[]");
            DisarmEquipPicker();
        }
        return;
    }

    void* lb = FindControlById(equipPanel, kEquipLbItemsId);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    // One-shot cursor park: once the picker is armed and its LB_ITEMS has been
    // populated (rowCount > 0), warp the cursor off the list so the engine's
    // per-frame hover-select stops fighting our SetSelectedControl writes.
    if (s_equipPickerActive && s_equipParkPending && rowCount > 0) {
        if (ParkPickerCursorOffList(equipPanel, kEquipBtnBackId, "EquipPicker")) {
            s_equipParkPending = false;
        }
    }

    short selIdx = *reinterpret_cast<short*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxSelectionIndexOffset);

    if (s_equipSelState.listBox != lb) {
        s_equipSelState.listBox       = lb;
        s_equipSelState.lastSelection = selIdx;
        acclog::Write("Menus.EquipPicker",
                      "monitor armed: panel=%p lb=%p rows=%d initialSel=%d",
                      equipPanel, lb, rowCount, selIdx);
        return;
    }

    if (selIdx == s_equipSelState.lastSelection) return;
    short prev = s_equipSelState.lastSelection;
    s_equipSelState.lastSelection = selIdx;

    if (selIdx < 0) {
        acclog::Write("Menus.EquipPicker", "selection cleared: lb=%p prev=%d",
                      lb, prev);
        return;
    }
    if (selIdx == 0) {
        acclog::Write("Menus.EquipPicker", "selection on protoitem (sel=0) lb=%p",
                      lb);
        return;
    }
    if (!lbList || !lbList->data || selIdx >= lbList->size) {
        acclog::Write("Menus.EquipPicker",
                      "selection out of range: lb=%p sel=%d size=%d",
                      lb, selIdx, lbList ? lbList->size : -1);
        return;
    }
    void* row = lbList->data[selIdx];
    if (!row) return;

    char rowText[256];
    const char* src = acc::menus::extract::FromControl(row, rowText,
                                                       sizeof(rowText));
    if (!src) {
        acclog::Write("Menus.EquipPicker", "row %d (lb=%p) no announceable text",
                      selIdx, lb);
        return;
    }

    // Row 0 is the protoitem template, hidden from nav (minSel=1), so the
    // user-visible position is selIdx as-is and the total is rowCount-1.
    int userPos   = selIdx;
    int userTotal = rowCount - 1;
    char msg[320];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtContainerItemAt),
             rowText, userPos, userTotal);
    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("Menus.EquipPicker", "row lb=%p sel=%d (was %d) text=\"%s\"",
                  lb, selIdx, prev, rowText);
}

// Disarms the workbench upgrade picker if the upgrade.gui panel is gone
// from CSWGuiManager.panels[]. Mirror of the EquipPicker disarm-on-panel-
// gone branch - resetStale only fires when the spec matches (i.e. the
// panel is still foreground), so a panel-pop between ticks would leave
// the picker stuck armed on the next reopen otherwise.
void MonitorWorkbenchUpgradePicker() {
    if (!s_workbenchUpgradePickerActive) return;

    if (!FindPanelByKind(PanelKind::WorkbenchUpgrade)) {
        acclog::Write("WorkbenchUpgrade", "disarm - panel gone from panels[]");
        DisarmWorkbenchUpgradePicker();
        return;
    }

    // TEMPORARY DIAGNOSTIC (lightsabercrystalcrash investigation): trace the
    // LB_ITEMS selection state every frame while the picker is armed, to find
    // what reverts our DriveListBoxSelection write between keypresses. The
    // crystal picker's selection never advances past row 2 in the field log —
    // every "Down" reads selection_index==1 again — so something resets it.
    // Trace folds a steady value to one line + "(repeated Nx more)"; only a
    // flip emits a fresh line, so this adds no spam. Read it against the
    // per-keypress "WorkbenchUpgrade: Down lb=.. sel=X->Y" logs:
    //   * trace shows our stepped value (e.g. 2) then a separate revert to 1
    //     => the engine reverts on a LATER frame (catchable, can re-assert).
    //   * trace NEVER shows 2, only ever 1, while the keypress log says 1->2
    //     => the revert happens within the same frame as our write (the engine
    //     re-selects synchronously; we'd need to write later in the tick).
    // rows/top/ipp included so a per-frame listbox REPOPULATION (rowCount or
    // row pointers churning) is visible too — that would reset selection as a
    // side effect. Remove once the mechanism is identified.
    void* lb = FindControlById(s_workbenchUpgradePickerPanel,
                               kWorkbenchUpgradeLbItemsId);
    if (lb) {
        auto* lbBase = reinterpret_cast<unsigned char*>(lb);
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            lbBase + kListBoxControlsOffset);
        int rowCount = (lbList && lbList->data) ? lbList->size : 0;
        short sel = *reinterpret_cast<short*>(
            lbBase + kListBoxSelectionIndexOffset);
        short top = *reinterpret_cast<short*>(
            lbBase + kListBoxTopVisibleIndexOffset);
        short ipp = *reinterpret_cast<short*>(
            lbBase + kListBoxItemsPerPageOffset);
        void* selRow = (sel >= 0 && sel < rowCount && lbList && lbList->data)
                           ? lbList->data[sel]
                           : nullptr;
        acclog::Trace("WorkbenchSel",
                      "lb=%p sel=%d top=%d ipp=%d rows=%d selRow=%p",
                      lb, sel, top, ipp, rowCount, selRow);

        // One-shot cursor park: once the compatible-mods list is populated
        // (rowCount > 0), warp the cursor off LB_ITEMS so the engine's
        // hover-select stops reverting our SetSelectedControl writes.
        if (s_workbenchUpgradeParkPending && rowCount > 0) {
            if (ParkPickerCursorOffList(s_workbenchUpgradePickerPanel,
                                        kWorkbenchUpgradeBtnBackId,
                                        "WorkbenchUpgrade")) {
                s_workbenchUpgradeParkPending = false;
            }
        }
    }
}

}  // namespace

void TickPickerMonitors() {
    MonitorEquipPickerSelection();
    MonitorWorkbenchUpgradePicker();
}

}  // namespace acc::menus::listbox
