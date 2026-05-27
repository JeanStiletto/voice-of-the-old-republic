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
#include "narrated_target.h" // clear on area transition
#include "same_name_suffix.h"   // Reset() on area transition
#include "strings.h"
#include "prism.h"
#include "wall_topology.h"      // single source of truth for perceptual-
                                // region labels (nav-graph decomposition)

namespace acc::transitions {

namespace {

// Module state. Both initialise to "no observation yet" sentinels — a
// nullptr area and a "none" cluster id. The first tick that resolves an
// area fires the area-change branch and sets prev_area_ptr, the first
// tick that resolves a labelled cluster fires the cluster-change branch.
// There is no separate first-tick suppression: the initial announce is
// the user's "you're now in {area} / {region}" orientation cue on game-
// load — silence here would be unhelpful per
// feedback_never_silence_fallback_announcement.
//
// Trigger moved off .lyt-room ids 2026-05-22 (see
// docs/room-shape-improvements.md). Empirical: room ids flipped on
// 94–98 % of player-step samples in dense Taris areas, forcing a text-
// dedup escape hatch to collapse the noise. `wall_topology` cluster
// ids are spatially stable and correspond to perceptual regions —
// walking inside a corridor keeps the id constant, crossing into a
// junction flips it once. Cluster-change is the right signal.
void* g_prev_area        = nullptr;
int   g_prev_cluster_id  = acc::wall_topology::kClusterIdNone;

// Latch covering the cutscene-load transient (see IsModuleLoadPending
// in the header for the why). Set in AnnouncePreLoadDestination from
// the OnSetMoveToModuleString entry hook; cleared in Tick() the next
// time we observe a fresh area pointer, or on player-loss reset.
bool  g_module_load_pending = false;

// Last friendly room name observed at the player position. Tracked so
// we ALSO fire on friendly-name change inside a single cluster — covers
// the TSL case where two adjacent .lyt-rooms share a cluster but have
// distinct human-readable names. K1 never trips this path in vanilla
// content (every room name is resref-style and filtered out by
// IsResrefStyleRoomName); the cost is a strcmp per tick.
char  g_prev_friendly_room_name[64] = {0};

// Stability dedup for the cluster trigger. Cluster boundaries flicker
// far less than .lyt-room boundaries (the player needs ~2-3m of motion
// to cross a real cluster boundary vs ~0.3m for some rooms), but a
// short stability window still smooths the rare snap-flip at a
// boundary. ~80ms at 60fps — too short to feel laggy, long enough to
// absorb a single-tick boundary glitch.
constexpr int kClusterStabilityTicks = 5;
int   g_pending_cluster_id    = acc::wall_topology::kClusterIdNone;
int   g_pending_cluster_count = 0;

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
// Set by wall_topology::AttachLandmarksToDoors when this slot's landmark
// gets embedded in a cluster label. TickProximityLandmarks skips matched
// slots so the player doesn't hear "Kreuzung, Ost, Tür Süd, Zur Oberstadt"
// followed by a redundant standalone "Zur Oberstadt" a second later.
bool   g_room_landmark_door_matched[kMaxRoomsCache];

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
    prism::Speak(speech, /*interrupt=*/false);
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
        g_room_landmark_door_matched[i] = false;
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

// Coordinate hysteresis retired 2026-05-13. The original problem it
// solved — 6 announcements in 6 seconds across 5 small .lyt-rooms with
// distinct labels — doesn't exist under Path 3, which collapses
// adjacent same-type regions onto a single nav-graph node. Text-dedup
// alone now handles the only remaining "spam" case (repeated same
// label). With the gate at 4m the system was silencing genuine
// junction-to-junction crossings in dense layouts (Apartments at the
// Dias Apartment cluster, 2026-05-13 patch-20260513-111345.log line
// 11:15:49 "Kreuzung, West, Ost, Nord" silenced by 1.95m displacement).
// The pos-valid/pos fields stay defined for log compatibility but are
// no longer consulted.
Vector g_last_spoken_pos       = {0.0f, 0.0f, 0.0f};
bool   g_last_spoken_pos_valid = false;

// Platz delayed-announce state. When a multi-node Path 3 cluster
// resolves (kind == KindPlatz) we defer the SAPI announce for
// kPlatzDelayMs ms so the player has time to walk further into the
// cluster — by the time the announce fires they're closer to the
// centroid, so the centroid-relative direction list (West, Nord, …)
// matches their actual perception of "which way is which" better.
//
// We re-resolve at fire time: if the player has crossed into a
// different cluster meanwhile, the new label fires instead of the
// stale one.
char   g_pending_platz_text[128] = {0};
DWORD  g_pending_platz_tick      = 0;
bool   g_pending_platz_valid     = false;
int    g_pending_platz_room      = -1;
Vector g_pending_platz_pos       = {0.0f, 0.0f, 0.0f};
constexpr DWORD kPlatzDelayMs = 1000;

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

// Resolve the speech at `worldPos` using a two-tier order:
//   1. Friendly room name at the resolved .lyt-room (filters resref-
//      style ids) → "room_name"
//   2. wall_topology::LookupAt (nav-graph topology) → "shape"
//
// Tier 1 stays on .lyt-room ids: the friendly-name table is keyed by
// room index, and the room-name look-up itself is stable for the rare
// authored names. Tier 2 is cluster-aware via wall_topology. Returns
// false (and leaves outBuf empty) when no tier resolves. Vanilla
// resref-style rooms with no shape classification stay silent rather
// than announce a meaningless engine-internal id.
//
// Transition / doorway composition was removed 2026-05-13 after
// empirical evidence (Oberstadt 40m east-west street labelled
// "Türschwelle" 14 times in a row) showed K1's .lyt-room boundaries
// don't correspond to doorways. The Path 3 classifier now treats every
// degree-2 node as a corridor; real doorway detection would need
// wall-geometry constriction sensing.
bool ResolveRoomSpeech(void* area, const Vector& worldPos,
                       char* outBuf, size_t bufSize,
                       const char*& outSource,
                       int* outClusterIdOpt = nullptr) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    outSource = "none";
    if (outClusterIdOpt) *outClusterIdOpt = acc::wall_topology::kClusterIdNone;

