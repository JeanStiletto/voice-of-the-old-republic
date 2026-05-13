#include "transitions.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "combat.h"          // IsCombatActive — gate room-change speech
#include "engine_area.h"
#include "engine_panels.h"   // IsForegroundUiBlocking — gate vs. menus / dialogs
#include "engine_player.h"
#include "engine_offsets.h"  // Vector
#include "engine_reads.h"    // ReadCExoString
#include "log.h"
#include "region_classifier.h"  // BuildCacheForArea + LookupRoomShape
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
// Position storage (2026-05-13): the waypoint's world position is
// stored alongside the text so callers can gate the landmark tier on
// proximity to the actual waypoint, not just "any point in the same
// .lyt-room". K1 ships .lyt-rooms shaped as long thin transition
// strips (the "Zur Oberstadt" doorway sliver covers rooms 50+ metres
// apart inside Taris South Apartments); without proximity gating the
// landmark fires every time the player crosses the sliver, regardless
// of how far they are from the actual waypoint.
//
// Sized at kMaxRoomsCache=128 — vanilla KOTOR areas have <50 rooms
// each. Cache is invalidated (zeroed) on every area change.
constexpr int kMaxRoomsCache = 128;
char   g_room_landmark[kMaxRoomsCache][128];
Vector g_room_landmark_pos[kMaxRoomsCache];
bool   g_room_landmark_has_pos[kMaxRoomsCache];
int    g_room_landmark_count = 0;

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
//
// Definition moved out of the anonymous namespace (below) so the
// map-cursor can reuse the same filter for its ambient announce.

void SpeakArea(void* area) {
    char nameBuf[128] = {0};
    if (!acc::engine::GetAreaDisplayName(area, nameBuf, sizeof(nameBuf)) ||
        nameBuf[0] == '\0') {
        // Even when name resolution fails entirely, log the event so
        // post-mortem can correlate the silence against an area change.
        acclog::Write("Transition", "area change detected but name resolve failed; "
            "areaPtr=%p", area);
        return;
    }
    char speech[160] = {0};
    std::snprintf(speech, sizeof(speech),
                  acc::strings::Get(acc::strings::Id::FmtTransitionArea),
                  nameBuf);
    tolk::Speak(speech, /*interrupt=*/false);
    acclog::Write("Transition", "area -> '%s' (areaPtr=%p)", nameBuf, area);
}

void RebuildLandmarkCache(void* area) {
    // Reset cache. Use the index loop instead of memset so we keep
    // the Vector member alignment guarantees of any future struct
    // refactor; cheap (128 × 1-byte zero each).
    for (int i = 0; i < kMaxRoomsCache; ++i) {
        g_room_landmark[i][0]     = '\0';
        g_room_landmark_pos[i]    = {0, 0, 0};
        g_room_landmark_has_pos[i] = false;
    }
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
            g_room_landmark_pos[roomIdx]     = pos;
            g_room_landmark_has_pos[roomIdx] = true;
            ++g_room_landmark_count;
            ++placed;
            acclog::Write("Transition",
                          "landmark room=%d '%s' pos=(%.2f,%.2f,%.2f)",
                          roomIdx, note, pos.x, pos.y, pos.z);
        }
    }

    acclog::Write("Transition", "landmark cache rebuilt — scanned=%d landmarks=%d "
        "placed=%d (areaPtr=%p)",
        scanned, landmarks, placed, area);
}

// Definition of GetLandmarkForRoom moved out of the anonymous
// namespace (below) so the map-cursor can read the same cache.

// Last spoken room label, used as the text-equality dedup key. The .lyt-
// room partition over-segments KOTOR areas (Endar Spire bridge is split
// into 6 indices that all classify as the same junction); raw room-
// index comparison would re-announce on every micro-crossing. Comparing
// the resolved label collapses the spam: as long as the player walks
// through cells whose label matches, transitions stays quiet. Resets on
// area change.
char g_last_spoken_room_text[128] = {0};

