#include "transitions.h"

#include <cstdio>

#include "engine_area.h"
#include "engine_player.h"
#include "engine_offsets.h"  // Vector
#include "log.h"
#include "strings.h"
#include "tolk.h"

namespace acc::transitions {

namespace {

// Module state. Both initialise to "no observation yet" sentinels — a
// nullptr area and -1 room index. The first tick that resolves an area
// fires the area-change branch and sets prev_area_ptr, the first tick
// that resolves a room index fires the room-change branch and sets
// prev_room_index. There is no separate first-tick suppression: the
// initial announce is the user's "you're now in {area} / {room}"
// orientation cue on game-load — silence here would be unhelpful per
// feedback_never_silence_fallback_announcement.
void* g_prev_area      = nullptr;
int   g_prev_room_idx  = -1;

void SpeakArea(void* area) {
    char nameBuf[128] = {0};
    if (!acc::engine::GetAreaDisplayName(area, nameBuf, sizeof(nameBuf)) ||
        nameBuf[0] == '\0') {
        // Even when name resolution fails entirely, log the event so
        // post-mortem can correlate the silence against an area change.
        acclog::Write(
            "Transition: area change detected but name resolve failed; "
            "areaPtr=%p", area);
        return;
    }
    char speech[160] = {0};
    std::snprintf(speech, sizeof(speech),
                  acc::strings::Get(acc::strings::Id::FmtTransitionArea),
                  nameBuf);
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write("Transition: area -> '%s' (areaPtr=%p)", nameBuf, area);
}

void SpeakRoom(void* area, int roomIndex) {
    char nameBuf[128] = {0};
    if (!acc::engine::GetRoomDisplayName(area, roomIndex,
                                         nameBuf, sizeof(nameBuf)) ||
        nameBuf[0] == '\0') {
        // Empty room names are common in some areas (a single unnamed
        // room covering the whole module). Log silently — no speech, no
        // user-facing fallback. Distinct from the area-name failure case
        // above where we want to flag the resolve miss.
        acclog::Write(
            "Transition: room %d empty/unresolved (areaPtr=%p)",
            roomIndex, area);
        return;
    }
    char speech[160] = {0};
    std::snprintf(speech, sizeof(speech),
                  acc::strings::Get(acc::strings::Id::FmtTransitionRoom),
                  nameBuf);
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write(
        "Transition: room -> %d '%s' (areaPtr=%p)", roomIndex, nameBuf, area);
}

}  // namespace

void Tick() {
    Vector pos = {};
    if (!acc::engine::GetPlayerPosition(pos)) {
        // Reset state on player loss so the next in-game tick re-anchors
        // cleanly (matches camera_announce's reset-on-gate-failure
        // discipline).
        g_prev_area     = nullptr;
        g_prev_room_idx = -1;
        return;
    }

    void* area = acc::engine::GetCurrentArea();
    if (!area) {
        // Player is loaded but area resolve faulted (mid-load hand-off).
        // Don't reset prev_area — the area pointer is stable across the
        // brief windows where GetCurrentArea returns null mid-frame, and
        // resetting would re-fire the area announce next tick.
        return;
    }

    if (area != g_prev_area) {
        SpeakArea(area);
        g_prev_area     = area;
        g_prev_room_idx = -1;  // re-announce room on new area
    }

    int roomIndex = -1;
    void* room = acc::engine::GetRoomAtIndexed(area, pos, roomIndex);
    if (!room || roomIndex < 0) return;  // outside any room (rare; void zones)

    if (roomIndex != g_prev_room_idx) {
        SpeakRoom(area, roomIndex);
        g_prev_room_idx = roomIndex;
    }
}

}  // namespace acc::transitions
