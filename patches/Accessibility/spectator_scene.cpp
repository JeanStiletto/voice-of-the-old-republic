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

// The scene's Republic soldiers. Prefix (not exact) so soldier1/soldier2/…
// all match — the whole doomed squad shares this tag stem.
constexpr char kSoldierTagPrefix[] = "end_cut2_soldier";

// Per-area-visit latch: the first-sight cue speaks once, not on every Q/E pass
// over the soldier. Reset when the current area pointer changes (re-entry or a
// module load reconstructs the area).
void* g_lastArea       = nullptr;
bool  g_spokeThisVisit = false;

}  // namespace

bool IsScriptedBattleSoldier(void* obj) {
    if (!obj) return false;
    char tag[96];
    if (!acc::engine::GetObjectTag(obj, tag, sizeof(tag)) || tag[0] == '\0') {
        return false;
    }
    return HasPrefixCI(tag, kSoldierTagPrefix);
}

const char* DramaticLine() {
    return acc::strings::Get(acc::strings::Id::SpectatorBattleDoomed);
}

void OnObjectNarrated(void* obj) {
    if (!IsScriptedBattleSoldier(obj)) return;

    void* area = acc::engine::GetCurrentArea();
    if (area != g_lastArea) {
        g_lastArea       = area;
        g_spokeThisVisit = false;
    }
    if (g_spokeThisVisit) return;
    g_spokeThisVisit = true;

    // interrupt=false so the warning follows the name/brief the funnel just
    // spoke ("Republikanischer Soldat, 17 Meter …") rather than cutting it.
    prism::Speak(DramaticLine(), /*interrupt=*/false);
    acclog::Write("Spectator", "first-sight doomed-battle cue -> [%s]",
                  DramaticLine());
}

}  // namespace acc::spectator