// Coordinate hysteresis state. Patch-20260513-045034 had bursts like
// 6 announcements in 6 seconds while crossing 5 small .lyt-rooms — the
// stability + text-dedup gates passed because each room had a distinct
// label, but the player perceived it as spam. Requiring N metres of
// player displacement from the previous announcement collapses these
// bursts to a much calmer cadence. Walking at 2 m/s with a 4 m gate
// caps speech at one announce per 2s along continuous travel.
// Stationary movement (boundary flickering at a doorway) is already
// caught by the stability-tick gate; this catches the orthogonal
// "moving fast through small cells" case.
Vector g_last_spoken_pos       = {0.0f, 0.0f, 0.0f};
bool   g_last_spoken_pos_valid = false;
constexpr float kMinAnnounceDistanceM = 4.0f;

// Proximity-based landmark fire state. Patch-20260513-045034 showed
// the player walked 1.5m from the "Zu deinem Apartment" waypoint at
// (91.98, 134.42) but never crossed into the .lyt-room that owns
// the landmark — room 11 is a thin sliver around the door. Result:
// the landmark never fired. Decouple landmark firing from .lyt-room
// crossing entirely: each tick, find the nearest landmark within
// enter-range; if stable for N ticks and not the last-spoken one,
// speak it. Exit at a larger range so a landmark re-announces when
// the player walks away and comes back, but not when standing
// nearby for a long time.
constexpr float kLandmarkEnterRangeM     = 8.0f;
constexpr float kLandmarkExitRangeM      = 12.0f;
constexpr int   kLandmarkStabilityTicks  = 5;
int   g_lm_prox_pending_idx     = -1;
int   g_lm_prox_pending_count   = 0;
int   g_lm_prox_last_spoken_idx = -1;

// Maximum distance (world units; KOTOR = metres) between the player /
// cursor and a landmark waypoint for the landmark tier to fire. .lyt-
// rooms in K1 stretch across long thin transition strips, so "same
// .lyt-room as the waypoint" alone over-fires (room 8 in Taris South
// Apartments covers a 50m sliver per patch-20260512-223500). 15m
// matches the typical room-diagonal in vanilla content while excluding
// the long sliver case. Tune from in-game testing.
constexpr float kLandmarkProximityMetres = 15.0f;

bool PlayerInLandmarkRange(int roomIdx, const Vector& worldPos) {
    Vector lp;
    if (!GetLandmarkPositionForRoom(roomIdx, lp)) {
        // No recorded waypoint position — treat as "always in range"
        // so we don't suppress speech for rooms whose position read
        // faulted at cache build. The position write happens
        // immediately after GetObjectPosition succeeds, so this is
        // only the SEH fault path.
        return true;
    }
    float dx = worldPos.x - lp.x;
    float dy = worldPos.y - lp.y;
    float d2 = dx * dx + dy * dy;
    return d2 <= (kLandmarkProximityMetres * kLandmarkProximityMetres);
}

// Resolve the speech for a room using the same tier order the map
// cursor's ambient announce uses:
//   1. Bioware-authored landmark   (e.g. "Brücke", "Mannschaftsquartier")
//   2. Mod-supplied friendly name  (filters resref-style ids)
//   3. Region-cache shape label    (e.g. "Korridor, Nord-Süd, 4 Meter")
// Returns false (and leaves outBuf empty) when no tier resolves — vanilla
// resref-style rooms with no shape classification stay silent rather than
// announce a meaningless engine-internal id.
bool ResolveRoomSpeech(void* area, int roomIndex,
                       const Vector& worldPos,
                       char* outBuf, size_t bufSize,
                       const char*& outSource) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    outSource = "none";

    // Landmark tier removed from the room-transition path 2026-05-13.
    // Landmarks now fire via TickProximityLandmarks below, which scans
    // every waypoint each tick and triggers on geometric proximity to
    // the waypoint position — not on .lyt-room crossing. This catches
    // landmarks whose .lyt-room is a thin sliver the player passes
    // *next to* without crossing into ("Zu deinem Apartment" case in
    // patch-20260513-045034). Room-tier kept for shape + friendly
    // room name only.

    char nameBuf[128] = {0};
    if (acc::engine::GetRoomDisplayName(area, roomIndex,
                                        nameBuf, sizeof(nameBuf)) &&
        nameBuf[0] != '\0' &&
        !IsResrefStyleRoomName(nameBuf)) {
        std::snprintf(outBuf, bufSize, "%s", nameBuf);
        outSource = "room_name";
        return true;
    }

    char shapeBuf[128] = {0};
    int  shapeSig = 0;
    // Prefer the direct room-index lookup (the resolved index is
    // authoritative); fall through to position-based lookup with
    // nearest-room snap if the room isn't populated in the cache.
    if (acc::region::LookupRoomShape(area, roomIndex,
                                     shapeBuf, sizeof(shapeBuf),
                                     shapeSig) ||
        acc::region::LookupShapeAt(area, worldPos,
                                   shapeBuf, sizeof(shapeBuf),
                                   shapeSig)) {
        if (shapeBuf[0] != '\0') {
            std::snprintf(outBuf, bufSize, "%s", shapeBuf);
            outSource = "shape";
            return true;
        }
    }
    return false;
}

