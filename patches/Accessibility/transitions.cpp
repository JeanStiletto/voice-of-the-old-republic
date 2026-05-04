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

void SpeakRoom(void* area, int roomIndex) {
    char nameBuf[128] = {0};
    bool gotName = acc::engine::GetRoomDisplayName(
        area, roomIndex, nameBuf, sizeof(nameBuf)) && nameBuf[0] != '\0';

    char speech[160] = {0};
    if (gotName && !IsResrefStyleRoomName(nameBuf)) {
        // Modder gave a real human-readable name — speak it as-is.
        std::snprintf(speech, sizeof(speech),
                      acc::strings::Get(acc::strings::Id::FmtTransitionRoom),
                      nameBuf);
    } else {
        // Vanilla resref or empty — synthesise "Raum N" / "Room N".
        // Keep the room index as the user-facing identifier; it's
        // unique within the area and pronounceable. Resref (when
        // available) still goes to the log for post-mortem.
        std::snprintf(speech, sizeof(speech),
                      acc::strings::Get(
                          acc::strings::Id::FmtTransitionRoomIndex),
                      roomIndex);
    }
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write(
        "Transition: room -> %d '%s' (areaPtr=%p)",
        roomIndex, gotName ? nameBuf : "(empty)", area);
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
