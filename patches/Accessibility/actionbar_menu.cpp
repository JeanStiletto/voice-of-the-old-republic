#include "actionbar_menu.h"

#include <cstdio>
#include <cstring>

#include "combat_diag.h"     // Probe — snapshot chain/overwrite state around
                             // FireSelectedVariant so we can grep
                             // "Combat.Diag PRE shift+N-enter" / "POST"
                             // and compare cm/qs deltas
#include "combat_queue.h"    // CountPlayerEntries — read queue depth
                             // after fire so the announce can report
                             // "X, Platz N"
#include "engine_actionbar.h"
#include "engine_input.h"     // kInputNavUp/Down, kInputEnter1/2,
                              // kInputEsc1/2
#include "menus_submenu.h"   // EnforceCombatHotkeyMutex (Shift+4..7 vs Shift+1..3)
#include "engine_panels.h"   // HasActiveDialogPanel — gate Open while a
                              // cinematic dialog is foreground (the dialog's
                              // listbox is sensitive to arrow input from the
                              // engine's DirectInput pipeline; opening the
                              // submenu inside a dialog corrupts the dialog's
                              // selection state and locks Enter advance).
#include "engine_reads.h"    // ResolveItemDescriptionFromActionId for Shift+arrow
#include "hotkeys.h"         // ShiftHeld
#include "log.h"
#include "menu_speak.h"
#include "strings.h"
#include "prism.h"

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
    acc::menu_speak::SpeakChoice("ActionBar", label,
                                 "col=%d idx=%d", slot, idx);
}

}  // namespace

int CurrentSelection(int slot) {
    if (slot < 0 || slot >= acc::engine_actionbar::kColumnCount) return 0;
    return g_selectedIndex[slot];
}

