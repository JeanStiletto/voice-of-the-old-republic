// KOTOR Accessibility — editbox dispatcher + per-tick monitor.
//
// See menus_editbox.h for the model (soft edit-mode flag, spec-table over
// panel kinds, focus-enter speech via FromControl, diff polling here).
//
// Single armed editbox at a time — vanilla KOTOR has exactly one editbox
// in the entire game (chargen Name's `name_editbox`); the file-static
// `s_state` reflects that with one spec/panel/editbox triple. If a future
// mod-introduced second editbox ever appears in the wild and the user
// can have both panels active simultaneously, this becomes a small array.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "menus_editbox.h"

#include "engine_input.h"
#include "engine_manager.h"
#include "engine_offsets.h"
#include "engine_panels.h"
#include "engine_reads.h"
#include "hotkeys.h"
#include "log.h"
#include "menus.h"            // ClearPendingAnnounce — editbox arm owns the announce
#include "menus_extract.h"
#include "menus_pending.h"
#include "strings.h"
#include "tolk.h"

using namespace acc::engine;

namespace acc::menus::editbox {

namespace {

// ============================================================================
// Spec — one per panel kind that owns an editbox. Mirrors ListBoxPanelSpec
// in shape; fewer fields because the editbox UX is uniform across hosting
// panels (it's the embedded widget that varies, not the keyboard semantics).
// ============================================================================

struct EditboxPanelSpec {
    // Diagnostic tag for log lines ("ChargenName").
    const char* logTag;

    // True if this spec applies to `panel`. Vtable comparison for vanilla
    // panels; future specs could use a structural matcher.
    bool (*matches)(void* panel);

    // Locate the embedded CSWGuiEditbox within `panel`. Returns nullptr if
    // the editbox isn't present (sanity check — vanilla panels always have
    // it at the documented offset).
    void* (*findEditbox)(void* panel);

    // Locate the panel's submit button (BTN_OK / "Annehmen" analogue) for
    // Enter-to-submit dispatch. Inline-embedded button; we feed it to
    // pending::QueueActivate which calls vtable[15].HandleInputEvent(0x27).
    void* (*findSubmitButton)(void* panel);

    // Optional title-speech override. Called from menus.cpp's
    // AnnouncePanelTitle when this panel matches; returning a non-null
    // string substitutes that string for the generic title-walk speech
    // (which picks the first announceable label child by panel-controls
    // index — wrong for CSWGuiNameChargen, where the first label is the
    // stale "CHARAKTERAUSWAHL" header BioWare reuses across all chargen
    // sub-panels). Returning nullptr falls back to the generic walk.
    //
    // Callbacks return a pointer to caller-stable storage (file-static
    // buffer is fine since AnnouncePanelTitle reads the result before any
    // other override path runs).
    const char* (*titleOverride)(void* panel);
};

// ----------------------------------------------------------------------------
// Chargen Name panel — `CSWGuiNameChargen`. The only vanilla user.
// ----------------------------------------------------------------------------

bool ChargenNameMatches(void* panel) {
    if (!panel) return false;
    void** vt = *reinterpret_cast<void***>(panel);
    return reinterpret_cast<uintptr_t>(vt) == kVtableCSWGuiNameChargen;
}

void* ChargenNameFindEditbox(void* panel) {
    if (!panel) return nullptr;
    return static_cast<unsigned char*>(panel) + kNameChargenEditboxOffset;
}

void* ChargenNameFindSubmitButton(void* panel) {
    if (!panel) return nullptr;
    return static_cast<unsigned char*>(panel) + kNameChargenEndButtonOffset;
}

// Substitute the chargen Name panel's title speech: read subtitle_label
// ("Name") instead of letting the generic walk land on main_title_label
// ("CHARAKTERAUSWAHL"). Reading the live label rather than hardcoding a
// literal preserves localisation — subtitle_label is filled from the
// panel's .gui resource which is localised per install.
const char* ChargenNameTitleOverride(void* panel) {
    if (!panel) return nullptr;
    void* subtitle =
        static_cast<unsigned char*>(panel) + kNameChargenSubtitleLabelOffset;
    static char s_buf[128];
    if (acc::menus::extract::FromControl(subtitle, s_buf, sizeof(s_buf), panel)) {
        return s_buf;
    }
    return nullptr;
}

constexpr EditboxPanelSpec kChargenNameSpec = {
    /*logTag*/           "ChargenName",
    /*matches*/          ChargenNameMatches,
    /*findEditbox*/      ChargenNameFindEditbox,
    /*findSubmitButton*/ ChargenNameFindSubmitButton,
    /*titleOverride*/    ChargenNameTitleOverride,
};

constexpr const EditboxPanelSpec* kSpecs[] = {
    &kChargenNameSpec,
};
constexpr int kNumSpecs = static_cast<int>(sizeof(kSpecs) / sizeof(kSpecs[0]));

// ============================================================================
// Armed-state storage. Single-armed-editbox model — see TU header.
// ============================================================================

struct ArmedState {
    const EditboxPanelSpec* spec;
    void* panel;
    void* editbox;
    bool  editMode;        // false after Esc; armed-but-passive

