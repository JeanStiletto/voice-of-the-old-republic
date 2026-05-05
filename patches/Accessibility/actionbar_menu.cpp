#include "actionbar_menu.h"

#include <cstdio>
#include <cstring>

#include "engine_actionbar.h"
#include "engine_input.h"     // kInputNavUp/Down, kInputEnter1/2,
                              // kInputEsc1/2
#include "engine_panels.h"   // HasActiveDialogPanel — gate Open while a
                              // cinematic dialog is foreground (the dialog's
                              // listbox is sensitive to arrow input from the
                              // engine's DirectInput pipeline; opening the
                              // submenu inside a dialog corrupts the dialog's
                              // selection state and locks Enter advance).
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::actionbar_menu {

namespace {

// Per-slot selected index, persisted across submenu sessions and across
// engine fires. Mirrors the engine's own per-column "currently selected"
// state — kept in lock-step by always pairing local-index updates with
// CycleNext/Prev calls. Exposed to the bare-key announce path so 4..7
// reads the same variant the engine fires.
int g_selectedIndex[acc::engine_actionbar::kColumnCount] = {0, 0, 0, 0, 0, 0};

struct State {
    bool active  = false;
    int  curSlot = 0;
};

State g_state;

// Clamp persisted index in case variant count shrank between sessions
// (e.g., the player consumed a medikit). Returns the valid index to
// announce or 0 when the column is empty.
int ClampIndex(void* mi, int slot) {
    int n = acc::engine_actionbar::VariantCount(mi, slot);
    if (n <= 0) return 0;
    if (g_selectedIndex[slot] < 0)  g_selectedIndex[slot] = 0;
    if (g_selectedIndex[slot] >= n) g_selectedIndex[slot] = n - 1;
    return g_selectedIndex[slot];
}

void SpeakCurrentVariant(void* mi, int slot) {
    int idx = ClampIndex(mi, slot);
    char label[128] = "";
    acc::engine_actionbar::ReadVariantLabel(mi, slot, idx,
                                            label, sizeof(label));
    if (label[0] == '\0') {
        acclog::Write("ActionBar: speak col=%d idx=%d -> empty",
                      slot, idx);
        return;
    }
    tolk::Speak(label, /*interrupt=*/true);
    acclog::Write("ActionBar: speak col=%d idx=%d [%s]",
                  slot, idx, label);
}

}  // namespace

int CurrentSelection(int slot) {
    if (slot < 0 || slot >= acc::engine_actionbar::kColumnCount) return 0;
    return g_selectedIndex[slot];
}

bool Open(int slot) {
    if (slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        acclog::Write("ActionBar: Open slot=%d out of range; ignoring", slot);
        return false;
    }

    // Refuse to arm while a dialog cinematic is foreground. The submenu's
    // arrow-key navigation reaches the engine via DirectInput regardless
    // of our handling (Win32 polling is read-only), and the cinematic
    // dialog's listbox treats arrow input as selection-cycle — pressing
    // Up/Down inside the dialog would deselect the auto-selected reply
    // row (sel changes from 0 to something else), at which point the
    // dialog's Enter-advance path has nothing to confirm. Verified
    // 2026-05-05: opening Shift+5 inside the post-fight cinematic
    // ("Da die Tür jetzt offen ist…") deterministically locks Enter for
    // the rest of the cinematic, only Esc → Quit confirm escapes.
    //
    // The action bar is only meaningful in-world (for combat use). While
    // a dialog is up, decline and speak nothing — the user already has
    // dialog reply hotkeys (1..9) for that context.
    if (acc::engine::HasActiveDialogPanel()) {
        acclog::Write(
            "ActionBar: Open slot=%d — dialog panel in stack; not arming",
            slot);
        return false;
    }

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write("ActionBar: Open slot=%d — main_interface unresolved; "
                      "not arming",
                      slot);
        return false;
    }

    acc::engine_actionbar::LogState(mi, "open");

    int nVar = acc::engine_actionbar::VariantCount(mi, slot);
    if (nVar <= 0) {
        char fmt[128];
        std::snprintf(fmt, sizeof(fmt),
                      acc::strings::Get(
                          acc::strings::Id::FmtActionBarColumnEmpty),
                      slot + 1);
        tolk::Speak(fmt, /*interrupt=*/true);
        acclog::Write(
            "ActionBar: Open slot=%d variants=0 — empty, not arming",
            slot);
        return false;
    }

    g_state.active  = true;
    g_state.curSlot = slot;
    int idx = ClampIndex(mi, slot);

    char label[128] = "";
    acc::engine_actionbar::ReadVariantLabel(mi, slot, idx,
                                            label, sizeof(label));

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  acc::strings::Get(acc::strings::Id::FmtActionBarOpened),
                  slot + 1, label[0] ? label : "?", nVar);
    tolk::Speak(msg, /*interrupt=*/true);
    acclog::Write(
        "ActionBar: ARMED slot=%d variants=%d idx=%d label=[%s]",
        slot, nVar, idx, label);
    return true;
}

