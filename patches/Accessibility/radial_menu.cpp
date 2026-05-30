#include "radial_menu.h"

#include <cstdio>
#include <cstring>

#include "engine_input.h"     // kInputNavUp/Down/Left/Right, kInputEnter1/2,
                              // kInputEsc1/2
#include "engine_radial.h"
#include "engine_reads.h"    // ReadControlTooltip for Shift+arrow tooltip
#include "hotkeys.h"         // ShiftHeld
#include "log.h"
#include "menu_speak.h"
#include "strings.h"
#include "prism.h"

namespace acc::radial_menu {

namespace {

// Module-local state. Single-threaded harness — no locking.
struct State {
    bool        active     = false;
    int         curRow     = 0;
    char        target[64] = "";  // for re-announce on row switch
};

State g_state;

// Diagnostic schedule — when > 0, Tick() emits a LogStateWide on each
// call until the counter reaches 0. Tag is copied at schedule time.
struct DiagSchedule {
    int  framesRemaining = 0;
    char tag[32] = "";
    int  ordinal = 0;       // increments each fire so the log shows
                            // "frame 1/3", "frame 2/3", ...
    int  totalFrames = 0;
};
DiagSchedule g_diag;

// Find the next/prev row whose action_lists[row].size > 0. Returns -1 if
// no row has actions (caller treats as "menu empty"). `start` is exclusive
// — the search begins one step in `dir` direction. Wraps at the
// boundaries (0..kRowCount-1). Used by Up/Down to skip the empty rows
// PopulateMenus left blank for the current target type.
int FindPopulatedRow(void* tam, int start, int dir) {
    if (!tam) return -1;
    const int N = acc::engine_radial::kRowCount;
    for (int step = 1; step <= N; ++step) {
        int r = ((start + dir * step) % N + N) % N;
        if (acc::engine_radial::RowActionCount(tam, r) > 0) return r;
    }
    return -1;
}

// Resolve the first row that has any actions, treating row 0 as the
// preferred starting position. Returns -1 if none — caller declines to
// arm.
int FirstPopulatedRow(void* tam) {
    if (!tam) return -1;
    const int N = acc::engine_radial::kRowCount;
    for (int r = 0; r < N; ++r) {
        if (acc::engine_radial::RowActionCount(tam, r) > 0) return r;
    }
    return -1;
}

// Speak "Aktion {i} von {N}: {label}" for the current row's selected
// action. `prefix` is prepended unchanged (e.g. the target name on the
// initial open) — pass nullptr for plain row-only announcements.
void SpeakRowAction(void* tam, int row, const char* prefix) {
    char label[128] = "";
    acc::engine_radial::ReadRowActionLabel(tam, row, label, sizeof(label));

    int total = 0;
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        if (acc::engine_radial::RowActionCount(tam, r) > 0) ++total;
    }
    int rowOrdinal = 0;
    for (int r = 0; r <= row; ++r) {
        if (acc::engine_radial::RowActionCount(tam, r) > 0) ++rowOrdinal;
    }

    char msg[256];
    if (prefix && prefix[0] != '\0') {
        std::snprintf(msg, sizeof(msg), "%s. %s %d/%d: %s",
                      prefix, "Aktion", rowOrdinal, total,
                      label[0] ? label : "?");
    } else {
        std::snprintf(msg, sizeof(msg), "%s %d/%d: %s",
                      "Aktion", rowOrdinal, total, label[0] ? label : "?");
    }
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("Radial", "speak row=%d ordinal=%d/%d label=[%s]",
                  row, rowOrdinal, total, label);
}

// Speak the current row's action label *only*, used after Left/Right
// cycling within a row (the row didn't change, so the row-counter would
// be redundant noise).
void SpeakCurrentLabel(void* tam, int row) {
    char label[128] = "";
    acc::engine_radial::ReadRowActionLabel(tam, row, label, sizeof(label));
    acc::menu_speak::SpeakChoice("Radial", label, "label row=%d", row);
}

}  // namespace

