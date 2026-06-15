// Help system — see help.h for the design summary.
//
// One catalog (kEntries) feeds both surfaces. `grp` decides the F1 section a
// line is listed under (F1 lists every entry); `ctx` is a curated bitmask of
// the screens that speak the line on Ctrl+F1 (kept short on purpose). Adding a
// keybind = add a strings::Id (strings.h) + one row here.

#include "help.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_panels.h"
#include "engine_player.h"      // GetPlayerPosition / Vector
#include "engine_subscreen.h"   // Begin/EndOverlayPause
#include "hotkeys.h"
#include "log.h"
#include "prism.h"
#include "strings.h"
#include "unified_action_menu.h"

namespace acc::help {

namespace {

using S = acc::strings::Id;

// ----- Context model ------------------------------------------------------
// Bits used by both the ctx tags below and Ctrl+F1 selection.
enum CtxBit : uint32_t {
    kWorld      = 1u << 0,
    kMenu       = 1u << 1,
    kMap        = 1u << 2,
    kActionMenu = 1u << 3,
    kDialog     = 1u << 4,
    kContainer  = 1u << 5,
    kStore      = 1u << 6,
};

enum class Context { World, Menu, Map, ActionMenu, Dialog, Container, Store };

uint32_t ContextBit(Context c) {
    switch (c) {
        case Context::World:      return kWorld;
        case Context::Menu:       return kMenu;
        case Context::Map:        return kMap;
        case Context::ActionMenu: return kActionMenu;
        case Context::Dialog:     return kDialog;
        case Context::Container:  return kContainer;
        case Context::Store:      return kStore;
    }
    return kWorld;
}

S ContextNameId(Context c) {
    switch (c) {
        case Context::World:      return S::HelpContextWorld;
        case Context::Menu:       return S::HelpContextMenu;
        case Context::Map:        return S::HelpContextMap;
        case Context::ActionMenu: return S::HelpContextActionMenu;
        case Context::Dialog:     return S::HelpContextDialog;
        case Context::Container:  return S::HelpContextContainer;
        case Context::Store:      return S::HelpContextStore;
    }
    return S::HelpContextWorld;
}

// Detect the screen the user is on. Priority order: the most specific
// surfaces first, the generic "some menu is up" next, pure world last.
Context DetectContext() {
    if (acc::unified_menu::IsActive() && !acc::unified_menu::IsSuspended()) {
        return Context::ActionMenu;
    }
    if (acc::engine::HasActiveDialogPanel()) return Context::Dialog;

    acc::engine::UiBlockState st;
    bool blocked = acc::engine::IsForegroundUiBlocking(&st);
    if (st.fgKind == acc::engine::PanelKind::Container) return Context::Container;
    if (st.fgKind == acc::engine::PanelKind::Store)     return Context::Store;
    if (acc::engine::HasActiveMapPanel(nullptr))        return Context::Map;
    if (blocked) return Context::Menu;

    Vector p;
    if (acc::engine::GetPlayerPosition(p)) return Context::World;
    return Context::Menu;  // title screen / loading — no world, no block
}

// ----- Catalog ------------------------------------------------------------
enum class Grp {
    General, Movement, Interaction, Combat, Exploration, Screens, Map, Mod,
    COUNT
};

constexpr S kGroupHeader[] = {
    S::HelpGroupGeneral, S::HelpGroupMovement, S::HelpGroupInteraction,
    S::HelpGroupCombat, S::HelpGroupExploration, S::HelpGroupScreens,
    S::HelpGroupMap, S::HelpGroupMod,
};
static_assert(sizeof(kGroupHeader) / sizeof(kGroupHeader[0]) ==
              static_cast<int>(Grp::COUNT),
              "kGroupHeader must cover every Grp");

struct Entry {
    S        label;     // header/simple-entry id, or (composed) the format id
    Grp      grp;
    uint32_t ctx;       // 0 = F1-list only, never spoken by Ctrl+F1
    bool     composed;  // true → label is a format; text built from MenuCat*
                        // names at BuildRows time (the number-key line)
};

// Order within a group is the order F1 reads them. The General nav keys are
// tagged for the menu-like screens they apply to (not World) so they're heard
// once, not repeated per section.
constexpr Entry kEntries[] = {
    // ---- General navigation ----
    { S::HelpKeyUpDown,          Grp::General, kMenu | kActionMenu | kDialog | kContainer | kStore },
    { S::HelpKeyLeftRight,       Grp::General, kMenu | kActionMenu },
    { S::HelpKeyHomeEnd,         Grp::General, kMenu | kActionMenu },
    { S::HelpKeyEnter,           Grp::General, kMenu | kActionMenu | kDialog | kContainer | kStore },
    { S::HelpKeyEsc,             Grp::General, kMenu | kMap | kActionMenu | kDialog | kContainer | kStore },
    { S::HelpKeyReadDescription, Grp::General, kMenu | kActionMenu },
    // Q/E — engine window/tab/screen cycling plus our store buy-sell and
    // container take-give mode toggles, all under one line.
    { S::HelpKeySwitchWindows,   Grp::General, kMenu | kContainer | kStore },
    // F1 / Ctrl+F1 are listed in the F1 menu but never spoken by Ctrl+F1
    // itself — a user who reached Ctrl+F1 has already found F1.
    { S::HelpKeyF1,              Grp::General, 0 },
    { S::HelpKeyCtrlF1,          Grp::General, 0 },

    // ---- Movement & camera ----
    { S::HelpKeyWalk,            Grp::Movement, kWorld },
    { S::HelpKeyCameraRotate,    Grp::Movement, kWorld },
    { S::HelpKeyStrafe,          Grp::Movement, 0 },
    { S::HelpKeyPause,           Grp::Movement, 0 },
    { S::HelpKeyViewMode,        Grp::Movement, 0 },
    { S::HelpKeySwitchLeader,    Grp::Movement, 0 },

    // ---- Targeting & interaction ----
    { S::HelpKeyCycleTargets,    Grp::Interaction, kWorld },
    { S::HelpKeyInteract,        Grp::Interaction, kWorld },
    { S::HelpKeyOpenActionMenu,  Grp::Interaction, kWorld },
    { S::HelpKeySelfStatus,      Grp::Interaction, kWorld },
    { S::HelpKeyAnnounceFocus,   Grp::Interaction, kWorld },
    { S::HelpKeyWalkToFocus,     Grp::Interaction, 0 },
    { S::HelpKeyBeacon,          Grp::Interaction, 0 },
    { S::HelpKeyDialogRepeat,    Grp::Interaction, kDialog },

    // ---- Exploration & orientation ----
    { S::HelpKeyCycleObjects,    Grp::Exploration, 0 },
    { S::HelpKeyCycleCategory,   Grp::Exploration, 0 },
    { S::HelpKeyCycleEnds,       Grp::Exploration, 0 },
    { S::HelpKeyHeading,         Grp::Exploration, 0 },
    { S::HelpKeyCameraOrient,    Grp::Exploration, 0 },
    { S::HelpKeyDropMarker,      Grp::Exploration, kMap },

    // ---- Combat & actions ----
    // Bare 1..7 fire a category's most recent action; Shift+1..7 open the
    // category. The bare-key line is composed from the MenuCat* names so it
    // matches what the action menu speaks (1 Attacks .. 6 Misc, 7 Explosives;
    // linear key→column order, matching the engine dispatch).
    { S::FmtHelpNumberActions,   Grp::Combat, 0, /*composed=*/true },
    { S::HelpKeyOpenCategory,    Grp::Combat, 0 },
    { S::HelpKeyActionQueue,     Grp::Combat, 0 },
    { S::HelpKeyLevelUp,         Grp::Combat, 0 },
    { S::HelpKeyCancelCombat,    Grp::Combat, 0 },

    // ---- Quick screens ----
    { S::HelpKeyScreenMap,       Grp::Screens, 0 },
    { S::HelpKeyScreenMessages,  Grp::Screens, 0 },
    { S::HelpKeyScreenQuests,    Grp::Screens, 0 },
    { S::HelpKeyScreenAbilities, Grp::Screens, 0 },
    { S::HelpKeyScreenCharacter, Grp::Screens, 0 },
    { S::HelpKeyScreenInventory, Grp::Screens, 0 },
    { S::HelpKeyScreenEquip,     Grp::Screens, 0 },
    { S::HelpKeyScreenOptions,   Grp::Screens, 0 },

    // ---- Map panel ----
    { S::HelpKeyMapCursor,       Grp::Map, kMap },
    { S::HelpKeyMapPosition,     Grp::Map, kMap },

    // ---- Mod features ----
    // F1-list only — Ctrl+F1 deliberately doesn't mention mod settings.
    { S::HelpKeyModSettings,     Grp::Mod, 0 },
};
constexpr int kEntryCount =
    static_cast<int>(sizeof(kEntries) / sizeof(kEntries[0]));

// ----- F1 list overlay state ----------------------------------------------
constexpr int kMaxRows = 96;  // 8 headers + entries, with headroom

struct Row {
    bool isHeader;
    bool composed;     // entry text pre-built into text[] (number-key line)
    S    label;        // header id, or simple-entry id
    char text[224];    // composed entry text (only when composed)
    int  entryPos;     // 1-based position among entries (headers: 0)
};

struct State {
    bool open        = false;
    bool pausedWorld = false;  // BeginOverlayPause held (in-world open only)
    int  focus       = 0;
    int  rowCount    = 0;
    int  entryTotal  = 0;
    Row  rows[kMaxRows];
};
State g_state;

// Resolve a composed entry's text. The bare-number line lists the category
// names (1..7) using the same MenuCat* strings the action menu speaks, so the
// help never drifts from what the user navigates. Key→category order is linear
// and matches the engine dispatch: 6 → Misc (Sonstiges), 7 → Explosives.
void BuildComposedText(S formatId, char* out, size_t cap) {
    if (formatId == S::FmtHelpNumberActions) {
        std::snprintf(out, cap, acc::strings::Get(formatId),
                      acc::strings::Get(S::MenuCatAttacks),
                      acc::strings::Get(S::MenuCatForcePowers),
                      acc::strings::Get(S::MenuCatItems),
                      acc::strings::Get(S::MenuCatSelfPowers),
                      acc::strings::Get(S::MenuCatMedical),
                      acc::strings::Get(S::MenuCatMisc),
                      acc::strings::Get(S::MenuCatExplosives));
        return;
    }
    std::snprintf(out, cap, "%s", acc::strings::Get(formatId));
}

// Build the flat row list: each non-empty group emits a header row followed
// by its entries, in catalog order.
void BuildRows() {
    int idx = 0;
    int entryNo = 0;
    for (int g = 0; g < static_cast<int>(Grp::COUNT) && idx < kMaxRows; ++g) {
        // Count this group's entries first so an (impossible) empty group is
        // skipped rather than emitting a lone header.
        bool any = false;
        for (int e = 0; e < kEntryCount; ++e) {
            if (kEntries[e].grp == static_cast<Grp>(g)) { any = true; break; }
        }
        if (!any) continue;

        Row& hdr = g_state.rows[idx];
        hdr.isHeader = true;
        hdr.composed = false;
        hdr.label    = kGroupHeader[g];
        hdr.text[0]  = '\0';
        hdr.entryPos = 0;
        ++idx;

        for (int e = 0; e < kEntryCount && idx < kMaxRows; ++e) {
            if (kEntries[e].grp != static_cast<Grp>(g)) continue;
            ++entryNo;
            Row& row = g_state.rows[idx];
            row.isHeader = false;
            row.composed = kEntries[e].composed;
            row.label    = kEntries[e].label;
            row.entryPos = entryNo;
            row.text[0]  = '\0';
            if (row.composed) {
                BuildComposedText(kEntries[e].label, row.text, sizeof(row.text));
            }
            ++idx;
        }
    }
    g_state.rowCount   = idx;
    g_state.entryTotal = entryNo;
}

void SpeakRow(int idx, bool interrupt) {
    if (idx < 0 || idx >= g_state.rowCount) return;
    const Row& r = g_state.rows[idx];
    char line[384];
    if (r.isHeader) {
        std::snprintf(line, sizeof(line),
                      acc::strings::Get(S::FmtHelpGroupHeader),
                      acc::strings::Get(r.label));
    } else {
        const char* base = r.composed ? r.text : acc::strings::Get(r.label);
        std::snprintf(line, sizeof(line),
                      acc::strings::Get(S::FmtHelpRowOf),
                      base, r.entryPos, g_state.entryTotal);
    }
    prism::Speak(line, interrupt);
}

// Ctrl+F1 — speak the keys tagged for the current screen, joined.
void SpeakContext() {
    Context c   = DetectContext();
    uint32_t bit = ContextBit(c);

    char joined[1024];
    joined[0] = '\0';
    size_t len = 0;
    int n = 0;
    for (int e = 0; e < kEntryCount; ++e) {
        if (!(kEntries[e].ctx & bit)) continue;
        const char* t = acc::strings::Get(kEntries[e].label);
        if (!t || !t[0]) continue;
        if (n > 0) {
            // Separator between entries.
            len += static_cast<size_t>(
                std::snprintf(joined + len, sizeof(joined) - len, ". "));
            if (len >= sizeof(joined) - 1) break;
        }
        len += static_cast<size_t>(
            std::snprintf(joined + len, sizeof(joined) - len, "%s", t));
        ++n;
        if (len >= sizeof(joined) - 1) break;
    }

    char out[1280];
    if (n == 0) {
        std::snprintf(out, sizeof(out), "%s",
                      acc::strings::Get(S::HelpContextNothing));
    } else {
        std::snprintf(out, sizeof(out),
                      acc::strings::Get(S::FmtHelpContextLine),
                      acc::strings::Get(ContextNameId(c)),
                      joined);
    }
    prism::Speak(out, /*interrupt=*/true);
    acclog::Write("Help", "context speak ctx=%d entries=%d", static_cast<int>(c), n);
}

}  // namespace

bool IsMenuOpen() { return g_state.open; }

void OpenMenu() {
    if (g_state.open) return;
    BuildRows();
    g_state.focus = 0;
    g_state.open  = true;

    // Pause the world only when opened from pure in-world context — in a menu
    // the engine panel already holds the world, and BeginOverlayPause routes
    // through the server-side combat pause which has no meaning there.
    g_state.pausedWorld = (DetectContext() == Context::World);
    if (g_state.pausedWorld) acc::engine::BeginOverlayPause();

    acclog::Write("Help", "menu opened rows=%d entries=%d pausedWorld=%d",
                  g_state.rowCount, g_state.entryTotal,
                  g_state.pausedWorld ? 1 : 0);

    prism::Speak(acc::strings::Get(S::HelpMenuOpened), /*interrupt=*/true);
    SpeakRow(g_state.focus, /*interrupt=*/false);
}

void CloseMenu() {
    if (!g_state.open) return;
    acclog::Write("Help", "menu closed");
    g_state.open = false;
    if (g_state.pausedWorld) {
        acc::engine::EndOverlayPause();
        g_state.pausedWorld = false;
    }
    g_state.focus    = 0;
    g_state.rowCount = 0;
    prism::Speak(acc::strings::Get(S::HelpMenuClosed), /*interrupt=*/true);
}

void PollWin32() {
    using A = acc::hotkeys::Action;

    // Ctrl+F1 — context help. Independent of the list being open.
    if (acc::hotkeys::Pressed(A::HelpContext)) {
        SpeakContext();
    }

    // F1 — toggle the list. Consume so the press can't leak to any other
    // consumer (and the manager hook separately suppresses the engine's
    // F1-as-activate when in a menu).
    if (acc::hotkeys::Pressed(A::HelpMenuOpen)) {
        if (g_state.open) CloseMenu();
        else              OpenMenu();
        acc::hotkeys::Consume(A::HelpMenuOpen);
    }

    if (!g_state.open) return;

    // List navigation. Pure-linear Up/Down with Home/End; Enter re-reads;
    // Esc closes. Each handled key is Consume()d so the downstream in-world
    // pollers (cycle / interact / examine) and the Home/End synthesiser don't
    // also act on it this tick.
    if (acc::hotkeys::Pressed(A::NavUp)) {
        if (g_state.focus > 0) --g_state.focus;
        SpeakRow(g_state.focus, /*interrupt=*/true);
        acc::hotkeys::Consume(A::NavUp);
    }
    if (acc::hotkeys::Pressed(A::NavDown)) {
        if (g_state.focus < g_state.rowCount - 1) ++g_state.focus;
        SpeakRow(g_state.focus, /*interrupt=*/true);
        acc::hotkeys::Consume(A::NavDown);
    }
    if (acc::hotkeys::Pressed(A::NavHome)) {
        g_state.focus = 0;
        SpeakRow(g_state.focus, /*interrupt=*/true);
        acc::hotkeys::Consume(A::NavHome);
    }
    if (acc::hotkeys::Pressed(A::NavEnd)) {
        if (g_state.rowCount > 0) g_state.focus = g_state.rowCount - 1;
        SpeakRow(g_state.focus, /*interrupt=*/true);
        acc::hotkeys::Consume(A::NavEnd);
    }
    if (acc::hotkeys::Pressed(A::InteractTarget)) {  // Enter — re-read
        SpeakRow(g_state.focus, /*interrupt=*/true);
        acc::hotkeys::Consume(A::InteractTarget);
    }
    if (acc::hotkeys::Pressed(A::SubmenuEsc)) {
        CloseMenu();
        acc::hotkeys::Consume(A::SubmenuEsc);
    }
}

void Tick() {
    if (!g_state.open) return;
    // Self-disarm: an in-world open whose world drops out (area load /
    // teardown) must release the pause hold and close, or the overlay
    // strands a paused world across the transition.
    if (g_state.pausedWorld) {
        Vector p;
        if (!acc::engine::GetPlayerPosition(p)) {
            acclog::Write("Help", "self-disarm: player position lost");
            // Close without the "closed" speech — the transition has its own
            // announce path and we don't want to step on it.
            g_state.open = false;
            acc::engine::EndOverlayPause();
            g_state.pausedWorld = false;
            g_state.focus    = 0;
            g_state.rowCount = 0;
        }
    }
}

}  // namespace acc::help
