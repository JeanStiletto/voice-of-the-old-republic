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
// The armed flag is the ENGINE's, not ours
// ----------------------------------------
// Both panels already carry a "picker open" bit that the engine raises when it
// opens the item zone and clears when it closes it — kEquipPickerOpenFlagOff
// and kUpgradePickerOpenFlagOff. Crucially, on the equip screen the same call
// that raises the bit (ShowDescription(1)) is what clears the interactive bit
// on all nine slot buttons and labels, and the same call that clears it
// (CloseDescription → ShowDescription(0)) is what puts them back. So the bit is
// not merely a good proxy for "the picker is up" — it is the exact predicate
// that decides whether the slots underneath are usable.
//
// This file used to keep its own bool alongside it, raised at keypress time and
// cleared at keypress time, while the engine's moved a tick later when the
// queued op drained. Every gap between the two produced a wrong answer, and one
// of them was permanent: Escape cleared OUR flag and never told the engine, so
// the engine's bit stayed set, the slot buttons stayed disabled, and every slot
// announced "unavailable" for the rest of the panel's life (K2
// patch-20260817-065149.log, K1 patch-20260813-204335.log). The commit path had
// the same bug in miniature — one tick of "unavailable" between our disarm and
// OnOKPressed draining.
//
// So: read the bit, never copy it. The only local state left is an ARM LATCH
// per picker, covering the one tick between queueing the engine's open call and
// the engine raising its own bit; it is self-limiting (the monitor drops it as
// soon as the bit appears, or as soon as the queued op has drained without one,
// which is also what happens when the slot had no items and the engine popped a
// message box instead). Plus the one-shot cursor park, which is an action to
// perform rather than a state to agree about.
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
#include "menus_pending.h"   // IsPending — bounds the arm latch to the queued open
#include "msg_router.h"      // preview-feedback suppression rule
#include "prism.h"
#include "strings.h"

using namespace acc::engine;  // PanelKind, FindPanelByKind, kAddr*/kMgr*

using acc::menus::detail::FindControlById;
using acc::menus::detail::GetControlCenter;

