#include "spatial_wall_surfaces.h"

#include <cmath>

#include "engine_area.h"
#include "log.h"

namespace acc::spatial::wall_surfaces {

namespace {

// --- Cache + clustering state -----------------------------------------

acc::engine::WallEdge g_walls[kMaxWallEdges];
int                   g_wall_count = 0;

// Per-edge surface-id mapping. Filled by the area-load clustering pass.
// -1 = "edge not assigned" (only seen briefly during clustering or if
// kMaxWallSurfaces overflowed).
int g_edge_surface_id[kMaxWallEdges];

// Number of distinct surfaces clustered for the current area.
int g_surface_count = 0;

// Per-surface segment descriptor built right after clustering.
// edge_count == 0 flags a degenerate surface (closed loop or zero
// free endpoints) — GetSurfaceDesc returns false for those.
WallSurfaceDesc g_surface_descriptors[kMaxWallSurfaces];

// --- Clustering constants ---------------------------------------------

// cos(15°) ≈ 0.966 — direction vectors that disagree by more than 15°
// are different surfaces. Generous enough to absorb walkmesh dicing
// noise on smooth corridor walls, tight enough to keep L-junctions
// distinct (90° → cos=0, well below threshold).
constexpr float kSurfaceCollinearityCosThreshold = 0.966f;

// Endpoint coincidence tolerance — 5cm. Walkmesh world-space transforms
// can introduce sub-mm float noise; 5cm is well below any realistic
// edge spacing in KOTOR's hand-authored layouts.
constexpr float kEndpointTolMeters  = 0.05f;
constexpr float kEndpointTolSquared = kEndpointTolMeters * kEndpointTolMeters;

// --- Geometry helpers --------------------------------------------------

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

// --- Union-find over edges (small-N implementation) -------------------
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
        acclog::Write("WallSurfaces", "surface overflow — %d edges collapsed to "
            "bucket 0 (raise kMaxWallSurfaces above %d)",
            overflow_collisions, kMaxWallSurfaces);
    }
    acclog::Write("WallSurfaces", "clustered %d edges into %d surfaces",
        g_wall_count, g_surface_count);
}

// Reduce each surface (a chain of collinear endpoint-sharing edges) to
// a single straight segment `a → b`. The two extreme endpoints are the
// only endpoints used by exactly one edge in the surface; interior
// endpoints are shared between two adjacent edges.
//
// Algorithm per surface:
//   1. Walk every edge with this surface id; bucket its two endpoints
//      into a small dedup'd point table (XY tolerance = kEndpointTol).
//   2. The two endpoints with count==1 are the segment ends.
//   3. Closed loops (zero count==1 endpoints) or anomalies (>2) leave
//      the descriptor flagged `edge_count = 0` so GetSurfaceDesc
//      reports false for them.
//
// Bounded per-surface point table at 64 — far above the typical chain
// length (a few edges per surface). Surfaces with more get truncated
// and fall into the anomaly path; we log a warning if this ever bites.
constexpr int kMaxPointsPerSurface = 64;

