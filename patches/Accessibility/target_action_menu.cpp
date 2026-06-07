#include "target_action_menu.h"

#include <cstdio>
#include <cstring>

#include "combat_diag.h"        // Probe — snapshot chain/overwrite state
                                 // around DispatchRowAction for Shift+1..3
#include "combat_queue.h"       // CountPlayerEntries — queue depth after
                                 // fire so the announce can report
                                 // "X, Platz N"
#include "menus_submenu.h"      // EnforceCombatHotkeyMutex (Shift+1..3 vs Shift+4..7)
#include "engine_actionbar.h"   // PrepareBareDispatch — refresh action_lists
                                 // against the narrated target before reading
#include "engine_input.h"       // kInputNavUp/Down, kInputEnter1/2, kInputEsc1/2
#include "engine_area.h"        // ResolveServerObjectHandle (for narrated handle)
#include "engine_panels.h"      // HasActiveDialogPanel — gate Open during
                                 // cinematic dialogs (same reason as actionbar_menu)
#include "engine_radial.h"
#include "engine_reads.h"       // ReadControlTooltip
#include "engine_subscreen.h"   // Begin/EndOverlayPause — freeze the world while open
#include "hotkeys.h"            // ShiftHeld
#include "log.h"
#include "menu_speak.h"
#include "narrated_target.h"
#include "strings.h"
#include "prism.h"

namespace acc::target_action_menu {

namespace {

// Per-row shadow index, persisted across submenu sessions and across the
// engine's PopulateMenus rebuilds. Engine's field1[target_type*3+row]
// stores an action_id which gets invalidated whenever PopulateMenus
// reassigns ids; we track the position-in-the-list and re-derive the
// action_id from the live descriptor on every stamp. Same shape as
// actionbar_menu's g_selectedIndex.
int g_selectedIndex[acc::engine_radial::kRowCount] = {0, 0, 0};

struct State {
    bool active = false;
    int  curRow = 0;
};
State g_state;

// Clamp persisted index to the live row count (which may have shrunk
// between sessions or after a target change). Returns the valid index.
int ClampIndex(void* tam, int row) {
    int n = acc::engine_radial::RowActionCount(tam, row);
    if (n <= 0) return 0;
    if (g_selectedIndex[row] < 0)  g_selectedIndex[row] = 0;
    if (g_selectedIndex[row] >= n) g_selectedIndex[row] = n - 1;
    return g_selectedIndex[row];
}

// Resolve the engine client-side handle for the currently narrated target.
// Returns kInvalidObjectId when nothing is narrated / the slot is a map
// pin / the handle no longer resolves to a live server object. Mirrors
// the same logic input_pipeline uses before firing PrepareBareDispatch
// on bare 1..3.
uint32_t ResolveNarratedClientHandle() {
    acc::narrated_target::Slot slot{};
    if (!acc::narrated_target::TryGet(slot)) return kInvalidObjectId;
    if (slot.isMapPin) return kInvalidObjectId;
    if (slot.handle == 0u || slot.handle == kInvalidObjectId) {
        return kInvalidObjectId;
    }
    void* obj = acc::engine::ResolveServerObjectHandle(slot.handle);
    if (!obj) return kInvalidObjectId;
    // Engine convention is client-side handles with the 0x80000000 high
    // bit set. narrated_target stores server-side; OR the bit in.
    return (slot.handle & 0x80000000u)
        ? slot.handle
        : (slot.handle | 0x80000000u);
}

void SpeakCurrentLabel(void* tam, int row) {
    char label[128] = "";
    acc::engine_radial::ReadRowActionLabel(tam, row, label, sizeof(label));
    acc::menu_speak::SpeakChoice("TargetMenu", label, "row=%d", row);
}

}  // namespace

int CurrentSelection(int row) {
    if (row < 0 || row >= acc::engine_radial::kRowCount) return 0;
    return g_selectedIndex[row];
}

bool Open(int row) {
    if (row < 0 || row >= acc::engine_radial::kRowCount) {
        acclog::Write("TargetMenu", "Open row=%d out of range; ignoring", row);
        return false;
    }

    // Refuse to arm during a cinematic dialog. Same rationale as
    // actionbar_menu::Open: arrow-key navigation reaches the engine via
    // DirectInput regardless of our gate, and the dialog's listbox treats
    // arrows as selection-cycle. The target-action menu isn't meaningful
    // during dialog anyway.
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write("TargetMenu", "Open row=%d — dialog panel in stack; not arming",
            row);
        return false;
    }

