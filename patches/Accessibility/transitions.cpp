#include "transitions.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "bringup_announce.h" // IsMovieWindowForeground — gate speech during movies
#include "combat.h"          // IsCombatActive — gate room-change speech
#include "discovery.h"       // discovery-tier area reconciliation + landmark record
#include "engine_area.h"
#include "engine_input.h"    // ForceReacquireInput — post-load DirectInput re-grab
#include "engine_panels.h"   // IsForegroundUiBlocking — gate vs. menus / dialogs
#include "engine_player.h"
#include "engine_offsets.h"  // Vector
#include "engine_reads.h"    // ReadCExoString
#include "log.h"
#include "menus_modsettings.h"  // RoomShapes toggle gates the shape tier
#include "narrated_target.h" // clear on area transition
#include "same_name_suffix.h"   // Reset() on area transition
#include "strings.h"
#include "prism.h"
#include "wall_topology.h"      // single source of truth for perceptual-
                                // region labels (nav-graph decomposition)

// Forward decl from core_dllmain.cpp. The OnSetMoveToModuleString detour
// below calls this so Prism is loaded if a transition fires before any
// menu / focus event has run the same call.
void EnsurePrismInitialized();

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

// Flat landmark cache. Built once on each area change by scanning
// every CSWSWaypoint with has_map_note != 0 AND map_note_enabled != 0
// and recording its world position + map_note CExoLocString text. The
// cache is intentionally NOT keyed by .lyt-room — that's what this
// module used to do (`g_room_landmark[roomIdx]`) and the keying broke
// on K1's sliver-shaped rooms: a player / cursor / marker inside one
// sliver routinely failed to surface a landmark stored under the
// adjacent sliver. Lookups are proximity-based instead, via
// `FindLandmarkNear`.
//
// Fog-of-war respect: filtering on map_note_enabled prevents spoiling
// locations the player hasn't yet discovered on the in-game map. When
// the player walks into an unrevealed area, the relevant landmarks
// aren't in the cache; we fall back to room-name / shape tiers —
// same information channel the sighted player has.
//
// `doorMatched` is set by wall_topology::AttachLandmarksToDoors when
// this landmark's name has been embedded in a cluster label.
// TickProximityLandmarks skips matched entries so the player doesn't
// hear "Kreuzung, Ost, Tür Süd, Zur Oberstadt" followed by a redundant
// standalone "Zur Oberstadt" a second later.
//
// Sized at kMaxLandmarks=128 — vanilla KOTOR areas have <50 landmark
// waypoints each. Cache is invalidated (zeroed) on every area change.
constexpr int kMaxLandmarks = 128;
struct Landmark {
    char     name[128];
    Vector   pos;
    bool     doorMatched;
    uint32_t handle;       // server handle of the source waypoint — for discovery::Record
};
Landmark g_landmarks[kMaxLandmarks];
int      g_landmark_count = 0;

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

// Landmark-cache staleness tracking. Map notes can be script-enabled long
// after area entry (Shyrack-Höhle 2026-07-15: the 'Abtrünniger Sith'
// note was disabled at the 14:31 entry scan and enabled ~35 min later;
// the stale cache made it invisible to proximity announce + discovery
// for the whole visit). g_landmark_enabled_at_scan records the enabled
// count seen by the last RebuildLandmarkCache; TickLandmarkCacheRecheck
// re-counts periodically and rebuilds on drift.
int g_landmark_enabled_at_scan = 0;