namespace acc::menus::listbox {

// ============================================================================
// Picker state. Both pickers follow the same shape: the engine's own
// "picker open" bit answers IsArmed, and the only thing we keep is the arm
// latch that covers the queued-open window plus the one-shot cursor park.
// See the "The armed flag is the ENGINE's" note in the file header.
// ============================================================================

namespace {
void* s_equipArmPending  = nullptr;  // panel we asked to open, until its bit shows
void* s_equipArmSlotBtn  = nullptr;  // the slot button that Enter activated
bool  s_equipParkPending = false;

void* s_workbenchUpgradeArmPending  = nullptr;
bool  s_workbenchUpgradeParkPending = false;

// Bit 0 of a panel's picker-open field. Under SEH like every engine read here:
// an unported offset on the other game would otherwise fault on a live panel
// pointer rather than simply answering "not armed".
bool ReadPickerOpenBit(void* panel, size_t fieldOff) {
    if (!panel) return false;
    __try {
        uint32_t flag = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(panel) + fieldOff);
        return (flag & 1u) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
}  // namespace

// Both IsArmed queries are PURE — they are called from the input dispatcher,
// the extractor's disabled-suffix path and the msg-router rule, several times
// per frame, and a query that mutated the latch would make the answer depend on
// who asked first. The monitors below own every latch transition.
bool IsEquipPickerArmed() {
    void* panel = FindPanelByKind(PanelKind::InGameEquip);
    if (!panel) return false;
    if (ReadPickerOpenBit(panel, kEquipPickerOpenFlagOff)) return true;
    return s_equipArmPending == panel;
}

void* EquipPickerPanel() {
    return IsEquipPickerArmed() ? FindPanelByKind(PanelKind::InGameEquip)
                                : nullptr;
}

void ArmEquipPicker(void* panel, void* slotBtn) {
    s_equipArmPending  = panel;
    s_equipArmSlotBtn  = slotBtn;
    s_equipParkPending = true;
}

// "We are done driving this picker." Deliberately does NOT clear the engine's
// bit — the engine clears it itself when the commit or cancel we queued reaches
// CloseDescription, and the slots are genuinely still disabled until then, so
// the suffix suppression and the input routing must both stay on for that tick.
void ClearEquipPickerArmLatch() {
    s_equipArmPending  = nullptr;
    s_equipArmSlotBtn  = nullptr;
    s_equipParkPending = false;
}

bool IsWorkbenchUpgradePickerArmed() {
    void* panel = FindPanelByKind(PanelKind::WorkbenchUpgrade);
    if (!panel) return false;
    if (ReadPickerOpenBit(panel, kUpgradePickerOpenFlagOff)) return true;
    return s_workbenchUpgradeArmPending == panel;
}

void* WorkbenchUpgradePickerPanel() {
    return IsWorkbenchUpgradePickerArmed()
               ? FindPanelByKind(PanelKind::WorkbenchUpgrade)
               : nullptr;
}

void ArmWorkbenchUpgradePicker(void* panel) {
    s_workbenchUpgradeArmPending  = panel;
    s_workbenchUpgradeParkPending = true;
}

void ClearWorkbenchUpgradeArmLatch() {
    s_workbenchUpgradeArmPending  = nullptr;
    s_workbenchUpgradeParkPending = false;
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

// Warp the OS cursor onto the panel's BTN_BACK so it sits OFF the LB_ITEMS
// list while a picker is armed — see the cursor-park note in the file
// header. BTN_BACK is a plain button (safe for MoveMouseToPosition's
// hover->active promotion, unlike a label) and a harmless parking spot: we
// never synthesise a click, and Enter/Esc are dispatched explicitly by the
// picker handlers regardless of hover.
//
// The caller resolves `backBtn` — both pickers pass the engine's
// ctor-bound member (a variant .gui renumbers the id and the old id-based
// resolve here parked on a LABEL instead, userlogs/077noequipment).
//
// Returns true once the warp is issued (caller clears its park-pending latch).
bool ParkPickerCursorOffList(void* panel, void* backBtn, const char* tag) {
    if (!panel || !backBtn) return false;
    void* gm = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!gm) return false;
    int cx = 0, cy = 0;
    if (!GetControlCenter(backBtn, cx, cy)) return false;
    auto move = reinterpret_cast<PFN_MoveMouseToPosition>(
        kAddrMoveMouseToPosition);
    move(gm, cx, cy);
    acclog::Write(tag, "park cursor off LB_ITEMS -> BTN_BACK %p at (%d,%d) "
                  "(neutralises hover-select)", backBtn, cx, cy);
    return true;
}

// Raw read of the equip panel's picker-open field for the diagnostic trace.
// Split out of the monitor so that function stays free of SEH (MSVC C2712 — see
// the CLAUDE.md note on __try and unwinding).
void TraceEquipPickerOpenField(void* panel, bool armed) {
    __try {
        uint32_t raw = *reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(panel) + kEquipPickerOpenFlagOff);
        acclog::Trace("EquipPickerBit",
                      "panel=%p raw=0x%08x open=%d latch=%p armed=%d",
                      panel, raw, (raw & 1u) ? 1 : 0, s_equipArmPending,
                      armed ? 1 : 0);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Trace("EquipPickerBit", "panel=%p read faulted", panel);
    }
}

// The engine refused to open the picker — its "no items for this slot"
// modal is what the user just heard (the ContentChange monitor speaks it).
// When the refused slot in fact HOLDS an item, follow up by naming it:
// KOTOR 2's OnSelectSlot refuses whenever inventory has no OTHER fitting
// item — the equipped one doesn't count (a per-slot no-candidates flag,
// checked before it ever looks at the item list) — and without this line
// the modal sounds like the equipped item vanished or the mod broke
// (userlogs 077noequipment: pistol equipped, "no items that can be
// equipped in this slot"). Silent when the slot is genuinely empty; the
// engine modal alone is accurate there. KOTOR 1 never reaches this with
// an occupied slot (its OnSelectSlot counts the equipped row), so the
// item-name gate also keeps it K1-inert.
void SpeakStillEquippedAfterRefusal(void* panel, void* slotBtn) {
    if (!panel || !slotBtn) return;
    char itemName[128];
    if (!acc::menus::extract::ReadEquipSlotItemName(panel, slotBtn,
                                                    itemName,
                                                    sizeof(itemName)) ||
        itemName[0] == '\0') {
        return;
    }
    char msg[192];
    snprintf(msg, sizeof(msg),
             acc::strings::Get(acc::strings::Id::FmtEquipStillEquipped),
             itemName);
    prism::Speak(msg, /*interrupt=*/false);
    acclog::Write("EquipPicker",
                  "refused open follow-up: slot item=\"%s\" still equipped "
                  "(panel=%p slot=%p)", itemName, panel, slotBtn);
}

// Equip picker: retire the arm latch, run the one-shot cursor park, and speak
// row changes. The announce lives here rather than in the spec's `announce`
// callback because the engine also moves the selection on its own (scroll,
// hover, the commit itself) — watching the selection index per tick catches
// every move, where the callback would only catch the ones we drove.
void MonitorEquipPickerSelection() {
    void* equipPanel = FindPanelByKind(PanelKind::InGameEquip);

    if (!equipPanel) {
        if (s_equipSelState.listBox) {
            acclog::Write("Menus.EquipPicker",
                          "monitor disarmed: no InGameEquip panel in stack");
            s_equipSelState.listBox       = nullptr;
            s_equipSelState.lastSelection = -1;
        }
        if (s_equipArmPending) {
            acclog::Write("EquipPicker", "arm latch dropped - panel gone from "
                          "panels[]");
            ClearEquipPickerArmLatch();
        }
        return;
    }

    // Retire the arm latch. Either the engine has taken over (its bit is up and
    // the latch is redundant), or our queued OnSelectSlot has already drained
    // without raising it — which is the "slot had no fitting items, engine
    // popped a message box instead" path. Bounding the latch on the queue is
    // what stops a refused open from leaving the arrows hijacked forever.
    if (s_equipArmPending) {
        bool engineOpen = ReadPickerOpenBit(equipPanel,
                                            kEquipPickerOpenFlagOff);
        if (engineOpen || !acc::menus::pending::IsPending()) {
            acclog::Write("EquipPicker", "arm latch retired (engineOpen=%d)",
                          engineOpen ? 1 : 0);
            if (!engineOpen) {
                SpeakStillEquippedAfterRefusal(equipPanel, s_equipArmSlotBtn);
            }
            s_equipArmPending = nullptr;
            s_equipArmSlotBtn = nullptr;
        }
    }

    bool armed = IsEquipPickerArmed();

    // The whole subsystem now hangs off kEquipPickerOpenFlagOff, and its KOTOR 2
    // column is second-hand (read out of that game's OnOKPressed during the port,
    // not decompiled here). A wrong offset fails LOUDLY in one direction and
    // silently in the other: bit 0 stuck high would leave the arrows hijacked and
    // the "unavailable" suffix suppressed forever on the equipment screen. Trace
    // folds a steady value to one line, so this costs nothing while the state is
    // still, and the first test round shows the field flipping 0 -> 1 on slot
    // Enter and 1 -> 0 on Escape/commit if the offset is right. Drop it once a
    // KOTOR 2 log has confirmed both edges.
    TraceEquipPickerOpenField(equipPanel, armed);

    void* lb = acc::menus::detail::EquipPanelItemsListBox(equipPanel);
    if (!lb) return;

    auto* lbList = reinterpret_cast<CExoArrayList*>(
        reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
    int rowCount = (lbList && lbList->data) ? lbList->size : 0;

    // One-shot cursor park: once the picker is armed and its LB_ITEMS has been
    // populated (rowCount > 0), warp the cursor off the list so the engine's
    // per-frame hover-select stops fighting our SetSelectedControl writes.
    if (armed && s_equipParkPending && rowCount > 0) {
        if (ParkPickerCursorOffList(
                equipPanel,
                acc::menus::detail::EquipPanelBackButton(equipPanel),
                "EquipPicker")) {
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

    // Track the move either way, but only SPEAK it while the picker is up. The
    // engine moves this selection when it isn't: the cancel path's
    // CloseDescription re-runs OnEnterSlot, which rebuilds LB_ITEMS from
    // scratch, and announcing that rebuild would read an item row out at the
    // exact moment the user backed out to the slot buttons.
    if (!armed) {
        acclog::Write("Menus.EquipPicker",
                      "selection moved while picker closed: lb=%p %d -> %d "
                      "(not announced)", lb, prev, selIdx);
        return;
    }

    if (selIdx < 0) {
        acclog::Write("Menus.EquipPicker", "selection cleared: lb=%p prev=%d",
                      lb, prev);
        return;
    }
    if (selIdx == 0) {
        acclog::Write("Menus.EquipPicker", "selection on unequip entry (sel=0) "
                      "lb=%p", lb);
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

    // Row 0 is the engine's "empty" entry (OnEnterSlot builds it first, with
    // item id 0x7f000000 and SetCanUse(0) — the unequip target, NOT a protoitem
    // template as this comment used to claim). It stays hidden from nav
    // (minSel=1) on purpose: unequipping is Enter on the row that is already
    // worn, which EquipPickerOnEnter routes to this row. So the user-visible
    // position is selIdx as-is and the total is rowCount-1.
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

// Workbench upgrade picker: retire the arm latch and run the one-shot cursor
// park. Mirror of the equip monitor above. The panel-gone branch matters
// because the spec's callbacks only run while the panel is foreground, so a
// panel-pop between ticks has no other place to drop the latch.
void MonitorWorkbenchUpgradePicker() {
    void* upgradePanel = FindPanelByKind(PanelKind::WorkbenchUpgrade);
    if (!upgradePanel) {
        if (s_workbenchUpgradeArmPending) {
            acclog::Write("WorkbenchUpgrade", "arm latch dropped - panel gone "
                          "from panels[]");
            ClearWorkbenchUpgradeArmLatch();
        }
        return;
    }

    // Same latch retirement as the equip monitor above: hand over to the
    // engine's bit as soon as it appears, and give up on the latch once the
    // queued OnSlotSelected has drained without one.
    if (s_workbenchUpgradeArmPending) {
        bool engineOpen = ReadPickerOpenBit(upgradePanel,
                                            kUpgradePickerOpenFlagOff);
        if (engineOpen || !acc::menus::pending::IsPending()) {
            acclog::Write("WorkbenchUpgrade",
                          "arm latch retired (engineOpen=%d)",
                          engineOpen ? 1 : 0);
            s_workbenchUpgradeArmPending = nullptr;
        }
    }

    if (!IsWorkbenchUpgradePickerArmed()) return;

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
    void* lb = acc::menus::detail::UpgradePanelItemsListBox(upgradePanel);
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
        // Corner park, NOT the BTN_BACK center: on this panel the K1
        // hit-test resolves warped coordinates offset from the extents
        // (patch-20260818-103126.log: every slot warp's mouseOver landed on
        // a different control, and a BTN_BACK park left the engine
        // hovering crystal ROW 2 — each keypress was hover-reverted there,
        // so of 8 crystals only the two rows adjacent to it were ever
        // reachable). The top-left corner is empty on every panel at any
        // resolution — same fix the dialogue-reply picker ships.
        if (s_workbenchUpgradeParkPending && rowCount > 0) {
            if (acc::menus::detail::ParkCursorToCorner("WorkbenchUpgrade")) {
                s_workbenchUpgradeParkPending = false;
            }
        }
    }
}

// ----------------------------------------------------------------------------
// Message-buffer rule: mute the engine's feedback while the equip picker is up.
//
// Browsing the equip list EQUIPS as it goes. CSWGuiInGameEquip::OnItemSelected
// is the row's SELECT handler, not a commit button — it writes the description
// AND calls EquipItem/UnequipItem straight away, remembering the displaced item
// so OnOKPressed can keep it or the cancel path can put it back. That is the
// engine's live preview, and it is exactly what mouse users get; our arrow keys
// reach it through the same CSWGuiListBox::SetSelectedControl.
//
// The side effect is that each arrow press makes the engine append its ordinary
// inventory feedback ("Gegenstand entfernt." / "Item Removed.") to the in-game
// message buffer, where the router's raw-speech fallback reads it out. Stepping
// through five items narrated five removals of things the player never removed
// (K2 patch-20260817-065149.log, K1 patch-20260813-204335.log).
//
// So claim those lines for as long as the picker owns the screen. The picker
// already announces the focused row, and the preview is not a completed action
// — the user hears the real outcome when Enter commits or Escape reverts.
// Deliberately equip-only: the workbench picker previews nothing, so its
// feedback lines always describe a real install and must stay audible.
bool RuleSuppressEquipPreviewFeedback(const char* text) {
    if (!IsEquipPickerArmed()) return false;
    acclog::Write("Menus.EquipPicker", "preview feedback suppressed: [%.200s]",
                  text ? text : "");
    return true;  // claimed — no raw speech
}

}  // namespace

void RegisterPickerMsgRules() {
    acc::msg::Router::Instance().AddRule("EquipPickerPreview",
                                         RuleSuppressEquipPreviewFeedback);
}

void TickPickerMonitors() {
    MonitorEquipPickerSelection();
    MonitorWorkbenchUpgradePicker();
}

}  // namespace acc::menus::listbox
