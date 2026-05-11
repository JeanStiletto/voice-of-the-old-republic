// KOTOR Accessibility — centralised hotkey registry.
//
// Single source of truth for every keyboard binding the mod introduces. The
// `Action` enum below lists every logical user gesture. Per-tick pollers ask
// `acc::hotkeys::Pressed(Action)` to learn whether the user just pressed the
// binding (rising edge + correct modifier state + KOTOR foreground). Default
// bindings live in `hotkeys.cpp` and are runtime-mutable via `Set()` so a
// future in-mod rebind UI can write into them without recompilation.
//
// Why not piggyback on the engine's `[Keymapping]` system: the engine action
// table is closed (no way to add new IDs without patching the Key Mapping
// UI), the format is bare DIK→Action (no modifier combos), and AltGr-specific
// behaviour we rely on isn't expressible. Modifier-aware bindings cover ~80%
// of what we need — see `docs/controls-and-input.md` "Mod hotkeys" for the
// architectural rationale.

#pragma once

#include <cstdint>

namespace acc::hotkeys {

// ---------------------------------------------------------------------------
// Modifier mask. Required + forbidden masks let a single VK host multiple
// actions distinguished by modifier (e.g. `B` toggles view mode, Shift+B
// fires the camera-state probe). AltGr is the right-Alt key alone (VK_RMENU);
// on German QWERTZ Windows synthesises a phantom Ctrl when AltGr is pressed,
// so the Ctrl-modifier path forbids RMENU to avoid double-firing.
// ---------------------------------------------------------------------------
enum ModifierBit : uint32_t {
    kModShift = 1u << 0,   // either Shift key (VK_SHIFT / L / R)
    kModCtrl  = 1u << 1,   // either Ctrl key (VK_CONTROL / L / R)
    kModAlt   = 1u << 2,   // either Alt key (VK_MENU / L / R)
    kModAltGr = 1u << 3,   // right Alt specifically (VK_RMENU)
};

// ---------------------------------------------------------------------------
// One Action per logical user gesture. The dispatch site decides what to do
// with it (which submenu consumes Up/Down, etc.); the registry only answers
// "did the bound key fire this tick".
// ---------------------------------------------------------------------------
enum class Action : int {
    // ----- World interaction -----
    InteractTarget,        // Enter
    InteractForceRadial,   // Shift+Enter
    TargetKey1,            // 1   (bare; engine fires action, we announce)
    TargetKey2,            // 2
    TargetKey3,            // 3
    PersonalKey1,          // 4   (bare player action bar column)
    PersonalKey2,          // 5
    PersonalKey3,          // 6
    PersonalKey4,          // 7
    ActionBarOpen1,        // Shift+4
    ActionBarOpen2,        // Shift+5
    ActionBarOpen3,        // Shift+6
    ActionBarOpen4,        // Shift+7
    LevelUpOpen,           // Shift+L
    ExamineOpen,           // Shift+H
    CombatQueueOpen,       // Shift+K
    StatBlockSpeak,        // Shift+S

    // ----- Submenu navigation -----
    NavUp,                 // Up    (routed to active submenu)
    NavDown,               // Down
    NavLeft,               // Left
    NavRight,              // Right
    SubmenuEsc,            // Esc   (close active mod submenu)
    QueueClearAll,         // Shift+Enter inside combat-queue submenu

    // ----- Container give-mode -----
    ContainerGiveMode,     // G inside an open Container panel

    // ----- In-world cycle (Pillar 4) -----
    CycleItemPrev,         // ,
    CycleCategoryPrev,     // Shift+,
    CycleItemNext,         // .
    CycleCategoryNext,     // Shift+.
    AnnounceFocus,         // - (QWERTZ) / / (QWERTY)
    PathfindFocus,         // Shift+-
    PathfindFocusForce,    // Alt+-
    BeaconFocus,           // Ctrl+-

    // ----- Orientation & party -----
    AnnounceDegrees,       // AltGr alone (Shift forbidden)
    PartyLeaderAnnounce,   // Tab

    // ----- View mode -----
    ViewModeToggle,        // B   (Shift forbidden)
    CameraStateProbe,      // Shift+B (diagnostic)

