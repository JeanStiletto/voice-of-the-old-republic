#include "party_leader_announce.h"

#include "engine_offsets.h"   // Vector (global struct)
#include "engine_panels.h"    // IsForegroundUiBlocking
#include "engine_player.h"    // GetPlayerPosition, GetClientLeader,
                              //   GetActiveLeaderName
#include "hotkeys.h"
#include "log.h"
#include "prism.h"

namespace acc::party_leader_announce {

namespace {

// Tab arms a pending announce; the actual read happens on a *later* tick,
// once the engine has had time to process the keypress and swap the
// active leader. Our hotkey rising-edge detector runs from
// core_tick::Dispatch, which the engine drives before its own per-frame
// input pump processes Tab and re-wires CClientExoApp::GetPlayerCreature
// to the next party member. Reading on the same tick gets the *old*
// leader — confirmed live by the user 2026-05-23 ("Tab announces the
// previous leader, shifted by one").
//
// Two pieces of state:
//   g_pendingTicks  — countdown after Tab press. >0 means "keep watching
//                     for the leader swap". 0 means idle.
//   g_pendingLeader — the leader pointer captured at Tab press time.
//                     We announce when GetClientLeader() returns a
//                     *different* pointer, or when the countdown
//                     expires (the solo-party case: leader pointer
//                     never changes; speak the current name as the
//                     "still solo" UX signal the header doc calls out).
//
// 6 ticks ≈ ~100 ms at 60 fps — plenty of slack for the engine to swap,
// short enough that the announce stays tightly coupled to the keypress.
constexpr int kPendingWindowTicks = 6;
int   g_pendingTicks  = 0;
void* g_pendingLeader = nullptr;

void Speak() {
    char creatureName[64] = {0};
    if (!acc::engine::GetActiveLeaderName(creatureName, sizeof(creatureName))) {
        acclog::Write("PartyLeader", "Tab — no leader name resolved");
        return;
    }
    acclog::Write("PartyLeader", "Tab — speaking leader=[%s]", creatureName);
    prism::Speak(creatureName, /*interrupt=*/true);
}

}  // namespace

void Tick() {
    // Tab rising edge + foreground gate via the central registry. Tab is
    // the universal alt-tab modifier outside KOTOR, so the foreground
    // gate matters; Pressed() covers it.
    if (acc::hotkeys::Pressed(acc::hotkeys::Action::PartyLeaderAnnounce)) {
        // Player-loaded gate — title screen / module-load have no party.
        Vector unused;
        if (!acc::engine::GetPlayerPosition(unused)) return;

        // UI-block gate — when a panel is capturing input, the manager-side
        // Tab handler is doing panel cycling. Speaking the leader name on
        // top of "Inventar" / "Karte" / etc. is noise.
        acc::engine::UiBlockState ui;
        if (acc::engine::IsForegroundUiBlocking(&ui)) {
            acclog::Write("PartyLeader",
                          "Tab suppressed — ui blocked (fg=%p kind=%s)",
                          ui.fgPanel,
                          ui.fgPanel ? acc::engine::PanelKindName(ui.fgKind)
                                     : "?");
            return;
        }

        // Arm: capture pre-press leader, start watching for the engine to
        // swap it. Each new Tab press extends the window — a rapid double-
        // tap will announce the most recent leader rather than two stale
        // names.
        g_pendingLeader = acc::engine::GetClientLeader();
        g_pendingTicks  = kPendingWindowTicks;
        acclog::Write("PartyLeader",
                      "Tab armed — pre-press leader=%p, window=%d ticks",
                      g_pendingLeader, g_pendingTicks);
        return;
    }

    if (g_pendingTicks <= 0) return;

    void* current = acc::engine::GetClientLeader();
    if (current && current != g_pendingLeader) {
        // Engine swapped leader. Announce the new one.
        acclog::Write("PartyLeader",
                      "Tab — leader swapped (%p → %p), speaking",
                      g_pendingLeader, current);
        g_pendingTicks  = 0;
        g_pendingLeader = nullptr;
        Speak();
        return;
    }

    if (--g_pendingTicks == 0) {
        // Window expired with no swap. Solo-party / cycle-blocked case —
        // speak the current leader anyway so the user gets confirmation
        // that Tab did something (or that they're still solo).
        acclog::Write("PartyLeader",
                      "Tab — window expired with no swap (leader=%p), "
                      "speaking current",
                      current);
        g_pendingLeader = nullptr;
        Speak();
    }
}

}  // namespace acc::party_leader_announce
