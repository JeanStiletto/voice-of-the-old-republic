#include "actionbar_menu.h"

#include <cstdio>
#include <cstring>

#include "engine_actionbar.h"
#include "engine_input.h"     // kInputNavUp/Down, kInputEnter1/2,
                              // kInputEsc1/2
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::actionbar_menu {

namespace {

struct State {
    bool active  = false;
    int  curSlot = 0;
};

State g_state;

// Speak the column's current variant after a cycle. No "X of N" suffix
// here — count is announced on Open; subsequent cycle navigation just
// reads back the new label so the user hears WHAT they cycled to without
// the count chatter. Mirrors the radial's SpeakCurrentLabel pattern.
void SpeakCurrentVariant(void* mi, int slot) {
    char label[128] = "";
    acc::engine_actionbar::ReadColumnLabel(mi, slot, label, sizeof(label));
    if (label[0] == '\0') {
        acclog::Write("ActionBar: speak col=%d -> empty", slot);
        return;
    }
    tolk::Speak(label, /*interrupt=*/true);
    acclog::Write("ActionBar: speak col=%d [%s]", slot, label);
}

}  // namespace

bool Open(int slot) {
    if (slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        acclog::Write("ActionBar: Open slot=%d out of range; ignoring", slot);
        return false;
    }

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write("ActionBar: Open slot=%d — main_interface unresolved; "
                      "not arming",
                      slot);
        return false;
    }

    // Diagnostic: full column dump on every Open so the log carries the
    // engine's own view of the action bar at the moment the user asked
    // about it. Cheap (6 columns × few reads); drop after the action-bar
    // path is verified working end-to-end.
    acc::engine_actionbar::LogState(mi, "open");

    int isAct = acc::engine_actionbar::IsColumnActive(mi, slot);
    int nVar  = acc::engine_actionbar::VariantCount(mi, slot);
    if (!isAct || nVar <= 0) {
        // Column unpopulated for the current actor (no items / no powers
        // of this category equipped or memorised). Speak the localised
        // empty phrase and decline to arm — the user falls back to
        // ordinary input rather than entering an unusable submenu.
        const char* msg = acc::strings::Get(
            acc::strings::Id::ActionBarColumnEmpty);
        char fmt[128];
        std::snprintf(fmt, sizeof(fmt),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      slot + 1);
        tolk::Speak(fmt[0] ? fmt : msg, /*interrupt=*/true);
        acclog::Write(
            "ActionBar: Open slot=%d is_action=%d variants=%d — empty, "
            "not arming",
            slot, isAct, nVar);
        return false;
    }

    g_state.active  = true;
    g_state.curSlot = slot;

    char label[128] = "";
    acc::engine_actionbar::ReadColumnLabel(mi, slot, label, sizeof(label));

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtActionBarOpened),
                  slot + 1, label[0] ? label : "?", nVar);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write(
        "ActionBar: ARMED slot=%d variants=%d label=[%s]",
        slot, nVar, label);
    return true;
}

bool IsActive() {
    return g_state.active;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;

    // Release edges always pass through. Mirror radial_menu: don't consume
    // key-up — letting it pass doesn't double-trigger anything and keeps
    // the engine's auto-repeat / state machine consistent.
    if (value == 0) return false;

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write(
            "ActionBar: HandleInputEvent — main_interface unresolved on "
            "key=%d; force-disarming",
            code);
        ForceDisarm("mi-unresolved");
        return false;
    }

    switch (code) {
        case kInputNavUp:
        case kInputNavDown: {
            int nVar = acc::engine_actionbar::VariantCount(mi, g_state.curSlot);
            if (nVar <= 1) {
                acclog::Write(
                    "ActionBar: %s slot=%d variants=%d — nothing to cycle",
                    code == kInputNavDown ? "NavDown" : "NavUp",
                    g_state.curSlot, nVar);
                // Re-speak the current label so the user gets feedback
                // (silent ignore would feel like the keypress was eaten —
                // see memory: feedback_never_silence_fallback_announcement).
                SpeakCurrentVariant(mi, g_state.curSlot);
                return true;
            }
            bool ok = (code == kInputNavUp)
                ? acc::engine_actionbar::FireUpButton(mi, g_state.curSlot)
                : acc::engine_actionbar::FireDownButton(mi, g_state.curSlot);
            acclog::Write(
                "ActionBar: %s slot=%d variants=%d ok=%d",
                code == kInputNavUp ? "NavUp" : "NavDown",
                g_state.curSlot, nVar, ok ? 1 : 0);
            SpeakCurrentVariant(mi, g_state.curSlot);
            return true;
        }
        case kInputEnter1:
        case kInputEnter2: {
            char label[128] = "";
            acc::engine_actionbar::ReadColumnLabel(
                mi, g_state.curSlot, label, sizeof(label));
            bool ok = acc::engine_actionbar::FireActionButton(
                mi, g_state.curSlot);

            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          acc::strings::Get(
                              acc::strings::Id::FmtActionBarFired),
                          label[0] ? label : "?");
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write(
                "ActionBar: ENTER slot=%d label=[%s] ok=%d -> [%s]",
                g_state.curSlot, label, ok ? 1 : 0, msg);

            // Drop the gate either way — the engine has either fired the
            // action (visible via combat round queue) or refused it (column
            // is_action gate); a fresh Shift+N press arms again.
            ForceDisarm("enter");
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            const char* msg = acc::strings::Get(
                acc::strings::Id::ActionBarCancelled);
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write("ActionBar: ESC slot=%d -> [%s]",
                          g_state.curSlot, msg);
            ForceDisarm("esc");
            return true;
        }
        default:
            return false;
    }
}

void ForceDisarm(const char* reason) {
    if (!g_state.active) return;
    acclog::Write("ActionBar: disarm — reason=%s", reason ? reason : "?");
    g_state.active  = false;
    g_state.curSlot = 0;
}

}  // namespace acc::actionbar_menu
