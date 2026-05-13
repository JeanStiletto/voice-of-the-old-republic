// Wall-topology decomposition — see wall_topology.h for goal + algorithm.
//
// EXPERIMENTAL — alternative-direction-calculation-system branch.
//
// First commit: Phase 1 (segment grouping) implemented + logged so we
// can inspect the segment graph on real K1 maps. Phases 2-5 (junction
// clustering, corridor detection, classification, spatial index) are
// stubbed with TODOs and will land iteratively.

#include "wall_topology.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "log.h"
#include "spatial_change_detector.h"

namespace acc::wall_topology {

namespace {

// ---------------------------------------------------------------------
// Tunable parameters. Pull these out into a settings struct once the
// algorithm stabilises; for now constants make it easy to A/B with the
// log output.
// ---------------------------------------------------------------------

// Endpoint-join tolerance: two segment endpoints within this distance
// are treated as the same vertex during merging + junction clustering.
// Walkmesh perimeter edges share endpoints exactly in well-formed
// content, but float drift through LocalToWorld can introduce a few
// mm of slop.
constexpr float kJoinToleranceM = 0.10f;

// Angle tolerance for "collinear" — when merging two edges that share
// an endpoint, their direction vectors must agree within this
// half-angle.
constexpr float kCollinearAngleDeg = 10.0f;

// Drop merged segments shorter than this — noise edges, single-tile
// artefacts, etc. Threshold chosen by inspection of K1 walkmesh
// geometry; refine after we see the first few areas.
constexpr float kMinSegmentLengthM = 0.5f;

// ---------------------------------------------------------------------
// Internal data types.
// ---------------------------------------------------------------------

// A merged wall — a chain of one or more collinear edges represented
// as two endpoints (a, b) and a cached direction unit-vector. We keep
// it as straight segments rather than polylines for the first pass;
// curved corridors will need polyline support, but the K1 geometry
// we've inspected so far is overwhelmingly axis-aligned.
struct WallSegment {
    Vector a;
    Vector b;
    float  dirx;     // unit direction (b - a) / length
    float  diry;
    float  length;
};

constexpr int kMaxSegments = 1024;
constexpr int kMaxEdgesIn  = 4096;

struct Graph {
    void*       area_owner = nullptr;
    bool        built      = false;
    int         segment_count = 0;
    WallSegment segments[kMaxSegments];
};

Graph g_graph;

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

float Distance(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool SamePoint(const Vector& a, const Vector& b) {
    return Distance(a, b) <= kJoinToleranceM;
}

// Dot product of two unit vectors, clamped to [-1, 1] to keep acos
// numerically safe under float drift.
float UnitDot(float ax, float ay, float bx, float by) {
    float d = ax * bx + ay * by;
    if (d >  1.0f) d =  1.0f;
    if (d < -1.0f) d = -1.0f;
    return d;
}

// Returns true if two unit vectors are within `tolDeg` degrees of
// being collinear (either same direction OR opposite). Wall segments
// don't have a natural "forward" — we care about line direction, not
// ray direction.
bool NearCollinear(float ax, float ay, float bx, float by, float tolDeg) {
    float d = UnitDot(ax, ay, bx, by);
    // |d| close to 1 means parallel-or-antiparallel.
    float cosTol = std::cos(tolDeg * 0.017453292519943295f);
    return std::fabs(d) >= cosTol;
}

bool BuildSegmentFromEdge(const acc::engine::WallEdge& e, WallSegment& out) {
    out.a = e.a;
    out.b = e.b;
    float dx = e.b.x - e.a.x;
    float dy = e.b.y - e.a.y;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) return false;  // degenerate edge
    out.dirx   = dx / len;
    out.diry   = dy / len;
    out.length = len;
    return true;
}

// Attempt to merge segment B into segment A. Returns true on success;
// A is updated in place. Four endpoint configurations to handle:
//   1. A.b ≈ B.a   — append B to A's end
//   2. A.b ≈ B.b   — append B reversed
//   3. A.a ≈ B.a   — prepend B reversed
//   4. A.a ≈ B.b   — prepend B
// In each case the join direction must be near-collinear.
bool TryMerge(WallSegment& a, const WallSegment& b) {
    // Configuration 1: a.b -> b.a -> b.b
    if (SamePoint(a.b, b.a) &&
        NearCollinear(a.dirx, a.diry, b.dirx, b.diry, kCollinearAngleDeg)) {
        a.b = b.b;
        float dx = a.b.x - a.a.x;
        float dy = a.b.y - a.a.y;
        a.length = std::sqrt(dx * dx + dy * dy);
        if (a.length > 1e-6f) { a.dirx = dx / a.length; a.diry = dy / a.length; }
        return true;
    }
    // Configuration 2: a.b -> b.b -> b.a  (B reversed)
    if (SamePoint(a.b, b.b) &&
        NearCollinear(a.dirx, a.diry, -b.dirx, -b.diry, kCollinearAngleDeg)) {
        a.b = b.a;
        float dx = a.b.x - a.a.x;
        float dy = a.b.y - a.a.y;
        a.length = std::sqrt(dx * dx + dy * dy);
        if (a.length > 1e-6f) { a.dirx = dx / a.length; a.diry = dy / a.length; }
        return true;
    }
    // Configuration 3: a.a -> b.a -> b.b (B reversed prepend)
    if (SamePoint(a.a, b.a) &&
        NearCollinear(a.dirx, a.diry, -b.dirx, -b.diry, kCollinearAngleDeg)) {
        a.a = b.b;
        float dx = a.b.x - a.a.x;
        float dy = a.b.y - a.a.y;
        a.length = std::sqrt(dx * dx + dy * dy);
        if (a.length > 1e-6f) { a.dirx = dx / a.length; a.diry = dy / a.length; }
        return true;
    }
    // Configuration 4: a.a -> b.b -> b.a (prepend)
    if (SamePoint(a.a, b.b) &&
        NearCollinear(a.dirx, a.diry, b.dirx, b.diry, kCollinearAngleDeg)) {
        a.a = b.a;
        float dx = a.b.x - a.a.x;
        float dy = a.b.y - a.a.y;
        a.length = std::sqrt(dx * dx + dy * dy);
        if (a.length > 1e-6f) { a.dirx = dx / a.length; a.diry = dy / a.length; }
        return true;
    }
    return false;
}

}  // namespace

void Reset() {
    g_graph.area_owner    = nullptr;
    g_graph.built         = false;
    g_graph.segment_count = 0;
}

bool HasGraphForArea(void* area) {
    return area != nullptr &&
           g_graph.built &&
           g_graph.area_owner == area;
}

void BuildForArea(void* area) {
    if (!area) return;
    if (HasGraphForArea(area)) return;

    Reset();
    g_graph.area_owner = area;

    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        acclog::Write("WallTopo",
                      "BuildForArea: wall cache not ready — leaving graph "
                      "empty (will retry on next call)");
        return;
    }

