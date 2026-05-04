#include "transitions.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "engine_player.h"
#include "engine_offsets.h"  // Vector
#include "engine_reads.h"    // ReadCExoString
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

// Stability dedup for room transitions. The room-resolver flickers
// every tick when the player stands at a boundary between rooms —
// captured live `2026-05-04` in `patch-20260504-203810.log`: 60+
// transitions m01aa_08d ↔ m01aa_09 over 21 seconds. Filter by
// requiring the new room to be observed for `kRoomStabilityTicks`
// consecutive ticks before announcing. At ~60 fps that's ~80ms — too
// short to feel laggy, long enough to absorb single-tick boundary
// flickers. Area changes don't need this (rare and definitive).
constexpr int kRoomStabilityTicks = 5;
int   g_pending_room_idx   = -1;
int   g_pending_room_count = 0;

// Per-room landmark cache. Built once on each area change by scanning
// every CSWSWaypoint with has_map_note != 0 AND map_note_enabled != 0,
// resolving its room via GetRoomAtIndexed, and recording its map_note
// CExoLocString text. Lookup by room index gives the Bioware-authored
// "atmospheric" label (e.g. "Brücke", "Frachtraum", "Mannschaftsquartier").
// Falls back to the resref / synthesised "Raum N" path when no
// landmark covers a given room.
//
// Fog-of-war respect: filtering on map_note_enabled prevents spoiling
// locations the player hasn't yet discovered on the in-game map. When
// the player walks into an unrevealed room, our cache won't have an
// entry and we fall back to "Raum N" — same information channel the
// sighted player has via the unmarked map slot.
//
// First-come wins on collision: if multiple landmark waypoints share
// a room (rare), the first one encountered during iteration is kept.
// Refinement (closest-to-room-centre, longest name, etc.) is parked
// until in-game testing shows ambiguous picks.
//
// Sized at kMaxRoomsCache=128 — vanilla KOTOR areas have <50 rooms
// each. Cache is invalidated (zeroed) on every area change.
constexpr int kMaxRoomsCache = 128;
char g_room_landmark[kMaxRoomsCache][128];
int  g_room_landmark_count = 0;

// Heuristic: vanilla KOTOR content stores room names as the .lyt-room
// identifier (`m01aa_10`, `stunt_03_main`, `unk_m13ab`) — pronounceable
// but meaningless, and they read as letter-soup noise through a screen
// reader. We match anything that looks like a resref token and fall
// back to a synthesised "Raum N" label for those cases. Custom mods
// that supply human-readable names ("Bridge", "Cargo Hold") fall
// through the heuristic and read normally.
//
// Heuristic rules — all of these flag the name as resref-style:
//   - Starts with `m\d`, `M\d`, `stunt`, or `Stunt`.
//   - Contains an underscore.
//
// The underscore rule is the catch-all: KOTOR room ids universally
// contain `_`, while real English / German room names don't.
bool IsResrefStyleRoomName(const char* name) {
    if (!name || name[0] == '\0') return true;
    if ((name[0] == 'm' || name[0] == 'M') && name[1] >= '0' && name[1] <= '9') {
        return true;
    }
    if ((name[0] == 's' || name[0] == 'S') &&
        (name[1] == 't') && (name[2] == 'u') &&
        (name[3] == 'n') && (name[4] == 't')) {
        return true;
    }
    for (const char* p = name; *p; ++p) {
        if (*p == '_') return true;
    }
    return false;
}

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

void RebuildLandmarkCache(void* area) {
    // Reset cache. Use the index loop instead of memset so we keep
    // the Vector member alignment guarantees of any future struct
    // refactor; cheap (128 × 1-byte zero each).
    for (int i = 0; i < kMaxRoomsCache; ++i) g_room_landmark[i][0] = '\0';
    g_room_landmark_count = 0;

    if (!area) return;

    int scanned = 0, landmarks = 0, placed = 0;
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        ++scanned;
        int kind = acc::engine::GetObjectKind(obj);
        if (kind != static_cast<int>(
                acc::engine::GameObjectKind::Waypoint)) {
            continue;
        }
        // Two gates: must be a landmark (has_map_note bit) AND map note
        // currently enabled (engine fog-of-war model). The latter is the
        // spoiler-protection path; without it we'd surface labels for
        // unrevealed locations.
        if (!acc::engine::IsLandmarkWaypoint(obj))   continue;
        if (!acc::engine::IsMapNoteEnabled(obj))     continue;
        ++landmarks;

        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

        int roomIdx = -1;
        void* room = acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
        if (!room || roomIdx < 0 || roomIdx >= kMaxRoomsCache) continue;

        char note[128] = {0};
        if (!acc::engine::GetWaypointMapNote(obj, note, sizeof(note))) {
            continue;
        }

        // First-come wins. Multiple landmarks per room is rare; refine
        // only if in-game testing shows ambiguous picks (e.g. prefer
        // closest-to-room-centre, prefer longest name).
        if (g_room_landmark[roomIdx][0] == '\0') {
            std::strncpy(g_room_landmark[roomIdx], note,
                         sizeof(g_room_landmark[roomIdx]) - 1);
            g_room_landmark[roomIdx]
                [sizeof(g_room_landmark[roomIdx]) - 1] = '\0';
            ++g_room_landmark_count;
            ++placed;
        }
    }

    acclog::Write(
        "Transition: landmark cache rebuilt — scanned=%d landmarks=%d "
        "placed=%d (areaPtr=%p)",
        scanned, landmarks, placed, area);
}

