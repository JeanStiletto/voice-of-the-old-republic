// KOTOR Accessibility — virtual mod-settings submenu.
//
// See menus_modsettings.h for the design summary. This TU owns the
// sentinel pointer, the toggle bits, and the input router.

#include <windows.h>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "menus_modsettings.h"

#include "engine_input.h"
#include "engine_manager.h"  // IsPanelInManager for close-time rebind guard
#include "engine_panels.h"
#include "log.h"
#include "menus_chain.h"
#include "prism.h"
#include "strings.h"

namespace acc::menus::modsettings {

namespace {

// Sentinel: address-of a never-deref'd static byte. Casting to void*
// gives a stable, unique pointer that we can use as the chain entry's
// `control` field without ever pointing at an engine-allocated control.
char s_rootSentinel = 0;

// Toggle state. In-memory only — persistence is a follow-up; the
// initial cut just demonstrates that toggles round-trip through the
// announce path.
bool s_toggles[static_cast<int>(Option::Count)] = { false, false, false };

// Submenu state. `s_open` flips on OpenSubMenu / Close; `s_focused`
// is the currently-selected option index while open. `s_parentPanel`
// is the panel the user came from so Close can rebind the chain.
bool   s_open         = false;
int    s_focused      = 0;
void*  s_parentPanel  = nullptr;

struct OptionSpec {
    Option              option;
    acc::strings::Id    label;
};

constexpr OptionSpec k_options[] = {
    { Option::ExtendedCycling, acc::strings::Id::ModSettingExtendedCycling },
    { Option::RoomShapes,      acc::strings::Id::ModSettingRoomShapes      },
    { Option::WallSounds,      acc::strings::Id::ModSettingWallSounds      },
};
constexpr int k_optionCount = static_cast<int>(
    sizeof(k_options) / sizeof(k_options[0]));
static_assert(k_optionCount == static_cast<int>(Option::Count),
              "k_options must cover every Option enumerator");

const char* StateText(int optionIdx) {
    bool on = s_toggles[optionIdx];
    return acc::strings::Get(on ? acc::strings::Id::ModSettingStateOn
                                : acc::strings::Id::ModSettingStateOff);
}

// Speak the current focused option as "Name: state". Always uses
// Speak(interrupt=true) — normal NVDA / screen-reader speech with
// previous-utterance interrupt, NOT SAPI urgent. Per-feedback
// 2026-05-26: SpeakUrgent is reserved for cross-cancel events
// (compass turns, etc.); UI navigation should stay on the normal
// speech bus so it batches with chain step speech and respects the
// user's screen-reader rate / voice.
void SpeakFocusedOption() {
    if (s_focused < 0 || s_focused >= k_optionCount) return;
    const char* name = acc::strings::Get(k_options[s_focused].label);
    const char* st   = StateText(s_focused);
    char line[160];
    snprintf(line, sizeof(line),
             acc::strings::Get(acc::strings::Id::FmtModSettingOption),
             name, st);
    prism::Speak(line, /*interrupt=*/true);
    acclog::Write("ModSettings", "speak focused idx=%d \"%s\"",
                  s_focused, line);
}

// True iff something other than our parent Optionen panel is now
// foreground — typically a MessageBoxModal raised by Alt+F4 / quit-
// confirm, but also any other panel the engine pushed in front of
// our parent while the submenu was open, OR the parent panel itself
// being gone (engine teardown after Spiel beenden, save reload, etc.).
// We auto-close on this transition so the new modal can receive input
// cleanly.
bool ForegroundDivergedFromParent() {
    if (!s_parentPanel) return false;
    if (!acc::engine::IsPanelInManager(s_parentPanel)) return true;
    void* mgr = *reinterpret_cast<void**>(kAddrGuiManagerPtr);
    if (!mgr) return false;
    void* fg = acc::engine::GetForegroundPanel(mgr);
    if (!fg) return false;
    return fg != s_parentPanel;
}

}  // namespace

void* GetRootAnchor() {
    return &s_rootSentinel;
}

bool IsRootAnchor(void* control) {
    return control == &s_rootSentinel;
}

void ForEachRootAnchor(void* panel,
                       bool (*callback)(void* sentinel, int sortCx, int sortCy,
                                        void* userData),
                       void* userData) {
    if (!panel || !callback) return;
    auto kind = acc::engine::IdentifyPanel(panel);
    if (kind != acc::engine::PanelKind::InGameOptions &&
        kind != acc::engine::PanelKind::MainMenuOptions) {
        return;
    }
    // sortCy = a very large synthetic y. The chain's insertion sort is
    // by cy ascending; placing the virtual entry at y=9000 lands it at
    // the end of the chain — below every real sub-screen button and
    // (on InGameOptions) below the bottom-row "Schliess." / "Spiel
    // beenden" buttons. User hears the existing options first on
    // Down-arrow, then "Mod settings" as the last stop. Top-of-chain
    // wrap (Home) still works.
    //
    // sortCx = 180 (matches the left-aligned column the engine paints
    // every other Options button in — observed in the chain dump at
    // patch-20260520-115529.log). Keeps the cursor warp on chain step
    // visually consistent for a sighted observer.
    callback(&s_rootSentinel, /*sortCx=*/180, /*sortCy=*/9000, userData);
}

bool ExtractRootLabel(char* outBuf, size_t bufSize) {
    if (!outBuf || bufSize == 0) return false;
    const char* label =
        acc::strings::Get(acc::strings::Id::ModSettingsRootLabel);
    if (!label) {
        outBuf[0] = '\0';
        return false;
    }
    size_t i = 0;
    for (; i + 1 < bufSize && label[i]; ++i) outBuf[i] = label[i];
    outBuf[i] = '\0';
    return i > 0;
}

bool IsOpen() {
    return s_open;
}

void OpenSubMenu(void* parentPanel) {
    s_open        = true;
    s_focused     = 0;
    s_parentPanel = parentPanel;
    acclog::Write("ModSettings",
                  "submenu opened (parent=%p)", parentPanel);
    // Two-part open speech: "Mod settings opened" then the first
    // option. Speak(interrupt=true) on the title cuts the chain-step
    // tail without going through SAPI urgent (reserved for cross-
    // cancel cues); the option follow-up uses interrupt=false so it
    // queues right after.
    prism::Speak(
        acc::strings::Get(acc::strings::Id::ModSettingsOpened),
        /*interrupt=*/true);
    const char* name = acc::strings::Get(k_options[s_focused].label);
    const char* st   = StateText(s_focused);
    char line[160];
    snprintf(line, sizeof(line),
             acc::strings::Get(acc::strings::Id::FmtModSettingOption),
             name, st);
    prism::Speak(line, /*interrupt=*/false);
}

void Close() {
    if (!s_open) return;
    acclog::Write("ModSettings", "submenu closed (parent=%p)",
                  s_parentPanel);
    s_open    = false;
    s_focused = 0;
    void* parent = s_parentPanel;
    s_parentPanel = nullptr;
    prism::Speak(
        acc::strings::Get(acc::strings::Id::ModSettingsClosed),
        /*interrupt=*/true);
    // Rebind the chain on the parent panel so the next arrow press
    // walks the real options again. ValidateChainPanel-style guard:
    // skip the rebind if the parent has been freed under us (extremely
    // unlikely while the submenu is open, but cheap to check).
    if (parent && acc::engine::IsPanelInManager(parent)) {
        acc::menus::chain::RebindChain(parent);
    }
}

// Silent auto-close used when the engine pushes a new panel
// (MessageBoxModal etc.) in front of our parent. Unlike Close(), this
// path doesn't speak the "closed" cue — the surfacing panel will
// announce itself through the existing menu-monitor speech path, and
// we don't want to step on its cue. Also skips the parent-rebind
// because the chain rebuilder is going to fire anyway when the new
// panel becomes foreground.
void AutoCloseSilent() {
    if (!s_open) return;
    acclog::Write("ModSettings",
                  "auto-close (foreground diverged from parent=%p)",
                  s_parentPanel);
    s_open        = false;
    s_focused     = 0;
    s_parentPanel = nullptr;
}

bool HandleInput(int keyCode) {
    if (!s_open) return false;
    // Auto-close on foreground divergence — the engine pushed a new
    // panel (MessageBoxModal from Alt+F4 quit-confirm, etc.) in front
    // of our parent while we held input control. Releasing now lets
    // the press flow into the modal's own dispatcher; we'd otherwise
    // strand the user inside the submenu with no way to reach the
    // popup without Esc-closing us first. Verified scenario:
    // user-reported Alt+F4 popup unreachable until Esc.
    if (ForegroundDivergedFromParent()) {
        AutoCloseSilent();
        return false;
    }
    // Up / Down: step focus with end-clamp (no wrap — sighted "list
    // box" semantics match Optionen panels above and below).
    if (keyCode == kInputNavUp) {
        if (k_optionCount <= 0) return true;
        if (s_focused > 0) {
            --s_focused;
            SpeakFocusedOption();
        } else {
            // Already at top — re-announce so the user gets feedback.
            SpeakFocusedOption();
        }
        return true;
    }
    if (keyCode == kInputNavDown) {
        if (k_optionCount <= 0) return true;
        if (s_focused < k_optionCount - 1) {
            ++s_focused;
            SpeakFocusedOption();
        } else {
            SpeakFocusedOption();
        }
        return true;
    }
    // Enter: toggle the focused option, re-announce the new state.
    if (keyCode == kInputEnter1 || keyCode == kInputEnter2) {
        if (s_focused < 0 || s_focused >= k_optionCount) return true;
        s_toggles[s_focused] = !s_toggles[s_focused];
        acclog::Write("ModSettings", "toggle idx=%d new=%d",
                      s_focused, s_toggles[s_focused] ? 1 : 0);
        SpeakFocusedOption();
        return true;
    }
    // Esc: close back to the parent panel.
    if (keyCode == kInputEsc1 || keyCode == kInputEsc2) {
        Close();
        return true;
    }
    // Block every other GUI-key press from reaching the parent panel
    // while the submenu is logically focused. Without this, e.g. Left
    // / Right would dispatch through the chain's cycle-arrow path and
    // mutate state on the parent (sub-screen-button hover etc.) while
    // the user thinks they're inside Mod settings. Whitelist of keys
    // that we let through: none currently — every gui-relevant input
    // is consumed.
    switch (keyCode) {
    case kInputNavLeft:
    case kInputNavRight:
    case kInputHome:
    case kInputEnd:
    case kInputActivate:
        return true;
    default:
        // Unmapped scancodes (in-world hotkeys etc.) fall through.
        // The submenu is a UI-only state; we don't want to break the
        // user's ability to e.g. cycle volume bus or any other global
        // hotkey while it happens to be open.
        return false;
    }
}

bool GetToggle(Option option) {
    int idx = static_cast<int>(option);
    if (idx < 0 || idx >= k_optionCount) return false;
    return s_toggles[idx];
}

}  // namespace acc::menus::modsettings