    // Refresh the engine's action_lists[row] against the narrated target.
    // Bare 1..3 gets this for free via input_pipeline's hook on the
    // engine's input-event path; Shift+1..3 isn't bound by the engine
    // so we have to fire the refresh ourselves before reading row state.
    uint32_t targetClient = ResolveNarratedClientHandle();
    (void)acc::engine_actionbar::PrepareBareDispatch(targetClient);

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("TargetMenu", "Open row=%d — TAM unresolved; not arming",
            row);
        return false;
    }

    int count = acc::engine_radial::RowActionCount(tam, row);
    if (count <= 0) {
        char fmt[128];
        std::snprintf(fmt, sizeof(fmt),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      row + 1);
        prism::Speak(fmt, /*interrupt=*/true);
        acclog::Write("TargetMenu", "Open row=%d count=0 (target=0x%08x) — empty, "
            "not arming",
            row, targetClient);
        return false;
    }

    acc::menus::submenu::EnforceCombatHotkeyMutex("target-menu-open");

    g_state.active = true;
    acc::engine::BeginOverlayPause();
    g_state.curRow = row;

    // Stamp field1 with the shadow index's action_id so ReadRowActionLabel
    // (which reads via FindSelectedActionDescriptor → field1 lookup) and
    // a subsequent Enter both fire the user's last-cycled variant rather
    // than action_lists[row].data[0]. On first-ever Open the shadow is 0
    // and field1 is whatever PopulateMenus left it at; both converge on
    // data[0] which is the right default.
    int idx = ClampIndex(tam, row);
    (void)acc::engine_radial::SelectActionInRow(tam, row, idx);

    char label[128] = "";
    acc::engine_radial::ReadRowActionLabel(tam, row, label, sizeof(label));

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtActionBarOpened),
                  row + 1, label[0] ? label : "?", count);
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("TargetMenu", "ARMED row=%d count=%d idx=%d label=[%s] "
        "target=0x%08x",
        row, count, idx, label, targetClient);
    return true;
}