    int edgeCap = wallCount > kMaxEdgesIn ? kMaxEdgesIn : wallCount;
    if (wallCount > kMaxEdgesIn) {
        acclog::Write("WallTopo",
                      "BuildForArea: %d edges exceeds cap %d — truncating",
                      wallCount, kMaxEdgesIn);
    }

    // ---------------------------------------------------------------
    // Phase 1: load each edge as a degenerate-zero-extent segment.
    // ---------------------------------------------------------------
    int    segCount = 0;
    int    skipped  = 0;
    for (int i = 0; i < edgeCap && segCount < kMaxSegments; ++i) {
        WallSegment s;
        if (!BuildSegmentFromEdge(walls[i], s)) {
            ++skipped;
            continue;
        }
        g_graph.segments[segCount++] = s;
    }
    g_graph.segment_count = segCount;
    int initialSegCount = segCount;

    // ---------------------------------------------------------------
    // Phase 2: iteratively merge collinear adjacent segments.
    //
    // Greedy O(N²) per pass; repeat until no merges in a full sweep.
    // Each merge collapses two segments to one, so total work is
    // bounded by O(N³) worst case (rarely hit in practice — most
    // merges happen in the first 1-2 passes).
    // ---------------------------------------------------------------
    int passes = 0;
    while (passes < 16) {
        ++passes;
        bool merged = false;
        for (int i = 0; i < g_graph.segment_count && !merged; ++i) {
            for (int j = i + 1; j < g_graph.segment_count && !merged; ++j) {
                if (TryMerge(g_graph.segments[i], g_graph.segments[j])) {
                    // Swap-remove j.
                    g_graph.segments[j] =
                        g_graph.segments[g_graph.segment_count - 1];
                    --g_graph.segment_count;
                    merged = true;
                }
            }
        }
        if (!merged) break;
    }

