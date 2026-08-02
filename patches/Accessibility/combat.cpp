#include "combat.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "engine_area.h"      // GetObjectName, ResolveServerObjectHandle
#include "engine_offsets.h"
#include "engine_panels.h"    // PanelKind, IdentifyPanel
#include "engine_app.h"       // GetClientApp
#include "engine_player.h"    // GetPlayerServerCreature
#include "log.h"
#include "menus_extract.h"    // FromControl — listbox row text reader
#include "same_name_suffix.h" // AppendSuffix for same-LocName disambiguator
#include "strings.h"
#include "prism.h"
#include "transitions.h"      // IsModuleLoadPending — gate during cutscene-load
#include "unified_action_menu.h" // ForceDisarm — auto-close the queueing menu
#include "engine_offsets_select.h"
                                 // when combat ends (experimental)
                              // transient (engine LYT loader use-after-free)

namespace acc::combat {

namespace {

// ============================================================================
// Phase 1A — combat-mode poll with stability debounce.
// Mirrors the pattern in turn_announce.cpp: a pending state is tracked
// per tick; only after kQuietMs of unchanged-pending do we speak.
// ============================================================================

// CClientExoApp::GetCombatMode — __thiscall, takes no args, returns int
// (0 = peace, !=0 = combat). The CClientExoApp facade is 8 bytes
// (vtable + internal); the flag itself lives on the internal struct,
// but the accessor on the facade walks it for us.
typedef int (__thiscall* PFN_GetCombatMode)(void* this_);

bool ReadCombatMode(int& outMode) {
    void* exoApp = acc::engine::GetClientApp();
    if (!exoApp) return false;
    __try {
        auto fn = reinterpret_cast<PFN_GetCombatMode>(kAddrGetCombatMode);
        outMode = fn(exoApp);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

constexpr DWORD kCombatModeQuietMs = 200;  // collapse oscillation near edges

}  // namespace

bool IsCombatActive() {
    int mode = 0;
    if (!ReadCombatMode(mode)) return false;
    return mode != 0;
}

// Encounter-level combat: OR of the controlled leader's global combat bit and
// every party member's per-creature bit. Read live (one chain walk + party
// scan) — cheap enough for a per-keypress gate, not a per-tick caller. Mirrors
// the partyInCombat derivation in TickCombatMode so the two never disagree.
bool IsPartyInCombat() {
    int mode = 0;
    bool leaderInCombat = ReadCombatMode(mode) && mode != 0;
    return acc::engine::IsAnyPartyMemberInCombat() || leaderInCombat;
}

void TickCombatMode() {
    // Module-load latch — see transitions.h IsModuleLoadPending. Combat
    // mode is read via a CClientExoApp engine accessor; probing it
    // through a stunt-module cutscene load tripped the engine's LYT
    // loader on the previous module's freed resref arena (dump
    // swkotor.exe(1).23224.dmp, 2026-05-26).
    if (acc::transitions::IsModuleLoadPending()) return;

    int mode = 0;
    if (!ReadCombatMode(mode)) return;

    // Two distinct signals (see action-menu-and-combat.md "combat-mode"):
    //   leaderInCombat — the engine global (GetCombatMode). It only mirrors the
    //     *controlled* leader's combat bit and is re-synced to the new leader on
    //     every Tab (CSWParty::SetLeader → SetCombatMode), so it flips to peace
    //     when you switch to a not-yet-engaged member even mid-encounter.
    //   partyInCombat — OR of every party member's per-creature combat bit. This
    //     is the true "is the encounter active" state, immune to which member
    //     happens to be controlled. We drive the strong begin/end cue (and the
    //     menu auto-close) off THIS so a party switch can't fake a combat end.
    bool leaderInCombat = (mode != 0);
    bool partyInCombat  = acc::engine::IsAnyPartyMemberInCombat() || leaderInCombat;

    DWORD now = GetTickCount();

    // ---- Strong cue: real encounter begin/end, debounced on partyInCombat. ----
    static int   s_lastParty    = -1;        // -1 = first tick, suppress
    static int   s_pendingParty = -1;
    static DWORD s_partyChanged  = 0;

    if (s_lastParty < 0) {
        s_lastParty    = partyInCombat ? 1 : 0;
        s_pendingParty = s_lastParty;
        s_partyChanged = now;
        acclog::Write("Combat.Mode", "first-tick suppress; mode=%d party=%d",
                      mode, partyInCombat);
    } else {
        int desired = partyInCombat ? 1 : 0;
        if (desired != s_pendingParty) {
            s_pendingParty = desired;
            s_partyChanged = now;
        }
        if (s_pendingParty != s_lastParty &&
            (now - s_partyChanged) >= kCombatModeQuietMs) {
            bool nowInCombat = (s_pendingParty == 1);
            auto id = nowInCombat ? acc::strings::Id::CombatBegins
                                  : acc::strings::Id::CombatEnds;
            const char* phrase = acc::strings::Get(id);
            prism::Speak(phrase, /*interrupt=*/false);
            acclog::Write("Combat.Mode", "%s -> [%s] (debounced %ums)",
                          nowInCombat ? "entering" : "leaving",
                          phrase, static_cast<unsigned>(now - s_partyChanged));
            s_lastParty = s_pendingParty;

            // Auto-close the unified action menu the moment the *encounter*
            // ends — it's a persistent paused queueing surface that otherwise
            // lingers across the combat→explore boundary, so the first
            // post-fight Enter lands on a menu entry instead of the world
            // object the user meant to use (patch-20260617-215141.log: Enter
            // queued "Heilen" instead of using the Versorgungsstation). Gated
            // on partyInCombat, not the leader global, so a Tab to a peaceful
            // member no longer tears the menu down (and releases its pause)
            // mid-fight.
            if (!nowInCombat && acc::unified_menu::IsActive()) {
                acc::unified_menu::ForceDisarm("combat-end");
            }
        }
    }

    // ---- Subtle cue: the controlled leader is at peace while the encounter
    //      continues (Tab to a not-yet-engaged member, or the leader breaking
    //      off while companions still fight). Debounced on the leader global so
    //      a flicker doesn't spam; only the falling edge while the party is
    //      still fighting is surprising enough to call out. The inverse (Tab
    //      onto a fighting member) is covered by the leader-name announce. ----
    static int   s_lastLeader    = -1;
    static int   s_pendingLeader = -1;
    static DWORD s_leaderChanged  = 0;

    if (s_lastLeader < 0) {
        s_lastLeader    = leaderInCombat ? 1 : 0;
        s_pendingLeader = s_lastLeader;
        s_leaderChanged = now;
    } else {
        int desired = leaderInCombat ? 1 : 0;
        if (desired != s_pendingLeader) {
            s_pendingLeader = desired;
            s_leaderChanged = now;
        }
        if (s_pendingLeader != s_lastLeader &&
            (now - s_leaderChanged) >= kCombatModeQuietMs) {
            bool wasLeaving = (s_pendingLeader == 0);
            s_lastLeader = s_pendingLeader;
            // Only speak when the leader dropped to peace but the encounter is
            // still live (partyInCombat). On a real end both fall together and
            // partyInCombat is already false here, so the strong "Kampf
            // beendet" fires instead and this stays silent.
            if (wasLeaving && partyInCombat) {
                const char* phrase =
                    acc::strings::Get(acc::strings::Id::CombatLeaderAtPeace);
                prism::Speak(phrase, /*interrupt=*/false);
                acclog::Write("Combat.Mode",
                              "leader at peace while party fights -> [%s]",
                              phrase);
            }
        }
    }
}

// ============================================================================
// Phase 1B — combat-log poll on CSWGuiInGameMessages.messages_listbox.
// ============================================================================

namespace {

// CGuiInGame.in_game_messages slot @+0x1c — the engine's persistent
// CSWGuiInGameMessages instance. Allocated once when CGuiInGame is
// constructed (start of game session) and lives the whole time, so
// AddMessages writes to it during live combat even though the review
// screen isn't mounted. The earlier panels[]-walk only found this panel
// when the user had the Messages screen open, missing every live
// combat-log row in the process.
// KOTOR 2 swaps this slot with PartySelection: the K2 creator 0x007BE4C0
// stores the CSWGuiInGameMessages ctor result (0x00757C40) at [gui+0x78]
// (see the slot table in engine_panels.cpp).
const size_t kCGuiInGameInGameMessagesOffset = acc::off::Pick(0x1c, 0x78);

// Resolve the persistent combat-log panel via the CGuiInGame singleton.
// nullptr until CGuiInGame is constructed (DLL attach / title screen).
void* FindInGameMessagesPanel() {
    void* gui = acc::engine::ResolveGuiInGame();
    if (!gui) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(gui) +
            kCGuiInGameInGameMessagesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Read a CSWGuiListBox's row count without dereferencing each row pointer.
int ReadListBoxRowCount(void* lb) {
    if (!lb) return 0;
    __try {
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
        if (!lbList || !lbList->data) return 0;
        return lbList->size;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Read row[i] as a control pointer. nullptr on miss / fault.
void* ReadListBoxRow(void* lb, int i) {
    if (!lb || i < 0) return nullptr;
    __try {
        auto* lbList = reinterpret_cast<CExoArrayList*>(
            reinterpret_cast<unsigned char*>(lb) + kListBoxControlsOffset);
        if (!lbList || !lbList->data) return nullptr;
        if (i >= lbList->size) return nullptr;
        return lbList->data[i];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

void TickCombatLog() {
    // Module-load latch (cutscene-load transient). FindInGameMessagesPanel
    // walks CSWGuiManager.panels[] which the engine is in the middle of
    // tearing down during a module transition.
    if (acc::transitions::IsModuleLoadPending()) return;

    void* panel = FindInGameMessagesPanel();
    if (!panel) {
        // Panel not mounted — keep a fresh baseline so the next open
        // doesn't replay history.
        return;
    }

    auto* base = reinterpret_cast<unsigned char*>(panel);
    void* lb = base + kInGameMessagesMessagesListBoxOffset;
    int   rows = ReadListBoxRowCount(lb);

    static void* s_lastLb     = nullptr;
    static int   s_lastRows   = 0;

    if (lb != s_lastLb) {
        // First sight of this listbox — adopt the current row count as
        // baseline; do NOT replay history.
        s_lastLb   = lb;
        s_lastRows = rows;
        acclog::Write("Combat.Log", "armed lb=%p rows=%d", lb, rows);
        return;
    }
    if (rows == s_lastRows) return;

    if (rows < s_lastRows) {
        // Listbox was reset (engine clear / reopen). Re-adopt baseline,
        // skip the spurious "delta" interpretation.
        acclog::Write("Combat.Log", "reset lb=%p rows %d -> %d", lb,
                      s_lastRows, rows);
        s_lastRows = rows;
        return;
    }

    // Delta path: log each newly-appended row. Speech is intentionally
    // OFF here — patch-20260521-093926.log proved that this listbox is
    // filled lazily when the review screen opens (all rows arrived in
    // one burst at panel-open time), not during live combat. Speaking
    // the burst would re-narrate the entire fight at review time. The
    // OnAddMessages hook is the live-narration source; this poll stays
    // as a sanity check on what messages_listbox ends up containing.
    int firstNew = s_lastRows;
    int delta    = rows - firstNew;
    acclog::Write("Combat.Log", "delta lb=%p +%d (rows %d -> %d)",
                  lb, delta, firstNew, rows);
    for (int i = firstNew; i < rows; ++i) {
        void* row = ReadListBoxRow(lb, i);
        if (!row) {
            acclog::Write("Combat.Log", "row %d null", i);
            continue;
        }
        char text[512];
        if (!acc::menus::extract::FromControl(row, text, sizeof(text))) {
            acclog::Write("Combat.Log", "row %d extract failed", i);
            continue;
        }
        if (text[0] == '\0') {
            acclog::Write("Combat.Log", "row %d empty", i);
            continue;
        }
        acclog::Write("Combat.Log", "row %d -> [%.300s]", i, text);
    }
    s_lastRows = rows;
}

}  // namespace acc::combat
