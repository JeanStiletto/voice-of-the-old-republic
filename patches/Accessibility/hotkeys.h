// Centralised hotkey registry. Single source of truth for every mod-added
// binding. Per-tick pollers query Pressed(Action) for rising edge +
// modifier match + KOTOR foreground. Bindings are runtime-mutable via
// Set() for a future rebind UI.
//
// We don't piggyback on the engine's [Keymapping] because it's bare
// DIK→Action with no modifier combos and no AltGr semantics.

#pragma once

#include <cstdint>

namespace acc::hotkeys {

// Required + forbidden masks let one VK host multiple actions (e.g. B vs.
// Shift+B). AltGr = VK_RMENU; Windows synthesises a phantom Ctrl alongside
// it on QWERTZ, so the Ctrl-modifier path forbids RMENU to avoid double-fire.
enum ModifierBit : uint32_t {
    kModShift = 1u << 0,   // either Shift key (VK_SHIFT / L / R)
    kModCtrl  = 1u << 1,   // either Ctrl key (VK_CONTROL / L / R)
    kModAlt   = 1u << 2,   // either Alt key (VK_MENU / L / R)
    kModAltGr = 1u << 3,   // right Alt specifically (VK_RMENU)
};

// One per logical gesture. The registry only answers "did the bound key
// fire this tick"; dispatch sites decide what to do with it.
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
    TargetActionOpen1,     // Shift+1 — opens target-row submenu (row 0)
    TargetActionOpen2,     // Shift+2
    TargetActionOpen3,     // Shift+3
    LevelUpOpen,           // Shift+L
    ExamineOpen,           // Shift+H
    CombatQueueOpen,       // Shift+K
    SelfStatusAnnounce,    // H — leader HP / effects / equipped weapon

    // ----- Submenu navigation -----
    NavUp,                 // Up    (routed to active submenu)
    NavDown,               // Down
    NavLeft,               // Left
    NavRight,              // Right
    NavHome,               // Home  (jump chain/listbox to first item)
    NavEnd,                // End   (jump chain/listbox to last item)
    SubmenuEsc,            // Esc   (close active mod submenu)
    QueueClearAll,         // Shift+Enter inside combat-queue submenu

    // ----- Container give-mode -----
    ContainerGiveMode,     // Q or E inside an open Container panel

    // ----- Store mode toggle -----
    StoreModeToggle,       // Q or E inside an open Store panel (flips Buy/Sell)

    // ----- In-world cycle (Pillar 4) -----
    CycleItemPrev,         // ,
    CycleCategoryPrev,     // Shift+,
    CycleItemNext,         // .
    CycleCategoryNext,     // Shift+.
    CycleItemFirst,        // Ctrl+,  (jump to first/closest item in category)
    CycleItemLast,         // Ctrl+.  (jump to last/farthest item in category)
    AnnounceFocus,         // - (QWERTZ) / / (QWERTY)
    PathfindFocus,         // Shift+-
    PathfindFocusForce,    // Alt+-
    BeaconFocus,           // Ctrl+-

    // ----- Orientation & party -----
    AnnounceDegrees,       // AltGr alone (Shift forbidden)
    PartyLeaderAnnounce,   // Tab
    CameraOrient,          // N — beacon waypoint or next cardinal CW
    SaveMarkerAtCursor,    // Shift+N — drops marker on the map

    // ----- View mode -----
    ViewModeToggle,        // B   (Shift forbidden)
    CameraStateProbe,      // Shift+B (diagnostic)

    // ----- Editbox modal (text-input panels) -----
    EditboxReReadUp,       // Up   while edit-mode armed
    EditboxReReadDown,     // Down while edit-mode armed
    EditboxSubmit,         // Enter while edit-mode armed
    EditboxCancel,         // Esc  while edit-mode armed

    // ----- In-game auto-updater -----
    CheckForUpdate,            // F5 — main menu only, gated on GetPlayerPosition

    // ----- Diagnostic probes (NOT user-rebindable) -----
    ProbePathfind,             // F9     (Ctrl forbidden)
    ProbeAudioCycle,           // F10    (Ctrl forbidden)
    ProbeAudioFire,            // F11    (Ctrl forbidden)
    ProbeCameraDump,           // F12    (Ctrl forbidden)
    ProbeMouseLookToggle,      // Shift+AltGr
    ProbeCameraDistDump,       // Ctrl+F12 — Option-B camera-distance snapshot
    ProbeCameraDistClampToggle,// Ctrl+F11 — cycle distance clamp modes

    // ----- Pazaak minigame (polled only while the board is foreground) -----
    PazaakStand,           // S
    PazaakEndTurn,         // E
    PazaakReviewHand,      // C
    PazaakReviewTable,     // T
    PazaakNextCard,        // Tab        (cycle playable hand cards)
    PazaakPrevCard,        // Shift+Tab
    PazaakPlay,            // Enter      (play card / confirm sign)
    PazaakOptLeft,         // Left       (card-options sub-zone)
    PazaakOptRight,        // Right      (card-options sub-zone)
    PazaakCancel,          // Esc        (card-options sub-zone)
    PazaakOppHand,         // Shift+C    (announce opponent's remaining hand count)

    // ----- Turret minigame (polled only while the gunner minigame is active) -----
    TurretCyclePrev,       // Q          (select previous fighter as locked target)
    TurretCycleNext,       // E          (select next fighter)

    // ----- Sentinel -----
    COUNT
};

// vk + optional altVk for layout portability (same physical key reports
// different VKs on US QWERTY vs German QWERTZ). vk == 0 means unbound.
struct Binding {
    int      vk;
    int      altVk;
    uint32_t modsRequired;
    uint32_t modsForbidden;
};

// Call at start/end of every per-frame dispatch. core_tick::Dispatch does this.
void BeginTick();
void EndTick();

// True iff binding VK (or altVk) is down this tick + was up last tick +
// modifiers match + KOTOR is foreground. Idempotent within a tick.
bool Pressed(Action a);

// Pure held-state query — no edge, no foreground gate. Used at editbox
// arm-time to suppress edges on keys already held.
bool Held(Action a);

// Claim the current tick's rising edge so other consumers don't see it.
// Used when one site (e.g. editbox arm) has just fired a parent action's
// Enter and the same press shouldn't also fire the now-armed handler.
void Consume(Action a);

// Pre-claim the NEXT rising edge. Use from sites firing BEFORE BeginTick
// on the tick where the edge will land — the manager-level
// HandleInputEvent hook runs between OnUpdate ticks where Consume can't
// see the press yet.
void ClaimRisingEdge(Action a);

// Direct OS-level modifier reads — no edge, no foreground gate.
bool ShiftHeld();
bool CtrlHeld();
bool AltHeld();
bool AltGrHeld();

bool IsForegroundGame();

// IsUserRebindable returns false for diagnostic probes (excluded from
// the future rebind UI).
Binding  Get(Action a);
void     Set(Action a, Binding b);
bool     IsUserRebindable(Action a);

// Describe(a) goes into a rotating static buffer so multiple calls in one
// log line stay valid (acclog::FmtPtr convention).
const char* Name(Action a);
const char* Describe(Action a);

}  // namespace acc::hotkeys