    // ---------------------------------------------------------------
    // Phase 3: drop noise segments (shorter than min length).
    // ---------------------------------------------------------------
    int kept = 0;
    int dropped = 0;
    for (int i = 0; i < g_graph.segment_count; ++i) {
        if (g_graph.segments[i].length >= kMinSegmentLengthM) {
            if (kept != i) g_graph.segments[kept] = g_graph.segments[i];
            ++kept;
        } else {
            ++dropped;
        }
    }
    g_graph.segment_count = kept;

    acclog::Write("WallTopo",
                  "BuildForArea: area=%p edges=%d -> initial segs=%d "
                  "(skipped %d degenerate) merged in %d passes -> %d "
                  "(dropped %d short)",
                  area, wallCount, initialSegCount, skipped, passes,
                  g_graph.segment_count, dropped);

    // TODO Phase 4: junction clustering (find endpoint clusters).
    // TODO Phase 5: corridor detection (parallel-segment pairs with
    //               overlapping projections, width 2-6m).
    // TODO Phase 6: classification + label rendering.
    // TODO Phase 7: spatial index (uniform grid or polygon walk).

    g_graph.built = true;
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    acclog::Write("WallTopo",
                  "graph dump area=%p segments=%d",
                  g_graph.area_owner, g_graph.segment_count);
    // Bucket segment lengths to get a quick distribution.
    int bucket_le_2  = 0;
    int bucket_le_5  = 0;
    int bucket_le_10 = 0;
    int bucket_gt_10 = 0;
    for (int i = 0; i < g_graph.segment_count; ++i) {
        float L = g_graph.segments[i].length;
        if      (L <= 2.0f)  ++bucket_le_2;
        else if (L <= 5.0f)  ++bucket_le_5;
        else if (L <= 10.0f) ++bucket_le_10;
        else                 ++bucket_gt_10;
    }
    acclog::Write("WallTopo",
                  "length buckets: <=2m=%d <=5m=%d <=10m=%d >10m=%d",
                  bucket_le_2, bucket_le_5, bucket_le_10, bucket_gt_10);

    // Log the top-N longest segments (interesting in their own right —
    // these are usually the major corridor walls).
    constexpr int kTopN = 12;
    int order[kMaxSegments];
    for (int i = 0; i < g_graph.segment_count; ++i) order[i] = i;
    // Selection-sort the top kTopN by length descending.
    int limit = g_graph.segment_count < kTopN ? g_graph.segment_count : kTopN;
    for (int i = 0; i < limit; ++i) {
        int best = i;
        for (int j = i + 1; j < g_graph.segment_count; ++j) {
            if (g_graph.segments[order[j]].length >
                g_graph.segments[order[best]].length) {
                best = j;
            }
        }
        if (best != i) {
            int t = order[i]; order[i] = order[best]; order[best] = t;
        }
        const auto& s = g_graph.segments[order[i]];
        acclog::Write("WallTopo",
                      "  seg[%d] len=%.2fm a=(%.2f,%.2f) b=(%.2f,%.2f) "
                      "dir=(%.2f,%.2f)",
                      order[i], s.length, s.a.x, s.a.y, s.b.x, s.b.y,
                      s.dirx, s.diry);
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    outSig = 0;
    if (!HasGraphForArea(area)) return false;
    // TODO: spatial index lookup. For now this returns false so
    // nothing is wired up yet; the decomposition runs as a parallel
    // observer until the algorithm is complete.
    (void)worldPos;
    return false;
}

}  // namespace acc::wall_topology