const char* GetLandmarkForRoom(int roomIdx) {
    if (roomIdx < 0 || roomIdx >= kMaxRoomsCache) return nullptr;
    if (g_room_landmark[roomIdx][0] == '\0') return nullptr;
    return g_room_landmark[roomIdx];
}

void SpeakRoom(void* area, int roomIndex) {
    char nameBuf[128] = {0};
    bool gotName = acc::engine::GetRoomDisplayName(
        area, roomIndex, nameBuf, sizeof(nameBuf)) && nameBuf[0] != '\0';

    // Resolution priority (most descriptive first):
    //   1. Bioware-curated map-note landmark in the same room (preferred).
    //   2. Modder-supplied room_name when human-readable.
    //   3. Synthesised "Raum N" — when name is a resref-style ID or empty.
    const char* landmark = GetLandmarkForRoom(roomIndex);

    char speech[192] = {0};
    const char* spokenSource = "(none)";
    if (landmark) {
        std::snprintf(speech, sizeof(speech),
                      acc::strings::Get(acc::strings::Id::FmtTransitionRoom),
                      landmark);
        spokenSource = "landmark";
    } else if (gotName && !IsResrefStyleRoomName(nameBuf)) {
        std::snprintf(speech, sizeof(speech),
                      acc::strings::Get(acc::strings::Id::FmtTransitionRoom),
                      nameBuf);
        spokenSource = "room_name";
    } else {
        std::snprintf(speech, sizeof(speech),
                      acc::strings::Get(
                          acc::strings::Id::FmtTransitionRoomIndex),
                      roomIndex);
        spokenSource = "index";
    }
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write(
        "Transition: room -> %d '%s' src=%s landmark=%s (areaPtr=%p)",
        roomIndex, gotName ? nameBuf : "(empty)", spokenSource,
        landmark ? landmark : "-", area);
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
        // Rebuild the per-room landmark cache for the new area before
        // any room-change branch can fire — the first room announce
        // after an area change should already use the curated label
        // when one exists.
        RebuildLandmarkCache(area);
        g_prev_area          = area;
        g_prev_room_idx      = -1;  // re-announce room on new area
        g_pending_room_idx   = -1;  // and reset stability tracker
        g_pending_room_count = 0;
    }

    int roomIndex = -1;
    void* room = acc::engine::GetRoomAtIndexed(area, pos, roomIndex);
    if (!room || roomIndex < 0) return;  // outside any room (rare; void zones)

    if (roomIndex == g_prev_room_idx) {
        // Already-announced room — clear any pending different-room
        // observation (player wandered toward the boundary then back).
        g_pending_room_idx   = -1;
        g_pending_room_count = 0;
        return;
    }

    // Different room observed. Require kRoomStabilityTicks consecutive
    // observations of the SAME new room before announcing — filters
    // boundary-flicker thrash (m01aa_08d ↔ m01aa_09 type oscillation).
    if (roomIndex == g_pending_room_idx) {
        ++g_pending_room_count;
    } else {
        g_pending_room_idx   = roomIndex;
        g_pending_room_count = 1;
    }

    if (g_pending_room_count >= kRoomStabilityTicks) {
        SpeakRoom(area, roomIndex);
        g_prev_room_idx      = roomIndex;
        g_pending_room_idx   = -1;
        g_pending_room_count = 0;
    }
}

void AnnouncePreLoadDestination(void* exoStringPtr) {
    if (!exoStringPtr) return;

    // CExoString = { char* c_string; uint32 length } at offset 0.
    // ReadCExoString already SEH-guards the c_string read.
    char dest[128] = {0};
    if (!acc::engine::ReadCExoString(exoStringPtr, /*offset=*/0,
                                     dest, sizeof(dest))) {
        acclog::Write(
            "Transition: pre-load string read failed (exoStr=%p)",
            exoStringPtr);
        return;
    }
    if (dest[0] == '\0') return;  // empty resref — silently skip

    // Dedup: the engine sometimes fires SetMoveToModuleString more than
    // once inside the same transition (e.g. once with the raw resref,
    // once with a normalized form). Suppress repeats of the same
    // destination within a 2s window so the user hears the announce
    // exactly once per transition.
    static char  s_lastDest[128] = {0};
    static DWORD s_lastTick      = 0;
    DWORD now = GetTickCount();
    if (std::strncmp(s_lastDest, dest, sizeof(s_lastDest)) == 0 &&
        (now - s_lastTick) < 2000u) {
        return;
    }
    std::strncpy(s_lastDest, dest, sizeof(s_lastDest) - 1);
    s_lastDest[sizeof(s_lastDest) - 1] = '\0';
    s_lastTick = now;

    char speech[160] = {0};
    std::snprintf(speech, sizeof(speech),
                  acc::strings::Get(acc::strings::Id::FmtTransitionLoading),
                  dest);
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write("Transition: pre-load -> '%s'", dest);
}

}  // namespace acc::transitions