    // Snapshot of the editbox state, refreshed on every poll-and-diff.
    // Buffer is sized to fit a chargen name comfortably (vanilla cap is
    // around 16 bytes); 64 covers any realistic hand-typed value plus a
    // generous safety margin.
    char     text[64];
    uint32_t textLen;       // strnlen(c_string), NOT the +0x15c field
    uint32_t rawLenField;   // raw +0x15c — render-side, diagnostic only
    short    shortA;        // +0x150 — render-side, diagnostic only
    short    shortB;        // +0x152 — render-side, diagnostic only
};

ArmedState s_state = {nullptr, nullptr, nullptr, false, {0}, 0, 0, 0, 0};

// Win32 modal-key edge state. The engine's text-input pipeline consumes
// keyboard events at the editbox before they reach our manager-level
// OnHandleInputEvent hook (verified against patch-20260509-200056.log:
// the user typed and Backspaced for 10 seconds while the editbox was
// armed, and zero Menus.Input lines fired in that window — only the
// per-tick text-diff caught the changes). So the modal Up/Down (re-
// speak) / Enter (submit) / Esc (exit) handlers in TryHandleInput are
// unreachable while editing. Mirror the cycle_input / interact_hotkey
// pattern: poll OS keyboard state per tick, edge-detect press
// transitions, dispatch the same handlers from there.
// Modal key state. Left/Right are deliberately omitted — the engine's
// CSWGuiEditbox doesn't track caret movement (verified by a 352-byte
// arm-time-baseline diff against 21 Left/Right presses in
// patch-20260509-202159.log: every press came back "no diff" across the
// entire struct). Without an engine-side caret to read, char-at-caret
// announce isn't possible; binding Left/Right to a patch-internal
// virtual cursor would diverge from the engine's actual append-only
// behaviour (Backspace deletes from end regardless of where our
// virtual cursor pointed). Cleanest: leave Left/Right unbound — the
// user has Up/Down for full-text re-read and Backspace from end works
// as expected.
// (Modal-key rising-edge detection used to live here as `ModalKeyState` +
// `s_lastKeyState`; both replaced by the central registry's BeginTick/
// EndTick + Pressed() lifecycle. Arm-time suppression is now handled via
// `acc::hotkeys::Consume(Action::Editbox*)` in the focus-enter branch.)

// ============================================================================
// Live editbox reads.
// ============================================================================

// Read (text, length, shortA, shortB, rawLen) from the editbox into `out`.
// Returns false on null editbox or null c_string pointer (treats the latter
// as "field not yet initialised by the engine").
//
// `outLen` is computed via strnlen on the c_string buffer — empirically the
// engine does NOT keep the +0x15c uint32 in sync with the visible string
// length on incremental Backspace. The c_string content itself is correct
// (the engine null-terminates it on every mutation), so strnlen gives the
// true visible length. `outRawLen` is the raw +0x15c value, retained for
// diagnostic logging while we're still figuring out the editbox layout.
bool ReadEditbox(void* editbox, char* outText, size_t outCap,
                 uint32_t& outLen, uint32_t& outRawLen,
                 short& outA, short& outB) {
    if (!editbox || outCap < 1) return false;
    auto* base = static_cast<unsigned char*>(editbox);
    const char* cstr = *reinterpret_cast<const char**>(base + kEditboxStringCStrOffset);
    outRawLen = *reinterpret_cast<uint32_t*>(base + kEditboxStringLengthOffset);
    outA = *reinterpret_cast<short*>(base + kEditboxShortA);
    outB = *reinterpret_cast<short*>(base + kEditboxShortB);
    if (cstr) {
        // Bound strnlen to outCap-1 — a corrupted buffer with no null
        // terminator can't blow past our caller's storage.
        size_t n = strnlen(cstr, outCap - 1);
        memcpy(outText, cstr, n);
        outText[n] = '\0';
        outLen = static_cast<uint32_t>(n);
    } else {
        outText[0] = '\0';
        outLen = 0;
    }
    return true;
}

void SnapshotInto(ArmedState& s) {
    ReadEditbox(s.editbox, s.text, sizeof(s.text), s.textLen, s.rawLenField,
                s.shortA, s.shortB);
}

// ============================================================================
// Speech helpers.
// ============================================================================

void SpeakFullText(const char* text, uint32_t len) {
    if (len == 0) {
        tolk::Speak(acc::strings::Get(acc::strings::Id::EditboxEmpty),
                    /*interrupt=*/false);
    } else {
        tolk::Speak(text, /*interrupt=*/false);
    }
}

// Speak a single character. Render as a one-char null-terminated string —
// Tolk's NVDA path handles this natively (NVDA reads single chars per its
// "characters" verbosity setting).
void SpeakSingleChar(char c) {
    char buf[2] = {c, '\0'};
    tolk::Speak(buf, /*interrupt=*/false);
}

// ============================================================================
// Diff + announce. Called per tick while edit mode is armed.
// ============================================================================

void PollAndAnnounceDiff(ArmedState& s) {
    char     newText[sizeof(s.text)];
    uint32_t newLen;
    uint32_t newRawLen;
    short    newA, newB;
    if (!ReadEditbox(s.editbox, newText, sizeof(newText),
                     newLen, newRawLen, newA, newB)) {
        return;
    }

    bool textChanged = (newLen != s.textLen) ||
                       (memcmp(newText, s.text, newLen) != 0);

    if (!textChanged) return;

    {
        int32_t deltaLen = static_cast<int32_t>(newLen) -
                           static_cast<int32_t>(s.textLen);
        if (deltaLen == 1) {
            // Single char inserted. Without a caret pointer we assume the
            // engine appended at end (typical for fresh typing) and speak
            // the new last char. Mid-string inserts will mis-speak by one
            // position; user can Up/Down re-read to verify.
            char inserted = newText[newLen - 1];
            SpeakSingleChar(inserted);
            acclog::Write("Editbox", "%s insert char='%c' len=%u->%u "
                          "(strnlen-derived; caret unknown) "
                          "shortA=%d->%d shortB=%d->%d rawLen=%u->%u",
                          s.spec->logTag, inserted, s.textLen, newLen,
                          s.shortA, newA, s.shortB, newB,
                          s.rawLenField, newRawLen);
        } else if (deltaLen == -1) {
            // Single char deleted. Same heuristic: assume Backspace from
            // end and speak the char that was at the old last position.
            char deleted = (s.textLen > 0) ? s.text[s.textLen - 1] : '?';
            SpeakSingleChar(deleted);
            acclog::Write("Editbox", "%s delete char='%c' len=%u->%u "
                          "(strnlen-derived; caret unknown) "
                          "shortA=%d->%d shortB=%d->%d rawLen=%u->%u",
                          s.spec->logTag, deleted, s.textLen, newLen,
                          s.shortA, newA, s.shortB, newB,
                          s.rawLenField, newRawLen);
        } else {
            // Bulk change (Random button, paste-like, multi-char delete).
            SpeakFullText(newText, newLen);
            acclog::Write("Editbox", "%s bulk text change len=%u->%u "
                          "shortA=%d->%d shortB=%d->%d rawLen=%u->%u "
                          "new=\"%s\"",
                          s.spec->logTag, s.textLen, newLen,
                          s.shortA, newA, s.shortB, newB,
                          s.rawLenField, newRawLen, newText);
        }
    }

    // Update snapshot.
    memcpy(s.text, newText, newLen + 1);
    s.textLen     = newLen;
    s.rawLenField = newRawLen;
    s.shortA      = newA;
    s.shortB      = newB;
}

// ============================================================================
// Modal-key Win32 polling. We poll Win32 rather than route through the
// engine's input dispatcher because in-world Enter/Up/Down/Esc bypass
// CSWGuiManager — see `project_inworld_input_pipeline` memory. Edge
// detection and foreground gating live in `acc::hotkeys::Pressed()`.
// ============================================================================

void PollModalKeys(ArmedState& s) {
    // Rising-edge detection comes from the central registry — BeginTick /
    // EndTick advance `last` every frame so a key the user is holding at
    // arm-time can't masquerade as a fresh edge until it's released and
    // re-pressed. Foreground gating is baked into Pressed().
    namespace hk = acc::hotkeys;
    bool upEdge    = hk::Pressed(hk::Action::EditboxReReadUp);
    bool downEdge  = hk::Pressed(hk::Action::EditboxReReadDown);
    bool enterEdge = hk::Pressed(hk::Action::EditboxSubmit);
    bool escEdge   = hk::Pressed(hk::Action::EditboxCancel);

    // Only act in edit mode. The registry tracks edges unconditionally so
    // re-entering edit mode mid-press doesn't mistake a held key for a
    // fresh press.
    if (!s.editMode || !s.spec) return;

    if (upEdge || downEdge) {
        SnapshotInto(s);
        SpeakFullText(s.text, s.textLen);
        acclog::Write("Editbox", "%s %s (Win32) -> re-speak text len=%u",
                      s.spec->logTag,
                      upEdge ? "VK_UP" : "VK_DOWN", s.textLen);
    }

    if (enterEdge) {
        // Drop edit mode so the next poll stops announcing keystrokes. We do
        // NOT QueueActivate the submit button here: the engine's own editbox
        // dispatcher fires HandleDoneButton on Enter natively, then snaps
        // focus back to the originating panel button. Queuing a second
        // FireActivate on the same OK button runs one frame later, by which
        // point the editbox panel has already been demoted out of the modal
        // stack — the second activation cascades through the closing logic
        // and pops the underlying panel (e.g. the chargen tab strip),
        // leaving routing stranded on a non-navigable parent frame and
        // locking the user out. patch-20260519-154827.log frames 12586-12614
        // captured this exact lockup.
        acclog::Write("Editbox", "%s VK_RETURN (Win32) -> edit-mode off "
                      "(engine handles submit natively)",
                      s.spec->logTag);
        s.editMode = false;
    }

    if (escEdge) {
        // Soft exit — drop the edit-mode flag so subsequent Up/Down don't
        // re-fire as re-read. The engine will probably also handle Esc as
        // a panel-cancel (CSWGuiNameChargen::HandleCancelButton @0x006f9ba0)
        // and the panel will close in the same frame, in which case the
        // monitor's "no matching panel" disarm path tears down whatever's
        // left of our state. If the engine doesn't cancel (future
        // editbox-bearing panels might bind Esc differently), the flag
        // drop alone is the correct behaviour: the user is now navigating
        // chain-style with the editbox still focused.
        acclog::Write("Editbox", "%s VK_ESCAPE (Win32) -> exit edit mode",
                      s.spec->logTag);
        s.editMode = false;
    }
}

// ============================================================================
// Spec lookup over the manager's panels[].
// ============================================================================

struct PanelMatch {
    const EditboxPanelSpec* spec;
    void* panel;
};

PanelMatch FindMatchingPanel() {
    PanelMatch m = {nullptr, nullptr};
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return m;
    auto* base = reinterpret_cast<unsigned char*>(mgr);
    int   panelCount = *reinterpret_cast<int*>(base + kMgrPanelsSizeOffset);
    void** panelData = *reinterpret_cast<void***>(base + kMgrPanelsDataOffset);
    if (!panelData || panelCount <= 0) return m;
    if (panelCount > 16) panelCount = 16;
    for (int i = 0; i < panelCount; ++i) {
        void* p = panelData[i];
        if (!p) continue;
        for (int s = 0; s < kNumSpecs; ++s) {
            if (kSpecs[s]->matches(p)) {
                m.spec = kSpecs[s];
                m.panel = p;
                return m;
            }
        }
    }
    return m;
}

void DisarmIfArmed(const char* reason) {
    if (!s_state.spec) return;
    acclog::Write("Editbox", "%s disarm: %s",
                  s_state.spec->logTag, reason);
    s_state = {nullptr, nullptr, nullptr, false, {0}, 0, 0, 0};
}

}  // namespace

// ============================================================================
// Public surface.
// ============================================================================

bool TryHandleInput(int /*n*/, void* /*thisPtr*/, void* activePanel,
                    int param_1, int param_2, int& outRv) {
    if (!s_state.editMode) return false;
    if (!activePanel || activePanel != s_state.panel) return false;
    if (param_2 == 0) return false;  // press-edge only

    // Esc — silent exit. The editbox keeps engine focus; subsequent Up/Down
    // fall through to chain nav so the user can move to other controls.
    // A second Esc (with editMode now false) reaches the engine's
    // panel-cancel path uneaten by us.
    if (param_1 == kInputEsc1 || param_1 == kInputEsc2) {
        acclog::Write("Editbox", "%s Esc -> exit edit mode (silent)",
                      s_state.spec->logTag);
        s_state.editMode = false;
        outRv = 1;
        return true;
    }

    // Enter — drop edit mode, let the engine's editbox handler commit the
    // name natively. We used to QueueActivate the panel's OK button here,
    // but that double-activates against the engine's own HandleDoneButton
    // path and breaks the modal stack (see the PollModalKeys Enter branch
    // for the full incident). In practice this branch is unreachable while
    // editMode is true (engine swallows Enter at the editbox before it
    // reaches the manager-level input hook — the user verified zero
    // Menus.Input lines fire during typing), but we keep the handler
    // consistent in case a future editbox spec routes Enter differently.
    if (param_1 == kInputEnter1 || param_1 == kInputEnter2) {
        acclog::Write("Editbox", "%s Enter -> edit-mode off "
                      "(engine handles submit natively)",
                      s_state.spec->logTag);
        s_state.editMode = false;
        outRv = 1;
        return true;
    }

    // Up / Down — re-speak the full current value. Single-line editbox so
    // there's no vertical caret motion to consume otherwise. Preserves the
    // user's ability to verify what they typed without losing focus.
    if (param_1 == kInputNavUp || param_1 == kInputNavDown) {
        // Refresh state first — caret may have moved between last poll and
        // this keypress; we want the announce to reflect the latest value.
        SnapshotInto(s_state);
        SpeakFullText(s_state.text, s_state.textLen);
        acclog::Write("Editbox", "%s %s -> re-speak text len=%u",
                      s_state.spec->logTag,
                      param_1 == kInputNavUp ? "Up" : "Down",
                      s_state.textLen);
        outRv = 1;
        return true;
    }

    // Letters / Backspace / Left / Right / etc. — let the engine handle
    // them; the per-tick monitor will catch the (text, caret) deltas and
    // announce them on the next tick.
    return false;
}

void TickEditboxMonitors() {
    PanelMatch m = FindMatchingPanel();
    if (!m.spec) {
        // No spec-matching panel anywhere. Disarm if we were armed against
        // a now-gone panel.
        DisarmIfArmed("no matching panel in stack");
        return;
    }

    void* editbox = m.spec->findEditbox(m.panel);
    if (!editbox) {
        DisarmIfArmed("findEditbox returned null");
        return;
    }

    // Read panel.activeControl to decide whether the editbox currently has
    // chain focus.
    void* activeControl = *reinterpret_cast<void**>(
        static_cast<unsigned char*>(m.panel) + kPanelActiveControlOffset);
    bool focusedNow = (activeControl == editbox);

    if (s_state.editbox != editbox) {
        // Either fresh entry (no prior arm) or re-arm onto a different
        // editbox (different panel instance). Tear down old state.
        if (s_state.spec) DisarmIfArmed("editbox pointer changed");
        if (focusedNow) {
            s_state.spec     = m.spec;
            s_state.panel    = m.panel;
            s_state.editbox  = editbox;
            s_state.editMode = true;
            SnapshotInto(s_state);

            // Suppress edge-detection for any key the user is already
            // holding when we arm — the Enter that activated the parent
            // panel's "Name" button to enter the editbox is often still
            // down on the first poll after arm, and would otherwise fire
            // a fresh Enter edge → submit → close the panel they just
            // opened (verified against patch-20260509-201147.log lines
            // 518-519 / 976-977: arm + VK_RETURN submit at the same
            // second). Consume each editbox-relevant edge so the first
            // PollModalKeys call this tick sees no rising edges on keys
            // the user happened to be holding when we armed.
            namespace hk = acc::hotkeys;
            hk::Consume(hk::Action::EditboxReReadUp);
            hk::Consume(hk::Action::EditboxReReadDown);
            hk::Consume(hk::Action::EditboxSubmit);
            hk::Consume(hk::Action::EditboxCancel);

            // Speak the focus-enter announce directly here with the
            // "Editbox. <value>" format the user expects. The drain path
            // (DrainPendingAnnounce) would otherwise also fire with bare
            // FromControl text on the next tick — clear the slot so we
            // own the announce.
            char msg[160];
            const char* role  = acc::strings::Get(acc::strings::Id::EditboxRole);
            const char* empty = acc::strings::Get(acc::strings::Id::EditboxEmpty);
            if (s_state.textLen > 0) {
                snprintf(msg, sizeof(msg), "%s. %s", role, s_state.text);
            } else {
                snprintf(msg, sizeof(msg), "%s. %s", role, empty);
            }
            tolk::Speak(msg, /*interrupt=*/false);
            acc::menus::ClearPendingAnnounce();

            acclog::Write("Editbox", "%s arm: panel=%p editbox=%p "
                          "initialText=\"%s\" len=%u rawLen=%u "
                          "shortA=%d shortB=%d spoke=\"%s\"",
                          m.spec->logTag, m.panel, editbox,
                          s_state.text, s_state.textLen, s_state.rawLenField,
                          s_state.shortA, s_state.shortB, msg);
        }
        return;
    }

    // Already armed against this editbox. Manage the focus edges.
    if (!focusedNow) {
        // Focus left the editbox (chain nav stepped to OK / Random /
        // Abbrechen). Disarm completely; if focus comes back, the next
        // tick's "fresh entry" branch will re-arm + auto-enter edit mode.
        DisarmIfArmed("focus left editbox");
        return;
    }

    // Focused + armed. If still in edit mode, run the text/state diff +
    // poll the modal Win32 keys. If not in edit mode (post-Esc), do
    // nothing — the user will navigate away with chain keys, and the
    // focus-leave branch above will tear down.
    if (s_state.editMode) {
        PollAndAnnounceDiff(s_state);
        PollModalKeys(s_state);
    }
}

const char* GetTitleOverride(void* panel) {
    if (!panel) return nullptr;
    for (int i = 0; i < kNumSpecs; ++i) {
        const EditboxPanelSpec& spec = *kSpecs[i];
        if (!spec.matches(panel)) continue;
        if (!spec.titleOverride) return nullptr;
        return spec.titleOverride(panel);
    }
    return nullptr;
}

}  // namespace acc::menus::editbox
