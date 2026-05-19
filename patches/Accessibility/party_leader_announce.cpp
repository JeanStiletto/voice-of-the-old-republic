#include "party_leader_announce.h"

#include "engine_offsets.h"   // Vector (global struct)
#include "engine_panels.h"    // IsForegroundUiBlocking
#include "engine_player.h"    // GetPlayerPosition, GetActiveLeaderName
#include "hotkeys.h"
#include "log.h"
#include "tolk.h"

namespace acc::party_leader_announce {

void Tick() {
    // Tab rising edge + foreground gate via the central registry. Tab is
    // the universal alt-tab modifier outside KOTOR, so the foreground
    // gate matters; Pressed() covers it.
    if (!acc::hotkeys::Pressed(acc::hotkeys::Action::PartyLeaderAnnounce)) return;

    // Player-loaded gate — title screen / module-load have no party.
    Vector unused;
    if (!acc::engine::GetPlayerPosition(unused)) return;

    // UI-block gate — when a panel is capturing input, the manager-side
    // Tab handler is doing panel cycling. Speaking the leader name on top
    // of "Inventar" / "Karte" / etc. is noise.
    acc::engine::UiBlockState ui;
    if (acc::engine::IsForegroundUiBlocking(&ui)) {
        acclog::Write("PartyLeader",
                      "Tab suppressed — ui blocked (fg=%p kind=%s)",
                      ui.fgPanel,
                      ui.fgPanel ? acc::engine::PanelKindName(ui.fgKind)
                                 : "?");
        return;
    }

    // Resolve the leader name via the single-sourced policy entry-point.
    // GetActiveLeaderName today returns the PC's CClientExoApp name slot
    // and intentionally avoids the engine's GetObjectName path: that path
    // overruns the caller's stack frame and trips the /GS canary during
    // the chargen→world transient (freezes new-game Play). When companion
    // leader support lands, GetActiveLeaderName grows the safe variant.
    char creatureName[64] = {0};
    if (!acc::engine::GetActiveLeaderName(creatureName, sizeof(creatureName))) {
        acclog::Write("PartyLeader", "Tab — no leader name resolved");
        return;
    }

    // No diff gate: repetition is intentional UX signal. With a solo party
    // (one member, or a story beat that strips companions, or the engine's
    // solo-mode toggle active), each Tab press speaks the same name; the
    // user reads that as "still solo / no swap available". Suppressing on
    // diff would silently swallow that confirmation.
    acclog::Write("PartyLeader", "Tab — speaking leader=[%s]", creatureName);
    tolk::Speak(creatureName, /*interrupt=*/true);
}

}  // namespace acc::party_leader_announce