// Speak a room change. Tier-based label resolution; text-equality dedup
// against `g_last_spoken_room_text` collapses adjacent .lyt-rooms with
// identical labels (corridor cells, repeated junctions). Gated by the
// caller via combat / UI-blocking checks — `Tick` decides whether to
// invoke us, this function speaks unconditionally once called.
void SpeakRoomChange(void* area, int roomIndex, const Vector& worldPos) {
    char speechBuf[128] = {0};
    const char* source = "none";
    if (!ResolveRoomSpeech(area, roomIndex, worldPos,
                           speechBuf, sizeof(speechBuf), source) ||
        speechBuf[0] == '\0') {
        acclog::Write("Transition",
                      "room -> %d unresolved (src=none) — staying silent "
                      "(areaPtr=%p)", roomIndex, area);
        return;
    }

    if (std::strncmp(speechBuf, g_last_spoken_room_text,
                     sizeof(g_last_spoken_room_text)) == 0) {
        acclog::Write("Transition",
                      "room -> %d '%s' src=%s — text-dedup match, silent",
                      roomIndex, speechBuf, source);
        return;
    }

    // Coordinate hysteresis. Suppress shape / room-name announcements
    // when the player hasn't moved far from the previous spoken
    // position — collapses the "rapid burst through small cells"
    // pattern (patch-20260513-045034 had 6/6s peaks crossing 5 .lyt-
    // rooms). Landmarks bypass: they're rare, high-value, and already
    // gated to within 15m of the actual waypoint.
    bool isLandmark = (std::strcmp(source, "landmark") == 0);
    if (!isLandmark && g_last_spoken_pos_valid) {
        float dx = worldPos.x - g_last_spoken_pos.x;
        float dy = worldPos.y - g_last_spoken_pos.y;
        float d2 = dx * dx + dy * dy;
        float minD2 = kMinAnnounceDistanceM * kMinAnnounceDistanceM;
        if (d2 < minD2) {
            acclog::Write("Transition",
                          "room -> %d '%s' src=%s — hysteresis "
                          "(%.2fm < %.1fm), silent",
                          roomIndex, speechBuf, source,
                          std::sqrt(d2), kMinAnnounceDistanceM);
            // Advance text-dedup state so the *next* qualifying
            // announce treats this label as already-known.
            std::strncpy(g_last_spoken_room_text, speechBuf,
                         sizeof(g_last_spoken_room_text) - 1);
            g_last_spoken_room_text[
                sizeof(g_last_spoken_room_text) - 1] = '\0';
            return;
        }
    }

    // Walking-adapter room change uses Prism+SAPI urgent so it survives
    // NVDA's typed-character cancellation while W/S is held. Same
    // rationale as the map cursor's ambient announce — sustained WASD
    // input would otherwise silently cancel a queued Tolk_Output call.
    tolk::SpeakUrgent(speechBuf);
    std::strncpy(g_last_spoken_room_text, speechBuf,
                 sizeof(g_last_spoken_room_text) - 1);
    g_last_spoken_room_text[sizeof(g_last_spoken_room_text) - 1] = '\0';
    g_last_spoken_pos       = worldPos;
    g_last_spoken_pos_valid = true;
    acclog::Write("Transition",
                  "room -> %d '%s' src=%s (areaPtr=%p)",
                  roomIndex, speechBuf, source, area);
}

