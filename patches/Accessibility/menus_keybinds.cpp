// Mod keybind configurator — see menus_keybinds.h for the design summary.

#include "menus_keybinds.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

#include "engine_input.h"     // kInput* codes
#include "engine_keymap.h"    // CodeForVk — hardcoded game-key conflict check
#include "hotkeys.h"
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::menus::keybinds {

namespace {

using A = acc::hotkeys::Action;
using S = acc::strings::Id;

const char* T(S id) { return acc::strings::Get(id); }

// ----- Catalog --------------------------------------------------------------
// Each rebindable Action paired with its localised display name. Grouped into
// the five configurator categories; every user-rebindable Action appears
// exactly once (the diagnostic probes + CameraStateProbe are excluded by
// hotkeys::IsUserRebindable and intentionally absent here).
struct ActionEntry { A action; S name; };

constexpr ActionEntry kWorld[] = {
    { A::InteractTarget,      S::KbNameInteractTarget },
    { A::InteractForceRadial, S::KbNameInteractForceRadial },
    { A::TargetKey1,          S::KbNameTargetKey1 },
    { A::TargetKey2,          S::KbNameTargetKey2 },
    { A::TargetKey3,          S::KbNameTargetKey3 },
    { A::PersonalKey1,        S::KbNamePersonalKey1 },
    { A::PersonalKey2,        S::KbNamePersonalKey2 },
    { A::PersonalKey3,        S::KbNamePersonalKey3 },
    { A::PersonalKey4,        S::KbNamePersonalKey4 },
    { A::ActionBarOpen1,      S::KbNameActionBarOpen1 },
    { A::ActionBarOpen2,      S::KbNameActionBarOpen2 },
    { A::ActionBarOpen3,      S::KbNameActionBarOpen3 },
    { A::ActionBarOpen4,      S::KbNameActionBarOpen4 },
    { A::TargetActionOpen1,   S::KbNameTargetActionOpen1 },
    { A::TargetActionOpen2,   S::KbNameTargetActionOpen2 },
    { A::TargetActionOpen3,   S::KbNameTargetActionOpen3 },
    { A::LevelUpOpen,         S::KbNameLevelUpOpen },
    { A::ExamineOpen,         S::KbNameExamineOpen },
    { A::CombatQueueOpen,     S::KbNameCombatQueueOpen },
    { A::SelfStatusAnnounce,  S::KbNameSelfStatusAnnounce },
};

constexpr ActionEntry kExploration[] = {
    { A::CycleItemPrev,       S::KbNameCycleItemPrev },
    { A::CycleCategoryPrev,   S::KbNameCycleCategoryPrev },
    { A::CycleItemNext,       S::KbNameCycleItemNext },
    { A::CycleCategoryNext,   S::KbNameCycleCategoryNext },
    { A::CycleItemFirst,      S::KbNameCycleItemFirst },
    { A::CycleItemLast,       S::KbNameCycleItemLast },
    { A::AnnounceFocus,       S::KbNameAnnounceFocus },
    { A::PathfindFocus,       S::KbNamePathfindFocus },
    { A::PathfindFocusForce,  S::KbNamePathfindFocusForce },
    { A::BeaconFocus,         S::KbNameBeaconFocus },
    { A::AnnounceDegrees,     S::KbNameAnnounceDegrees },
    { A::PartyLeaderAnnounce, S::KbNamePartyLeaderAnnounce },
    { A::CameraOrient,        S::KbNameCameraOrient },
    { A::SaveMarkerAtCursor,  S::KbNameSaveMarkerAtCursor },
    { A::ViewModeToggle,      S::KbNameViewModeToggle },
};

constexpr ActionEntry kMenus[] = {
    { A::NavUp,             S::KbNameNavUp },
    { A::NavDown,           S::KbNameNavDown },
    { A::NavLeft,           S::KbNameNavLeft },
    { A::NavRight,          S::KbNameNavRight },
    { A::NavHome,           S::KbNameNavHome },
    { A::NavEnd,            S::KbNameNavEnd },
    { A::SubmenuEsc,        S::KbNameSubmenuEsc },
    { A::QueueClearAll,     S::KbNameQueueClearAll },
    { A::ContainerGiveMode, S::KbNameContainerGiveMode },
    { A::StoreModeToggle,   S::KbNameStoreModeToggle },
    { A::EditboxReReadUp,   S::KbNameEditboxReReadUp },
    { A::EditboxReReadDown, S::KbNameEditboxReReadDown },
    { A::EditboxSubmit,     S::KbNameEditboxSubmit },
    { A::EditboxCancel,     S::KbNameEditboxCancel },
};

constexpr ActionEntry kMinigames[] = {
    { A::PazaakStand,       S::KbNamePazaakStand },
    { A::PazaakEndTurn,     S::KbNamePazaakEndTurn },
    { A::PazaakReviewHand,  S::KbNamePazaakReviewHand },
    { A::PazaakReviewTable, S::KbNamePazaakReviewTable },
    { A::PazaakNextCard,    S::KbNamePazaakNextCard },
    { A::PazaakPrevCard,    S::KbNamePazaakPrevCard },
    { A::PazaakPlay,        S::KbNamePazaakPlay },
    { A::PazaakOptLeft,     S::KbNamePazaakOptLeft },
    { A::PazaakOptRight,    S::KbNamePazaakOptRight },
    { A::PazaakCancel,      S::KbNamePazaakCancel },
    { A::PazaakOppHand,     S::KbNamePazaakOppHand },
    { A::TurretCyclePrev,   S::KbNameTurretCyclePrev },
    { A::TurretCycleNext,   S::KbNameTurretCycleNext },
};

constexpr ActionEntry kGeneral[] = {
    { A::HelpMenuOpen,     S::KbNameHelpMenuOpen },
    { A::HelpContext,      S::KbNameHelpContext },
    { A::CheckForUpdate,   S::KbNameCheckForUpdate },
    { A::DialogRepeatLine, S::KbNameDialogRepeatLine },
};

struct Category { S name; const ActionEntry* entries; int count; };

template <int N>
constexpr Category MakeCat(S name, const ActionEntry (&arr)[N]) {
    return { name, arr, N };
}

const Category kCategories[] = {
    MakeCat(S::KeybindCatWorld,       kWorld),
    MakeCat(S::KeybindCatExploration, kExploration),
    MakeCat(S::KeybindCatMenus,       kMenus),
    MakeCat(S::KeybindCatMinigames,   kMinigames),
    MakeCat(S::KeybindCatGeneral,     kGeneral),
};
constexpr int kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);
// Category-level list = the categories plus a trailing "restore defaults" row.
constexpr int kResetIndex    = kCategoryCount;        // synthetic last entry
constexpr int kCatLevelCount = kCategoryCount + 1;

// ----- State ----------------------------------------------------------------
enum class Level { Categories, Actions };

bool  g_open      = false;
Level g_level     = Level::Categories;
int   g_catCursor = 0;   // index into the category-level list (incl. reset row)
int   g_curCat    = 0;   // category being browsed at the Actions level
int   g_actCursor = 0;   // index into the current category's entries

bool  g_capturing = false;
bool  g_snap[256] = {};  // key-down snapshot taken when capture is armed

const ActionEntry* FindEntry(A action) {
    for (const Category& c : kCategories) {
        for (int i = 0; i < c.count; ++i) {
            if (c.entries[i].action == action) return &c.entries[i];
        }
    }
    return nullptr;
}

// ----- Speech ---------------------------------------------------------------
void SpeakCategory(bool interrupt) {
    if (g_catCursor == kResetIndex) {
        prism::Speak(T(S::KeybindResetAll), interrupt);
        return;
    }
    if (g_catCursor < 0 || g_catCursor >= kCategoryCount) return;
    prism::Speak(T(kCategories[g_catCursor].name), interrupt);
}

void SpeakActionRow(bool interrupt) {
    const Category& c = kCategories[g_curCat];
    if (g_actCursor < 0 || g_actCursor >= c.count) return;
    const ActionEntry& e = c.entries[g_actCursor];
    char line[192];
    snprintf(line, sizeof(line), T(S::FmtKeyBinding),
             T(e.name), acc::hotkeys::Describe(e.action));
    prism::Speak(line, interrupt);
}

// ----- Capture --------------------------------------------------------------
void SnapshotKeys() {
    for (int vk = 0; vk < 256; ++vk) {
        g_snap[vk] = (GetAsyncKeyState(vk) & 0x8000) != 0;
    }
}

// VKs that may NOT be captured as a binding key: mouse buttons, the modifier
// keys themselves (those form the combo, not the base key), the Windows keys,
// and Caps Lock.
bool IsCandidateVk(int vk) {
    switch (vk) {
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:  // mouse
    case VK_SHIFT:   case VK_CONTROL:  case VK_MENU:                   // generic mods
    case VK_LSHIFT:  case VK_RSHIFT:
    case VK_LCONTROL: case VK_RCONTROL:
    case VK_LMENU:   case VK_RMENU:
    case VK_LWIN:    case VK_RWIN:
    case VK_CAPITAL:
        return false;
    default:
        return true;
    }
}

void ArmCapture() {
    const Category& c = kCategories[g_curCat];
    if (g_actCursor < 0 || g_actCursor >= c.count) return;
    const ActionEntry& e = c.entries[g_actCursor];
    SnapshotKeys();  // ignore keys already held (notably the Enter that armed us)
    g_capturing = true;
    char line[224];
    snprintf(line, sizeof(line), T(S::FmtKeybindCapturePrompt), T(e.name));
    prism::Speak(line, /*interrupt=*/true);
    acclog::Write("Keybinds", "capture armed for %s",
                  acc::hotkeys::Name(e.action));
}

// Evaluate a freshly-pressed key as the candidate binding. Applies it, or — on
// a clash — announces the conflict and stays armed for another try.
void ApplyCapturedKey(int vk) {
    using namespace acc::hotkeys;
    const Category& c = kCategories[g_curCat];
    const ActionEntry& e = c.entries[g_actCursor];

    uint32_t mods   = CurrentModifiers();
    uint32_t req    = mods & (kModShift | kModCtrl | kModAlt | kModAltGr);
    uint32_t forbid = (kModShift | kModCtrl | kModAlt) & ~req;

    // Mod-vs-mod clash — another hotkey would also fire on this exact combo.
    Action clash = FindConflict(e.action, vk, req);
    if (clash != Action::COUNT) {
        const ActionEntry* ce = FindEntry(clash);
        char line[224];
        snprintf(line, sizeof(line), T(S::FmtKeybindConflictMod),
                 ce ? T(ce->name) : "?");
        prism::Speak(line, /*interrupt=*/true);
        acclog::Write("Keybinds", "conflict: vk=0x%02x req=%u clashes with %s",
                      vk, (unsigned)req, Name(clash));
        SnapshotKeys();  // re-baseline so the held keys don't re-fire; await next
        return;
    }

    // Game-bind clash. Only a BARE bind collides — the engine is modifier-blind,
    // and the in-world hook reserves the modifier space for mod hotkeys, so a
    // modified combo on the same physical key is safe. IsKeyUsedByGame covers
    // both the hardcoded quick keys AND the player's configurable swkotor.ini
    // binds, so this warns on the full game keymap, not just the few hardcoded.
    if (req == 0 && acc::engine_keymap::IsKeyUsedByGame(vk)) {
        prism::Speak(T(S::KeybindConflictEngine), /*interrupt=*/true);
        acclog::Write("Keybinds", "conflict: bare vk=0x%02x bound by the game",
                      vk);
        SnapshotKeys();
        return;
    }

    Binding b{};
    b.vk = vk;
    b.altVk = 0;
    b.modsRequired  = req;
    b.modsForbidden = forbid;
    SetUserBinding(e.action, b);
    g_capturing = false;

    char line[192];
    snprintf(line, sizeof(line), T(S::FmtKeybindRebound),
             T(e.name), Describe(e.action));
    prism::Speak(line, /*interrupt=*/true);
    acclog::Write("Keybinds", "rebound %s -> vk=0x%02x req=%u forbid=%u",
                  Name(e.action), vk, (unsigned)req, (unsigned)forbid);
}

}  // namespace