bool Open(int slot) {
    if (slot < 0 || slot >= acc::engine_actionbar::kColumnCount) {
        acclog::Write("ActionBar", "Open slot=%d out of range; ignoring", slot);
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
        acclog::Write("ActionBar", "Open slot=%d — dialog panel in stack; not arming",
            slot);
        return false;
    }

    void* mi = acc::engine_actionbar::ResolveMainInterface();
    if (!mi) {
        acclog::Write("ActionBar", "Open slot=%d — main_interface unresolved; "
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
        prism::Speak(fmt, /*interrupt=*/true);
        acclog::Write("ActionBar", "Open slot=%d variants=0 — empty, not arming",
            slot);
        return false;
    }

    acc::menus::submenu::EnforceCombatHotkeyMutex("actionbar-open");

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
    prism::Speak(msg, /*interrupt=*/true);
    acclog::Write("ActionBar", "ARMED slot=%d variants=%d idx=%d label=[%s]",
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
        acclog::Write("ActionBar", "HandleInputEvent — main_interface unresolved on "
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
            // Shift+Up/Down: speak the variant's full item-property
            // description (the same text the equip-picker / inventory
            // speak on Shift+arrow), do NOT cycle the variant. Same
            // contract as peek_description.cpp's shift-arrow gesture.
            //
            // The descriptor's action_id at +0x08 carries the
            // server-side item handle ORed with 0x40000000 for slots
            // 1..3 (medical / grenades / mines) — see
            // engine_reads::ResolveItemDescriptionFromActionId. Force-
            // power slot (0) and any future non-item descriptor types
            // fall back to the localised "no description available" cue.
            // (The engine's own CSWGuiControl::DisplayToolTip path on
            // the column button reads CExoString +0x28 which the engine
            // never populates for these dynamically-built slots, so the
            // earlier tooltip-read attempt produced CP1252 garbage.)
            if (acc::hotkeys::ShiftHeld()) {
                int curIdx = g_selectedIndex[slot];
                if (curIdx < 0) curIdx = 0;
                uint32_t actionId =
                    acc::engine_actionbar::ReadVariantActionId(
                        mi, slot, curIdx);
                char text[8192];
                if (actionId &&
                    acc::engine::ResolveItemDescriptionFromActionId(
                        actionId, text, sizeof(text)))
                {
                    prism::Speak(text, /*interrupt=*/true);
                    acclog::Write("ActionBar",
                        "Shift+%s slot=%d idx=%d action_id=0x%x item-desc=\"%s\"",
                        code == kInputNavDown ? "Down" : "Up",
                        slot, curIdx, actionId, text);
                } else {
                    const char* msg = acc::strings::Get(
                        acc::strings::Id::NoTooltipAvailable);
                    prism::Speak(msg, /*interrupt=*/true);
                    acclog::Write("ActionBar",
                        "Shift+%s slot=%d idx=%d action_id=0x%x no item-desc; "
                        "spoke fallback",
                        code == kInputNavDown ? "Down" : "Up",
                        slot, curIdx, actionId);
                }
                return true;
            }
            if (nVar <= 1) {
                acclog::Write("ActionBar", "%s slot=%d variants=%d — nothing to cycle",
                    code == kInputNavDown ? "NavDown" : "NavUp",
                    slot, nVar);
                SpeakCurrentVariant(mi, slot);
                return true;
            }
            // Update our shadow then stamp the engine's per-column
            // selected_action_id field so a subsequent ENTER (or bare
            // 4..7 press while the gate is closed) fires the variant
            // the user just heard. Up = next variant, Down = prev —
            // mirrors the sighted action bar's mouse-wheel-up
            // convention (wheel up = next variant). Clamps at the
            // ends (no wrap) so the user gets an unambiguous "I'm at
            // the top/bottom" cue from the unchanged speech (the same
            // label repeats) instead of teleporting to the other end.
            int prevIdx = g_selectedIndex[slot];
            if (code == kInputNavUp) {
                if (g_selectedIndex[slot] + 1 < nVar) {
                    g_selectedIndex[slot] += 1;
                }
            } else {
                if (g_selectedIndex[slot] > 0) {
                    g_selectedIndex[slot] -= 1;
                }
            }
            bool ok = acc::engine_actionbar::SelectVariant(
                mi, slot, g_selectedIndex[slot]);
            acclog::Write("ActionBar", "%s slot=%d variants=%d idx %d -> %d ok=%d",
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

            // Re-stamp on the fire path so a no-cycle ENTER (user
            // opened the menu and pressed Enter immediately on the
            // pre-armed variant) still lands on the right action_id
            // even if the engine field was stale from a prior session
            // or RePopulate.
            acc::engine_actionbar::SelectVariant(mi, slot, idx);

            char diag_label[24];
            std::snprintf(diag_label, sizeof(diag_label),
                          "shift+%d-enter", slot + 4);  // slot 0→4 .. slot 3→7
            // Snapshot queue depth pre-press so the post-press announce
            // can detect engine cap-hits (count was already 4 → new
            // action freed silently inside CSWSCombatRound::AddAction).
            acc::combat::queue::ReportPrePressDepth();
            acc::combat_diag::LogPreFire(diag_label);
            bool ok = acc::engine_actionbar::FireSelectedVariant(mi, slot);
            acc::combat_diag::LogPostFire(diag_label);

            // PRE-only slot calc. Reading the POST count races the
            // engine's queue mutation, so use the deterministic PRE
            // depth + 1 for the slot the press just landed in.
            // Cap hit when PRE was already at 4 — AddAction's
            // `if (3 < count) { free; return; }` arm silently rejected.
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
            acclog::Write("ActionBar", "ENTER slot=%d idx=%d label=[%s] ok=%d "
                "pre=%d slot=%d capHit=%d -> [%s]",
                slot, idx, label, ok ? 1 : 0, preDepth, slotNum,
                capHit ? 1 : 0, msg);

            ForceDisarm("enter");
            return true;
        }
        case kInputEsc1:
        case kInputEsc2: {
            const char* msg = acc::strings::Get(
                acc::strings::Id::ActionBarCancelled);
            prism::Speak(msg, /*interrupt=*/true);
            acclog::Write("ActionBar", "ESC slot=%d -> [%s]",
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
    acclog::Write("ActionBar", "disarm — reason=%s", reason ? reason : "?");
    g_state.active  = false;
    g_state.curSlot = 0;
}

}  // namespace acc::actionbar_menu