// Forward declaration so TickProximityLandmarks can call it.
bool IsWorldSpeechGatedImpl();

// Per-tick scan over the landmark cache. Decoupled from .lyt-room
// crossings so landmarks whose room is a thin sliver still announce
// when the player walks close to the waypoint. Three-state machine
// mirroring turn_announce: pending idx + pending count for stability,
// last-spoken idx for "already announced this approach" suppression;
// last-spoken resets when the player walks past `kLandmarkExitRangeM`
// so a return visit re-announces.
void TickProximityLandmarks(const Vector& playerPos) {
    // Re-arm: if the last-spoken landmark is now beyond exit range,
    // clear it so we can re-announce on a future approach.
    if (g_lm_prox_last_spoken_idx >= 0) {
        int i = g_lm_prox_last_spoken_idx;
        if (i >= 0 && i < kMaxRoomsCache && g_room_landmark_has_pos[i]) {
            float dx = playerPos.x - g_room_landmark_pos[i].x;
            float dy = playerPos.y - g_room_landmark_pos[i].y;
            float d2 = dx * dx + dy * dy;
            if (d2 > kLandmarkExitRangeM * kLandmarkExitRangeM) {
                acclog::Write("Transition",
                              "landmark proximity re-arm idx=%d "
                              "(dist=%.2fm > %.1fm exit)",
                              i, std::sqrt(d2), kLandmarkExitRangeM);
                g_lm_prox_last_spoken_idx = -1;
            }
        } else {
            // Stale index after area change.
            g_lm_prox_last_spoken_idx = -1;
        }
    }

    // Find the nearest landmark inside enter range.
    int   nearest = -1;
    float bestD2  = kLandmarkEnterRangeM * kLandmarkEnterRangeM;
    for (int i = 0; i < kMaxRoomsCache; ++i) {
        if (!g_room_landmark_has_pos[i])    continue;
        if (g_room_landmark[i][0] == '\0')  continue;
        float dx = playerPos.x - g_room_landmark_pos[i].x;
        float dy = playerPos.y - g_room_landmark_pos[i].y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestD2) {
            bestD2  = d2;
            nearest = i;
        }
    }

    if (nearest < 0) {
        // No landmark in range — clear pending.
        g_lm_prox_pending_idx   = -1;
        g_lm_prox_pending_count = 0;
        return;
    }

    if (nearest == g_lm_prox_last_spoken_idx) {
        // Same landmark we already announced this approach.
        g_lm_prox_pending_idx   = -1;
        g_lm_prox_pending_count = 0;
        return;
    }

    if (nearest == g_lm_prox_pending_idx) {
        ++g_lm_prox_pending_count;
    } else {
        g_lm_prox_pending_idx   = nearest;
        g_lm_prox_pending_count = 1;
    }

    if (g_lm_prox_pending_count < kLandmarkStabilityTicks) return;

    // Fire — unless gated by combat / blocking UI. We still advance
    // last-spoken so the gate releasing doesn't trigger a burst of
    // stale landmarks for a player who stood still during combat.
    if (IsWorldSpeechGatedImpl()) {
        acclog::Write("Transition",
                      "landmark proximity -> '%s' (idx=%d dist=%.2fm) "
                      "gated, state advanced silently",
                      g_room_landmark[nearest], nearest, std::sqrt(bestD2));
    } else {
        tolk::SpeakUrgent(g_room_landmark[nearest]);
        acclog::Write("Transition",
                      "landmark proximity -> '%s' (idx=%d dist=%.2fm)",
                      g_room_landmark[nearest], nearest, std::sqrt(bestD2));
    }
    g_lm_prox_last_spoken_idx = nearest;
    g_lm_prox_pending_idx     = -1;
    g_lm_prox_pending_count   = 0;
}

// Forward declaration of the public predicate (defined at file scope
// below namespace). Used internally by Tick before going through the
// indirection.
bool IsWorldSpeechGatedImpl() {
    if (acc::combat::IsCombatActive()) return true;
    acc::engine::UiBlockState ui;
    if (acc::engine::IsForegroundUiBlocking(&ui)) return true;
    return false;
}

}  // namespace

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