bool IsActive() {
    return g_state.active;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;
    if (value == 0) return false;

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("TargetMenu", "HandleInputEvent — TAM unresolved on "
            "key=%d; force-disarming",
            code);
        ForceDisarm("tam-unresolved");
        return false;
    }

    int row   = g_state.curRow;
    int count = acc::engine_radial::RowActionCount(tam, row);

    switch (code) {
        case kInputNavUp:
        case kInputNavDown: {
            // Shift+arrow: speak the currently-selected descriptor's
            // full description (item / force power / feat) without
            // cycling. Mirrors actionbar_menu's Shift+arrow contract —
            // dispatch by action_id high-nibble tag via
            // ResolveActionDescriptionFromActionId; plain verbs
            // (attack actions, door open/unlock, computer hack, etc.)
            // fall back to the localised "Keine Beschreibung verfügbar".
            if (acc::hotkeys::ShiftHeld()) {
                uint32_t actionId =
                    acc::engine_radial::ReadSelectedRowActionId(tam, row);
                char text[8192];
                if (actionId &&
                    acc::engine::ResolveActionDescriptionFromActionId(
                        actionId, text, sizeof(text)))
                {
                    prism::Speak(text, /*interrupt=*/true);
                    acclog::Write("TargetMenu",
                        "Shift+%s row=%d action_id=0x%x desc=\"%s\"",
                        code == kInputNavDown ? "Down" : "Up",
                        row, actionId, text);
                } else {
                    const char* msg = acc::strings::Get(
                        acc::strings::Id::NoTooltipAvailable);
                    prism::Speak(msg, /*interrupt=*/true);
                    acclog::Write("TargetMenu",
                        "Shift+%s row=%d action_id=0x%x no desc; "
                        "spoke fallback",
                        code == kInputNavDown ? "Down" : "Up", row, actionId);
                }
                return true;
            }
            if (count <= 1) {
                acclog::Write("TargetMenu", "%s row=%d count=%d — nothing to cycle",
                    code == kInputNavDown ? "NavDown" : "NavUp",
                    row, count);
                SpeakCurrentLabel(tam, row);
                return true;
            }
            // Drive the shadow index then stamp the matching action_id
            // into field1[]. Mirrors actionbar_menu's pattern exactly —
            // shadow is the local source of truth (survives PopulateMenus
            // re-assignment of action_ids), engine's field1 is updated on
            // every cycle so DoTargetAction reads the right entry. Up =
            // next, Down = previous; same direction as Shift+4..7. Clamps
            // at the ends rather than wrapping (matches actionbar_menu).
            int prevIdx = g_selectedIndex[row];
            if (code == kInputNavUp) {
                if (g_selectedIndex[row] + 1 < count) {
                    g_selectedIndex[row] += 1;
                }
            } else {
                if (g_selectedIndex[row] > 0) {
                    g_selectedIndex[row] -= 1;
                }
            }
            bool ok = acc::engine_radial::SelectActionInRow(
                tam, row, g_selectedIndex[row]);
            acclog::Write("TargetMenu", "%s row=%d count=%d idx %d -> %d ok=%d",
                code == kInputNavUp ? "NavUp" : "NavDown",
                row, count, prevIdx, g_selectedIndex[row], ok ? 1 : 0);
            SpeakCurrentLabel(tam, row);
            return true;
        }
        case kInputEnter1:
        case kInputEnter2: {
            // Re-stamp on the fire path: handles the no-cycle Open→Enter
            // case where the user immediately confirmed the pre-armed
            // variant. Without this, a prior session's stale field1 (or
            // a PopulateMenus reset between Open and Enter) could land
            // on the wrong descriptor.
            int idx = ClampIndex(tam, row);
            (void)acc::engine_radial::SelectActionInRow(tam, row, idx);

            char label[128] = "";
            acc::engine_radial::ReadRowActionLabel(
                tam, row, label, sizeof(label));

            char diag_label[24];
            std::snprintf(diag_label, sizeof(diag_label),
                          "shift+%d-enter", row + 1);  // row 0→1 .. row 2→3
            acc::combat::queue::ReportPrePressDepth();
            acc::combat_diag::LogPreFire(diag_label);
            bool ok = acc::engine_radial::DispatchRowAction(tam, row);
            acc::combat_diag::LogPostFire(diag_label);

            // PRE-only slot calc — same rationale as actionbar_menu.
            int preDepth = acc::combat::queue::GetPrePressDepth();
            const bool capHit  = (preDepth >= 4);
            const int  slotNum = preDepth + 1;
            char msg[192];
            if (capHit) {
                std::snprintf(msg, sizeof(msg),
                              acc::strings::Get(
                                  acc::strings::Id::FmtFireQueueFull),
                              label[0] ? label : "?");
            } else {
                std::snprintf(msg, sizeof(msg),
                              acc::strings::Get(
                                  acc::strings::Id::FmtFireAtPosition),
                              label[0] ? label : "?", slotNum);
            }
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("TargetMenu", "ENTER row=%d label=[%s] ok=%d "
                "pre=%d slot=%d capHit=%d -> [%s]",
                row, label, ok ? 1 : 0, preDepth, slotNum,
                capHit ? 1 : 0, msg);

            ForceDisarm("enter");
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            const char* msg = acc::strings::Get(
                acc::strings::Id::ActionBarCancelled);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("TargetMenu", "ESC row=%d -> [%s]", row, msg);
            ForceDisarm("esc");
            return true;
        }
        default:
            return false;
    }
}

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("TargetMenu", "disarm — reason=%s", reason ? reason : "?");
    g_state.active = false;
    acc::engine::EndOverlayPause();
    g_state.curRow = 0;
}

}  // namespace acc::target_action_menu