// ----- Public API -----------------------------------------------------------
const char* DisplayName(A action) {
    const ActionEntry* e = FindEntry(action);
    return e ? T(e->name) : "";
}

bool IsOpen() { return g_open; }

void Open() {
    g_open      = true;
    g_capturing = false;
    g_level     = Level::Categories;
    g_catCursor = 0;
    g_curCat    = 0;
    g_actCursor = 0;
    // Refresh the game's configurable keymap so conflict warnings reflect any
    // changes the player made in the engine's Key Mapping screen this session.
    acc::engine_keymap::ReloadGameConfig();
    acclog::Write("Keybinds", "configurator opened");
    prism::Speak(T(S::KeybindsOpened), /*interrupt=*/true);
    SpeakCategory(/*interrupt=*/false);
}

void Reset() {
    g_open      = false;
    g_capturing = false;
    g_level     = Level::Categories;
    g_catCursor = 0;
    g_curCat    = 0;
    g_actCursor = 0;
}

bool HandleInput(int keyCode) {
    if (!g_open) return false;
    // While capture is armed, swallow every routed key so the underlying
    // Optionen panel can't act on it. The capture itself runs in Tick() off
    // Win32 polling; Esc-to-cancel is handled there too.
    if (g_capturing) return true;

    const bool up    = (keyCode == kInputNavUp);
    const bool down  = (keyCode == kInputNavDown);
    const bool home  = (keyCode == kInputHome);
    const bool end   = (keyCode == kInputEnd);
    const bool enter = (keyCode == kInputEnter1 || keyCode == kInputEnter2);
    const bool esc   = (keyCode == kInputEsc1   || keyCode == kInputEsc2);

    if (g_level == Level::Categories) {
        if (up || down || home || end) {
            int ni = g_catCursor;
            if      (up)   ni = g_catCursor - 1;
            else if (down) ni = g_catCursor + 1;
            else if (home) ni = 0;
            else           ni = kCatLevelCount - 1;
            if (ni < 0) ni = 0;
            if (ni >= kCatLevelCount) ni = kCatLevelCount - 1;
            g_catCursor = ni;
            SpeakCategory(/*interrupt=*/true);
            return true;
        }
        if (enter) {
            if (g_catCursor == kResetIndex) {
                acc::hotkeys::ResetUserBindings();
                prism::Speak(T(S::KeybindResetDone), /*interrupt=*/true);
                return true;
            }
            g_curCat    = g_catCursor;
            g_level     = Level::Actions;
            g_actCursor = 0;
            SpeakActionRow(/*interrupt=*/true);
            acclog::Write("Keybinds", "drill into category %d", g_curCat);
            return true;
        }
        if (esc) {
            // Close back to the Mod-settings root. The caller re-announces the
            // Tastenbelegung row once it sees IsOpen() == false.
            g_open = false;
            acclog::Write("Keybinds", "closed (esc at category level)");
            return true;
        }
        return false;  // let the caller's block-everything switch decide
    }

    // ---- Action level ----
    const Category& c = kCategories[g_curCat];
    if (up || down || home || end) {
        int ni = g_actCursor;
        if      (up)   ni = g_actCursor - 1;
        else if (down) ni = g_actCursor + 1;
        else if (home) ni = 0;
        else           ni = c.count - 1;
        if (ni < 0) ni = 0;
        if (ni >= c.count) ni = c.count - 1;
        g_actCursor = ni;
        SpeakActionRow(/*interrupt=*/true);
        return true;
    }
    if (enter) {
        ArmCapture();
        return true;
    }
    if (esc) {
        g_level     = Level::Categories;
        g_catCursor = g_curCat;  // land back on the category we drilled from
        SpeakCategory(/*interrupt=*/true);
        acclog::Write("Keybinds", "undrill to category level");
        return true;
    }
    return false;
}

void Tick() {
    if (!g_capturing) return;

    // Escape cancels — must be tested before any binding capture so it is never
    // itself recorded as the new key.
    bool escDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    if (escDown && !g_snap[VK_ESCAPE]) {
        g_capturing = false;
        prism::Speak(T(S::KeybindCaptureCancelled), /*interrupt=*/true);
        SpeakActionRow(/*interrupt=*/false);  // re-anchor on the row
        acclog::Write("Keybinds", "capture cancelled");
        return;
    }
    if (!escDown) g_snap[VK_ESCAPE] = false;

    for (int vk = 8; vk < 256; ++vk) {
        if (vk == VK_ESCAPE) continue;
        if (!IsCandidateVk(vk)) continue;
        bool downNow = (GetAsyncKeyState(vk) & 0x8000) != 0;
        if (downNow && !g_snap[vk]) {
            ApplyCapturedKey(vk);
            return;  // one capture per tick
        }
        if (!downNow) g_snap[vk] = false;  // released — re-press is a fresh edge
    }
}

}  // namespace acc::menus::keybinds