bool ArmAfterPopulate(const char* targetName) {
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("Radial", "ArmAfterPopulate — TAM unresolved; not arming");
        return false;
    }

    // Diagnostic dump — fires every time, regardless of whether we end up
    // arming. Lets us correlate "Picker says radial_opened=1 / count=0"
    // against what's actually in the TAM (action_lists data/size, per-row
    // is_action flag, gui_string text). Drop after the radial path is
    // verified working end-to-end.
    acc::engine_radial::LogState(tam, "post-populate");

    int firstRow = FirstPopulatedRow(tam);
    if (firstRow < 0) {
        acclog::Write("Radial", "ArmAfterPopulate — no rows populated "
                      "(action_lists size all=0); not arming");
        return false;
    }

    g_state.active = true;
    g_state.curRow = firstRow;
    if (targetName && targetName[0]) {
        std::snprintf(g_state.target, sizeof(g_state.target), "%s", targetName);
    } else {
        g_state.target[0] = '\0';
    }

    char engineTargetName[64] = "";
    acc::engine_radial::ReadTargetName(tam, engineTargetName,
                                       sizeof(engineTargetName));

    // Build "Aktionsmenü, <name>" prefix using the existing radial-pre-roll
    // string, then append the row+action announce.
    char prefix[128];
    std::snprintf(prefix, sizeof(prefix),
                  acc::strings::Get(acc::strings::Id::FmtInteractRadial),
                  g_state.target);

    acclog::Write("Radial", "ARMED target=[%s] engineName=[%s] row=%d "
        "counts={r0=%d, r1=%d, r2=%d}",
        g_state.target, engineTargetName, firstRow,
        acc::engine_radial::RowActionCount(tam, 0),
        acc::engine_radial::RowActionCount(tam, 1),
        acc::engine_radial::RowActionCount(tam, 2));

    SpeakRowAction(tam, firstRow, prefix);
    return true;
}

bool IsActive() {
    return g_state.active;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;

    // Release edges always pass through. Consume on press only.
    if (value == 0) {
        // For consumed-on-press keys we still need to swallow the up edge
        // so the engine doesn't see only the release. Mirror menus.cpp's
        // chain-Enter behavior: don't consume key-up — letting it pass
        // doesn't double-trigger anything.
        return false;
    }

    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        acclog::Write("Radial", "HandleInputEvent — TAM unresolved on key=%d; "
            "force-disarming",
            code);
        ForceDisarm("tam-unresolved");
        return false;
    }

    // Shift+arrow on any nav key: speak the currently-selected
    // descriptor's full description (item / force power / feat) and do
    // NOT cycle. Mirrors actionbar_menu's Shift+arrow contract. Dispatch
    // by action_id high-nibble tag — see
    // ResolveActionDescriptionFromActionId. Plain attack verbs / door
    // open / unlock etc. fall back to the localised "Keine Beschreibung
    // verfügbar" cue.
    if ((code == kInputNavUp   || code == kInputNavDown ||
         code == kInputNavLeft || code == kInputNavRight) &&
        acc::hotkeys::ShiftHeld())
    {
        uint32_t actionId = acc::engine_radial::ReadSelectedRowActionId(
            tam, g_state.curRow);
        char text[8192];
        if (actionId &&
            acc::engine::ResolveActionDescriptionFromActionId(
                actionId, text, sizeof(text)))
        {
            prism::Speak(text, /*interrupt=*/true);
            acclog::Write("Radial",
                "Shift+nav row=%d action_id=0x%x desc=\"%s\"",
                g_state.curRow, actionId, text);
        } else {
            const char* msg = acc::strings::Get(
                acc::strings::Id::NoTooltipAvailable);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("Radial",
                "Shift+nav row=%d action_id=0x%x no desc; spoke fallback",
                g_state.curRow, actionId);
        }
        return true;
    }

    switch (code) {
        case kInputNavUp:
        case kInputNavDown: {
            int dir = (code == kInputNavDown) ? +1 : -1;
            int next = FindPopulatedRow(tam, g_state.curRow, dir);
            if (next < 0 || next == g_state.curRow) {
                acclog::Write("Radial", "%s — no other populated row; ignoring",
                    code == kInputNavDown ? "NavDown" : "NavUp");
                return true;
            }
            int prev = g_state.curRow;
            g_state.curRow = next;
            acclog::Write("Radial", "row %s %d -> %d",
                          dir > 0 ? "down" : "up", prev, next);
            SpeakRowAction(tam, next, /*prefix=*/nullptr);
            return true;
        }
        case kInputNavLeft:
        case kInputNavRight: {
            // Cycle the action *within* the current row using the engine's
            // own primitive. The engine's SelectNext/Prev mutates
            // target_actions[row].action_button.text in-place; reading it
            // back gives us the new label.
            int count = acc::engine_radial::RowActionCount(tam, g_state.curRow);
            if (count <= 1) {
                acclog::Write("Radial", "%s row=%d count=%d — nothing to cycle",
                    code == kInputNavRight ? "NavRight" : "NavLeft",
                    g_state.curRow, count);
                return true;
            }
            bool ok;
            if (code == kInputNavRight) {
                ok = acc::engine_radial::SelectNextActionInRow(
                    tam, g_state.curRow);
            } else {
                ok = acc::engine_radial::SelectPrevActionInRow(
                    tam, g_state.curRow);
            }
            acclog::Write("Radial", "%s row=%d count=%d ok=%d",
                          code == kInputNavRight ? "NavRight" : "NavLeft",
                          g_state.curRow, count, ok ? 1 : 0);
            SpeakCurrentLabel(tam, g_state.curRow);
            return true;
        }
        case kInputEnter1:
        case kInputEnter2: {
            char label[128] = "";
            acc::engine_radial::ReadRowActionLabel(
                tam, g_state.curRow, label, sizeof(label));
            bool ok = acc::engine_radial::DispatchRowAction(
                tam, g_state.curRow);
            acclog::Write("Radial", "ENTER dispatch row=%d label=[%s] ok=%d",
                          g_state.curRow, label, ok ? 1 : 0);
            // The dispatch may or may not visually clear the radial. Drop
            // our gate either way so a subsequent Enter on a new target
            // arms a fresh radial cleanly.
            g_state.active = false;
            prism::Speak(label[0] ? label : "?", /*interrupt=*/true);
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            acclog::Write("Radial", "ESC — disarming (no engine cleanup)");
            ForceDisarm("esc");
            return true;
        }
        default:
            return false;
    }
}