    // ----- Editbox modal (text-input panels) -----
    EditboxReReadUp,       // Up   while edit-mode armed
    EditboxReReadDown,     // Down while edit-mode armed
    EditboxSubmit,         // Enter while edit-mode armed
    EditboxCancel,         // Esc  while edit-mode armed

    // ----- Diagnostic probes (NOT user-rebindable) -----
    ProbePathfind,         // F9
    ProbeAudioCycle,       // F10
    ProbeAudioFire,        // F11
    ProbeCameraDump,       // F12
    ProbeMouseLookToggle,  // Shift+AltGr

    // ----- Sentinel -----
    COUNT
};

// ---------------------------------------------------------------------------
// Binding. `vk` is the primary VK code (Win32 virtual key); `altVk` is an
// optional secondary VK so a single Action can match either of two keys for
// layout-portability (e.g. the "announce focus" key is VK_OEM_2 on US QWERTY
// and VK_OEM_MINUS on German QWERTZ — same physical key, different VK).
//
// `modsRequired` = all of these mod bits must be held.
// `modsForbidden` = none of these mod bits may be held.
//
// vk == 0 means "unbound" (the action is never reported as pressed).
// ---------------------------------------------------------------------------
struct Binding {
    int      vk;
    int      altVk;
    uint32_t modsRequired;
    uint32_t modsForbidden;
};

// ---------------------------------------------------------------------------
// Tick lifecycle. Call at the start and end of every per-frame dispatch so
// edge-detection sees a coherent snapshot. `core_tick::Dispatch()` does this.
// ---------------------------------------------------------------------------
void BeginTick();
void EndTick();

// ---------------------------------------------------------------------------
// Edge-detected query. Returns true iff:
//   1. The binding's VK (or altVk) is down THIS tick and was up LAST tick.
//   2. All `modsRequired` bits are currently held.
//   3. No `modsForbidden` bits are currently held.
//   4. KOTOR has foreground focus.
// Idempotent within a tick — safe to query the same Action from multiple
// sites; both get the same answer until the next BeginTick.
// ---------------------------------------------------------------------------
bool Pressed(Action a);

// Pure held-state query (no edge, no foreground gate). Used by the editbox
// arm-time snapshot to suppress edges on keys the user is already holding.
bool Held(Action a);

// Consume the current rising edge for `a`. After this call, `Pressed(a)`
// returns false for the rest of this tick (and stays false on subsequent
// ticks until the key is released and re-pressed). Use when one site has
// just claimed a press and other potential consumers in the same tick
// should not see it — e.g. the editbox arm path consumes the Enter that
// fired the parent button so it can't also fire the editbox's "submit
// edit" handler on the very first poll after arming.
void Consume(Action a);

// ---------------------------------------------------------------------------
// Modifier convenience queries. Some sites need raw modifier state without
// binding to a specific Action (e.g. `cycle_input::HandleInputEventEngine`
// reads the engine-side shift latch alongside Win32 shift to disambiguate).
// These are direct GetAsyncKeyState reads — no edge, no foreground gate.
// ---------------------------------------------------------------------------
bool ShiftHeld();
bool CtrlHeld();
bool AltHeld();
bool AltGrHeld();

// Returns true iff KOTOR is the foreground process. Exposed so legacy /
// pre-registry sites can share the same gate.
bool IsForegroundGame();

// ---------------------------------------------------------------------------
// Rebinding API. Defaults are baked at static-init; `Set` overrides them at
// runtime. A future rebind UI calls `Set` per row when the user picks a new
// key. `IsUserRebindable` returns false for the diagnostic probes — they
// shouldn't appear in the rebind UI.
// ---------------------------------------------------------------------------
Binding  Get(Action a);
void     Set(Action a, Binding b);
bool     IsUserRebindable(Action a);

// ---------------------------------------------------------------------------
// Names + human-readable descriptions for logs + rebind UI.
//   Name(a)        -> "InteractTarget" / "AnnounceDegrees" / ...
//   Describe(a)    -> "Enter" / "Shift+B" / "AltGr" / "Ctrl+-" — the user-
//                     facing label that would appear in a rebind row.
// Both return pointers to static / static-bound storage; safe to log without
// copy. Describe() formats into a small rotating buffer so multiple calls
// in one log line stay valid (matches `acclog::FmtPtr` convention).
// ---------------------------------------------------------------------------
const char* Name(Action a);
const char* Describe(Action a);

}  // namespace acc::hotkeys