const char* GetLandmarkForRoom(int roomIdx) {
    if (roomIdx < 0 || roomIdx >= kMaxRoomsCache) return nullptr;
    if (g_room_landmark[roomIdx][0] == '\0') return nullptr;
    return g_room_landmark[roomIdx];
}

bool GetLandmarkPositionForRoom(int roomIdx, Vector& outPos) {
    if (roomIdx < 0 || roomIdx >= kMaxRoomsCache) return false;
    if (!g_room_landmark_has_pos[roomIdx])        return false;
    outPos = g_room_landmark_pos[roomIdx];
    return true;
}

void Tick() {
    Vector pos = {};
    if (!acc::engine::GetPlayerPosition(pos)) {
        // Reset state on player loss so the next in-game tick re-anchors
        // cleanly (matches camera_announce's reset-on-gate-failure
        // discipline).
        g_prev_area     = nullptr;
        g_prev_room_idx = -1;
        g_last_spoken_room_text[0] = '\0';
        g_last_spoken_pos_valid    = false;
        g_lm_prox_pending_idx      = -1;
        g_lm_prox_pending_count    = 0;
        g_lm_prox_last_spoken_idx  = -1;
        acc::region::Reset();
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
        // Build the region-classifier shape cache for the new area at
        // the same moment. Single source of truth shared with the map-
        // cursor and view-mode adapters; built once on area-enter so
        // walking, view-mode panning, and map-cursor exploration all
        // see the same labels.
        //
        // The wall-edge cache the classifier depends on may not be
        // ready on this exact tick (spatial_change_detector::Tick runs
        // later in the dispatch order). BuildCacheForArea silently
        // leaves the cache empty when that's the case; we retry below
        // on every tick until it builds successfully.
        acc::region::BuildCacheForArea(area);
        g_prev_area          = area;
        g_prev_room_idx      = -1;  // re-announce room on new area
        g_pending_room_idx   = -1;  // and reset stability tracker
        g_pending_room_count = 0;
        g_last_spoken_room_text[0] = '\0';
        g_last_spoken_pos_valid    = false;
        g_lm_prox_pending_idx      = -1;
        g_lm_prox_pending_count    = 0;
        g_lm_prox_last_spoken_idx  = -1;
    } else if (!acc::region::HasCacheForArea(area)) {
        // Same area as last tick but the cache still isn't built — wall
        // cache wasn't ready when the area-change branch fired. Retry
        // cheaply each tick until the build succeeds. BuildCacheForArea
        // self-gates on the wall cache so this is a no-op until that's
        // populated.
        acc::region::BuildCacheForArea(area);
    }

    // Proximity-based landmark scan runs every tick, independent of
    // .lyt-room crossings. Fires when the player enters within 8m of
    // any landmark waypoint, with stability + exit-hysteresis. Must
    // run BEFORE the room-transition early-returns so it isn't gated
    // by the player standing still in one .lyt-room.
    TickProximityLandmarks(pos);

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
        // Gate speech (not state) on combat / blocking-UI. We still
        // commit g_prev_room_idx so the player walking back-and-forth
        // during combat doesn't queue up a burst of room announcements
        // for the moment combat ends; the room they're standing in
        // when combat ends becomes the new baseline.
        if (IsWorldSpeechGatedImpl()) {
            acclog::Write("Transition",
                          "room -> %d gated (combat / blocking UI), "
                          "state advanced silently", roomIndex);
        } else {
            SpeakRoomChange(area, roomIndex, pos);
        }
        g_prev_room_idx      = roomIndex;
        g_pending_room_idx   = -1;
        g_pending_room_count = 0;
    }
}

bool IsWorldSpeechGated() {
    return IsWorldSpeechGatedImpl();
}

void AnnouncePreLoadDestination(void* exoStringPtr) {
    if (!exoStringPtr) return;

    // CExoString = { char* c_string; uint32 length } at offset 0.
    // ReadCExoString already SEH-guards the c_string read.
    char dest[128] = {0};
    if (!acc::engine::ReadCExoString(exoStringPtr, /*offset=*/0,
                                     dest, sizeof(dest))) {
        acclog::Write("Transition", "pre-load string read failed (exoStr=%p)",
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
    acclog::Write("Transition", "pre-load -> '%s'", dest);
}

}  // namespace acc::transitions
