#include "pad_quickmenu.h"

#include <cstdio>

#include "engine_game.h"      // IsKotor2
#include "engine_input.h"     // kInputNav* / kInputEnter1 / kInputEsc1
#include "engine_keymap.h"    // GameActionScancode
#include "engine_manager.h"   // GetGuiManager / GetForegroundPanel
#include "engine_offsets_select.h"
#include "engine_panels.h"    // IdentifyPanel — PanelKind::GamepadQuickMenu
#include "engine_player.h"    // GetPlayerPosition
#include "engine_reads.h"     // TryReadU32 / TryReadU16
#include "menus_extract.h"    // FromControl — the entry captions
#include "help.h"             // ToggleMenu — the Help entry
#include "key_inject.h"
#include "log.h"
#include "pad_input.h"      // EnginePadDead — is the mod the pad driver here?
#include "prism.h"
#include "strings.h"

namespace acc::pad::quickmenu {

namespace {

using S = acc::strings::Id;

// One row. `actionId` is a swkotor.ini [Keymapping] id (keymap.2da row + 200)
// and `defaultDik` that row's own key, used when the ini carries no line for
// it. actionId 0 marks the Help row, which fires no engine key at all.
//
// Order is the reading order, and it is deliberate: the two entries a player
// reaches for most (the menu screens, switching who they are) come first, and
// Help sits last exactly where KOTOR 2's own gamepad menu puts it.
struct Entry {
    S   label;
    int actionId;
    int defaultDik;
};

constexpr Entry kEntries[] = {
    {S::PadQuickMenus,       223, 0x01},   // GUI          — default Escape
    {S::PadQuickPartyLeader, 206, 0x0F},   // ChangeChar   — default Tab
    {S::PadQuickSoloMode,    207, 0x2F},   // PartyActive  — default V
    {S::PadQuickStealth,     264, 0x22},   // STEALTH      — default G
    {S::PadQuickSave,        218, 0x3E},   // Quicksave    — default F4
    {S::PadQuickHelp,          0, 0x00},   // the mod's key list
};
constexpr int kEntryCount =
    static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

// KOTOR 1's Free Look (row 8) is absent on purpose. Its default bind is Caps
// Lock, which is the screen reader's own modifier key and a stateful OS toggle
// — injecting it would fire NVDA and leave the keyboard latched. It is also
// the one entry on KOTOR 2's list with nothing to offer a blind player.

bool g_open  = false;
int  g_focus = 0;

void SpeakRow(bool interrupt) {
    if (g_focus < 0 || g_focus >= kEntryCount) return;
    char line[256];
    std::snprintf(line, sizeof(line), acc::strings::Get(S::FmtHelpRowOf),
                  acc::strings::Get(kEntries[g_focus].label), g_focus + 1,
                  kEntryCount);
    prism::Speak(line, interrupt);
}

// Clamp, never wrap: hearing the same line twice is the boundary cue.
void StepFocus(int delta) {
    const int next = g_focus + delta;
    if (next >= 0 && next < kEntryCount) g_focus = next;
    SpeakRow(/*interrupt=*/true);
}

void Activate() {
    const Entry& e = kEntries[g_focus];

    // Help opens the mod's own key list, which is a mod surface and must not
    // be preceded by a synthesised keystroke. Close first either way: the
    // engine action lands on the world, not on a list that is still up.
    if (e.actionId == 0) {
        acclog::Write("Pad.quick", "activate -> mod key list");
        Close();
        acc::help::ToggleMenu();
        return;
    }

    int scan = acc::engine_keymap::GameActionScancode(e.actionId);
    if (scan == 0) scan = e.defaultDik;
    acclog::Write("Pad.quick", "activate entry %d -> action %d (scan 0x%02x)",
                  g_focus, e.actionId, scan);
    Close();
    acc::key_inject::Tap(scan);
}

// ---- KOTOR 2 engine quick menu ---------------------------------------------

constexpr int kEngineEntryCount   = 8;
constexpr int kEngineEntryStealth = 3;
constexpr int kEngineEntryHelp    = 7;

void* g_enginePanel = nullptr;   // the menu last seen in the foreground
int   g_engineSel   = -1;        // its selection when last spoken

bool IsEngineMenu(void* panel) {
    return panel && acc::game::IsKotor2() &&
           acc::engine::IdentifyPanel(panel) ==
               acc::engine::PanelKind::GamepadQuickMenu;
}

// -1 when unreadable or outside the eight entries.
int ReadEngineSelection(void* panel) {
    uint32_t sel = 0;
    if (!acc::engine::TryReadU32(panel, kGamepadMenuSelectedEntryOffset, &sel)) {
        return -1;
    }
    const int s = static_cast<int>(sel);
    return (s >= 0 && s < kEngineEntryCount) ? s : -1;
}

// The engine's step, minus the wrap: where Up/Down from `sel` lands, or -1
// when it would run off an end. Mirrors the handler exactly — one step, one
// more past Stealth while stealth is unavailable.
int EngineStepTarget(void* panel, int sel, int delta) {
    uint16_t gate = 0;
    const bool stealthOk =
        !acc::engine::TryReadU16(panel, kGamepadMenuStealthAvailableOffset,
                                 &gate) ||
        (gate & 0xff) != 0;
    int next = sel + delta;
    if (!stealthOk && next == kEngineEntryStealth) next += delta;
    return (next >= 0 && next < kEngineEntryCount) ? next : -1;
}

void SpeakEngineEntry(void* panel, int sel, bool interrupt) {
    if (sel < 0 || !acc::off::Ok(kGamepadMenuCaptionButtonsOffset)) return;
    void* button = acc::off::Ptr(
        panel, kGamepadMenuCaptionButtonsOffset +
                   static_cast<size_t>(sel) * kGamepadMenuButtonStride);
    char text[128];
    if (!acc::menus::extract::FromControl(button, text, sizeof(text), panel)) {
        acclog::Write("Pad.quick", "engine menu entry %d has no caption", sel);
        return;
    }
    prism::Speak(text, interrupt);
}

void TickEngineMenu() {
    if (!acc::game::IsKotor2()) return;
    void* mgr = acc::engine::GetGuiManager();
    void* fg  = mgr ? acc::engine::GetForegroundPanel(mgr) : nullptr;
    if (!IsEngineMenu(fg)) {
        if (g_enginePanel) acclog::Write("Pad.quick", "engine menu gone");
        g_enginePanel = nullptr;
        g_engineSel   = -1;
        return;
    }
    const int sel = ReadEngineSelection(fg);
    if (fg != g_enginePanel) {
        g_enginePanel = fg;
        g_engineSel   = sel;
        acclog::Write("Pad.quick", "engine menu opened, entry %d", sel);
        prism::Speak(acc::strings::Get(S::PadQuickMenuOpened), /*interrupt=*/true);
        SpeakEngineEntry(fg, sel, /*interrupt=*/false);
        return;
    }
    if (sel == g_engineSel) return;
    acclog::Write("Pad.quick", "engine menu entry %d -> %d", g_engineSel, sel);
    g_engineSel = sel;
    SpeakEngineEntry(fg, sel, /*interrupt=*/true);
}

}  // namespace

bool FilterEngineMenuInput(void* panel, int code, int value, bool& consume) {
    consume = false;
    switch (code) {
        case kInputNavUp:   case kInputNavDown:
        case kInputNavLeft: case kInputNavRight:
        case kInputEnter1:  case kInputEnter2:
        case kInputEsc1:    case kInputEsc2:
        case kInputHome:    case kInputEnd:
            break;
        default:
            return false;
    }
    if (!IsEngineMenu(panel)) return false;
    // Releases carry no action; the engine gets them as it always did.
    if (value == 0) return true;

    const int sel = ReadEngineSelection(panel);
    switch (code) {
        case kInputHome:
        case kInputEnd:
            consume = true;
            return true;
        case kInputNavUp:
        case kInputNavDown:
            if (sel >= 0 &&
                EngineStepTarget(panel, sel,
                                 code == kInputNavUp ? -1 : +1) < 0) {
                acclog::Write("Pad.quick", "engine menu clamp at entry %d", sel);
                consume = true;
                SpeakEngineEntry(panel, sel, /*interrupt=*/true);
            }
            return true;
        case kInputEnter1:
        case kInputEnter2:
            if (sel == kEngineEntryHelp) {
                acclog::Write("Pad.quick", "engine menu Help -> mod key list");
                consume = true;
                acc::help::ToggleMenu();
            }
            return true;
        default:
            // Left / Right switch the party leader inside the menu; Esc closes.
            return true;
    }
}

bool IsOpen() { return g_open; }

void Open() {
    if (g_open) return;
    // KOTOR 2 has the engine's own quick-menu panel — except when its engine
    // never saw the pad, where that panel cannot be reached at all (Y is one of
    // the events that are not arriving) and this menu is the pad's only route
    // to the game screens.
    if (!acc::pad::EnginePadDead()) return;
    // The F1 list gets first refusal on every nav code, so a quick menu opened
    // underneath it would be unreachable — audible, focused, and deaf. Decline
    // instead of stacking.
    if (acc::help::IsMenuOpen()) {
        acclog::Write("Pad.quick", "declined — the key list is open");
        return;
    }
    g_open  = true;
    g_focus = 0;
    acclog::Write("Pad.quick", "opened");
    prism::Speak(acc::strings::Get(S::PadQuickMenuOpened), /*interrupt=*/true);
    SpeakRow(/*interrupt=*/false);
}

void Close() {
    if (!g_open) return;
    g_open  = false;
    g_focus = 0;
    acclog::Write("Pad.quick", "closed");
    prism::Speak(acc::strings::Get(S::PadQuickMenuClosed), /*interrupt=*/true);
}

void Toggle() {
    if (g_open) Close();
    else        Open();
}

bool HandleNavCode(int code) {
    if (!g_open) return false;
    switch (code) {
        case kInputNavUp:   StepFocus(-1); return true;
        case kInputNavDown: StepFocus(+1); return true;
        case kInputEnter1:
        case kInputEnter2:
            Activate();
            return true;
        case kInputEsc1:
        case kInputEsc2:
            Close();
            return true;
        default:
            // Left / Right mean nothing in a flat list. Swallow them so the
            // world underneath cannot cycle objects beneath an open menu.
            if (code == kInputNavLeft || code == kInputNavRight) return true;
            return false;
    }
}

void Tick() {
    TickEngineMenu();
    if (!g_open) return;
    Vector p;
    if (!acc::engine::GetPlayerPosition(p)) {
        // The world went away under it (area load, teardown). Close silently:
        // the transition has its own announce path and must not be spoken over.
        acclog::Write("Pad.quick", "self-disarm: player position lost");
        g_open  = false;
        g_focus = 0;
    }
}

}  // namespace acc::pad::quickmenu
