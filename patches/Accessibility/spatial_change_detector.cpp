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

// Per-surface last-cued distance. Negative sentinel = "surface is
// currently out of range" (or "we haven't observed it in range yet this
// area"). On area change CalibrateInRange seeds this for every in-range
// surface without firing cues, so the next normal tick fires only on
// actual player motion past the threshold.
float g_surface_last_distance[kMaxWallSurfaces];

// Per-surface last-cued timestamp (ms via GetTickCount). Shared between
// T1 and T2: T1 stamps when it fires a surface; T2 reads to gate its
// own fire (and stamps too). 0 = never cued. On area change all entries
// reset to 0 so the first T2 candidate post-load is allowed to fire
// after the T2 first-tick suppression is cleared.
DWORD g_surface_last_cued_at[kMaxWallSurfaces];

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
        acclog::Write(
            "ChangeDetector: surface overflow — %d edges collapsed to "
            "bucket 0 (raise kMaxWallSurfaces above %d)",
            overflow_collisions, kMaxWallSurfaces);
    }
    acclog::Write(
        "ChangeDetector: clustered %d edges into %d surfaces",
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

// Per-tick "surfaces that crossed the threshold this tick" — used to
// pick the K closest for actual cue firing. Sized to surface count cap.
struct SurfaceCandidate {
    int    surface_index;
    float  distance;
    Vector closest_point;
};

SurfaceCandidate g_surface_candidates[kMaxWallSurfaces];

// --- Direction sectors for "one cue per direction" selection -----------
//
// The plan locks Trigger 1 as 360° awareness, but the user-visible cue
// budget is K=3 per tick (settings: trigger1MaxWallCuesPerTick). With
// raw "K closest by distance" selection in dense areas, all K picks
// can land on fragments of the *same* physical wall (the closest one),
// leaving the user with three near-identical pans on one ear.
//
// We instead bin candidates into four cardinal sectors *relative to
// player heading* — front, left, back, right — and pick the closest
// candidate in each sector. Each sector contributes at most one cue,
// so a corridor wall fragmented into N pieces on the right collapses
// to one "right" cue regardless of N. The K-cap then picks the K
// closest among the per-sector reps; in a 4-walls-around-you room
// the furthest sector (typically "back") is the one silenced first.
//
// Sectors are defined relative to player heading:
//   Front: rel bearing in [-45°, +45°)  i.e. [315°, 360°) ∪ [0°, 45°)
//   Left : rel bearing in [+45°, +135°)
//   Back : rel bearing in [+135°, +225°)
//   Right: rel bearing in [+225°, +315°)
//
// Bearing convention follows engine_player::GetPlayerYawDegrees: world
// frame, 0° = +X = east, CCW positive. Relative bearing = world bearing
// from player to candidate, minus player yaw, normalised to [0, 360).
// Left = +90° (CCW) because both yaw and atan2 are CCW-positive.
//
// Trigger 2's ±45° front cone equals the Front sector exactly — both
// triggers share this classifier so the same in-front candidate set
// drives T2 firing decisions.
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

const char* SectorTag(WallSector s) {
    switch (s) {
        case WallSector::Front: return "F";
        case WallSector::Left:  return "L";
        case WallSector::Back:  return "B";
        case WallSector::Right: return "R";
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
        g_surface_last_distance[i] = -1.0f;
        g_surface_last_cued_at[i]  = 0;
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

    if (overflow) {
        acclog::Write(
            "ChangeDetector: area change — walls cached=%d/%d (OVERFLOW; "
            "increase kMaxWallEdges) areaPtr=%p",
            g_wall_count, totalDiscovered, area);
    } else {
        acclog::Write(
            "ChangeDetector: area change — walls cached=%d areaPtr=%p",
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

    // Reset per-surface scratch and last_distance, then walk every edge
    // and propagate its distance into its surface's per-area minimum.
    for (int i = 0; i < g_surface_count; ++i) {
        g_surface_last_distance[i] = -1.0f;
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
        if (g_surface_last_distance[sid] < 0.0f ||
            dist < g_surface_last_distance[sid]) {
            g_surface_last_distance[sid] = dist;
        }
    }
    int wallsCalibrated = 0;
    for (int i = 0; i < g_surface_count; ++i) {
        if (g_surface_last_distance[i] >= 0.0f) ++wallsCalibrated;
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

    acclog::Write(
        "ChangeDetector: calibrated (%s) walls=%d objects=%d at "
        "ref=(%.2f,%.2f,%.2f)",
        reason ? reason : "?", wallsCalibrated, objectsCalibrated,
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

    float range     = settings.awarenessRangeMeters;
    float threshold = settings.distanceDeltaThresholdMeters;
    float rangeSq   = range * range;
    int   maxCues   = settings.trigger1MaxWallCuesPerTick;
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

    int walls_in_range    = 0;       // surfaces in range (not edges)
    int surface_candidates= 0;
    int walls_cued        = 0;
    int objs_in_range     = 0;
    int objs_cued         = 0;
    char sector_log[16]   = {0};   // F/L/B/R per fired surface

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
        if (distSq > rangeSq) continue;
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

    // --- Walls: pass 2 — surface-level threshold check + T2 foremost --
    for (int s = 0; s < g_surface_count; ++s) {
        SurfaceScratch& ss = g_surface_scratch[s];
        if (!ss.in_range) {
            g_surface_last_distance[s] = -1.0f;
            continue;
        }
        ++walls_in_range;

        // T2 candidate detection — every in-range surface whose closest
        // point lies in the Front sector competes for foremost.
        if (t2_enabled) {
            float dx = ss.best_closest_point.x - referencePos.x;
            float dy = ss.best_closest_point.y - referencePos.y;
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            if (ClassifyRelativeBearing(wb - effectiveYaw) ==
                    WallSector::Front &&
                ss.best_distance < t2_best_dist) {
                t2_best_dist                = ss.best_distance;
                t2_best.kind                = FeatureKind::Wall;
                t2_best.wall_surface_index  = s;
                t2_best.object_handle       = 0;
                t2_best_pos                 = ss.best_closest_point;
                t2_best_cue                 = acc::audio::NavCue::Wall;
            }
        }

        if (!settings.trigger1DistanceDelta) continue;

        // Threshold check on the surface minimum. Walking parallel to a
        // long wall holds best_distance ≈ constant → no fire. Approach
        // / retreat from the wall changes the minimum smoothly → fires
        // on each `threshold` step.
        bool crossed = false;
        if (g_surface_last_distance[s] < 0.0f) {
            crossed = true;  // first observation in range during play
        } else if (std::fabs(ss.best_distance - g_surface_last_distance[s])
                   > threshold) {
            crossed = true;
        }
        if (!crossed) continue;

        g_surface_last_distance[s] = ss.best_distance;

        // Per-tick same-closest-point dedup. Vertex-sharing T- / X-
        // junctions produce multiple surfaces whose best_closest_point
        // coincides at the shared corner. Without this merge, all of
        // them eat slots in the K-cap and fire near-identical cues at
        // the same world position. Keep the smaller-distance entry; the
        // surface losing the merge still has its last_distance updated
        // above, so it doesn't immediately re-trip the threshold.
        bool merged = false;
        for (int c = 0; c < surface_candidates; ++c) {
            float dx = g_surface_candidates[c].closest_point.x -
                       ss.best_closest_point.x;
            float dy = g_surface_candidates[c].closest_point.y -
                       ss.best_closest_point.y;
            float dz = g_surface_candidates[c].closest_point.z -
                       ss.best_closest_point.z;
            if (dx * dx + dy * dy + dz * dz < kFireDedupTolSquared) {
                if (ss.best_distance < g_surface_candidates[c].distance) {
                    g_surface_candidates[c].surface_index  = s;
                    g_surface_candidates[c].distance       = ss.best_distance;
                    g_surface_candidates[c].closest_point  =
                        ss.best_closest_point;
                }
                merged = true;
                break;
            }
        }
        if (merged) continue;

        if (surface_candidates < kMaxWallSurfaces) {
            g_surface_candidates[surface_candidates].surface_index = s;
            g_surface_candidates[surface_candidates].distance      =
                ss.best_distance;
            g_surface_candidates[surface_candidates].closest_point =
                ss.best_closest_point;
            ++surface_candidates;
        }
    }

    // --- Walls: pass 3 — fire K closest surfaces by distance ----------
    //
    // No sector binning: surfaces ARE the spatial diversity (each
    // surface is a distinct physical wall with a distinct bearing).
    // K-closest selection ensures we never spam more than K cues per
    // tick even in dense rooms; surfaces that lost the cut keep their
    // last_distance updated above so they don't immediately re-queue
    // next tick.
    if (surface_candidates > 0) {
        // Selection sort the first K candidates by ascending distance.
        // K is small (default 3); selection sort is cheap.
        int firedCount = surface_candidates < maxCues ? surface_candidates
                                                      : maxCues;
        for (int i = 0; i < firedCount; ++i) {
            int minIdx = i;
            for (int j = i + 1; j < surface_candidates; ++j) {
                if (g_surface_candidates[j].distance <
                    g_surface_candidates[minIdx].distance) {
                    minIdx = j;
                }
            }
            if (minIdx != i) {
                SurfaceCandidate tmp = g_surface_candidates[i];
                g_surface_candidates[i]      = g_surface_candidates[minIdx];
                g_surface_candidates[minIdx] = tmp;
            }
        }

        int logIdx = 0;
        for (int k = 0; k < firedCount; ++k) {
            const auto& c = g_surface_candidates[k];
            float dx = c.closest_point.x - referencePos.x;
            float dy = c.closest_point.y - referencePos.y;
            float wb = std::atan2(dy, dx) * 57.29577951308232f;
            WallSector s = ClassifyRelativeBearing(wb - effectiveYaw);
            if (logIdx < static_cast<int>(sizeof(sector_log)) - 1) {
                sector_log[logIdx++] = SectorTag(s)[0];
            }
            if (acc::audio::PlayCueAtPosition(
                    acc::audio::NavCue::Wall, c.closest_point,
                    referencePos, range)) {
                ++walls_cued;
                g_surface_last_cued_at[c.surface_index] = now;
            }
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

        if (distSq > rangeSq) {
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
        ObjectState* s = FindOrAddObjectState(handle, isNew);
        if (!s) continue;

        bool fire = isNew ||
                    (std::fabs(dist - s->last_distance) > threshold);
        if (fire) {
            if (acc::audio::PlayCueAtPosition(cue, pos, referencePos, range)) {
                ++objs_cued;
                s->last_cued_at = now;
            }
            s->last_distance = dist;
        }
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
            acclog::Write(
                "ChangeDetector: T2 first-tick suppress; foremost=%s",
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
                //   2. Wall  → check g_wall_last_cued_at[index].
                //   3. Object → check ObjectState.last_cued_at.
                bool fire = false;
                if (g_t2_pending.kind == FeatureKind::Wall &&
                    g_t2_pending.wall_surface_index >= 0 &&
                    g_t2_pending.wall_surface_index < g_surface_count) {
                    DWORD lastAt = g_surface_last_cued_at[
                        g_t2_pending.wall_surface_index];
                    if (lastAt == 0 || now - lastAt > kT2QuietMs) fire = true;
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
                    acclog::Write(
                        "ChangeDetector: T2 fire kind=%s dist=%.2f "
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
        acclog::Write(
            "ChangeDetector: tick surfaces_in_range=%d surface_candidates=%d "
            "walls_cued=%d sectors=%s objs_in_range=%d objs_cued=%d "
            "t2_fired=%d",
            walls_in_range, surface_candidates,
            walls_cued, sector_log,
            objs_in_range, objs_cued, t2_fired ? 1 : 0);
    } else if (objs_cued > 0 || t2_fired) {
        acclog::Write(
            "ChangeDetector: tick surfaces_in_range=%d surface_candidates=%d "
            "objs_in_range=%d objs_cued=%d t2_fired=%d",
            walls_in_range, surface_candidates,
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