bool IsActive() {
    return g_state.active;
}

bool HandleInputEvent(int code, int value) {
    if (!g_state.active) return false;

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

    int slot = g_state.curSlot;
    int nVar = acc::engine_actionbar::VariantCount(mi, slot);

    switch (code) {
        case kInputNavUp:
        case kInputNavDown: {
            if (nVar <= 1) {
                acclog::Write(
                    "ActionBar: %s slot=%d variants=%d — nothing to cycle",
                    code == kInputNavDown ? "NavDown" : "NavUp",
                    slot, nVar);
                SpeakCurrentVariant(mi, slot);
                return true;
            }
            // Pair the engine cycle call with our local index update so
            // bare 4..7 fires the same variant we just announced. Up
            // navigates "next", Down navigates "prev" — mirrors the
            // sighted action bar's mouse-wheel-up convention (wheel up
            // = next variant). The engine's OnActionUpArrowPressed name
            // matches that direction.
            bool ok;
            int prevIdx = g_selectedIndex[slot];
            if (code == kInputNavUp) {
                ok = acc::engine_actionbar::CycleNextVariant(mi, slot);
                g_selectedIndex[slot] =
                    (g_selectedIndex[slot] + 1) % nVar;
            } else {
                ok = acc::engine_actionbar::CyclePrevVariant(mi, slot);
                g_selectedIndex[slot] =
                    (g_selectedIndex[slot] - 1 + nVar) % nVar;
            }
            acclog::Write(
                "ActionBar: %s slot=%d variants=%d idx %d -> %d ok=%d",
                code == kInputNavUp ? "NavUp" : "NavDown",
                slot, nVar, prevIdx, g_selectedIndex[slot], ok ? 1 : 0);
            SpeakCurrentVariant(mi, slot);
            return true;
        }
        case kInputEnter1:
        case kInputEnter2: {
            int idx = ClampIndex(mi, slot);
            char label[128] = "";
            acc::engine_actionbar::ReadVariantLabel(
                mi, slot, idx, label, sizeof(label));

            bool ok = acc::engine_actionbar::FireSelectedVariant(mi, slot);

            char msg[192];
            std::snprintf(msg, sizeof(msg),
                          acc::strings::Get(
                              acc::strings::Id::FmtActionBarFired),
                          label[0] ? label : "?");
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write(
                "ActionBar: ENTER slot=%d idx=%d label=[%s] ok=%d -> [%s]",
                slot, idx, label, ok ? 1 : 0, msg);

            ForceDisarm("enter");
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            const char* msg = acc::strings::Get(
                acc::strings::Id::ActionBarCancelled);
            tolk::Speak(msg, /*interrupt=*/true);
            acclog::Write("ActionBar: ESC slot=%d -> [%s]",
                          slot, msg);
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
