#include "party_leader_announce.h"

#include <cstdint>

#include "engine_area.h"      // GetObjectName
#include "engine_offsets.h"   // Vector (global struct)
#include "engine_panels.h"    // IsForegroundUiBlocking
#include "engine_player.h"    // GetPlayerPosition, GetPlayerServerCreature,
                              // GetPlayerCharacterName
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

    // Read the controlled creature AFTER the engine has processed Tab
    // (this Tick runs from the per-frame dispatcher, which runs once per
    // frame after input). Same chain passive_narrate / interact_hotkey
    // use for resolving the leader.
    void*    creature   = acc::engine::GetPlayerServerCreature();
    char     creatureName[64] = {0};
    if (creature) {
        acc::engine::GetObjectName(creature, creatureName,
                                   sizeof(creatureName));
        // Player creature: CSWSCreatureStats.first_name + tag are empty in
        // vanilla saves (chargen writes the chosen name to
        // CClientExoAppInternal::player_character_name @+0x294, not the
        // creature stats). Fall back to the dedicated accessor so the PC
        // announces as "Test" / "Revan" / etc. instead of silence.
        if (creatureName[0] == '\0') {
            acc::engine::GetPlayerCharacterName(creatureName,
                                                sizeof(creatureName));
        }
    }
    if (creatureName[0] == '\0') {
        acclog::Write("PartyLeader",
                      "Tab — no leader name resolved (creature=%p)",
                      creature);
        return;
    }

    // No diff gate: repetition is intentional UX signal. With a solo party
    // (one member, or a story beat that strips companions, or the engine's
    // solo-mode toggle active), each Tab press speaks the same name; the
    // user reads that as "still solo / no swap available". Suppressing on
    // diff would silently swallow that confirmation.
    acclog::Write("PartyLeader",
                  "Tab — speaking leader=%p name=[%s]",
                  creature, creatureName);
    tolk::Speak(creatureName, /*interrupt=*/true);
}

}  // namespace acc::party_leader_announce
