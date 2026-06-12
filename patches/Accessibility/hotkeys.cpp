// central hotkey registry implementation.
// See hotkeys.h for the design rationale + API contract.

#include "hotkeys.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "user32.lib")

namespace acc::hotkeys {

namespace {

// ----- Default bindings ------------------------------------------------------
// One row per `Action`. Indexed by static_cast<int>(Action). Edit defaults
// here — every call site reads through `g_bindings`, so changing a row
// here changes the binding mod-wide. Future rebind UI overwrites at runtime
// via `Set()`.
Binding g_bindings[static_cast<int>(Action::COUNT)] = {};

// Per-Action edge-detection state. Stored in arrays parallel to `g_bindings`
// so an Action's index lookup is a single subscript. `now` is sampled in
// BeginTick; `last` is shifted from `now` in EndTick. `Pressed` returns
// `now && !last` (plus foreground gate).
//
// `claimed` is a one-shot guard set by ClaimRisingEdge from sites that
// fire between EndTick and BeginTick (the engine's input-dispatch window),
// where Consume() can't suppress the upcoming rising edge because
// BeginTick hasn't run yet. EndTick clears the guard so the next genuine
// rising edge fires normally.
struct EdgeState {
    bool now;
    bool last;
    bool claimed;
};
EdgeState g_edge[static_cast<int>(Action::COUNT)] = {};

// Per-Action name strings for logs / future rebind UI. One slot per Action;
// must be kept in sync with the enum. Lined up vertically so a missing row
// is visually obvious.
const char* const kActionNames[static_cast<int>(Action::COUNT)] = {
    "InteractTarget",
    "InteractForceRadial",
    "TargetKey1",
    "TargetKey2",
    "TargetKey3",
    "PersonalKey1",
    "PersonalKey2",
    "PersonalKey3",
    "PersonalKey4",
    "ActionBarOpen1",
    "ActionBarOpen2",
    "ActionBarOpen3",
    "ActionBarOpen4",
    "TargetActionOpen1",
    "TargetActionOpen2",
    "TargetActionOpen3",
    "LevelUpOpen",
    "ExamineOpen",
    "CombatQueueOpen",
    "SelfStatusAnnounce",
    "NavUp",
    "NavDown",
    "NavLeft",
    "NavRight",
    "NavHome",
    "NavEnd",
    "SubmenuEsc",
    "QueueClearAll",
    "ContainerGiveMode",
    "StoreModeToggle",
    "CycleItemPrev",
    "CycleCategoryPrev",
    "CycleItemNext",
    "CycleCategoryNext",
    "CycleItemFirst",
    "CycleItemLast",
    "AnnounceFocus",
    "PathfindFocus",
    "PathfindFocusForce",
    "BeaconFocus",
    "AnnounceDegrees",
    "PartyLeaderAnnounce",
    "CameraOrient",
    "SaveMarkerAtCursor",
    "ViewModeToggle",
    "CameraStateProbe",
    "EditboxReReadUp",
    "EditboxReReadDown",
    "EditboxSubmit",
    "EditboxCancel",
    "CheckForUpdate",
    "ProbePathfind",
    "ProbeAudioCycle",
    "ProbeAudioFire",
    "ProbeCameraDump",
    "ProbeMouseLookToggle",
    "ProbeCameraDistDump",
    "ProbeCameraDistClampToggle",
    "PazaakStand",
    "PazaakEndTurn",
    "PazaakReviewHand",
    "PazaakReviewTable",
    "PazaakNextCard",
    "PazaakPrevCard",
    "PazaakPlay",
    "PazaakOptLeft",
    "PazaakOptRight",
    "PazaakCancel",
    "PazaakOppHand",
    "TurretCyclePrev",
    "TurretCycleNext",
    "DialogRepeatLine",
};

bool IsDownVk(int vk) {
    if (vk == 0) return false;
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Compute the current modifier mask from OS state. Reads VK_SHIFT (either
// shift), VK_CONTROL (either ctrl), VK_MENU (either alt), and VK_RMENU
// (right Alt = AltGr on QWERTZ) independently so `kModAltGr` can be set
// alongside `kModAlt` when AltGr is held (engine treats them as one Alt;
// we keep them split because announce_degrees needs to distinguish).
uint32_t ReadModifiers() {
    uint32_t m = 0;
    if (IsDownVk(VK_SHIFT) || IsDownVk(VK_LSHIFT) || IsDownVk(VK_RSHIFT)) {
        m |= kModShift;
    }
    if (IsDownVk(VK_CONTROL) || IsDownVk(VK_LCONTROL) || IsDownVk(VK_RCONTROL)) {
        m |= kModCtrl;
    }
    if (IsDownVk(VK_MENU) || IsDownVk(VK_LMENU) || IsDownVk(VK_RMENU)) {
        m |= kModAlt;
    }
    if (IsDownVk(VK_RMENU)) {
        m |= kModAltGr;
    }
    return m;
}

// Match a binding against the current keyboard state. VK pressed (primary
// or alt) AND every required mod held AND no forbidden mod held.
bool BindingMatches(const Binding& b, uint32_t mods) {
    if (b.vk == 0 && b.altVk == 0) return false;
    bool keyDown = IsDownVk(b.vk) || (b.altVk != 0 && IsDownVk(b.altVk));
    if (!keyDown) return false;
    if ((mods & b.modsRequired)  != b.modsRequired)  return false;
    if ((mods & b.modsForbidden) != 0)               return false;
    return true;
}

// ----- Default-binding initialiser ------------------------------------------
// Filled the first time anything in this TU runs. Guarded so callers don't
// have to remember to call an init step — every entry point in the header
// (BeginTick, Pressed, Get, ...) routes through here.
//
// Order MUST match the Action enum. Mismatched ordering is the kind of bug
// that only surfaces at runtime as "Shift+B reads as `B` was pressed", so
// keep the comments aligned with `Action` declaration order in hotkeys.h.
bool g_inited = false;

void InitDefaults() {
    if (g_inited) return;
    g_inited = true;

    auto bind = [](Action a, int vk, int altVk = 0,
                   uint32_t req = 0, uint32_t forbid = 0) {
        g_bindings[static_cast<int>(a)] = {vk, altVk, req, forbid};
    };

    // ----- World interaction -----
    bind(Action::InteractTarget,       VK_RETURN, 0, 0,         kModShift);
    bind(Action::InteractForceRadial,  VK_RETURN, 0, kModShift, 0);
    bind(Action::TargetKey1,           '1',       0, 0,         kModShift);
    bind(Action::TargetKey2,           '2',       0, 0,         kModShift);
    bind(Action::TargetKey3,           '3',       0, 0,         kModShift);
    bind(Action::PersonalKey1,         '4',       0, 0,         kModShift);
    bind(Action::PersonalKey2,         '5',       0, 0,         kModShift);
    bind(Action::PersonalKey3,         '6',       0, 0,         kModShift);
    bind(Action::PersonalKey4,         '7',       0, 0,         kModShift);
    bind(Action::ActionBarOpen1,       '4',       0, kModShift, 0);
    bind(Action::ActionBarOpen2,       '5',       0, kModShift, 0);
    bind(Action::ActionBarOpen3,       '6',       0, kModShift, 0);
    bind(Action::ActionBarOpen4,       '7',       0, kModShift, 0);
    bind(Action::TargetActionOpen1,    '1',       0, kModShift, 0);
    bind(Action::TargetActionOpen2,    '2',       0, kModShift, 0);
    bind(Action::TargetActionOpen3,    '3',       0, kModShift, 0);
    bind(Action::LevelUpOpen,          'L',       0, kModShift, 0);
    // Bare Ö (VK_OEM_3 on the German layout — the right-pinky home key).
    // The engine binds every quick-menu *letter* (J/K/L/M, U/I/O/P), so a
    // Shift+letter open would leak to the engine's screen on the bare key
    // (e.g. Shift+K still opened Skills). Ö is engine-unbound, so a bare
    // press is safe; forbid every modifier so it stays distinct.
    bind(Action::ExamineOpen,          VK_OEM_3,  0, 0,
                                       kModShift | kModCtrl | kModAlt | kModAltGr);
    // Shift+H — H is engine-unbound, so Shift+H does NOT leak to any engine
    // screen (unlike the former Shift+K, which the engine read as plain K =
    // Skills/Feats/Force Powers). Bare H below is SelfStatusAnnounce.
    bind(Action::CombatQueueOpen,      'H',       0, kModShift, 0);
    // Bare H — quick HP / effects / equipped-weapon readout for the
    // currently-controlled leader. Shift+H is CombatQueueOpen (the
    // action-queue submenu), so forbid every modifier here to keep the
    // two routes distinct.
    bind(Action::SelfStatusAnnounce,   'H',       0, 0,
                                       kModShift | kModCtrl | kModAlt | kModAltGr);

    // ----- Submenu navigation -----
    // No modifier gate — these are pure routing keys. The Shift+Enter
    // QueueClearAll variant lives separately below.
    bind(Action::NavUp,                VK_UP,     0, 0, 0);
    bind(Action::NavDown,              VK_DOWN,   0, 0, 0);
    bind(Action::NavLeft,              VK_LEFT,   0, 0, 0);
    bind(Action::NavRight,             VK_RIGHT,  0, 0, 0);
    // Home / End: the engine drops these scancodes before our manager hook
    // (no [Keymapping] Action maps to KEYBOARD_HOME(32) / KEYBOARD_END(33)
    // in stock kotor.ini, verified empirically — patch-20260522-102841.log
    // shows zero events for these codes). Win32 polling is the only path
    // that reaches us, same as the cycle keys. menus.cpp's PollHomeEndKeys
    // synthesises a manager HandleInputEvent on rising edge.
    bind(Action::NavHome,              VK_HOME,   0, 0, 0);
    bind(Action::NavEnd,               VK_END,    0, 0, 0);
    bind(Action::SubmenuEsc,           VK_ESCAPE, 0, 0, 0);
    bind(Action::QueueClearAll,        VK_RETURN, 0, kModShift, 0);

    // ----- Container give-mode -----
    // Q or E — either key toggles. Mirrors the engine's in-panel Q/E
    // semantics (panel/character cycle) into "cycle the view". Container
    // and Store aren't part of the engine's strip cycle, so Q/E in those
    // panels is engine-noop and free for us to repurpose.
    bind(Action::ContainerGiveMode,    'Q', 'E', 0, 0);

    // ----- Store mode toggle. Same keys as ContainerGiveMode — the two
    // handlers gate on the foreground panel kind, so only one fires per
    // press.
    bind(Action::StoreModeToggle,      'Q', 'E', 0, 0);

    // ----- In-world cycle -----
    // Cycle navigate keys use VK_OEM_COMMA / VK_OEM_PERIOD — same VK on
    // both QWERTY and QWERTZ. Modifier-precedence at the "/-" key is
    // expressed by forbidding the higher-precedence mods so each variant
    // only fires when no stronger modifier is held: AnnounceFocus forbids
    // all three; PathfindFocus (Shift) forbids Alt+Ctrl; PathfindFocusForce
    // (Alt) forbids Ctrl; BeaconFocus (Ctrl) forbids AltGr (otherwise
    // QWERTZ AltGr+- double-fires as beacon because Windows synthesises a
    // phantom Ctrl alongside RMENU).
    bind(Action::CycleItemPrev,        VK_OEM_COMMA,  0, 0,         kModShift | kModCtrl);
    bind(Action::CycleCategoryPrev,    VK_OEM_COMMA,  0, kModShift, 0);
    bind(Action::CycleItemNext,        VK_OEM_PERIOD, 0, 0,         kModShift | kModCtrl);
    bind(Action::CycleCategoryNext,    VK_OEM_PERIOD, 0, kModShift, 0);
    // Ctrl+, / Ctrl+. jump to the first / last item of the current category.
    // Forbid Shift/Alt/AltGr so they stay distinct from the item/category
    // steps above and so QWERTZ AltGr+,/. (phantom Ctrl) doesn't fire them.
    bind(Action::CycleItemFirst,       VK_OEM_COMMA,  0, kModCtrl,  kModShift | kModAlt | kModAltGr);
    bind(Action::CycleItemLast,        VK_OEM_PERIOD, 0, kModCtrl,  kModShift | kModAlt | kModAltGr);
    bind(Action::AnnounceFocus,        VK_OEM_2, VK_OEM_MINUS, 0,
                                       kModShift | kModCtrl | kModAlt);
    bind(Action::PathfindFocus,        VK_OEM_2, VK_OEM_MINUS, kModShift,
                                       kModCtrl | kModAlt);
    bind(Action::PathfindFocusForce,   VK_OEM_2, VK_OEM_MINUS, kModAlt,
                                       kModCtrl);
    bind(Action::BeaconFocus,          VK_OEM_2, VK_OEM_MINUS, kModCtrl,
                                       kModAltGr);

    // ----- Orientation & party -----
    // AnnounceDegrees: AltGr alone, no Shift. The mouse-look diagnostic
    // probe shares the key with Shift held, so forbid shift here to keep
    // the two routes distinct.
    bind(Action::AnnounceDegrees,      VK_RMENU, 0, 0, kModShift);
    bind(Action::PartyLeaderAnnounce,  VK_TAB,   0, 0, 0);
    // N alone — camera-orient (face beacon's next waypoint when armed,
    // else cycle camera CW to the next cardinal). Shift forbidden so we
    // don't fight SaveMarkerAtCursor (Shift+N drops a map marker).
    bind(Action::CameraOrient,         'N', 0, 0,
                                       kModShift | kModCtrl | kModAlt | kModAltGr);

    // ----- Map saved markers -----
    // Shift+N drops a marker at the map cursor's current world position.
    // N has zero engine-vanilla binding (verified against the controls-
    // and-input.md "Default keyboard controls" survey), so picking N
    // avoids the engine intercepting the keypress. Forbid Ctrl/Alt/AltGr
    // so this stays distinct from any future modifier variants.
    bind(Action::SaveMarkerAtCursor,   'N', 0, kModShift,
                                       kModCtrl | kModAlt | kModAltGr);

    // ----- View mode -----
    bind(Action::ViewModeToggle,       'B', 0, 0,         kModShift);
    bind(Action::CameraStateProbe,     'B', 0, kModShift, 0);

    // ----- Editbox modal -----
    // Same VKs as the submenu nav keys above; routing happens at the
    // call site (menus_editbox queries only while edit-mode armed).
    bind(Action::EditboxReReadUp,      VK_UP,     0, 0, 0);
    bind(Action::EditboxReReadDown,    VK_DOWN,   0, 0, 0);
    bind(Action::EditboxSubmit,        VK_RETURN, 0, 0, 0);
    bind(Action::EditboxCancel,        VK_ESCAPE, 0, 0, 0);

    // ----- Help system -----
    // F1 toggles the global keybind list; Ctrl+F1 reads the current screen's
    // keys. Both are Win32-polled (help::PollWin32) so they fire in every
    // context — in-world, menus, dialog, map. F1 forbids every modifier so it
    // stays distinct from Ctrl+F1; Ctrl+F1 requires Ctrl and forbids the rest.
    // The engine reuses InputIndex 0x27 (KEYBOARD_F1) as its GUI "activate"
    // code, so the menu manager hook also suppresses a raw F1 reaching a panel
    // (see menus.cpp) to stop F1 doubling as Enter in menus.
    bind(Action::HelpMenuOpen,         VK_F1, 0, 0,
                                       kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::HelpContext,          VK_F1, 0, kModCtrl,
                                       kModShift | kModAlt | kModAltGr);

    // ----- In-game auto-updater -----
    // F5 alone. Forbid every modifier so a stray Shift/Ctrl/Alt+F5 (no
    // sibling binding yet, but cheap insurance) can't trigger the
    // installer download mid-gameplay. Per-tick gate in update_checker
    // refuses the press once the game world has loaded.
    bind(Action::CheckForUpdate,       VK_F5, 0, 0,
                                       kModShift | kModCtrl | kModAlt | kModAltGr);

    // ----- Diagnostic probes -----
    // Plain F9-F12 forbid Ctrl so the Option-B distance probes below can
    // own Ctrl+F11 / Ctrl+F12 without double-firing the bare-F-key probes.
    bind(Action::ProbePathfind,        VK_F9,  0, 0, kModCtrl);
    bind(Action::ProbeAudioCycle,      VK_F10, 0, 0, kModCtrl);
    // F11 unbound — the camera-distance clamp toggle (Ctrl+F11) needs the
    // key free for sustained per-mode testing; the fixed-North audio probe
    // it formerly hosted was getting in the way. Rebind here when re-
    // investigating the listener-frame question.
    bind(Action::ProbeAudioFire,       0,      0, 0, 0);
    bind(Action::ProbeCameraDump,      VK_F12, 0, 0, kModCtrl);
    bind(Action::ProbeMouseLookToggle, VK_RMENU, 0, kModShift, 0);

    // Option-B camera-distance probes. Ctrl-modified to disambiguate from
    // the bare F-key probes above. See probe_camera_distance.h for the
    // engine surfaces and intent.
    bind(Action::ProbeCameraDistDump,        VK_F12, 0, kModCtrl, 0);
    bind(Action::ProbeCameraDistClampToggle, VK_F11, 0, kModCtrl, 0);

    // ----- Pazaak minigame -----
    // Letter keys forbid every modifier so they stay distinct from Shift/
    // Ctrl combos elsewhere; the pollers in pazaak.cpp gate on the board
    // being foreground. Tab / Enter / arrows / Esc reuse the standard VKs —
    // the pazaak tick Consume()s the in-world / menu actions that share them
    // so only one handler fires per press.
    bind(Action::PazaakStand,       'S',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::PazaakEndTurn,     'E',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::PazaakReviewHand,  'C',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::PazaakReviewTable, 'T',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::PazaakNextCard,    VK_TAB,    0, 0,         kModShift);
    bind(Action::PazaakPrevCard,    VK_TAB,    0, kModShift, 0);
    bind(Action::PazaakPlay,        VK_RETURN, 0, 0,         kModShift);
    bind(Action::PazaakOptLeft,     VK_LEFT,   0, 0,         0);
    bind(Action::PazaakOptRight,    VK_RIGHT,  0, 0,         0);
    bind(Action::PazaakCancel,      VK_ESCAPE, 0, 0,         0);
    // Shift+C — opponent's remaining hand count (public info: sighted players
    // see the face-down cards). Bare C is PazaakReviewHand, so require Shift.
    bind(Action::PazaakOppHand,     'C',       0, kModShift, kModCtrl | kModAlt | kModAltGr);

    // ----- Turret minigame -----
    // Q/E cycle the locked fighter; polled only while the gunner minigame is
    // active (turret_game.cpp). Forbid every modifier like the Pazaak letters.
    bind(Action::TurretCyclePrev,   'Q',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
    bind(Action::TurretCycleNext,   'E',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);

    // ----- Dialog -----
    // Bare R re-speaks the current NPC dialog line. Vanilla R is the in-world
    // "default action on current target", but a dialog screen has no selected
    // world target, so R is free here. Polled (Win32) only while a dialog panel
    // is foreground (dialog_speech.cpp), like the cycle keys. Forbid every
    // modifier so it stays distinct from any future Shift/Ctrl+R combo.
    bind(Action::DialogRepeatLine,  'R',       0, 0,         kModShift | kModCtrl | kModAlt | kModAltGr);
}

}  // namespace

// ----- Lifecycle ------------------------------------------------------------

void BeginTick() {
    InitDefaults();
    uint32_t mods = ReadModifiers();
    for (int i = 0; i < static_cast<int>(Action::COUNT); ++i) {
        g_edge[i].now = BindingMatches(g_bindings[i], mods);
    }
}

void EndTick() {
    for (int i = 0; i < static_cast<int>(Action::COUNT); ++i) {
        g_edge[i].last = g_edge[i].now;
        g_edge[i].claimed = false;
    }
}

// ----- Queries --------------------------------------------------------------

bool IsForegroundGame() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

bool Pressed(Action a) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return false;
    if (g_edge[idx].claimed) return false;
    if (!g_edge[idx].now || g_edge[idx].last) return false;
    return IsForegroundGame();
}

bool Held(Action a) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return false;
    return g_edge[idx].now;
}

void Consume(Action a) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return;
    // Force `last == now` so Pressed() returns false for the rest of this
    // tick. EndTick will re-run `last = now`, which is idempotent here.
    g_edge[idx].last = g_edge[idx].now;
}

void ClaimRisingEdge(Action a) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return;
    g_edge[idx].claimed = true;
}