void RebuildLandmarkCache(void* area) {
    // Reset cache. Use the index loop instead of memset so the Vector
    // member alignment guarantees survive any future struct refactor;
    // cheap (128 small entries).
    for (int i = 0; i < kMaxLandmarks; ++i) {
        g_landmarks[i].name[0]     = '\0';
        g_landmarks[i].pos         = {0, 0, 0};
        g_landmarks[i].doorMatched = false;
        g_landmarks[i].handle      = 0;
    }
    g_landmark_count = 0;

    if (!area) return;

    int scanned = 0, landmarks = 0, placed = 0, dropped = 0;
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

        char note[128] = {0};
        if (!acc::engine::GetWaypointMapNote(obj, note, sizeof(note))) {
            continue;
        }

        if (g_landmark_count >= kMaxLandmarks) {
            ++dropped;
            continue;
        }

        Landmark& lm = g_landmarks[g_landmark_count];
        std::strncpy(lm.name, note, sizeof(lm.name) - 1);
        lm.name[sizeof(lm.name) - 1] = '\0';
        lm.pos         = pos;
        lm.doorMatched = false;
        lm.handle      = acc::engine::GetObjectHandle(obj);
        acclog::Write("Transition",
                      "landmark[%d] '%s' pos=(%.2f,%.2f,%.2f)",
                      g_landmark_count, note, pos.x, pos.y, pos.z);
        ++g_landmark_count;
        ++placed;
    }

    if (dropped > 0) {
        acclog::Write("Transition",
                      "landmark cache overflow — %d landmarks dropped "
                      "(raise kMaxLandmarks above %d)",
                      dropped, kMaxLandmarks);
    }
    acclog::Write("Transition", "landmark cache rebuilt — scanned=%d landmarks=%d "
        "placed=%d (areaPtr=%p)",
        scanned, landmarks, placed, area);
    g_landmark_enabled_at_scan = landmarks;
}

// Last spoken room label, used as the text-equality dedup key. The .lyt-
// room partition over-segments KOTOR areas (Endar Spire bridge is split
// into 6 indices that all classify as the same junction); raw room-
// index comparison would re-announce on every micro-crossing. Comparing
// the resolved label collapses the spam: as long as the player walks
// through cells whose label matches, transitions stays quiet. Resets on
// area change.
std::string g_last_spoken_room_text;

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

// Boundary flap dedup (2026-07-15, Shyrack-Höhle). A small-catchment
// cluster next to a large one flips the nearest-node winner with half
// a step near the seam: the log shows 'Nord-Süd' ↔ 'Nord-West, Süd'
// alternating four times in 16 s at one spot, and a Kreuzung ↔ Bereich
// pair re-announcing 11 s apart from the same walkmesh room. Suppress
// only the RETURN leg: a label is skipped when it was itself spoken
// within kFlapWindowMs AND the player is still within kFlapRadiusM of
// where they heard it. First-time sequences through distinct regions
// (Korridor → Kreuzung → Bereich) stay fast and untouched, and a real
// revisit (walked away and back) re-announces. Unlike the retired
// 2026-05-13 coordinate hysteresis this can never silence a NEW label —
// it only ever eats the exact label heard seconds ago at the same spot.
constexpr DWORD kFlapWindowMs = 15000;
constexpr float kFlapRadiusM  = 4.0f;
// Label spoken one before the current g_last_spoken_room_text, with
// where/when it was spoken.
std::string g_flap_prev_text;
Vector      g_flap_prev_pos    = {0.0f, 0.0f, 0.0f};
DWORD       g_flap_prev_ms     = 0;
// Where/when the CURRENT g_last_spoken_room_text was spoken (rotates
// into the prev slot on the next commit).
Vector      g_flap_cur_pos     = {0.0f, 0.0f, 0.0f};
DWORD       g_flap_cur_ms      = 0;

// True when speaking `text` at `pos` would be the flap return-leg.
bool IsFlapRepeat(const std::string& text, const Vector& pos, DWORD now) {
    if (text.empty() || text != g_flap_prev_text) return false;
    if ((now - g_flap_prev_ms) >= kFlapWindowMs)  return false;
    const float dx = pos.x - g_flap_prev_pos.x;
    const float dy = pos.y - g_flap_prev_pos.y;
    return (dx * dx + dy * dy) < (kFlapRadiusM * kFlapRadiusM);
}

// Rotate the two-deep spoken-label history. Call wherever a label is
// committed as spoken (or Platz-enqueued, which commits the text).
void CommitSpokenLabel(const std::string& text, const Vector& pos,
                       DWORD now) {
    g_flap_prev_text = g_last_spoken_room_text;
    g_flap_prev_pos  = g_flap_cur_pos;
    g_flap_prev_ms   = g_flap_cur_ms;
    g_flap_cur_pos   = pos;
    g_flap_cur_ms    = now;
    g_last_spoken_room_text = text;
}

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
std::string g_pending_platz_text;
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