    // Landmark tier removed from the room-transition path 2026-05-13.
    // Landmarks now fire via TickProximityLandmarks below, which scans
    // every waypoint each tick and triggers on geometric proximity to
    // the waypoint position — not on .lyt-room crossing.

    int roomIndex = -1;
    acc::engine::GetRoomAtIndexed(area, worldPos, roomIndex);

    char tier1Buf[128] = {0};
    if (roomIndex >= 0 &&
        acc::engine::GetRoomDisplayName(area, roomIndex,
                                        tier1Buf, sizeof(tier1Buf)) &&
        tier1Buf[0] != '\0' &&
        !IsResrefStyleRoomName(tier1Buf)) {
        std::snprintf(outBuf, bufSize, "%s", tier1Buf);
        outSource = "room_name";
        // Still consult cluster for the trigger key — fire on either
        // friendly-name OR cluster change.
        if (outClusterIdOpt) {
            char scratch[128] = {0};
            int  scratchSig = 0;
            acc::wall_topology::LookupAt(area, worldPos,
                                         scratch, sizeof(scratch),
                                         scratchSig, *outClusterIdOpt);
        }
        return true;
    }

    char path3Buf[128] = {0};
    int  path3Sig    = 0;
    int  clusterId   = acc::wall_topology::kClusterIdNone;
    if (acc::wall_topology::LookupAt(area, worldPos,
                                     path3Buf, sizeof(path3Buf),
                                     path3Sig, clusterId) &&
        path3Buf[0] != '\0') {
        std::snprintf(outBuf, bufSize, "%s", path3Buf);
        outSource = "shape";
        if (outClusterIdOpt) *outClusterIdOpt = clusterId;
        return true;
    }
    return false;
}

// Diagnostic — log wall_topology's resolution at this position alongside
// whatever the resolved speech was (room name, shape, or transition
// compose). Mirrors the announcement quad we see in MapCursor / ViewMode
// so a single grep across categories shows every nav-graph decision.
void LogWallTopoComparison(void* area, int roomIndex,
                           const Vector& worldPos,
                           const char* spoken,
                           const char* source) {
    char path3[128] = {0};
    int  path3Sig    = 0;
    int  path3Cid    = acc::wall_topology::kClusterIdNone;
    bool havePath3 = acc::wall_topology::LookupAt(
        area, worldPos, path3, sizeof(path3), path3Sig, path3Cid);
    const int path3Kind = havePath3 ? (path3Sig & 0xff) : -1;

    acclog::Write("WallTopo.Compare",
                  "pos=(%.2f,%.2f) room=%d spoken=\"%s\" src=%s | "
                  "path3=\"%s\" kind=%d sig=%d cluster=%d",
                  worldPos.x, worldPos.y, roomIndex,
                  spoken ? spoken : "", source ? source : "",
                  havePath3 ? path3 : "<no graph>",
                  path3Kind, path3Sig, path3Cid);
}

// Speak a perceptual-region change. The caller (Tick) decides whether
// to invoke us based on cluster_id transitions; this function resolves
// the label, applies text-equality dedup against `g_last_spoken_room_text`
// as a belt-and-braces filter (rare case: two clusters generating the
// exact same label string), and speaks. Gated by the caller via
// combat / UI-blocking checks.
//
// Platz delay: when the resolved label is a multi-node Path 3 cluster
// (kind == KindPlatz), defer the SAPI announce for kPlatzDelayMs. The
// label + position are stashed; `TickPendingPlatz` fires the announce
// later, re-resolving at the current player position so a player who
// walked through doesn't get a stale label.
//
// `clusterId` is the cluster the player just entered, captured by the
// caller via LookupAt. We log it for diagnostics; resolution itself
// goes through ResolveRoomSpeech so the friendly-name tier wins when
// available (TSL coverage).
void SpeakRoomChange(void* area, int clusterId, const Vector& worldPos) {
    char speechBuf[128] = {0};
    const char* source = "none";
    int resolvedCluster = acc::wall_topology::kClusterIdNone;
    if (!ResolveRoomSpeech(area, worldPos,
                           speechBuf, sizeof(speechBuf), source,
                           &resolvedCluster) ||
        speechBuf[0] == '\0') {
        acclog::Write("Transition",
                      "cluster -> %d unresolved (src=none) — staying silent "
                      "(areaPtr=%p)", clusterId, area);
        return;
    }

    // Resolve a diagnostic roomIndex once for log + LogWallTopoComparison.
    // Not used to gate anything — the trigger is upstream on cluster_id.
    int roomIndex = -1;
    acc::engine::GetRoomAtIndexed(area, worldPos, roomIndex);

    if (std::strncmp(speechBuf, g_last_spoken_room_text,
                     sizeof(g_last_spoken_room_text)) == 0) {
        acclog::Write("Transition",
                      "cluster -> %d '%s' src=%s room=%d — text-dedup "
                      "match, silent",
                      clusterId, speechBuf, source, roomIndex);
        return;
    }

    // Peek at the Path 3 kind to decide between immediate announce and
    // Platz-delay path. Re-runs LookupAt — cheap (linear scan over ~50
    // nodes); avoids threading the sig back through ResolveRoomSpeech.
    char   peekBuf[128] = {0};
    int    peekSig      = 0;
    int    peekCid      = acc::wall_topology::kClusterIdNone;
    int    peekKind     = -1;
    if (acc::wall_topology::LookupAt(area, worldPos,
                                     peekBuf, sizeof(peekBuf),
                                     peekSig, peekCid)) {
        peekKind = peekSig & 0xff;
    }

    if (peekKind == acc::wall_topology::KindPlatz) {
        // Defer: stash the resolved label + context for the timer in
        // Tick to fire. Advance g_last_spoken_room_text now so further
        // cluster transitions producing the same label get text-deduped
        // out — we only fire once per Platz entry.
        std::strncpy(g_pending_platz_text, speechBuf,
                     sizeof(g_pending_platz_text) - 1);
        g_pending_platz_text[sizeof(g_pending_platz_text) - 1] = '\0';
        g_pending_platz_tick  = GetTickCount();
        g_pending_platz_room  = roomIndex;
        g_pending_platz_pos   = worldPos;
        g_pending_platz_valid = true;
        std::strncpy(g_last_spoken_room_text, speechBuf,
                     sizeof(g_last_spoken_room_text) - 1);
        g_last_spoken_room_text[sizeof(g_last_spoken_room_text) - 1] = '\0';
        acclog::Write("Transition",
                      "cluster -> %d '%s' src=%s room=%d — Platz, deferred "
                      "%lums (areaPtr=%p)",
                      clusterId, speechBuf, source, roomIndex,
                      (unsigned long)kPlatzDelayMs, area);
        LogWallTopoComparison(area, roomIndex, worldPos, speechBuf, source);
        return;
    }

    // Non-Platz path: walking-adapter cluster change uses Prism+SAPI
    // urgent so it survives NVDA's typed-character cancellation while
    // W/S is held. Cancel any pending Platz announce (we just entered
    // a different shape, the deferred Platz no longer applies).
    g_pending_platz_valid = false;

    prism::SpeakUrgent(speechBuf);
    std::strncpy(g_last_spoken_room_text, speechBuf,
                 sizeof(g_last_spoken_room_text) - 1);
    g_last_spoken_room_text[sizeof(g_last_spoken_room_text) - 1] = '\0';
    g_last_spoken_pos       = worldPos;
    g_last_spoken_pos_valid = true;
    acclog::Write("Transition",
                  "cluster -> %d '%s' src=%s room=%d (areaPtr=%p)",
                  clusterId, speechBuf, source, roomIndex, area);
    LogWallTopoComparison(area, roomIndex, worldPos, speechBuf, source);
}

// Fire any pending Platz announce whose delay has elapsed. Re-resolves
// the label at the player's CURRENT position so a player who walked
// through the cluster in <1s gets the label they actually ended up at
// (or no announce, if they're now somewhere with the same label as the
// pending one — that already got committed to last_spoken_room_text on
// enqueue, so text-dedup eats the re-fire).
void TickPendingPlatz(void* area, const Vector& playerPos) {
    if (!g_pending_platz_valid) return;
    DWORD now = GetTickCount();
    if ((now - g_pending_platz_tick) < kPlatzDelayMs) return;

    // Diagnostic roomIdx for the log; not used for resolution itself.
    int roomIdx = -1;
    acc::engine::GetRoomAtIndexed(area, playerPos, roomIdx);

    char fireBuf[128] = {0};
    const char* source = "none";
    bool resolved = ResolveRoomSpeech(area, playerPos,
                                      fireBuf, sizeof(fireBuf), source) &&
                    fireBuf[0] != '\0';

    if (resolved &&
        std::strncmp(fireBuf, g_pending_platz_text,
                     sizeof(g_pending_platz_text)) == 0) {
        // Same Platz still under the player. Fire the announce now.
        prism::SpeakUrgent(fireBuf);
        g_last_spoken_pos       = playerPos;
        g_last_spoken_pos_valid = true;
        acclog::Write("Transition",
                      "Platz announce fired after %lums: '%s' (room=%d "
                      "areaPtr=%p)",
                      (unsigned long)(now - g_pending_platz_tick),
                      fireBuf, roomIdx, area);
    } else if (resolved &&
               std::strncmp(fireBuf, g_last_spoken_room_text,
                            sizeof(g_last_spoken_room_text)) != 0) {
        // Player has moved on to a different shape during the delay.
        // Fire the NEW label so we don't leave them unannounced.
        prism::SpeakUrgent(fireBuf);
        std::strncpy(g_last_spoken_room_text, fireBuf,
                     sizeof(g_last_spoken_room_text) - 1);
        g_last_spoken_room_text[sizeof(g_last_spoken_room_text) - 1] = '\0';
        g_last_spoken_pos       = playerPos;
        g_last_spoken_pos_valid = true;
        acclog::Write("Transition",
                      "Platz announce superseded after %lums: pending='%s' "
                      "current='%s' src=%s (room=%d areaPtr=%p)",
                      (unsigned long)(now - g_pending_platz_tick),
                      g_pending_platz_text, fireBuf, source, roomIdx, area);
    } else {
        // Either resolution failed or text-dedups against the same
        // last-spoken label (we already advanced last_spoken to the
        // pending Platz label on enqueue, so this branch fires when
        // the player is back where they started — same label).
        acclog::Write("Transition",
                      "Platz announce expired (no new label) pending='%s' "
                      "(room=%d areaPtr=%p)",
                      g_pending_platz_text, roomIdx, area);
    }
    g_pending_platz_valid = false;
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

    // Find the nearest landmark inside enter range. Skip slots whose
    // landmark wall_topology already embedded in a cluster label — the
    // player heard the name as part of the room-shape announce on
    // cluster entry, and a redundant standalone fire would talk over
    // the next legitimate cue (see project memory on the
    // cluster-vs-landmark double-announce regression).
    int   nearest = -1;
    float bestD2  = kLandmarkEnterRangeM * kLandmarkEnterRangeM;
    for (int i = 0; i < kMaxRoomsCache; ++i) {
        if (!g_room_landmark_has_pos[i])    continue;
        if (g_room_landmark[i][0] == '\0')  continue;
        if (g_room_landmark_door_matched[i]) continue;
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
        prism::SpeakUrgent(g_room_landmark[nearest]);
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

bool IterateLandmarks(int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outRoomIdx) {
    if (!nameOut || nameBufSize == 0) return false;
    if (cursor < 0) cursor = 0;
    while (cursor < kMaxRoomsCache) {
        int i = cursor++;
        if (!g_room_landmark_has_pos[i])    continue;
        if (g_room_landmark[i][0] == '\0')  continue;
        std::strncpy(nameOut, g_room_landmark[i], nameBufSize - 1);
        nameOut[nameBufSize - 1] = '\0';
        posOut     = g_room_landmark_pos[i];
        outRoomIdx = i;
        return true;
    }
    return false;
}

void MarkLandmarkClaimedByDoor(int roomIdx) {
    if (roomIdx < 0 || roomIdx >= kMaxRoomsCache) return;
    g_room_landmark_door_matched[roomIdx] = true;
}

void Tick() {
    Vector pos = {};
    if (!acc::engine::GetPlayerPosition(pos)) {
        // Reset state on player loss so the next in-game tick re-anchors
        // cleanly (matches camera_announce's reset-on-gate-failure
        // discipline).
        g_prev_area       = nullptr;
        g_prev_cluster_id = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_id    = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_count = 0;
        g_prev_friendly_room_name[0] = '\0';
        g_last_spoken_room_text[0]   = '\0';
        g_last_spoken_pos_valid      = false;
        g_lm_prox_pending_idx        = -1;
        g_lm_prox_pending_count      = 0;
        g_lm_prox_last_spoken_idx    = -1;
        g_pending_platz_valid        = false;
        acc::wall_topology::Reset();
        acc::narration::Reset();
        // Leave g_module_load_pending alone. Player-loss is the standard
        // mid-load symptom (PC slot wiped while the engine swaps modules);
        // clearing the latch here would re-open the per-tick probes
        // immediately and undo the whole point of the latch. The flag
        // clears on the area-pointer change below, which marks the new
        // module as actually surfaced.
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
        // Lift the module-load latch. The engine has surfaced a new area
        // pointer, so the CResRef-arena swap that the latch was protecting
        // against is complete. Per-tick consumers can resume probing
        // engine accessors on player / leader / area-object state.
        g_module_load_pending = false;
        SpeakArea(area);
        // Drop the unified narrated-target slot — any object from the old
        // area is now invalid. Stale pointers would survive otherwise
        // (TryGet's handle round-trip resolves through the active area's
        // game-object array, so a cross-area stale obj could in principle
        // get a false-positive validation). Explicit Clear is the safe
        // path.
        acc::narrated_target::Clear();
        // Rebuild the per-room landmark cache for the new area before
        // any room-change branch can fire — the first room announce
        // after an area change should already use the curated label
        // when one exists.
        RebuildLandmarkCache(area);
        // Build the nav-graph region decomposition for the new area.
        // Single source of truth shared with the map-cursor and
        // view-mode adapters; built once on area-enter so walking,
        // view-mode panning, and map-cursor exploration all see the
        // same labels.
        //
        // The wall-edge cache the decomposition depends on may not be
        // ready on this exact tick (spatial_change_detector::Tick runs
        // later in the dispatch order). BuildForArea silently leaves
        // the graph empty when that's the case; we retry below on
        // every tick until it builds successfully.
        acc::wall_topology::BuildForArea(area);
        g_prev_area             = area;
        g_prev_cluster_id       = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_id    = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_count = 0;
        g_prev_friendly_room_name[0] = '\0';
        g_last_spoken_room_text[0]   = '\0';
        g_last_spoken_pos_valid      = false;
        g_lm_prox_pending_idx        = -1;
        g_lm_prox_pending_count      = 0;
        g_lm_prox_last_spoken_idx    = -1;
        g_pending_platz_valid        = false;
    } else if (!acc::wall_topology::HasGraphForArea(area)) {
        // Same area as last tick but the graph still isn't built —
        // wall cache wasn't ready when the area-change branch fired.
        // Retry cheaply each tick until it builds. BuildForArea
        // self-gates on the wall cache (no-op until populated) and
        // is idempotent on a same-area call once built.
        acc::wall_topology::BuildForArea(area);
    }

    // Door snapshot stabiliser. The initial SnapshotDoors call inside
    // BuildForArea can lose late-arriving door handles to a partially-
    // populated server-object array; this per-tick refresh keeps
    // re-snapshotting until the count settles. No-op once the door
    // set commits or until the graph itself is built.
    acc::wall_topology::MaybeRefreshDoors(area);

    // Proximity-based landmark scan runs every tick, independent of
    // .lyt-room crossings. Fires when the player enters within 8m of
    // any landmark waypoint, with stability + exit-hysteresis. Must
    // run BEFORE the room-transition early-returns so it isn't gated
    // by the player standing still in one .lyt-room.
    TickProximityLandmarks(pos);

    // Pending Platz announce: fires whenever its delay elapses,
    // regardless of whether a new .lyt-room transition triggered this
    // tick. Lives outside the room-stability gate below so the
    // deferred speech reliably lands ~1s after entering a Platz
    // cluster.
    TickPendingPlatz(area, pos);

    // Cluster-based trigger (replaces .lyt-room change 2026-05-22).
    //
    // We probe wall_topology at the player position each tick — this is
    // the canonical "what perceptual region am I in" signal. cluster_id
    // is stable across .lyt-room flicker (the 94-98% per-step room flips
    // collapse to ~one flip per genuine region entry).
    //
    // Sentinel handling:
    //   kClusterIdNone     — graph not built yet / outside snap radius
    //                        with no candidates. Don't trigger; keep
    //                        prev state so the next valid cluster
    //                        compares correctly.
    //   kClusterIdOpenArea — LookupAt synthesised "Offene Fläche". This
    //                        is a STABLE id, so leaving labelled
    //                        terrain fires exactly one announcement
    //                        and re-entering a labelled cluster fires
    //                        again. Prevents the silent-Offene-Fläche
    //                        regression flagged in the plan review.
    //
    // We ALSO track friendly room-name change as a secondary trigger so
    // TSL content (where two .lyt-rooms inside one cluster can have
    // distinct human-readable names) still announces on the name flip.
    // K1 vanilla content never trips this — every room name is resref-
    // style and IsResrefStyleRoomName filters it out.
    char shapeBuf[128] = {0};
    int  shapeSig      = 0;
    int  clusterId     = acc::wall_topology::kClusterIdNone;
    // Per-tick probe: suppress LookupAt's WALL-FILTERED / ALL-BLOCKED
    // diagnostic logs. Speech-time call sites (SpeakRoomChange peek,
    // ResolveRoomSpeech, LogWallTopoComparison) still emit them on
    // every cluster transition, so we keep full visibility into wall-
    // filter behaviour without flooding the log with identical lines
    // at 60 fps when the player stands at a wall-filtered boundary.
    acc::wall_topology::LookupAt(area, pos,
                                 shapeBuf, sizeof(shapeBuf),
                                 shapeSig, clusterId,
                                 /*allowDiagLog=*/false);

    // Resolve the friendly room name (if any) for the player's current
    // .lyt-room. Empty / resref-style names normalise to "".
    char friendlyBuf[64] = {0};
    {
        int roomIdx = -1;
        acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
        if (roomIdx >= 0) {
            char nameBuf[64] = {0};
            if (acc::engine::GetRoomDisplayName(area, roomIdx,
                                                nameBuf, sizeof(nameBuf)) &&
                nameBuf[0] != '\0' &&
                !IsResrefStyleRoomName(nameBuf)) {
                std::strncpy(friendlyBuf, nameBuf, sizeof(friendlyBuf) - 1);
                friendlyBuf[sizeof(friendlyBuf) - 1] = '\0';
            }
        }
    }

    // Nothing to compare against yet — wait for the next tick where the
    // graph has finished building. Don't advance prev state on kClusterIdNone
    // so the first real cluster observation still triggers.
    if (clusterId == acc::wall_topology::kClusterIdNone &&
        friendlyBuf[0] == '\0') {
        return;
    }

    bool clusterChanged  = (clusterId != g_prev_cluster_id);
    bool friendlyChanged = (std::strncmp(friendlyBuf,
                                         g_prev_friendly_room_name,
                                         sizeof(g_prev_friendly_room_name)) != 0);

    if (!clusterChanged && !friendlyChanged) {
        // Stable inside the same cluster + same friendly name — clear
        // any pending different-cluster observation (player wandered
        // toward the boundary then back).
        g_pending_cluster_id    = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_count = 0;
        return;
    }

    // A change is observed. Require kClusterStabilityTicks consecutive
    // observations of the SAME new cluster before announcing — filters
    // single-tick boundary glitches. We key the stability on cluster_id
    // alone (friendly-name changes inside the same cluster fire after
    // the same window because the friendly check happens on commit).
    if (clusterId == g_pending_cluster_id) {
        ++g_pending_cluster_count;
    } else {
        g_pending_cluster_id    = clusterId;
        g_pending_cluster_count = 1;
    }

    if (g_pending_cluster_count >= kClusterStabilityTicks) {
        // Gate speech (not state) on combat / blocking-UI. We still
        // commit g_prev_cluster_id so the player walking back-and-forth
        // during combat doesn't queue up a burst of announcements for
        // the moment combat ends.
        if (IsWorldSpeechGatedImpl()) {
            acclog::Write("Transition",
                          "cluster -> %d gated (combat / blocking UI), "
                          "state advanced silently", clusterId);
        } else {
            SpeakRoomChange(area, clusterId, pos);
        }
        g_prev_cluster_id = clusterId;
        std::strncpy(g_prev_friendly_room_name, friendlyBuf,
                     sizeof(g_prev_friendly_room_name) - 1);
        g_prev_friendly_room_name[sizeof(g_prev_friendly_room_name) - 1] = '\0';
        g_pending_cluster_id    = acc::wall_topology::kClusterIdNone;
        g_pending_cluster_count = 0;
    }
}

bool IsWorldSpeechGated() {
    return IsWorldSpeechGatedImpl();
}

bool IsModuleLoadPending() {
    return g_module_load_pending;
}

void AnnouncePreLoadDestination(void* exoStringPtr) {
    // Latch the module-load transient unconditionally on every fire.
    // Even if the dedup window below suppresses the announce, the engine
    // is still beginning a transition — gating consumers off the announce
    // path would let the second-of-a-pair fire go un-latched and the
    // probes would resume mid-handoff.
    g_module_load_pending = true;

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
    prism::Speak(speech, /*interrupt=*/false);
    acclog::Write("Transition", "pre-load -> '%s'", dest);
}

}  // namespace acc::transitions