void ScheduleWideDiag(int frames, const char* tag) {
    if (frames < 1) frames = 1;
    if (frames > 10) frames = 10;
    g_diag.framesRemaining = frames;
    g_diag.totalFrames = frames;
    g_diag.ordinal = 0;
    if (tag && tag[0]) {
        std::snprintf(g_diag.tag, sizeof(g_diag.tag), "%s", tag);
    } else {
        g_diag.tag[0] = '\0';
    }
    acclog::Write("Radial", "ScheduleWideDiag frames=%d tag=[%s]",
                  frames, g_diag.tag);
}

void Tick() {
    // Emit any scheduled wide-diagnostic dumps. Runs independently of
    // g_state.active so we can investigate populate calls that didn't
    // arm (the empty-rows case is exactly when we want the data).
    if (g_diag.framesRemaining > 0) {
        ++g_diag.ordinal;
        char framedTag[64];
        std::snprintf(framedTag, sizeof(framedTag), "%s/f%d-of-%d",
                      g_diag.tag[0] ? g_diag.tag : "diag",
                      g_diag.ordinal, g_diag.totalFrames);
        void* dtam = acc::engine_radial::ResolveTargetActionMenu();
        if (dtam) {
            acc::engine_radial::LogStateWide(dtam, framedTag);
        } else {
            acclog::Write("Radial.StateW", "[%s] tam=NULL (chain failed this tick)",
                          framedTag);
        }
        --g_diag.framesRemaining;
    }

    if (!g_state.active) return;
    void* tam = acc::engine_radial::ResolveTargetActionMenu();
    if (!tam) {
        ForceDisarm("tam-gone");
        return;
    }
    int total = 0;
    for (int r = 0; r < acc::engine_radial::kRowCount; ++r) {
        total += acc::engine_radial::RowActionCount(tam, r);
    }
    if (total == 0) {
        ForceDisarm("rows-empty");
        return;
    }
    // Current row drained to 0 while another row still has actions —
    // step to the first populated one so the next nav key has somewhere
    // to start from.
    if (acc::engine_radial::RowActionCount(tam, g_state.curRow) <= 0) {
        int r = FirstPopulatedRow(tam);
        if (r >= 0) {
            acclog::Write("Radial", "tick — current row drained, slot %d -> %d",
                          g_state.curRow, r);
            g_state.curRow = r;
        }
    }
}

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("Radial", "disarm — reason=%s", reason ? reason : "?");
    g_state.active = false;
    g_state.curRow = 0;
    g_state.target[0] = '\0';
}

}  // namespace acc::radial_menu