void BuildSurfaceDescriptors() {
    // Anomaly breakdown counters — each anomalous surface increments
    // exactly one of these so we can tell which case dominates the
    // 15-33% flagged rate seen on Apartments / Upper City. Phase 1
    // diagnostic; remove once the cause is understood.
    int anomCount     = 0;
    int anomZeroFree  = 0;  // closed loop — every endpoint shared
    int anomOneFree   = 0;  // odd half-loop (one end attaches to itself)
    int anomThreeFree = 0;  // Y-fork (silently truncated previously)
    int anomFourFree  = 0;  // X-fork / cross
    int anomMoreFree  = 0;  // tree with 5+ leaves
    int anomOverflow  = 0;  // >64 distinct endpoints

    // Multi-elevation tag: an anomalous surface whose edges span
    // distinctly different Z values (>kMultiElevThreshold) is a legit
    // 3D feature — multi-floor walls, balcony rails over a hall, etc.
    // — not a bug. We can't reduce these to one 2D segment because
    // they aren't one in 3D space; downstream consumers that need to
    // distinguish elevations would have to operate per-Z-band rather
    // than over a single segment. Tracked separately from "broken"
    // anomalies so the diagnostic doesn't lump them together.
    constexpr float kMultiElevThreshold = 0.5f;
    int anomMultiElevation = 0;

    // Sample the first few anomalies per category for log dump.
    constexpr int kSampleLimit = 4;
    struct AnomalySample {
        int    surface_idx;
        int    edge_count;
        int    point_count;
        int    free_count;
        bool   overflow;
        bool   multi_elevation;
        float  z_spread;
        Vector first_point;
    };
    AnomalySample samples[kSampleLimit];
    int sampleCount = 0;

    for (int s = 0; s < g_surface_count; ++s) {
        g_surface_descriptors[s]            = {};
        g_surface_descriptors[s].edge_count = 0;

        struct EndpointCount {
            Vector pos;
            int    count;
        };
        EndpointCount points[kMaxPointsPerSurface];
        int           pointCount = 0;
        int           edgeCount  = 0;
        bool          overflow   = false;

        auto bumpEndpoint = [&](const Vector& p) {
            for (int k = 0; k < pointCount; ++k) {
                if (DistanceSquaredXY(points[k].pos, p) <= kEndpointTolSquared) {
                    ++points[k].count;
                    return;
                }
            }
            if (pointCount >= kMaxPointsPerSurface) {
                overflow = true;
                return;
            }
            points[pointCount].pos   = p;
            points[pointCount].count = 1;
            ++pointCount;
        };

        // Track Z range for multi-elevation classification of any
        // anomaly we end up flagging.
        float minZ = 1e30f;
        float maxZ = -1e30f;
        for (int i = 0; i < g_wall_count; ++i) {
            if (g_edge_surface_id[i] != s) continue;
            ++edgeCount;
            bumpEndpoint(g_walls[i].a);
            bumpEndpoint(g_walls[i].b);
            if (g_walls[i].a.z < minZ) minZ = g_walls[i].a.z;
            if (g_walls[i].b.z < minZ) minZ = g_walls[i].b.z;
            if (g_walls[i].a.z > maxZ) maxZ = g_walls[i].a.z;
            if (g_walls[i].b.z > maxZ) maxZ = g_walls[i].b.z;
        }
        float zSpread = (edgeCount > 0) ? (maxZ - minZ) : 0.0f;
        bool multiElevation = zSpread > kMultiElevThreshold;

        // Count free endpoints (no cap — we want the true count for
        // diagnostics). Pick the first two for the segment endpoints
        // (used only on the freeCount==2 happy path).
        int    totalFreeCount = 0;
        Vector freeEnds[2] = {{0, 0, 0}, {0, 0, 0}};
        for (int k = 0; k < pointCount; ++k) {
            if (points[k].count == 1) {
                if (totalFreeCount < 2) {
                    freeEnds[totalFreeCount] = points[k].pos;
                }
                ++totalFreeCount;
            }
        }

        if (totalFreeCount == 2 && !overflow) {
            WallSurfaceDesc& d = g_surface_descriptors[s];
            d.a = freeEnds[0];
            d.b = freeEnds[1];
            float dx  = d.b.x - d.a.x;
            float dy  = d.b.y - d.a.y;
            float len = std::sqrt(dx * dx + dy * dy);
            d.length = len;
            if (len > 1e-6f) {
                d.dir_x = dx / len;
                d.dir_y = dy / len;
            }
            d.edge_count = edgeCount;
            continue;
        }

        // Anomaly — categorise + sample.
        ++anomCount;
        if      (overflow)              ++anomOverflow;
        else if (totalFreeCount == 0)   ++anomZeroFree;
        else if (totalFreeCount == 1)   ++anomOneFree;
        else if (totalFreeCount == 3)   ++anomThreeFree;
        else if (totalFreeCount == 4)   ++anomFourFree;
        else                            ++anomMoreFree;
        if (multiElevation) ++anomMultiElevation;

        if (sampleCount < kSampleLimit) {
            AnomalySample& smp = samples[sampleCount++];
            smp.surface_idx     = s;
            smp.edge_count      = edgeCount;
            smp.point_count     = pointCount;
            smp.free_count      = totalFreeCount;
            smp.overflow        = overflow;
            smp.multi_elevation = multiElevation;
            smp.z_spread        = zSpread;
            smp.first_point     = (pointCount > 0) ? points[0].pos
                                                   : Vector{0.0f, 0.0f, 0.0f};
        }
    }

    if (anomCount > 0) {
        int anomBroken = anomCount - anomMultiElevation;
        acclog::Write("WallSurfaces",
                      "surface-descriptor anomalies: %d/%d flagged "
                      "(multi-elev=%d broken=%d — 0free=%d 1free=%d "
                      "3free=%d 4free=%d 5+free=%d overflow=%d)",
                      anomCount, g_surface_count,
                      anomMultiElevation, anomBroken,
                      anomZeroFree, anomOneFree, anomThreeFree,
                      anomFourFree, anomMoreFree, anomOverflow);
        for (int i = 0; i < sampleCount; ++i) {
            const AnomalySample& smp = samples[i];
            acclog::Write("WallSurfaces",
                          "  anom[%d] surf=%d edges=%d points=%d free=%d "
                          "overflow=%d %s(zSpread=%.2fm) sample_pt=(%.2f,%.2f)",
                          i, smp.surface_idx, smp.edge_count,
                          smp.point_count, smp.free_count,
                          smp.overflow ? 1 : 0,
                          smp.multi_elevation ? "multi-elev " : "",
                          smp.z_spread,
                          smp.first_point.x, smp.first_point.y);
            // Dump the underlying edges of this anomalous surface so
            // we can see whether the lens pairs are same-room or
            // cross-room, and how the endpoints actually relate.
            int dumped = 0;
            constexpr int kEdgeDumpLimit = 6;
            for (int e = 0; e < g_wall_count && dumped < kEdgeDumpLimit;
                 ++e) {
                if (g_edge_surface_id[e] != smp.surface_idx) continue;
                acclog::Write("WallSurfaces",
                              "    edge[%d] room=%d a=(%.4f,%.4f,%.4f) "
                              "b=(%.4f,%.4f,%.4f) mat=%d",
                              e, g_walls[e].room_id,
                              g_walls[e].a.x, g_walls[e].a.y, g_walls[e].a.z,
                              g_walls[e].b.x, g_walls[e].b.y, g_walls[e].b.z,
                              g_walls[e].material_id);
                ++dumped;
            }
        }
    }
}

}  // namespace