bool ShiftHeld() {
    return IsDownVk(VK_SHIFT) || IsDownVk(VK_LSHIFT) || IsDownVk(VK_RSHIFT);
}

bool CtrlHeld() {
    return IsDownVk(VK_CONTROL) || IsDownVk(VK_LCONTROL) || IsDownVk(VK_RCONTROL);
}

bool AltHeld() {
    return IsDownVk(VK_MENU) || IsDownVk(VK_LMENU) || IsDownVk(VK_RMENU);
}

bool AltGrHeld() {
    return IsDownVk(VK_RMENU);
}

// ----- Rebinding ------------------------------------------------------------

Binding Get(Action a) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return {};
    return g_bindings[idx];
}

void Set(Action a, Binding b) {
    InitDefaults();
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return;
    g_bindings[idx] = b;
    // Reset edge state — a brand-new binding shouldn't fire on the keys
    // the user happens to be holding at the moment of the rebind.
    g_edge[idx].now     = false;
    g_edge[idx].last    = false;
    g_edge[idx].claimed = false;
}

bool IsUserRebindable(Action a) {
    switch (a) {
    case Action::ProbePathfind:
    case Action::ProbeAudioCycle:
    case Action::ProbeAudioFire:
    case Action::ProbeCameraDump:
    case Action::ProbeMouseLookToggle:
    case Action::ProbeCameraDistDump:
    case Action::ProbeCameraDistClampToggle:
        return false;
    default:
        return true;
    }
}

