#include "spatial_change_detector.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "audio_cue_player.h"
#include "audio_cues.h"           // NavCue
#include "core_settings.h"
#include "engine_area.h"
#include "engine_offsets.h"       // Vector
#include "engine_player.h"        // GetPlayerPosition / GetPlayerYawDegrees
#include "view_mode.h"            // GetEffectiveOrientationYawDegrees —
                                  // routes T2 cone through camera yaw
                                  // when view mode is active
#include "filter_objects.h"
#include "log.h"

namespace acc::spatial::change_detector {

namespace {

// --- Wall cache ---------------------------------------------------------
//
// Sized at 4096 — Endar Spire areas have logged 405–908 perimeter edges
// (lay-off 1 + initial Trigger 1 verification). Open-environment areas
// (Manaan, Korriban) plausibly run higher; 4096 leaves comfortable
// headroom. Overflow is logged once per area-change so we can size up
// if a real area exceeds it.

constexpr int kMaxWallEdges = 4096;
acc::engine::WallEdge g_walls[kMaxWallEdges];
int                   g_wall_count = 0;

// --- Wall surface clustering -------------------------------------------
//
// The walkmesh stores wall geometry as many short edges (Endar Spire
// corridors are ~0.7-1m per edge). Tracking per-edge distance produces
// permanent chatter as the player walks parallel to a wall: each edge's
// closest-point distance jumps when the player passes its endpoint, even
// though the *physical wall surface* hasn't changed distance to the
// player. We solve this by clustering adjacent collinear edges into
// surfaces at area-load time and tracking distance per *surface*. A
// player walking along a corridor sees the left/right surface's
// minimum distance stay roughly constant — no spurious fires.
//
// Cluster rule: two edges merge if they share an endpoint AND their
// direction vectors are within kSurfaceCollinearityCosThreshold (i.e.
// the angle between them is small enough to count as "same wall"). An
// L-junction at a corner has two adjacent edges with perpendicular
// directions; those stay in separate surfaces because the dot product
// of their direction vectors is near zero, well below the cos(15°)
// threshold. A long straight wall split into 5 short edges all collapse
// into one surface.
//
// Endpoint-sharing is detected with a small tolerance (kEndpointTolMeters)
// to absorb floating-point noise in the walkmesh extractor's world-space
// transform.

constexpr int kMaxWallSurfaces = 1024;

// cos(15°) ≈ 0.966 — direction vectors that disagree by more than 15°
// are different surfaces. Generous enough to absorb walkmesh dicing
// noise on smooth corridor walls, tight enough to keep L-junctions
// distinct (90° → cos=0, well below threshold).
constexpr float kSurfaceCollinearityCosThreshold = 0.966f;

// Endpoint coincidence tolerance — 5cm. Walkmesh world-space transforms
// can introduce sub-mm float noise; 5cm is well below any realistic
// edge spacing in KOTOR's hand-authored layouts.
constexpr float kEndpointTolMeters    = 0.05f;
constexpr float kEndpointTolSquared   = kEndpointTolMeters * kEndpointTolMeters;

// Per-tick same-closest-point dedup tolerance — 5cm. T- and X-junction
// vertices have 3+ edges meeting at a single point; each edge clusters
// into its own surface (correctly, since the edges go off in different
// directions). When the player is closest to the shared vertex, every
// one of those surfaces independently reports that vertex as its
// closest point, and our K-cap fires several near-identical cues from
// the same world location. We collapse them at candidate-collection
// time: any surface whose closest point coincides with an already-
// pending candidate within this tolerance merges into that candidate
// (keeping the smaller distance).
constexpr float kFireDedupTolMeters   = 0.05f;
constexpr float kFireDedupTolSquared  = kFireDedupTolMeters * kFireDedupTolMeters;

// Per-edge surface-id mapping. Filled by the area-load clustering pass.
// -1 = "edge not assigned" (only seen briefly during clustering or if
// kMaxWallSurfaces overflowed).
int g_edge_surface_id[kMaxWallEdges];

// Number of distinct surfaces clustered for the current area.
int g_surface_count = 0;

// Per-surface last-cued timestamp (ms via GetTickCount). T2's
// foremost-in-front debounce reads this to avoid double-firing a surface
// the per-sector T1 just announced. Stamped by T1 (the "best surface"
// in the sector that fired) and by T2 itself. 0 = never cued.
DWORD g_surface_last_cued_at[kMaxWallSurfaces];

// --- Per-sector continuous distance-delta ----------------------------
//
// Final design (2026-05-07): no discrete zones. Each cardinal sector
// (Front / Left / Back / Right, relative to player heading) tracks the
// distance at which its closest wall last fired a cue and refires
// whenever the current distance has changed by
// `distanceDeltaThresholdMeters` since then. 3D audio handles distance
// + direction nuance — a cue at 4 m plays quieter than a cue at 1 m,
// panned to the wall's bearing. The trigger only needs to know "did
// distance change enough to be worth a fresh cue." Discrete zones
// (Close/Mid/Far) added complexity without adding meaning — they were
// pure firing gates and the same gate is expressible as a single
// threshold against the last fire.
//
// State per sector:
//   - last_fired_surface: identity of the surface whose distance we
//     last announced. -1 means "sector is currently out of range" or
//     "never fired in this area." Tracking identity is critical: as the
//     player walks, the bearing to each surface changes, and the
//     "closest in this sector" can swap identity from one surface to
//     another in adjacent ticks. Without identity tracking, the swap
//     looks like a phantom multi-metre distance jump and fires
//     spuriously even though the world didn't change.
//   - last_fired_distance: the distance at which last_fired_surface was
//     announced. Used for threshold-crossing test against the *same*
//     surface only.
//   - last_closest_point: cached so an in-range → out-of-range fire
//     ("wall ended on the left") plays at the wall's last position.
//   - last_cued_at: cooldown stamp.
//
// Fire rules (per sector, applied independently):
//   - Sector stays in range with the SAME closest surface and
//     |current - last_fired| > threshold → fire.
//   - All other transitions (enter, exit, identity swap) silently
//     retrack. Reasoning:
//       * Entry: the sector just gained a wall. The next threshold
//         crossing on that wall fires naturally as the player
//         approaches. An explicit "entry ping" at 4.99 m on the edge
//         of awareness mostly announces walls the player will never
//         care about (they'll walk past at the bubble's edge).
//       * Exit: silence after the last threshold delta is itself the
//         signal — the player heard the wall fade as it receded,
//         then nothing. No need for an explicit ping.
//       * Identity swap: not a real-world event, just bookkeeping
//         (closest fragment changed). Always silent.
//   - Per-sector cooldown caps absolute refire rate at one per
//     kSectorCooldownMs even if rapid motion produces back-to-back
//     threshold crossings.
//
// Walking parallel to a corridor wall keeps the side sector's distance
// roughly constant (closest-point slides along) → no fire. Approaching
// a front wall makes Front's distance shrink linearly → fires every
// `threshold` metres of approach. Each successive fire plays at the
// wall's current closest point and is louder than the previous because
// the wall is closer.

constexpr DWORD kSectorCooldownMs = 1000;

// Hysteresis on the awareness-range boundary itself. A wall enters the
// 5 m bubble at the configured range; once in, it has to recede past
// range + this band before it counts as having left. Stops sectors
// from flapping enter/exit when a wall sits right at 5.0 m and the
// player nudges back and forth across that line.
//
// 0.3 m chosen as the smallest gap that visibly stops the flapping in
// captured logs without dragging the "wall left" event noticeably
// late. Easy to bump if it still feels twitchy.
constexpr float kAwarenessRangeHysteresisMeters = 0.3f;

constexpr int kSectorCount = 4;  // Front, Left, Back, Right (matches WallSector enum)

int    g_sector_last_fired_surface [kSectorCount];  // -1 = out of range / never fired
float  g_sector_last_fired_distance[kSectorCount];
Vector g_sector_last_closest_point [kSectorCount];
DWORD  g_sector_last_cued_at       [kSectorCount];

// --- Object state -------------------------------------------------------
//
// In-range Pillar 1 vocabulary objects (Door / Npc / Container / Item /
// Landmark / Transition). Linear-probed table keyed by engine handle.
// Sized at 256 — typical per-area object counts are in the dozens; the
// in-range subset at 5m is much smaller. Empty slot = handle == 0.

struct ObjectState {
    uint32_t handle;
    float    last_distance;
    DWORD    last_cued_at;   // 0 = never cued (shared T1/T2)
};

constexpr int kMaxTrackedObjects = 256;
ObjectState g_object_state[kMaxTrackedObjects];

// --- Trigger 2 — foremost-in-front debounce state ----------------------
//
// Three-variable pattern ported from turn_announce.cpp. The "feature
// identity" can be a wall surface (by surface index) or an object (by
// engine handle) or None (cone clear). Equality compares (kind +
// discriminator) so a wall and an object with happenstance-matching
// integers are distinguishable.

constexpr DWORD kT2QuietMs = 250;

enum class FeatureKind : int {
    None   = 0,
    Wall   = 1,
    Object = 2,
};

struct Foremost {
    FeatureKind kind;
    int         wall_surface_index;
    uint32_t    object_handle;
};

bool ForemostEqual(const Foremost& a, const Foremost& b) {
    if (a.kind != b.kind) return false;
    switch (a.kind) {
        case FeatureKind::Wall:   return a.wall_surface_index == b.wall_surface_index;
        case FeatureKind::Object: return a.object_handle      == b.object_handle;
        case FeatureKind::None:   return true;
    }
    return false;
}

const char* ForemostKindTag(FeatureKind k) {
    switch (k) {
        case FeatureKind::None:   return "none";
        case FeatureKind::Wall:   return "wall";
        case FeatureKind::Object: return "obj";
    }
    return "?";
}

Foremost g_t2_last_fired      = { FeatureKind::None, -1, 0u };
Foremost g_t2_pending         = { FeatureKind::None, -1, 0u };
DWORD    g_t2_pending_changed_at = 0;
bool     g_t2_initialised     = false;

// Timestamp of the last T1 wall fire (any sector). T2 wall fires are
// suppressed within kSectorCooldownMs of this — when T1 has just
// announced wall geometry, the player is already getting wall info this
// beat and a T2 "wall in front" cue is a duplicate even if it's a
// different surface than T1 picked. Object T2 fires are NOT gated by
// this (they carry distinct semantic content). 0 = never fired.
DWORD    g_t1_wall_last_fired_at = 0;

// --- Geometry -----------------------------------------------------------

// Closest-point-on-segment squared distance. Writes the closest point
// on the [a,b] segment to outClosest. Degenerate segment (a == b) treats
// the segment as the single point a.
float ClosestPointDistanceSquared(const Vector& p,
                                  const Vector& a,
                                  const Vector& b,
                                  Vector& outClosest) {
    Vector ab = { b.x - a.x, b.y - a.y, b.z - a.z };
    Vector ap = { p.x - a.x, p.y - a.y, p.z - a.z };
    float ab_len_sq = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    if (ab_len_sq < 1e-6f) {
        outClosest = a;
        return ap.x * ap.x + ap.y * ap.y + ap.z * ap.z;
    }
    float t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / ab_len_sq;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    outClosest = { a.x + t * ab.x, a.y + t * ab.y, a.z + t * ab.z };
    Vector diff = {
        p.x - outClosest.x, p.y - outClosest.y, p.z - outClosest.z };
    return diff.x * diff.x + diff.y * diff.y + diff.z * diff.z;
}

// Squared distance between two Vectors in XY only (clustering ignores
// vertical noise — KOTOR walkmesh edges are flat per-room and a small Z
// jitter on the seam would otherwise prevent endpoint coincidence).
float DistanceSquaredXY(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Returns true if the two edges should belong to the same wall surface:
// they share an endpoint (within kEndpointTolMeters) AND their direction
// vectors are roughly collinear (cosine within
// kSurfaceCollinearityCosThreshold). Direction is taken in XY only;
// walkmesh edges within a single room are co-planar enough that
// vertical components add noise without information.
bool EdgesAreSameSurface(const acc::engine::WallEdge& e1,
                         const acc::engine::WallEdge& e2) {
    bool share_aa = DistanceSquaredXY(e1.a, e2.a) <= kEndpointTolSquared;
    bool share_ab = DistanceSquaredXY(e1.a, e2.b) <= kEndpointTolSquared;
    bool share_ba = DistanceSquaredXY(e1.b, e2.a) <= kEndpointTolSquared;
    bool share_bb = DistanceSquaredXY(e1.b, e2.b) <= kEndpointTolSquared;
    if (!(share_aa || share_ab || share_ba || share_bb)) return false;

    float d1x = e1.b.x - e1.a.x;
    float d1y = e1.b.y - e1.a.y;
    float d2x = e2.b.x - e2.a.x;
    float d2y = e2.b.y - e2.a.y;
    float len1 = std::sqrt(d1x * d1x + d1y * d1y);
    float len2 = std::sqrt(d2x * d2x + d2y * d2y);
    if (len1 < 1e-4f || len2 < 1e-4f) return true;  // degenerate, accept
    float cos_unsigned =
        std::fabs((d1x * d2x + d1y * d2y) / (len1 * len2));
    return cos_unsigned >= kSurfaceCollinearityCosThreshold;
}

// --- Union-find over edges (small-N implementation) --------------------
//
// Iterative path compression with union-by-rank. We use it once per
// area-load to cluster edges into surfaces; runtime is O(N² α(N)) in
// the worst case (we do N² pair tests; the union/find calls are
// effectively constant). For 900 edges that's ~800k pair tests + O(N)
// union-find ops, which finishes in well under 100ms — runs once per
// zone, never on a hot path.

int g_uf_parent[kMaxWallEdges];
int g_uf_rank[kMaxWallEdges];

int UfFind(int i) {
    while (g_uf_parent[i] != i) {
        g_uf_parent[i] = g_uf_parent[g_uf_parent[i]];  // path compress
        i = g_uf_parent[i];
    }
    return i;
}

void UfUnion(int i, int j) {
    int ri = UfFind(i);
    int rj = UfFind(j);
    if (ri == rj) return;
    if (g_uf_rank[ri] < g_uf_rank[rj]) {
        g_uf_parent[ri] = rj;
    } else if (g_uf_rank[ri] > g_uf_rank[rj]) {
        g_uf_parent[rj] = ri;
    } else {
        g_uf_parent[rj] = ri;
        ++g_uf_rank[ri];
    }
}

// Cluster the cached wall edges (g_walls[0..g_wall_count-1]) into
// connected-and-collinear surfaces. Fills g_edge_surface_id[] and sets
// g_surface_count. Each unique cluster representative is assigned a
// dense surface index in [0, g_surface_count). Surfaces beyond
// kMaxWallSurfaces are coalesced into surface index 0 (degraded but
// safe; logs a warning).
void ClusterEdgesIntoSurfaces() {
    g_surface_count = 0;
    for (int i = 0; i < g_wall_count; ++i) {
        g_uf_parent[i] = i;
        g_uf_rank[i]   = 0;
        g_edge_surface_id[i] = -1;
    }
    // Cross-room merging is intentional. KOTOR's "room" is an internal
    // .lyt segmentation of an area (not a literal room), and a single
    // physical corridor wall regularly spans multiple rooms. Refusing
    // to cluster across room_id leaves long walls split into per-room
    // fragments, each firing its own threshold crossings. We let the
    // geometric tests (shared endpoint + collinearity) decide alone.
    for (int i = 0; i < g_wall_count; ++i) {
        for (int j = i + 1; j < g_wall_count; ++j) {
            if (EdgesAreSameSurface(g_walls[i], g_walls[j])) {
                UfUnion(i, j);
            }
        }
    }
    // Densify: walk roots in iteration order, assigning surface indices
    // 0..N-1.
    int root_to_surface[kMaxWallEdges];
    for (int i = 0; i < g_wall_count; ++i) root_to_surface[i] = -1;
    int overflow_collisions = 0;
    for (int i = 0; i < g_wall_count; ++i) {
        int r = UfFind(i);
        if (root_to_surface[r] < 0) {
            if (g_surface_count < kMaxWallSurfaces) {
                root_to_surface[r] = g_surface_count++;
            } else {
                root_to_surface[r] = 0;  // degrade to bucket 0
                ++overflow_collisions;
            }
        }
        g_edge_surface_id[i] = root_to_surface[r];
    }
    if (overflow_collisions > 0) {
        acclog::Write("ChangeDetector", "surface overflow — %d edges collapsed to "
            "bucket 0 (raise kMaxWallSurfaces above %d)",
            overflow_collisions, kMaxWallSurfaces);
    }
    acclog::Write("ChangeDetector", "clustered %d edges into %d surfaces",
        g_wall_count, g_surface_count);
}

// --- Object classification ---------------------------------------------

acc::audio::NavCue CategoryToNavCue(acc::filter::CycleCategory c) {
    using F = acc::filter::CycleCategory;
    using N = acc::audio::NavCue;
    switch (c) {
        case F::Door:        return N::Door;
        case F::Npc:         return N::NpcCreature;
        case F::Container:   return N::ContainerPlaceable;
        case F::Item:        return N::Item;
        case F::Landmark:    return N::Landmark;
        case F::Transition:  return N::TransitionExit;
        default:             return N::Wall;  // unreachable
    }
}

bool ClassifyObject(void* obj, acc::audio::NavCue& outCue) {
    using F = acc::filter::CycleCategory;
    for (int c = 0; c < static_cast<int>(F::Count_); ++c) {
        if (acc::filter::ObjectMatches(obj, static_cast<F>(c))) {
            outCue = CategoryToNavCue(static_cast<F>(c));
            return true;
        }
    }
    return false;
}

// --- Object state table ------------------------------------------------

ObjectState* FindOrAddObjectState(uint32_t handle, bool& outIsNew) {
    outIsNew = false;
    if (handle == 0) return nullptr;
    for (auto& s : g_object_state) {
        if (s.handle == handle) return &s;
    }
    for (auto& s : g_object_state) {
        if (s.handle == 0) {
            s.handle        = handle;
            s.last_distance = 0.0f;
            s.last_cued_at  = 0;
            outIsNew        = true;
            return &s;
        }
    }
    return nullptr;  // table full
}

ObjectState* FindObjectState(uint32_t handle) {
    if (handle == 0) return nullptr;
    for (auto& s : g_object_state) {
        if (s.handle == handle) return &s;
    }
    return nullptr;
}

void RemoveObjectState(uint32_t handle) {
    if (handle == 0) return;
    for (auto& s : g_object_state) {
        if (s.handle == handle) {
            s.handle        = 0;
            s.last_distance = 0.0f;
            s.last_cued_at  = 0;
            return;
        }
    }
}

// --- Per-tick surface scratch ------------------------------------------
//
// Per-tick best-closest-point for each surface. Reset at the start of
// every Tick. Used to find each surface's closest edge this tick before
// applying the distance-delta threshold check.

struct SurfaceScratch {
    bool   in_range;
    float  best_distance;
    Vector best_closest_point;
};

SurfaceScratch g_surface_scratch[kMaxWallSurfaces];

// Per-tick "sectors whose distance crossed the threshold this tick" —
// at most one entry per sector. Fired at the closest-point of the
// sector's closest surface (or the cached last-known point for
// out-of-range fires).
struct SectorCandidate {
    int    sector_index;
    int    best_surface_index;  // for stamping g_surface_last_cued_at (T2 reads it)
    float  distance;             // current distance (or -1 = out of range)
    float  last_fired_distance;  // for log; -1 = first fire
    Vector closest_point;
};

SectorCandidate g_sector_candidates[kSectorCount];

// --- Direction sectors for "one cue per direction" selection -----------
//
// T1 uses **world-frame** sectors as a cue-grouping mechanism: each
// sector contributes at most one fire per tick, ensuring simultaneous
// events in different physical directions don't mask each other. The
// sectors are NOT user-perceptible directions — the audio pan is
// computed from the cue's world position and the listener's
// orientation by the engine's 3D audio, so a wall to your physical
// left always pans left regardless of which world-frame bin it lives
// in. World-frame is preferred over player-relative because KOTOR's
// engine drifts the player's yaw on plain W/S/strafe motion (the
// character body slowly rotates to face movement direction even with
// no rotate-key input), and player-relative sectors would treat that
// drift as the world rotating, causing cascade enter/exit fires
// every time you take a few steps. World-frame sectors only shift
// when the player's *position* moves enough to put a wall on the
// other side of a 90° world quadrant — much rarer than yaw drift.
//
// T2's "is this in front of me?" cone is genuinely a player-relative
// question (the user wants to know what's in their *view direction*),
// so T2 uses the same ClassifyRelativeBearing helper but feeds it
// `worldBearing - effectiveYaw` to get player-frame Front membership.
// Two callers, two frames; one classifier.
//
// Sector bins (numerically identical, just interpreted differently
// per caller):
//   Bin 0 (Q0):  bearing in [-45°, +45°)     ≈ around world-east  / player-front
//   Bin 1 (Q1):  bearing in [+45°, +135°)    ≈ around world-north / player-left
//   Bin 2 (Q2):  bearing in [+135°, +225°)   ≈ around world-west  / player-back
//   Bin 3 (Q3):  bearing in [+225°, +315°)   ≈ around world-south / player-right
//
// Bearing convention: world frame, 0° = +X = east, CCW positive
// (matches engine_player::GetPlayerYawDegrees). Player-relative
// bearing = world bearing - yaw.
//
// Enum names retained as Front/Left/Back/Right to keep T2's "Front
// cone" code readable (T2 *does* use them as player-frame). For T1
// they're abstract bin indices; the SectorTag helper labels them
// E/N/W/S to make T1 logs unambiguously world-frame.
enum class WallSector : int {
    Front = 0,
    Left  = 1,
    Back  = 2,
    Right = 3,
    Count_ = 4,
};

WallSector ClassifyRelativeBearing(float relBearingDeg) {
    while (relBearingDeg <    0.0f) relBearingDeg += 360.0f;
    while (relBearingDeg >= 360.0f) relBearingDeg -= 360.0f;
    if (relBearingDeg <  45.0f) return WallSector::Front;
    if (relBearingDeg < 135.0f) return WallSector::Left;
    if (relBearingDeg < 225.0f) return WallSector::Back;
    if (relBearingDeg < 315.0f) return WallSector::Right;
    return WallSector::Front;
}

// Tag used in T1 logs — world-frame quadrant labels. The enum slots
// happen to be named Front/Left/Back/Right but for T1's use those names
// are misleading; this gives the log unambiguous E/N/W/S labels.
const char* SectorTag(WallSector s) {
    switch (s) {
        case WallSector::Front: return "E";  // world-east bin
        case WallSector::Left:  return "N";
        case WallSector::Back:  return "W";
        case WallSector::Right: return "S";
        default:                return "?";
    }
}

// --- Area tracking ------------------------------------------------------

void* g_prev_area = nullptr;

// Reference-frame tracking for view-mode transitions. T1 / T2 measure
// distances from the *user's effective position* — the player creature
// when walking normally, and the virtual cursor when view mode is
// active (matches the listener-override hook in audio_bus.cpp:
// 3D audio attenuates from the cursor in view mode, so the cue gates
// should too). When the reference swaps between the two, every
// in-range feature's `last_distance` is anchored against the OLD
// reference; without re-seeding, the next tick would observe a delta
// equal to the player↔cursor separation and fire a wall-of-sound. We
// edge-detect the swap and run CalibrateInRange against the new
// reference before the change-detection body sees it.
bool g_was_using_cursor = false;

void OnAreaChange(void* area) {
    if (!area) {
        g_wall_count    = 0;
        g_surface_count = 0;
        return;
    }
    g_wall_count = acc::engine::BuildAreaWallCache(area, g_walls, kMaxWallEdges);
    int totalDiscovered = acc::engine::BuildAreaWallCache(area, nullptr, 0);
    bool overflow = (totalDiscovered > kMaxWallEdges);

    // Cluster edges into wall surfaces. After this, g_edge_surface_id[i]
    // gives the surface index for edge i, and g_surface_count is the
    // total number of distinct surfaces.
    ClusterEdgesIntoSurfaces();

    for (int i = 0; i < kMaxWallSurfaces; ++i) {
        g_surface_last_cued_at[i] = 0;
    }
    for (int i = 0; i < kSectorCount; ++i) {
        g_sector_last_fired_surface[i]  = -1;
        g_sector_last_fired_distance[i] = -1.0f;
        g_sector_last_closest_point[i]  = { 0.0f, 0.0f, 0.0f };
        g_sector_last_cued_at[i]        = 0;
    }
    for (int i = 0; i < kMaxTrackedObjects; ++i) {
        g_object_state[i].handle        = 0;
        g_object_state[i].last_distance = 0.0f;
        g_object_state[i].last_cued_at  = 0;
    }

    // T2 first-tick suppression: clear identity state. The first
    // post-CalibrateInRange tick will seed g_t2_last_fired without
    // firing.
    g_t2_last_fired         = { FeatureKind::None, -1, 0u };
    g_t2_pending            = { FeatureKind::None, -1, 0u };
    g_t2_pending_changed_at = 0;
    g_t2_initialised        = false;
    g_t1_wall_last_fired_at = 0;

    if (overflow) {
        acclog::Write("ChangeDetector", "area change — walls cached=%d/%d (OVERFLOW; "
            "increase kMaxWallEdges) areaPtr=%p",
            g_wall_count, totalDiscovered, area);
    } else {
        acclog::Write("ChangeDetector", "area change — walls cached=%d areaPtr=%p",
            g_wall_count, area);
    }
}

// Calibration scan — runs once per area-change after OnAreaChange has
// rebuilt the cache. Fills `last_distance` for every in-range wall and
// seeds an object-state entry for every in-range Pillar 1 vocabulary
// object, WITHOUT firing any cues. Eliminates the wall-of-sound the
// user experienced on every save-load (when 23+ in-range walls all
// fired their first-observation entry cue on the same frame).
//
// Also reused on view-mode enter/exit transitions: the reference
// position swaps between the player creature and the virtual cursor,
// and the per-feature deltas need to re-anchor against the new
// reference or every wall would appear to "jump" by metres on the
// transition tick and fire a flood of T1 cues.
//
// After calibration, the next normal Tick() fires only on actual
// reference motion past the threshold (or on features that newly
// enter the awareness bubble during play).
void CalibrateInRange(void* area, const Vector& referencePos,
                      const char* reason) {
    if (!area) return;
    const auto& settings = acc::core::Get().pillar1;
    float range   = settings.awarenessRangeMeters;
    float rangeSq = range * range;

    // Reset per-sector state, then walk every edge to derive the
    // per-sector minimum distance and closest point. Then seed each
    // sector's zone from that distance — first observation uses
    // prev=Open (no hysteresis carry), so subsequent Tick() calls fire
    // only on actual zone transitions caused by player motion.
    //
    // Per-sector aggregation is world-frame, so no yaw read needed
    // here (Tick() uses the same convention).

    struct SeedAggregate { bool has; float dist; Vector pos; int surface_index; };
    SeedAggregate seed[kSectorCount] = {};
    for (int i = 0; i < kSectorCount; ++i) {
        seed[i] = { false, 1e30f, { 0, 0, 0 }, -1 };
    }

    // Per-surface min distance + closest point (transient — we only
    // need it to drive the per-sector aggregation below).
    float  surfBestDist[kMaxWallSurfaces];
    Vector surfBestPt[kMaxWallSurfaces];
    bool   surfHasData[kMaxWallSurfaces];
    for (int i = 0; i < g_surface_count; ++i) {
        surfBestDist[i] = 1e30f;
        surfHasData[i]  = false;
    }
    for (int i = 0; i < g_wall_count; ++i) {
        const auto& w = g_walls[i];
        Vector closest;
        float distSq = ClosestPointDistanceSquared(
            referencePos, w.a, w.b, closest);
        if (distSq > rangeSq) continue;
        int sid = g_edge_surface_id[i];
        if (sid < 0 || sid >= g_surface_count) continue;
        float dist = std::sqrt(distSq);
        if (!surfHasData[sid] || dist < surfBestDist[sid]) {
            surfHasData[sid] = true;
            surfBestDist[sid] = dist;
            surfBestPt[sid]   = closest;
        }
    }

    // Aggregate surfaces into world-frame sectors. (Tick uses world-
    // frame too — keeps calibration aligned with runtime semantics so
    // the first tick after calibration doesn't see phantom transitions
    // due to a frame mismatch.)
    for (int s = 0; s < g_surface_count; ++s) {
        if (!surfHasData[s]) continue;
        float dx = surfBestPt[s].x - referencePos.x;
        float dy = surfBestPt[s].y - referencePos.y;
        float worldBearing = std::atan2(dy, dx) * 57.29577951308232f;
        WallSector sec = ClassifyRelativeBearing(worldBearing);
        int idx = static_cast<int>(sec);
        if (!seed[idx].has || surfBestDist[s] < seed[idx].dist) {
            seed[idx].has           = true;
            seed[idx].dist          = surfBestDist[s];
            seed[idx].pos           = surfBestPt[s];
            seed[idx].surface_index = s;
        }
    }

    int wallsCalibrated = 0;
    for (int i = 0; i < kSectorCount; ++i) {
        if (seed[i].has) {
            g_sector_last_fired_surface[i]  = seed[i].surface_index;
            g_sector_last_fired_distance[i] = seed[i].dist;
            g_sector_last_closest_point[i]  = seed[i].pos;
            ++wallsCalibrated;
        } else {
            g_sector_last_fired_surface[i]  = -1;
            g_sector_last_fired_distance[i] = -1.0f;
        }
    }

    int objectsCalibrated = 0;
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        acc::audio::NavCue cue;
        if (!ClassifyObject(obj, cue)) continue;
        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;
        float dx = pos.x - referencePos.x;
        float dy = pos.y - referencePos.y;
        float dz = pos.z - referencePos.z;
        float distSq = dx * dx + dy * dy + dz * dz;
        if (distSq > rangeSq) continue;
        uint32_t handle = acc::engine::GetObjectHandle(obj);
        if (handle == 0) continue;
        bool isNew = false;
        ObjectState* s = FindOrAddObjectState(handle, isNew);
        if (!s) continue;
        s->last_distance = std::sqrt(distSq);
        ++objectsCalibrated;
    }

    acclog::Write("ChangeDetector", "calibrated (%s) sectors_with_walls=%d/%d "
        "(F=%.2f/s%d L=%.2f/s%d B=%.2f/s%d R=%.2f/s%d) objects=%d "
        "at ref=(%.2f,%.2f,%.2f)",
        reason ? reason : "?", wallsCalibrated, kSectorCount,
        g_sector_last_fired_distance[0], g_sector_last_fired_surface[0],
        g_sector_last_fired_distance[1], g_sector_last_fired_surface[1],
        g_sector_last_fired_distance[2], g_sector_last_fired_surface[2],
        g_sector_last_fired_distance[3], g_sector_last_fired_surface[3],
        objectsCalibrated,
        referencePos.x, referencePos.y, referencePos.z);
}

}  // namespace

void Tick() {
    Vector playerPos;
    if (!acc::engine::GetPlayerPosition(playerPos)) {
        // Player gone (menu / chargen / pre-spawn). Drop area tracking
        // so re-load triggers a fresh OnAreaChange.
        g_prev_area = nullptr;
        return;
    }

    void* area = acc::engine::GetCurrentArea();
    if (!area) return;

    // Pick the reference position. View mode swaps in the virtual
    // cursor as the user's effective listener, so T1 distance-deltas
    // and T2 cone tests track cursor motion (W/S translates the cursor
    // along camera yaw) — without this, the player creature is frozen
    // by view-mode's SetPlayerInputEnabled(false) and T1 never fires.
    Vector referencePos = playerPos;
    bool   usingCursor  = false;
    if (acc::view_mode::IsActive()) {
        Vector cursor;
        if (acc::view_mode::TryGetCursorPosition(cursor)) {
            referencePos = cursor;
            usingCursor  = true;
        }
    }

    if (area != g_prev_area) {
        OnAreaChange(area);
        CalibrateInRange(area, referencePos, "area-change");
        g_was_using_cursor = usingCursor;
        g_prev_area = area;
        // Calibration completed silently — last_distance now reflects
        // the as-loaded position for every in-range feature, so the
        // first cue we fire on the next tick will be a real motion
        // event, not the area-load entry flood.
        return;
    }

    // Reference-frame swap (view-mode enter/exit). Re-seed last_distance
    // against the new reference so the body's distance-delta test
    // doesn't observe the player↔cursor separation as a synthetic
    // motion event. Mirrors the area-change calibration shape.
    if (usingCursor != g_was_using_cursor) {
        CalibrateInRange(area, referencePos,
                         usingCursor ? "view-mode-enter"
                                     : "view-mode-exit");
        g_was_using_cursor = usingCursor;
        return;
    }

    const auto& settings = acc::core::Get().pillar1;
    if (!settings.trigger1DistanceDelta && !settings.trigger2FrontCone) return;

    float range       = settings.awarenessRangeMeters;
    float threshold   = settings.distanceDeltaThresholdMeters;
    float rangeSq     = range * range;
    // Outer hysteresis ring — surfaces between range and rangeExit are
    // tracked-but-only-keep-existing-sectors-in-range. Walls in this
    // band can't initiate a new "sector entered awareness" cue but can
    // delay the corresponding exit cue.
    float rangeExit   = range + kAwarenessRangeHysteresisMeters;
    float rangeExitSq = rangeExit * rangeExit;
    int   maxCues     = settings.trigger1MaxWallCuesPerTick;
    if (maxCues < 0) maxCues = 0;

    DWORD now = GetTickCount();

    // Hoisted yaw — used for both T1 wall-sector binning and T2 Front-cone
    // candidate detection. One read per tick. Camera yaw when view mode
    // is active (T2 tracks where the user is looking as they pan A/D);
    // player yaw otherwise. T1 distance-delta now also follows the
    // cursor in view mode (see referencePos above), so its sector
    // binning riding the same camera-yaw is the right pairing.
    float effectiveYaw = 0.0f;
    if (!acc::view_mode::GetEffectiveOrientationYawDegrees(effectiveYaw)) {
        effectiveYaw = 0.0f;
    }

    int walls_in_range     = 0;      // surfaces in range (not edges)
    int sector_candidates  = 0;
    int walls_cued         = 0;
    int objs_in_range      = 0;
    int objs_cued          = 0;
    char sector_log[16]    = {0};    // F/L/B/R per fired sector

    // T2 foremost-in-front candidate (best across walls + objects).
    // Tracked inline during the T1 passes — every in-range surface is
    // a T2 candidate, regardless of whether its distance crossed the
    // T1 threshold this tick.
    Foremost           t2_best        = { FeatureKind::None, -1, 0u };
    float              t2_best_dist   = 1e30f;
    Vector             t2_best_pos    = { 0.0f, 0.0f, 0.0f };
    acc::audio::NavCue t2_best_cue    = acc::audio::NavCue::Wall;
    const bool         t2_enabled     = settings.trigger2FrontCone;

    // --- Walls: pass 1 — fold edge distances into per-surface minimum -
    //
    // Each physical wall is one or more collinear-adjacent edges that
    // were clustered into a single "surface" at area-load. Per tick we
    // walk all edges, compute each edge's closest-point distance to the
    // reference position, and feed it into its surface's per-tick
    // minimum. Walking parallel to a corridor wall keeps each surface's
    // minimum stable across ticks because the next edge along is
    // already at the same perpendicular distance — no spurious deltas
    // from edge endpoints jumping past the player.
    for (int i = 0; i < g_surface_count; ++i) {
        g_surface_scratch[i].in_range      = false;
        g_surface_scratch[i].best_distance = 1e30f;
    }
    for (int i = 0; i < g_wall_count; ++i) {
        const auto& w = g_walls[i];
        Vector closest;
        float distSq = ClosestPointDistanceSquared(
            referencePos, w.a, w.b, closest);
        // rangeExitSq, not rangeSq — surfaces in the hysteresis band still
        // need to flow through aggregation so a previously-in-range
        // sector keeps its tracking surface visible (Pass 3 decides
        // whether the sector still "counts" as in range based on prior
        // state).
        if (distSq > rangeExitSq) continue;
        int sid = g_edge_surface_id[i];
        if (sid < 0 || sid >= g_surface_count) continue;
        float dist = std::sqrt(distSq);
        SurfaceScratch& ss = g_surface_scratch[sid];
        if (!ss.in_range || dist < ss.best_distance) {
            ss.in_range           = true;
            ss.best_distance      = dist;
            ss.best_closest_point = closest;
        }
    }

    // --- Walls: pass 2 — surface → sector aggregation + T2 foremost ----
    //
    // T1 sector aggregation uses **world-frame** quadrants — surfaces
    // don't migrate sectors when the player's yaw drifts (a recurring
    // false-fire source). T2's Front cone is genuinely player-frame
    // (the user wants to know what's in their *view direction*), so we
    // compute both bearings per surface.
    struct SectorAggregate {
        bool   has_data;
        float  best_distance;
        int    best_surface_index;
        Vector best_closest_point;
    };
    SectorAggregate sectorAgg[kSectorCount];
    for (int i = 0; i < kSectorCount; ++i) {
        sectorAgg[i] = { false, 1e30f, -1, { 0, 0, 0 } };
    }

    for (int s = 0; s < g_surface_count; ++s) {
        SurfaceScratch& ss = g_surface_scratch[s];
        if (!ss.in_range) continue;
        ++walls_in_range;

        float dx = ss.best_closest_point.x - referencePos.x;
        float dy = ss.best_closest_point.y - referencePos.y;
        float worldBearing = std::atan2(dy, dx) * 57.29577951308232f;

        WallSector worldSec  = ClassifyRelativeBearing(worldBearing);
        WallSector playerSec = ClassifyRelativeBearing(worldBearing - effectiveYaw);

        // T2 candidate — player-relative Front cone (in front of the
        // user's view direction, not world-east).
        if (t2_enabled && playerSec == WallSector::Front &&
                ss.best_distance < t2_best_dist) {
            t2_best_dist                = ss.best_distance;
            t2_best.kind                = FeatureKind::Wall;
            t2_best.wall_surface_index  = s;
            t2_best.object_handle       = 0;
            t2_best_pos                 = ss.best_closest_point;
            t2_best_cue                 = acc::audio::NavCue::Wall;
        }

        // T1 per-sector aggregation — world-frame to immunise against
        // yaw drift.
        int idx = static_cast<int>(worldSec);
        if (idx < 0 || idx >= kSectorCount) continue;
        if (!sectorAgg[idx].has_data ||
            ss.best_distance < sectorAgg[idx].best_distance) {
            sectorAgg[idx].has_data           = true;
            sectorAgg[idx].best_distance      = ss.best_distance;
            sectorAgg[idx].best_surface_index = s;
            sectorAgg[idx].best_closest_point = ss.best_closest_point;
        }
    }

    // --- Walls: pass 3 — per-sector distance-delta check + queue fires
    //
    // For each cardinal sector:
    //   - If the sector entered range from out-of-range → fire (new wall
    //     came within awareness on this side).
    //   - If the sector left range from in-range → fire at the cached
    //     last-known position ("wall ended on the left" event).
    //   - If still in range and |current - last_fired| > threshold → fire
    //     (meaningful distance change since the previous announcement).
    //   - Per-sector cooldown caps refire rate to one per kSectorCooldownMs.
    //
    // Approaching a wall produces evenly-spaced "approach pings," each
    // played at the wall's actual current closest-point position so 3D
    // attenuation makes them louder as the wall draws closer.
    if (settings.trigger1DistanceDelta) {
        for (int sec = 0; sec < kSectorCount; ++sec) {
            int   lastSurf   = g_sector_last_fired_surface[sec];
            float lastDist   = g_sector_last_fired_distance[sec];

            // Hysteresis on the awareness boundary: a sector that was
            // out of range only enters when distance ≤ range; a sector
            // already in range stays in range until distance > rangeExit.
            // Stops back-and-forth flapping when a wall sits right at the
            // 5 m line.
            bool wasInRange = (lastSurf >= 0);
            float effectiveRange = wasInRange ? rangeExit : range;
            bool  inRange    = sectorAgg[sec].has_data &&
                               sectorAgg[sec].best_distance <= effectiveRange;
            float curDist    = inRange ? sectorAgg[sec].best_distance      : -1.0f;
            int   curSurf    = inRange ? sectorAgg[sec].best_surface_index : -1;

            // Fire only on threshold crossings of the SAME tracked
            // surface in this sector. Entry / exit / identity-swap are
            // all silent retracks (state updates, no audio).
            bool sameSurface = inRange && curSurf == lastSurf && lastSurf >= 0;
            bool fire = sameSurface &&
                        std::fabs(curDist - lastDist) > threshold;

            if (!fire) {
                // Silent retrack — update tracking state so the next
                // tick's compare is correct. Three sub-cases:
                //   - entry:        lastSurf < 0, inRange → adopt curSurf at curDist
                //   - exit:         !inRange, lastSurf ≥ 0 → clear to -1
                //   - identity swap: inRange, curSurf ≠ lastSurf → adopt new surface
                //   - same surface, sub-threshold motion → leave state unchanged
                //     so accumulated drift can eventually cross threshold
                if (inRange && (lastSurf < 0 || curSurf != lastSurf)) {
                    g_sector_last_fired_surface[sec]  = curSurf;
                    g_sector_last_fired_distance[sec] = curDist;
                    g_sector_last_closest_point[sec]  = sectorAgg[sec].best_closest_point;
                } else if (!inRange && lastSurf >= 0) {
                    g_sector_last_fired_surface[sec]  = -1;
                    g_sector_last_fired_distance[sec] = -1.0f;
                }
                continue;
            }

            // Cooldown gate — suppress the fire. We do NOT update
            // last_fired_distance here: pinning it at the truly
            // last-fired value means a future threshold compare measures
            // motion since the last *audible* announcement, not since
            // the last suppressed sample. Otherwise an approach during
            // cooldown silently advances the baseline, and a retreat
            // back to the original distance fires (perceptually
            // backwards: player heard a "wall further" cue after
            // walking towards the wall).
            DWORD lastCued = g_sector_last_cued_at[sec];
            if (lastCued != 0 && now - lastCued < kSectorCooldownMs) {
                continue;
            }

            // Cue plays at the current closest fragment. (Fires only
            // happen on same-surface threshold crossings now, so
            // inRange is always true here.)
            if (sector_candidates < kSectorCount) {
                g_sector_candidates[sector_candidates].sector_index        = sec;
                g_sector_candidates[sector_candidates].best_surface_index  = curSurf;
                g_sector_candidates[sector_candidates].distance            = curDist;
                g_sector_candidates[sector_candidates].last_fired_distance = lastDist;
                g_sector_candidates[sector_candidates].closest_point       = sectorAgg[sec].best_closest_point;
                ++sector_candidates;
            }

            // Commit new state — same surface advances to current dist.
            g_sector_last_fired_distance[sec] = curDist;
            g_sector_last_closest_point[sec]  = sectorAgg[sec].best_closest_point;
        }
    }

    // --- Walls: pass 4 — fire K closest sector candidates -------------
    //
    // At most kSectorCount=4 candidates per tick. K=maxCues caps further
    // (default 3). Sorting by ascending distance ensures that when
    // multiple sectors transition the same tick (e.g. player enters a
    // small room and Front + Left + Right all enter Close together),
    // we prioritise the closer ones if K runs out.
    if (sector_candidates > 0) {
        int firedCount = sector_candidates < maxCues ? sector_candidates
                                                     : maxCues;
        for (int i = 0; i < firedCount; ++i) {
            int minIdx = i;
            for (int j = i + 1; j < sector_candidates; ++j) {
                if (g_sector_candidates[j].distance <
                    g_sector_candidates[minIdx].distance) {
                    minIdx = j;
                }
            }
            if (minIdx != i) {
                SectorCandidate tmp = g_sector_candidates[i];
                g_sector_candidates[i]      = g_sector_candidates[minIdx];
                g_sector_candidates[minIdx] = tmp;
            }
        }

        int logIdx = 0;
        for (int k = 0; k < firedCount; ++k) {
            const auto& c = g_sector_candidates[k];
            WallSector s = static_cast<WallSector>(c.sector_index);
            if (logIdx < static_cast<int>(sizeof(sector_log)) - 1) {
                sector_log[logIdx++] = SectorTag(s)[0];
            }
            if (acc::audio::PlayCueAtPosition(
                    acc::audio::NavCue::Wall, c.closest_point,
                    referencePos, range)) {
                ++walls_cued;
                g_sector_last_cued_at[c.sector_index] = now;
                g_t1_wall_last_fired_at = now;
                if (c.best_surface_index >= 0 &&
                    c.best_surface_index < kMaxWallSurfaces) {
                    g_surface_last_cued_at[c.best_surface_index] = now;
                }
            }
            acclog::Write("ChangeDetector", "T1 sector=%s dist=%.2f "
                "(was %.2f, dt=%+.2f) surface=%d",
                SectorTag(s), c.distance, c.last_fired_distance,
                c.distance - c.last_fired_distance,
                c.best_surface_index);
        }
        sector_log[logIdx] = '\0';
    }

    // --- Objects --------------------------------------------------------
    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        acc::audio::NavCue cue;
        if (!ClassifyObject(obj, cue)) continue;
        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

        float dx = pos.x - referencePos.x;
        float dy = pos.y - referencePos.y;
        float dz = pos.z - referencePos.z;
        float distSq = dx * dx + dy * dy + dz * dz;

        uint32_t handle = acc::engine::GetObjectHandle(obj);

        // Hysteresis on the awareness-range boundary, mirroring the
        // wall sector path. Previously-tracked objects use the wider
        // rangeExit; untracked objects must enter the inner range.
        // Stops doors / NPCs sitting near the 5 m line from flapping
        // in / out and re-firing every entry.
        ObjectState* existing =
            (handle != 0) ? FindObjectState(handle) : nullptr;
        float effRangeSq = (existing != nullptr) ? rangeExitSq : rangeSq;

        if (distSq > effRangeSq) {
            if (handle != 0) RemoveObjectState(handle);
            continue;
        }
        ++objs_in_range;
        if (handle == 0) continue;
        float dist = std::sqrt(distSq);

        // T2 candidate detection — Front-sector + closer than current
        // best wins. atan2 with horizontal delta only (Z ignored to
        // match the cycle/passive-narrate horizontal convention).
        if (t2_enabled) {
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            if (ClassifyRelativeBearing(wb - effectiveYaw) ==
                    WallSector::Front &&
                dist < t2_best_dist) {
                t2_best_dist                = dist;
                t2_best.kind                = FeatureKind::Object;
                t2_best.wall_surface_index  = -1;
                t2_best.object_handle       = handle;
                t2_best_pos                 = pos;
                t2_best_cue                 = cue;
            }
        }

        if (!settings.trigger1DistanceDelta) continue;

        bool isNew = false;
        ObjectState* s = existing;
        if (!s) s = FindOrAddObjectState(handle, isNew);
        if (!s) continue;

        // First observation in range: silent retrack. The next
        // threshold crossing on approach fires naturally and is
        // distance-correct via 3D audio. Mirrors the wall-sector
        // silent-retrack-on-entry behaviour and removes the entry
        // ping cluster after view rotation / area load.
        if (isNew) {
            s->last_distance = dist;
            continue;
        }

        if (std::fabs(dist - s->last_distance) <= threshold) continue;

        // Per-object cooldown — same kSectorCooldownMs (1 s) as the
        // wall pipeline. Pin last_distance on the suppressed sample
        // so the next threshold check measures motion since the
        // last *audible* fire (matches walls; otherwise an approach
        // during cooldown silently advances the baseline and a
        // retreat to the original distance fires perceptually
        // backwards).
        if (s->last_cued_at != 0 &&
            now - s->last_cued_at < kSectorCooldownMs) {
            continue;
        }

        if (acc::audio::PlayCueAtPosition(cue, pos, referencePos, range)) {
            ++objs_cued;
            s->last_cued_at = now;
        }
        s->last_distance = dist;
    }