void Clear() {
    g_wall_count    = 0;
    g_surface_count = 0;
}

void RebuildForArea(void* area) {
    if (!area) {
        Clear();
        return;
    }
    g_wall_count = acc::engine::BuildAreaWallCache(area, g_walls, kMaxWallEdges);
    int totalDiscovered = acc::engine::BuildAreaWallCache(area, nullptr, 0);
    bool overflow = (totalDiscovered > kMaxWallEdges);

    ClusterEdgesIntoSurfaces();
    BuildSurfaceDescriptors();

    // Telemetry: g_wall_count is post-seam-filter (real walls); the
    // overflow check uses the pre-filter discovered count vs buffer size,
    // because edges dropped during the scan never reach the seam filter.
    if (overflow) {
        acclog::Write("WallSurfaces", "area change — walls cached=%d "
            "(discovered=%d, OVERFLOW pre-seam-filter; increase "
            "kMaxWallEdges) areaPtr=%p",
            g_wall_count, totalDiscovered, area);
    } else {
        acclog::Write("WallSurfaces", "area change — walls cached=%d "
            "(seam-filtered from discovered=%d) areaPtr=%p",
            g_wall_count, totalDiscovered, area);
    }
}

const acc::engine::WallEdge* GetWallBuffer() {
    return g_wall_count > 0 ? g_walls : nullptr;
}

const int* GetEdgeSurfaceIdBuffer() {
    return g_wall_count > 0 ? g_edge_surface_id : nullptr;
}

int GetWallCount() {
    return g_wall_count;
}

int GetSurfaceCount() {
    return g_surface_count;
}

bool GetSurfaceDesc(int idx, WallSurfaceDesc& outDesc) {
    if (idx < 0 || idx >= g_surface_count) return false;
    const WallSurfaceDesc& d = g_surface_descriptors[idx];
    if (d.edge_count <= 0) return false;
    outDesc = d;
    return true;
}

bool SegmentCrossesSurface(const Vector& a, const Vector& b,
                           Vector& outHitPoint) {
    // Movement direction in 2D. If both deltas are ~0 the segment isn't
    // actually moving and there's nothing to test (and the parametric
    // formula below would divide by zero).
    float abx = b.x - a.x;
    float aby = b.y - a.y;
    if (abx * abx + aby * aby < 1e-10f) return false;

    bool   anyHit  = false;
    float  bestT   = 1e30f;
    Vector bestHit = a;

    // Iterate over clustered surfaces (the same representation the audio
    // wall-cue system reads). Each surface is a straight line segment
    // from `a` to `b` covering one or more collinear+connected raw
    // edges; portal seams that happen to be collinear with a real wall
    // are absorbed into that wall's surface during clustering, so this
    // test ignores phantom edges by construction. edge_count == 0
    // flags a degenerate descriptor and is skipped.
    for (int i = 0; i < g_surface_count; ++i) {
        const WallSurfaceDesc& s = g_surface_descriptors[i];
        if (s.edge_count <= 0) continue;
        float cdx = s.b.x - s.a.x;
        float cdy = s.b.y - s.a.y;

        // 2D segment-segment intersection in XY.
        //   a + t*(b - a) == s.a + u*(s.b - s.a)
        float denom = abx * cdy - aby * cdx;
        if (denom > -1e-8f && denom < 1e-8f) continue;

        float dx = s.a.x - a.x;
        float dy = s.a.y - a.y;
        float t  = (dx * cdy - dy * cdx) / denom;
        float u  = (dx * aby - dy * abx) / denom;

        if (t < 0.0f || t > 1.0f) continue;
        if (u < 0.0f || u > 1.0f) continue;

        if (t < bestT) {
            bestT     = t;
            bestHit.x = a.x + t * abx;
            bestHit.y = a.y + t * aby;
            bestHit.z = a.z + t * (b.z - a.z);
            anyHit    = true;
        }
    }

    if (anyHit) outHitPoint = bestHit;
    return anyHit;
}

}  // namespace acc::spatial::wall_surfaces