// ----- Names + descriptions -------------------------------------------------

const char* Name(Action a) {
    int idx = static_cast<int>(a);
    if (idx < 0 || idx >= static_cast<int>(Action::COUNT)) return "?";
    return kActionNames[idx];
}

namespace {

const char* VkLabel(int vk) {
    switch (vk) {
    case 0:             return "(unbound)";
    case VK_RETURN:     return "Enter";
    case VK_ESCAPE:     return "Esc";
    case VK_TAB:        return "Tab";
    case VK_UP:         return "Up";
    case VK_DOWN:       return "Down";
    case VK_LEFT:       return "Left";
    case VK_RIGHT:      return "Right";
    case VK_HOME:       return "Home";
    case VK_END:        return "End";
    case VK_SPACE:      return "Space";
    case VK_OEM_COMMA:  return ",";
    case VK_OEM_PERIOD: return ".";
    case VK_OEM_MINUS:  return "-";
    case VK_OEM_2:      return "/";
    case VK_OEM_3:      return "\xD6";  // Ö on the German layout
    case VK_RMENU:      return "AltGr";
    case VK_LMENU:      return "LAlt";
    case VK_MENU:       return "Alt";
    case VK_LCONTROL:   return "LCtrl";
    case VK_RCONTROL:   return "RCtrl";
    case VK_CONTROL:    return "Ctrl";
    case VK_LSHIFT:     return "LShift";
    case VK_RSHIFT:     return "RShift";
    case VK_SHIFT:      return "Shift";
    case VK_F1:         return "F1";
    case VK_F2:         return "F2";
    case VK_F3:         return "F3";
    case VK_F4:         return "F4";
    case VK_F5:         return "F5";
    case VK_F6:         return "F6";
    case VK_F7:         return "F7";
    case VK_F8:         return "F8";
    case VK_F9:         return "F9";
    case VK_F10:        return "F10";
    case VK_F11:        return "F11";
    case VK_F12:        return "F12";
    default: break;
    }
    // Printable ASCII range that VK codes match directly (A-Z, 0-9).
    if (vk >= '0' && vk <= '9') {
        static thread_local char digit[2] = {0, 0};
        digit[0] = static_cast<char>(vk);
        return digit;
    }
    if (vk >= 'A' && vk <= 'Z') {
        static thread_local char letter[2] = {0, 0};
        letter[0] = static_cast<char>(vk);
        return letter;
    }
    return "?";
}

// Rotating buffer pool so several Describe() calls in one log line can
// coexist. 4 slots × 32 chars covers "Shift+Ctrl+AltGr+OEM_MINUS" easily.
char  g_describeBufs[4][32];
int   g_describeNext = 0;

}  // namespace

const char* Describe(Action a) {
    Binding b = Get(a);
    char* out = g_describeBufs[g_describeNext];
    g_describeNext = (g_describeNext + 1) % 4;
    out[0] = '\0';

    auto appendStr = [&](const char* s) {
        size_t cur = std::strlen(out);
        size_t rem = sizeof(g_describeBufs[0]) - cur;
        if (rem <= 1) return;
        std::strncat(out, s, rem - 1);
    };

    // AltGr is a special modifier name — emit it instead of "Alt" when set.
    // When both kModAlt and kModAltGr are required (rare), prefer AltGr.
    if (b.modsRequired & kModCtrl)  appendStr("Ctrl+");
    if (b.modsRequired & kModAltGr) appendStr("AltGr+");
    else if (b.modsRequired & kModAlt) appendStr("Alt+");
    if (b.modsRequired & kModShift) appendStr("Shift+");

    appendStr(VkLabel(b.vk));
    if (b.altVk != 0) {
        appendStr(" / ");
        appendStr(VkLabel(b.altVk));
    }
    return out;
}

}  // namespace acc::hotkeys