    // --- Trigger 2: foremost-in-front debounce + fire ------------------
    //
    // First-tick suppression: on the first tick after area-change (or
    // first tick since DLL load), seed g_t2_last_fired = current
    // foremost without firing. Mirrors turn_announce's "first
    // observation since DLL load" handling.
    bool t2_fired = false;
    if (t2_enabled) {
        if (!g_t2_initialised) {
            g_t2_last_fired         = t2_best;
            g_t2_pending            = t2_best;
            g_t2_pending_changed_at = now;
            g_t2_initialised        = true;
            acclog::Write("ChangeDetector", "T2 first-tick suppress; foremost=%s",
                ForemostKindTag(t2_best.kind));
        } else {
            // Track most-recent-observed foremost + when it last
            // changed. While the player is mid-rotation across multiple
            // features, this fires every tick and keeps changed_at
            // moving — no announcement until the rotation settles.
            if (!ForemostEqual(t2_best, g_t2_pending)) {
                g_t2_pending            = t2_best;
                g_t2_pending_changed_at = now;
            }

            if (!ForemostEqual(g_t2_pending, g_t2_last_fired) &&
                now - g_t2_pending_changed_at >= kT2QuietMs) {
                // Settled on a new foremost. Three exit paths:
                //   1. None  → cone-clear silence (record, don't fire).
                //   2. Wall  → fire only on `none → wall` AND no recent
                //              T1 wall fire (kSectorCooldownMs).
                //   3. Object → check ObjectState.last_cued_at.
                bool fire = false;
                if (g_t2_pending.kind == FeatureKind::Wall &&
                    g_t2_pending.wall_surface_index >= 0 &&
                    g_t2_pending.wall_surface_index < g_surface_count) {
                    // (C) Only announce walls when the cone transitions
                    // from clear/object → wall. `wall → wall` (foremost
                    // wall identity changed because the player panned
                    // or walked) is mostly noise: T1 already announces
                    // per-direction wall distance changes, so the
                    // player isn't missing wall info on those beats.
                    bool coneEnteredWall =
                        g_t2_last_fired.kind != FeatureKind::Wall;
                    // (B) Suppress within the T1 wall cooldown window.
                    // When T1 just announced any wall in any sector,
                    // adding a T2 "wall in front" cue is a duplicate
                    // signal even if it's a different surface — the
                    // player is already being told about wall geometry
                    // this beat.
                    bool t1Quiet =
                        g_t1_wall_last_fired_at == 0 ||
                        now - g_t1_wall_last_fired_at >= kSectorCooldownMs;
                    DWORD lastAt = g_surface_last_cued_at[
                        g_t2_pending.wall_surface_index];
                    bool surfaceQuiet =
                        lastAt == 0 || now - lastAt > kT2QuietMs;
                    if (coneEnteredWall && t1Quiet && surfaceQuiet) {
                        fire = true;
                    } else {
                        // Diagnostic: the foremost wall settled on a
                        // new value but a gate suppressed the cue.
                        // Lets us tell "no T2 wall fires because the
                        // cone never sees one" (correct silence) from
                        // "T2 wanted to fire but was gated" (also
                        // correct, just useful to see). Only logs on
                        // actual transition events, not every tick.
                        acclog::Write("ChangeDetector", "T2 wall blocked "
                            "coneEnteredWall=%d t1Quiet=%d "
                            "surfaceQuiet=%d (%s -> wall) surface=%d "
                            "dist=%.2f",
                            coneEnteredWall ? 1 : 0,
                            t1Quiet ? 1 : 0,
                            surfaceQuiet ? 1 : 0,
                            ForemostKindTag(g_t2_last_fired.kind),
                            g_t2_pending.wall_surface_index,
                            t2_best_dist);
                    }
                } else if (g_t2_pending.kind == FeatureKind::Object) {
                    ObjectState* s = FindObjectState(
                        g_t2_pending.object_handle);
                    DWORD lastAt = s ? s->last_cued_at : 0;
                    if (lastAt == 0 || now - lastAt > kT2QuietMs) fire = true;
                }
                // None → fall through with fire=false (record only).

                if (fire) {
                    bool ok = acc::audio::PlayCueAtPosition(
                        t2_best_cue, t2_best_pos, referencePos, range);
                    if (ok) {
                        t2_fired = true;
                        if (g_t2_pending.kind == FeatureKind::Wall) {
                            g_surface_last_cued_at[
                                g_t2_pending.wall_surface_index] = now;
                        } else if (g_t2_pending.kind == FeatureKind::Object) {
                            ObjectState* s = FindObjectState(
                                g_t2_pending.object_handle);
                            if (s) s->last_cued_at = now;
                        }
                    }
                    acclog::Write("ChangeDetector", "T2 fire kind=%s dist=%.2f "
                        "played=%d (%s -> %s)",
                        ForemostKindTag(g_t2_pending.kind),
                        t2_best_dist, ok ? 1 : 0,
                        ForemostKindTag(g_t2_last_fired.kind),
                        ForemostKindTag(g_t2_pending.kind));
                }
                g_t2_last_fired = g_t2_pending;
            }
        }
    }

    // Tick summary — emitted only when something actually fired this
    // tick. Wall, object, and T2 summaries are split so the per-sector
    // diagnostic only shows when walls actually fired (drops the
    // confusing "sectors=-" placeholder for object-only / T2-only ticks).
    if (walls_cued > 0) {
        acclog::Write("ChangeDetector", "tick surfaces_in_range=%d sector_candidates=%d "
            "walls_cued=%d sectors=%s objs_in_range=%d objs_cued=%d "
            "t2_fired=%d",
            walls_in_range, sector_candidates,
            walls_cued, sector_log,
            objs_in_range, objs_cued, t2_fired ? 1 : 0);
    } else if (objs_cued > 0 || t2_fired) {
        acclog::Write("ChangeDetector", "tick surfaces_in_range=%d sector_candidates=%d "
            "objs_in_range=%d objs_cued=%d t2_fired=%d",
            walls_in_range, sector_candidates,
            objs_in_range, objs_cued, t2_fired ? 1 : 0);
    }
}

bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount) {
    if (g_wall_count <= 0) return false;
    outBuf   = g_walls;
    outCount = g_wall_count;
    return true;
}

}  // namespace acc::spatial::change_detector