// Cadence for TickLandmarkCacheRecheck (see g_landmark_enabled_at_scan
// above RebuildLandmarkCache for the staleness story).
constexpr DWORD kLandmarkRecheckMs = 1000;
DWORD g_landmark_recheck_last_ms  = 0;

// Post-gate cluster re-announce. Cluster changes during combat / blocking
// UI advance state silently; without a refire the player ends combat
// standing in a room they never heard described (Shyrack-Höhle: clusters
// 33/75/76/125/141 all swallowed on first entry). When a gated change is
// recorded, refire the CURRENT room label once the gate has stayed clear
// for kGatedRefireDelayMs — the delay keeps it from talking over the
// combat-end announcement. Text-dedup in SpeakRoomChange makes this a
// no-op when the player is back in the last-spoken room.
constexpr DWORD kGatedRefireDelayMs   = 1500;
bool  g_gated_cluster_pending   = false;
DWORD g_gate_clear_since_ms     = 0;

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
                       std::string& outBuf,
                       const char*& outSource,
                       int* outClusterIdOpt = nullptr) {
    outBuf.clear();
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
        outBuf = tier1Buf;
        outSource = "room_name";
        // Still consult cluster for the trigger key — fire on either
        // friendly-name OR cluster change.
        if (outClusterIdOpt) {
            std::string scratch;
            int  scratchSig = 0;
            acc::wall_topology::LookupAt(area, worldPos,
                                         scratch,
                                         scratchSig, *outClusterIdOpt);
        }
        return true;
    }

    // Mod Settings → Room shape descriptions: when OFF, skip the
    // wall_topology shape tier entirely. Friendly room names (tier 1)
    // still announce because they're authored content, not synthesised
    // corridor / junction / Platz vocabulary. Cluster-id stays at None
    // so SpeakRoomChange's caller logs "unresolved" and the Platz
    // delay-enqueue path never arms.
    if (!acc::menus::modsettings::GetToggle(
            acc::menus::modsettings::Option::RoomShapes)) {
        return false;
    }

    std::string path3Buf;
    int  path3Sig    = 0;
    int  clusterId   = acc::wall_topology::kClusterIdNone;
    if (acc::wall_topology::LookupAt(area, worldPos,
                                     path3Buf,
                                     path3Sig, clusterId) &&
        !path3Buf.empty()) {
        outBuf = path3Buf;
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
    std::string path3;
    int  path3Sig    = 0;
    int  path3Cid    = acc::wall_topology::kClusterIdNone;
    bool havePath3 = acc::wall_topology::LookupAt(
        area, worldPos, path3, path3Sig, path3Cid);
    const int path3Kind = havePath3 ? (path3Sig & 0xff) : -1;

    acclog::Write("WallTopo.Compare",
                  "pos=(%.2f,%.2f) room=%d spoken=\"%s\" src=%s | "
                  "path3=\"%s\" kind=%d sig=%d cluster=%d",
                  worldPos.x, worldPos.y, roomIndex,
                  spoken ? spoken : "", source ? source : "",
                  havePath3 ? path3.c_str() : "<no graph>",
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
    std::string speechBuf;
    const char* source = "none";
    int resolvedCluster = acc::wall_topology::kClusterIdNone;
    if (!ResolveRoomSpeech(area, worldPos,
                           speechBuf, source,
                           &resolvedCluster) ||
        speechBuf.empty()) {
        acclog::Write("Transition",
                      "cluster -> %d unresolved (src=none) — staying silent "
                      "(areaPtr=%p)", clusterId, area);
        return;
    }

    // Resolve a diagnostic roomIndex once for log + LogWallTopoComparison.
    // Not used to gate anything — the trigger is upstream on cluster_id.
    int roomIndex = -1;
    acc::engine::GetRoomAtIndexed(area, worldPos, roomIndex);

    if (speechBuf == g_last_spoken_room_text) {
        acclog::Write("Transition",
                      "cluster -> %d '%s' src=%s room=%d — text-dedup "
                      "match, silent",
                      clusterId, speechBuf.c_str(), source, roomIndex);
        return;
    }

    // Boundary flap: this exact label was spoken moments ago and the
    // player hasn't really left the spot — the return leg of a seam
    // ping-pong, not new information. History is NOT rotated on the
    // suppressed leg, so the pair stays quiet until real displacement
    // or the window expiring re-arms it.
    if (IsFlapRepeat(speechBuf, worldPos, GetTickCount())) {
        acclog::Write("Transition",
                      "cluster -> %d '%s' src=%s room=%d — flap-dedup "
                      "(spoken %lums ago within %.1fm), silent",
                      clusterId, speechBuf.c_str(), source, roomIndex,
                      (unsigned long)(GetTickCount() - g_flap_prev_ms),
                      kFlapRadiusM);
        return;
    }

    // Peek at the Path 3 kind to decide between immediate announce and
    // Platz-delay path. Re-runs LookupAt — cheap (linear scan over ~50
    // nodes); avoids threading the sig back through ResolveRoomSpeech.
    std::string peekBuf;
    int    peekSig      = 0;
    int    peekCid      = acc::wall_topology::kClusterIdNone;
    int    peekKind     = -1;
    if (acc::wall_topology::LookupAt(area, worldPos,
                                     peekBuf,
                                     peekSig, peekCid)) {
        peekKind = peekSig & 0xff;
    }

    if (peekKind == acc::wall_topology::KindPlatz) {
        // Defer: stash the resolved label + context for the timer in
        // Tick to fire. Advance g_last_spoken_room_text now so further
        // cluster transitions producing the same label get text-deduped
        // out — we only fire once per Platz entry.
        g_pending_platz_text  = speechBuf;
        g_pending_platz_tick  = GetTickCount();
        g_pending_platz_room  = roomIndex;
        g_pending_platz_pos   = worldPos;
        g_pending_platz_valid = true;
        CommitSpokenLabel(speechBuf, worldPos, g_pending_platz_tick);
        acclog::Write("Transition",
                      "cluster -> %d '%s' src=%s room=%d — Platz, deferred "
                      "%lums (areaPtr=%p)",
                      clusterId, speechBuf.c_str(), source, roomIndex,
                      (unsigned long)kPlatzDelayMs, area);
        LogWallTopoComparison(area, roomIndex, worldPos, speechBuf.c_str(),
                              source);
        return;
    }

    // Non-Platz path: walking-adapter cluster change uses Prism+SAPI
    // urgent so it survives NVDA's typed-character cancellation while
    // W/S is held. Cancel any pending Platz announce (we just entered
    // a different shape, the deferred Platz no longer applies).
    g_pending_platz_valid = false;

    prism::SpeakUrgent(speechBuf.c_str());
    CommitSpokenLabel(speechBuf, worldPos, GetTickCount());
    g_last_spoken_pos       = worldPos;
    g_last_spoken_pos_valid = true;
    acclog::Write("Transition",
                  "cluster -> %d '%s' src=%s room=%d (areaPtr=%p)",
                  clusterId, speechBuf.c_str(), source, roomIndex, area);
    LogWallTopoComparison(area, roomIndex, worldPos, speechBuf.c_str(), source);
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

    std::string fireBuf;
    const char* source = "none";
    bool resolved = ResolveRoomSpeech(area, playerPos,
                                      fireBuf, source) &&
                    !fireBuf.empty();

    if (resolved && fireBuf == g_pending_platz_text) {
        // Same Platz still under the player. Fire the announce now.
        prism::SpeakUrgent(fireBuf.c_str());
        g_last_spoken_pos       = playerPos;
        g_last_spoken_pos_valid = true;
        acclog::Write("Transition",
                      "Platz announce fired after %lums: '%s' (room=%d "
                      "areaPtr=%p)",
                      (unsigned long)(now - g_pending_platz_tick),
                      fireBuf.c_str(), roomIdx, area);
    } else if (resolved && fireBuf != g_last_spoken_room_text) {
        // Player has moved on to a different shape during the delay.
        // Fire the NEW label so we don't leave them unannounced.
        prism::SpeakUrgent(fireBuf.c_str());
        CommitSpokenLabel(fireBuf, playerPos, now);
        g_last_spoken_pos       = playerPos;
        g_last_spoken_pos_valid = true;
        acclog::Write("Transition",
                      "Platz announce superseded after %lums: pending='%s' "
                      "current='%s' src=%s (room=%d areaPtr=%p)",
                      (unsigned long)(now - g_pending_platz_tick),
                      g_pending_platz_text.c_str(), fireBuf.c_str(), source,
                      roomIdx, area);
    } else {
        // Either resolution failed or text-dedups against the same
        // last-spoken label (we already advanced last_spoken to the
        // pending Platz label on enqueue, so this branch fires when
        // the player is back where they started — same label).
        acclog::Write("Transition",
                      "Platz announce expired (no new label) pending='%s' "
                      "(room=%d areaPtr=%p)",
                      g_pending_platz_text.c_str(), roomIdx, area);
    }
    g_pending_platz_valid = false;
}

// Forward declaration — defined below near TickProximityLandmarks.
bool IsWorldSpeechGatedImpl();

// Refire the current room label after a gated cluster change, once the
// gate (combat / blocking UI) has stayed clear for kGatedRefireDelayMs.
// Resolution happens at the CURRENT player position — if several gated
// changes piled up during a fight, only where the player actually ended
// up gets spoken. SpeakRoomChange's text-dedup keeps this silent when
// the player is back in the last room they already heard.
void TickGatedClusterRefire(void* area, const Vector& playerPos) {
    if (!g_gated_cluster_pending) return;

    DWORD now = GetTickCount();
    if (IsWorldSpeechGatedImpl()) {
        g_gate_clear_since_ms = 0;  // gate re-engaged — restart the clock
        return;
    }
    if (g_gate_clear_since_ms == 0) {
        g_gate_clear_since_ms = now;
        return;
    }
    if ((now - g_gate_clear_since_ms) < kGatedRefireDelayMs) return;

    g_gated_cluster_pending = false;
    g_gate_clear_since_ms   = 0;
    acclog::Write("Transition",
                  "gated-cluster refire: gate clear for %lums — "
                  "re-resolving at current position",
                  (unsigned long)kGatedRefireDelayMs);
    SpeakRoomChange(area, g_prev_cluster_id, playerPos);
}

// Forward declaration so TickProximityLandmarks can call it.
bool IsWorldSpeechGatedImpl();

// Detect map notes whose enabled flag flipped since the last cache scan
// and rebuild. Cheap: one flag sweep over the area object list, at most
// once per kLandmarkRecheckMs. On drift the proximity state is reset —
// cache indices are positional and a mid-list insertion would leave the
// pending/last-spoken indices pointing at the wrong entry.
void TickLandmarkCacheRecheck(void* area) {
    DWORD now = GetTickCount();
    if (g_landmark_recheck_last_ms != 0 &&
        (now - g_landmark_recheck_last_ms) < kLandmarkRecheckMs) {
        return;
    }
    g_landmark_recheck_last_ms = now;

    int enabled = 0;
    acc::engine::AreaObjectIterator iter(area);
    while (void* obj = iter.Next()) {
        if (acc::engine::GetObjectKind(obj) != static_cast<int>(
                acc::engine::GameObjectKind::Waypoint)) {
            continue;
        }
        if (!acc::engine::IsLandmarkWaypoint(obj)) continue;
        if (!acc::engine::IsMapNoteEnabled(obj))   continue;
        ++enabled;
    }
    if (enabled == g_landmark_enabled_at_scan) return;

    acclog::Write("Transition",
                  "landmark recheck: enabled notes %d -> %d — rebuilding "
                  "cache (areaPtr=%p)",
                  g_landmark_enabled_at_scan, enabled, area);
    RebuildLandmarkCache(area);
    acc::wall_topology::AttachLandmarksToDoors(area);
    g_lm_prox_pending_idx     = -1;
    g_lm_prox_pending_count   = 0;
    g_lm_prox_last_spoken_idx = -1;
}

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
        if (i >= 0 && i < g_landmark_count) {
            float dx = playerPos.x - g_landmarks[i].pos.x;
            float dy = playerPos.y - g_landmarks[i].pos.y;
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

    // Find the nearest landmark inside enter range. Skip entries whose
    // landmark wall_topology already embedded in a cluster label — the
    // player heard the name as part of the room-shape announce on
    // cluster entry, and a redundant standalone fire would talk over
    // the next legitimate cue (see project memory on the
    // cluster-vs-landmark double-announce regression).
    int   nearest = -1;
    float bestD2  = kLandmarkEnterRangeM * kLandmarkEnterRangeM;
    for (int i = 0; i < g_landmark_count; ++i) {
        if (g_landmarks[i].name[0] == '\0') continue;
        if (g_landmarks[i].doorMatched)     continue;
        float dx = playerPos.x - g_landmarks[i].pos.x;
        float dy = playerPos.y - g_landmarks[i].pos.y;
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
                      g_landmarks[nearest].name, nearest, std::sqrt(bestD2));
    } else {
        prism::SpeakUrgent(g_landmarks[nearest].name);
        acclog::Write("Transition",
                      "landmark proximity -> '%s' (idx=%d dist=%.2fm)",
                      g_landmarks[nearest].name, nearest, std::sqrt(bestD2));
        // Organic discovery: the player walked close enough to hear this
        // landmark named. Record the source waypoint so the discovery-tier
        // cycle can resurface it. Only on the spoken (non-gated) path —
        // a silently-advanced gate means the player never heard it.
        void* lmObj = acc::engine::ResolveServerObjectHandle(
            g_landmarks[nearest].handle);
        if (lmObj) acc::discovery::Record(lmObj);
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

bool FindLandmarkNear(const Vector& pos, float rangeM,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut,
                      int* outLandmarkIdx) {
    if (outLandmarkIdx) *outLandmarkIdx = -1;
    if (!nameOut || nameBufSize == 0) return false;
    nameOut[0] = '\0';
    if (rangeM <= 0.0f) return false;
    const float maxD2 = rangeM * rangeM;

    int   best   = -1;
    float bestD2 = maxD2;
    for (int i = 0; i < g_landmark_count; ++i) {
        if (g_landmarks[i].name[0] == '\0') continue;
        float dx = pos.x - g_landmarks[i].pos.x;
        float dy = pos.y - g_landmarks[i].pos.y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestD2) {
            bestD2 = d2;
            best   = i;
        }
    }
    if (best < 0) return false;
    std::strncpy(nameOut, g_landmarks[best].name, nameBufSize - 1);
    nameOut[nameBufSize - 1] = '\0';
    posOut = g_landmarks[best].pos;
    if (outLandmarkIdx) *outLandmarkIdx = best;
    return true;
}

bool IterateLandmarks(int& cursor,
                      char* nameOut, size_t nameBufSize,
                      Vector& posOut, int& outLandmarkIdx) {
    if (!nameOut || nameBufSize == 0) return false;
    if (cursor < 0) cursor = 0;
    while (cursor < g_landmark_count) {
        int i = cursor++;
        if (g_landmarks[i].name[0] == '\0') continue;
        std::strncpy(nameOut, g_landmarks[i].name, nameBufSize - 1);
        nameOut[nameBufSize - 1] = '\0';
        posOut         = g_landmarks[i].pos;
        outLandmarkIdx = i;
        return true;
    }
    return false;
}

void MarkLandmarkClaimedByDoor(int landmarkIdx) {
    if (landmarkIdx < 0 || landmarkIdx >= g_landmark_count) return;
    g_landmarks[landmarkIdx].doorMatched = true;
}

void Tick() {
    // Stay completely silent + hands-off while an engine movie owns the
    // foreground (intro logos, stunt cutscene movies like 03.bik at the
    // Leviathan capture). KOTOR's movie player aborts its play queue if
    // windows/focus churn during playback — a SAPI speech-worker window
    // spun up by an announce here can trigger that abort, which surfaces
    // as the game closing instead of starting the next queued movie
    // (patch-20260601-210737.log: 03.bik played, the process exited
    // before movie #2). We touch NO state and don't advance g_prev_area,
    // so the area-change branch below re-fires cleanly on the first tick
    // after the movie ends — the "Bereich: …" announce is deferred, not
    // dropped. The main loop is usually frozen during playback anyway;
    // this guards the boundary ticks where it isn't.
    if (acc::bringup_announce::IsMovieWindowForeground()) {
        return;
    }

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
        g_last_spoken_room_text.clear();
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
        // Re-acquire DirectInput now that the load is fully complete. The
        // engine recreates its render window during a load; if the player
        // pressed keys during that churn the input `active` flag can desync
        // to 1 while the keyboard is actually unacquired, and the engine's
        // own post-load SetActive(1) no-ops against the stuck flag — leaving
        // the keyboard dead until an alt-tab. We reach this edge AFTER the
        // engine's HideLoadScreen has run, so forcing a 0->1 cycle here lands
        // last and wins. Harmless on a healthy load (a sub-frame device
        // re-grab, exactly what the engine does on alt-tab). See
        // engine_input.h ForceReacquireInput for the full mechanism.
        acc::engine::ForceReacquireInput();
        SpeakArea(area);
        // Drop the unified narrated-target slot — any object from the old
        // area is now invalid. Stale pointers would survive otherwise
        // (TryGet's handle round-trip resolves through the active area's
        // game-object array, so a cross-area stale obj could in principle
        // get a false-positive validation). Explicit Clear is the safe
        // path.
        acc::narrated_target::Clear();
        // Reconcile the discovery-tier index for the new area: capture its
        // tag and clear the in-memory set. The actual read of the save var is
        // deferred (discovery::Tick) until the player creature's table has
        // settled — a read here, on the save-load tick, can race
        // CSWSObject::LoadObjectState. On a normal walk-through transition the
        // creature isn't reloaded, so this is effectively just a re-key.
        acc::discovery::OnAreaChanged(area);
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
        g_last_spoken_room_text.clear();
        g_last_spoken_pos_valid      = false;
        g_lm_prox_pending_idx        = -1;
        g_lm_prox_pending_count      = 0;
        g_lm_prox_last_spoken_idx    = -1;
        g_pending_platz_valid        = false;
        g_gated_cluster_pending      = false;
        g_gate_clear_since_ms        = 0;
        g_landmark_recheck_last_ms   = 0;
        g_flap_prev_text.clear();
        g_flap_prev_ms               = 0;
        g_flap_cur_ms                = 0;
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
    // Staleness recheck must run BEFORE the proximity scan so a note the
    // engine just enabled becomes proximity-eligible on the same tick the
    // player is already standing next to it.
    TickLandmarkCacheRecheck(area);

    TickProximityLandmarks(pos);

    // Pending Platz announce: fires whenever its delay elapses,
    // regardless of whether a new .lyt-room transition triggered this
    // tick. Lives outside the room-stability gate below so the
    // deferred speech reliably lands ~1s after entering a Platz
    // cluster.
    TickPendingPlatz(area, pos);

    // Post-combat room refire: if a cluster change was gated during the
    // fight, describe where the player actually ended up once speech is
    // allowed again.
    TickGatedClusterRefire(area, pos);

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
    std::string shapeBuf;
    int  shapeSig      = 0;
    int  clusterId     = acc::wall_topology::kClusterIdNone;
    // Per-tick probe: suppress LookupAt's WALL-FILTERED / ALL-BLOCKED
    // diagnostic logs. Speech-time call sites (SpeakRoomChange peek,
    // ResolveRoomSpeech, LogWallTopoComparison) still emit them on
    // every cluster transition, so we keep full visibility into wall-
    // filter behaviour without flooding the log with identical lines
    // at 60 fps when the player stands at a wall-filtered boundary.
    acc::wall_topology::LookupAt(area, pos,
                                 shapeBuf,
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

    // Snap-gap seam suppression. kClusterIdOpenArea is the synthetic
    // fallback LookupAt returns when the player sits beyond the snap
    // radius of every cluster node. In an area that already has labelled
    // clusters that is almost never "genuinely open" — it's a thin
    // coverage seam *between* two regions (the Kashyyyk Great Walkway has
    // a ~15 m band between cluster 4 and cluster 12 where neither node is
    // in range). Announcing the bare "Bereich" there and re-acquiring the
    // real cluster on the far side produces boundary thrash (observed:
    // cluster 4 -> -2 -> cluster 12 -> -2 over tens of seconds of pacing).
    // Hold the last real cluster across the seam by collapsing the
    // fallback to the previous id, so it reads as "still in that region."
    // A genuinely open area still announces the fallback once: there the
    // previous committed id is None/-2 (no real cluster was ever in
    // range), so this guard doesn't fire.
    if (clusterId == acc::wall_topology::kClusterIdOpenArea &&
        g_prev_cluster_id >= 0) {
        clusterId = g_prev_cluster_id;
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
            g_gated_cluster_pending = true;
            g_gate_clear_since_ms   = 0;
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

void NotifyExternalLoadStarting(const char* reason) {
    // Same latch the OnSetMoveToModuleString detour sets — arms the
    // load-pending window so per-tick / hook consumers (combat narration,
    // the menu focus hooks) stay quiet while the engine rebuilds the
    // in-game GUI on a save-game load. Cleared in Tick() on the first
    // fresh area pointer, exactly like the module-transition path.
    g_module_load_pending = true;
    acclog::Write("Transition", "external load latch armed (%s)",
                  reason ? reason : "?");
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

    // If a movie is already on screen when this fires, suppress the
    // "Lade: …" speech for the same reason Tick() goes silent during
    // playback — don't spin up a SAPI worker window that can abort the
    // engine's movie queue. The latch above is already set, so per-tick
    // probes stay gated regardless.
    if (acc::bringup_announce::IsMovieWindowForeground()) {
        acclog::Write("Transition",
            "pre-load -> '%s' (speech suppressed — movie foreground)", dest);
        return;
    }

    char speech[160] = {0};
    std::snprintf(speech, sizeof(speech),
                  acc::strings::Get(acc::strings::Id::FmtTransitionLoading),
                  dest);
    prism::Speak(speech, /*interrupt=*/false);
    acclog::Write("Transition", "pre-load -> '%s'", dest);
}

}  // namespace acc::transitions

// CServerExoApp::SetMoveToModuleString — entry hook @0x004aecd0. Fires once
// per area transition with the destination module's resref CExoString*. Pre-
// load announce path; reads the resref via the dedup-and-speak helper above.
//
// **LEA-vs-MOV bug workaround** (memory: project_kpatchmanager_lea_bug.md).
// The wrapper emits LEA (not MOV) for `source = "esp+4"` parameters, so
// `arg_addr` is the *address* of the original [esp+4] stack slot, not the
// CExoString* value at that slot. Dereference once to get the actual
// CExoString*. SEH-guarded — if the wrapper hands us a bogus address (e.g.
// stack frame mid-teardown), absorb the fault rather than crashing.
//
// `serverApp` (ECX = CServerExoApp* facade) is unused; the handler only
// needs the destination string. Kept in the signature so the parameter
// order matches the hooks.toml declaration.
extern "C" void __cdecl OnSetMoveToModuleString(void* /*serverApp*/,
                                                void* arg_addr) {
    EnsurePrismInitialized();

    void* exoStringPtr = nullptr;
    __try {
        exoStringPtr = *reinterpret_cast<void**>(arg_addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        acclog::Write("Transition", "pre-load arg deref faulted (arg_addr=%p)",
            arg_addr);
        return;
    }

    acc::transitions::AnnouncePreLoadDestination(exoStringPtr);
}
