#include "spectator_scene.h"

#include <cstddef>

#include "engine_area.h"   // GetObjectTag, GetCurrentArea
#include "log.h"
#include "prism.h"
#include "strings.h"

namespace acc::spectator {

namespace {

// Case-insensitive ASCII prefix test (tags are ASCII resref-style).
bool HasPrefixCI(const char* s, const char* prefix) {
    for (; *prefix; ++s, ++prefix) {
        char a = *s;
        char b = *prefix;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;   // also catches s ending early (a == '\0')
    }
    return true;
}

// The scripted scene's combatants. Prefix (not exact) so the whole squad
// matches on both sides — the doomed Republic soldiers (end_cut2_soldier1..4)
// AND the Sith cutting them down (end_cut2_sith1..5) all share the "end_cut2_"
// tag stem. Matching only the soldiers left the Sith reading as normal enemies
// when the player cycled onto their side, so the "this is scenery" cue never
// fired. The area's real, killable Sith are tagged "end_sith*"/"end_soldier*"
// (no "cut2"), so they're untouched by this widened prefix.
constexpr char kSceneTagPrefix[] = "end_cut2_";

// Per-soldier latch: the doomed cue speaks once for EACH distinct scripted-
// battle creature, not once per area and not on every passive re-narration of
// the same one. Players were getting stuck because the old once-per-visit latch
// only flagged the first soldier as scenery — the rest read as normal enemies,
// so people kept trying to engage/reach a battle they can't join. Now every
// soldier you cycle onto is clearly marked, at the cost of some repetition
// (accepted: clarity matters more here than brevity). The announced-set is
// reset when the area pointer changes (re-entry / module load rebuilds it).
constexpr int kMaxAnnounced = 32;  // scene has 9 creatures; headroom to spare
void* g_lastArea             = nullptr;
void* g_announced[kMaxAnnounced] = {};
int   g_announcedCount       = 0;

}  // namespace

bool IsScriptedBattleSoldier(void* obj) {
    if (!obj) return false;
    char tag[96];
    if (!acc::engine::GetObjectTag(obj, tag, sizeof(tag)) || tag[0] == '\0') {
        return false;
    }
    return HasPrefixCI(tag, kSceneTagPrefix);
}

const char* DramaticLine() {
    return acc::strings::Get(acc::strings::Id::SpectatorBattleDoomed);
}

void OnObjectNarrated(void* obj) {
    if (!IsScriptedBattleSoldier(obj)) return;

    void* area = acc::engine::GetCurrentArea();
    if (area != g_lastArea) {
        g_lastArea        = area;
        g_announcedCount  = 0;   // new visit — every soldier is "unheard" again
    }

    // Already announced this soldier this visit? Stay quiet on re-narration.
    for (int i = 0; i < g_announcedCount; ++i) {
        if (g_announced[i] == obj) return;
    }
    if (g_announcedCount < kMaxAnnounced) {
        g_announced[g_announcedCount++] = obj;
    }
    // If the set is somehow full (never for this 9-creature scene), fall through
    // and speak anyway rather than go silent — a rare repeat beats dropping the
    // cue (cf. never-silence-the-fallback).

    // interrupt=false so the warning follows the name/brief the funnel just
    // spoke ("Republikanischer Soldat, 17 Meter …") rather than cutting it.
    prism::Speak(DramaticLine(), /*interrupt=*/false);
    acclog::Write("Spectator", "doomed-battle cue -> [%s]", DramaticLine());
}

}  // namespace acc::spectator
