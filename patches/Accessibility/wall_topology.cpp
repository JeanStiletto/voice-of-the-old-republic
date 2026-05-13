// Wall-topology decomposition — see wall_topology.h for goal + algorithm.
//
// EXPERIMENTAL — alternative-direction-calculation-system branch.
//
// Phase 1 (segment input) now consumes Pillar 1's already-clustered
// wall surfaces from `acc::spatial::change_detector` instead of
// re-running its own collinear-merge pass over the raw edge list.
// Pillar 1's union-find clustering is ~2× more aggressive on the same
// input (462 edges → 121 surfaces vs walltopo's prior 237 segments),
// and getting longer / cleaner segments out of Phase 1 is a strict
// precondition for the parallel-pair sweep in Phase 4 to find real
// corridors rather than tiny corner fragments.
//
// Phases 4-5 (corridor detection, junction clustering) and the lookup
// path still use the old "two parallel walls forming a 1.5-6m gap"
// model; that's the next thing to redesign (toward face-graph cell
// decomposition — see chat 2026-05-13).

#include "wall_topology.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "log.h"
#include "spatial_change_detector.h"
#include "strings.h"

namespace acc::wall_topology {

namespace {

// ---------------------------------------------------------------------
// Tunable parameters. Pull these out into a settings struct once the
// algorithm stabilises; for now constants make it easy to A/B with the
// log output.
// ---------------------------------------------------------------------

// Drop wall surfaces shorter than this — noise / single-tile artefacts.
// Pillar 1's clustering already collapses chains of collinear edges
// into one surface, so a "short" surface here is genuinely a short
// physical wall (a 50cm jut, not a few unmerged dicing artefacts).
// Threshold matches the value used by walltopo's prior in-house merge
// pass; refine after we see a few more areas with the cleaner
// upstream segments.
constexpr float kMinSegmentLengthM = 0.5f;

// Corridor parallel-pair detection (Phase 2). Two segments form a
// corridor candidate if they are roughly parallel, separated by a
// distance within the corridor-width band, and their projections
// onto the parallel direction overlap by at least min-overlap.
constexpr float kCorridorParallelTolDeg = 10.0f;
constexpr float kCorridorMinWidthM      = 1.5f;
constexpr float kCorridorMaxWidthM      = 6.0f;
constexpr float kCorridorMinOverlapM    = 2.0f;

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

// A detected corridor formed by two parallel segments. Stored as the
// indices of the two segments plus the corridor's derived geometry:
// axis direction, perpendicular width, and overlap range (start / end
// parameters along the axis). Label is pre-rendered at build time
// from the geometry + localized strings.
struct CorridorCell {
    int   seg_a;            // index into Graph.segments
    int   seg_b;
    float axis_x, axis_y;   // unit direction along corridor length
    float perp_x, perp_y;   // unit direction perpendicular (a → b side)
    float width;            // perpendicular distance between walls
    float midline_x, midline_y;   // axis-zero point on corridor midline
    float overlap_start;    // start parameter along axis (from midline)
    float overlap_end;      // end parameter
    float length;           // overlap_end - overlap_start
    char  label[128];       // Phase 4: pre-rendered "Korridor {axis}, {w}m"
    int   sig;              // signature for dedup
};

// A junction in the floor-plan graph — a cluster of corridor endpoints
// that lie within kJunctionClusterRadiusM of each other. Degree =
// number of corridors meeting at this cluster. Label is pre-rendered:
//   degree 1 → "Sackgasse, {direction}"
//   degree ≥ 3 → "Kreuzung, {directions}"
// Degree 2 (pass-through / corner) is intentionally label-less; the
// adjacent corridor labels carry the information.
struct JunctionNode {
    float pos_x, pos_y;
    int   corridor_count;
    static constexpr int kMaxCorridorsPerNode = 8;
    int   corridor_idx[kMaxCorridorsPerNode];
    // Direction from this node toward each connected corridor's centre
    // (used for label rendering — "Kreuzung, Nord, Ost, ...").
    float corridor_dir_x[kMaxCorridorsPerNode];
    float corridor_dir_y[kMaxCorridorsPerNode];
    char  label[128];       // empty when degree==2
    int   sig;
};

constexpr int kMaxCorridors  = 256;
constexpr int kMaxJunctions  = 256;

// Junction-clustering radius. Corridor endpoints within this distance
// fold into one node. 3m matches the typical corridor-width / corner-
// blob size in K1 geometry.
constexpr float kJunctionClusterRadiusM = 3.0f;

// Spatial lookup tolerances.
constexpr float kJunctionLookupRadiusM = 3.0f;

struct Graph {
    void*        area_owner     = nullptr;
    bool         built          = false;
    int          segment_count  = 0;
    WallSegment  segments[kMaxSegments];
    int          corridor_count = 0;
    CorridorCell corridors[kMaxCorridors];
    int          junction_count = 0;
    JunctionNode junctions[kMaxJunctions];
};

Graph g_graph;

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

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

// Project a world-space point onto the line through `s` along its
// direction; return the parameter `t` (distance from s.a, signed
// along s.dir). Also returns the perpendicular signed distance.
void ProjectPointOnto(const WallSegment& s, float px, float py,
                      float& outT, float& outPerp) {
    float dx = px - s.a.x;
    float dy = py - s.a.y;
    outT    = dx * s.dirx + dy * s.diry;
    outPerp = dx * (-s.diry) + dy * s.dirx;  // 90° CCW rotation of dir
}

// Compute the 1D interval [start, end] that's the *intersection*
// of the projections of A and B onto a shared axis (direction
// axisDir originating at origin). Returns false if no overlap.
bool ProjectionOverlap(const WallSegment& a, const WallSegment& b,
                       float originX, float originY,
                       float axisX, float axisY,
                       float& outStart, float& outEnd) {
    auto proj = [&](const Vector& p) -> float {
        return (p.x - originX) * axisX + (p.y - originY) * axisY;
    };
    float a_lo = proj(a.a), a_hi = proj(a.b);
    if (a_lo > a_hi) { float t = a_lo; a_lo = a_hi; a_hi = t; }
    float b_lo = proj(b.a), b_hi = proj(b.b);
    if (b_lo > b_hi) { float t = b_lo; b_lo = b_hi; b_hi = t; }
    outStart = a_lo > b_lo ? a_lo : b_lo;
    outEnd   = a_hi < b_hi ? a_hi : b_hi;
    return outEnd > outStart;
}

// 4-way cardinal name from a 2D unit-ish vector. Picks the dominant
// axis (|dx| vs |dy|). Caller already knows the engine's frame
// (0° = +X = East). Returns a localized Id.
acc::strings::Id CardinalFromVector(float dx, float dy) {
    using acc::strings::Id;
    if (std::fabs(dx) > std::fabs(dy)) {
        return dx >= 0.0f ? Id::DirEast : Id::DirWest;
    }
    return dy >= 0.0f ? Id::DirNorth : Id::DirSouth;
}

// Render the corridor's localized label into c.label.
// Format reused from the room-cache classifier so the user experience
// stays consistent.
void RenderCorridorLabel(CorridorCell& c) {
    using acc::strings::Id;
    bool axisIsEW = std::fabs(c.axis_x) > std::fabs(c.axis_y);
    const char* axisStr = acc::strings::Get(
        axisIsEW ? Id::AxisEastWest : Id::AxisNorthSouth);
    const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridor);
    if (fmt && fmt[0] && axisStr && axisStr[0]) {
        std::snprintf(c.label, sizeof(c.label), fmt, axisStr, c.width);
    } else {
        c.label[0] = '\0';
    }
    int kind = axisIsEW ? 4 : 3;
    int w    = static_cast<int>(c.width + 0.5f);
    c.sig = (kind & 0xff) | ((w & 0xff) << 8);
}

// Append a direction word into a comma-separated list, growing dirList
// until it's full. Returns the new length.
size_t AppendDirWord(char* dirList, size_t bufSize, size_t dirLen,
                     acc::strings::Id dirId) {
    const char* word = acc::strings::Get(dirId);
    if (!word || !word[0]) return dirLen;
    if (dirLen > 0 && dirLen + 2 < bufSize) {
        dirList[dirLen++] = ',';
        dirList[dirLen++] = ' ';
        dirList[dirLen]   = '\0';
    }
    size_t remaining = bufSize > dirLen ? bufSize - dirLen : 0;
    if (remaining == 0) return dirLen;
    int n = std::snprintf(dirList + dirLen, remaining, "%s", word);
    if (n > 0) {
        size_t advanced = static_cast<size_t>(n) < remaining
                              ? static_cast<size_t>(n)
                              : (remaining > 0 ? remaining - 1 : 0);
        dirLen += advanced;
    }
    return dirLen;
}

// Render a junction node's label. Degree 1 → "Sackgasse, {dir}".
// Degree ≥ 3 → "Kreuzung, {dirs}". Degree 2 → no label.
void RenderJunctionLabel(JunctionNode& n) {
    using acc::strings::Id;
    if (n.corridor_count == 2) {
        n.label[0] = '\0';
        n.sig = 0;
        return;
    }

    if (n.corridor_count == 1) {
        Id dirId = CardinalFromVector(n.corridor_dir_x[0],
                                      n.corridor_dir_y[0]);
        const char* dir = acc::strings::Get(dirId);
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
        if (fmt && fmt[0] && dir && dir[0]) {
            std::snprintf(n.label, sizeof(n.label), fmt, dir);
        } else {
            n.label[0] = '\0';
        }
        n.sig = 5 | (static_cast<int>(dirId) << 8);
        return;
    }

    // degree ≥ 3 — collect unique cardinal directions.
    char dirList[96] = {0};
    size_t dirLen = 0;
    int mask = 0;
    for (int i = 0; i < n.corridor_count; ++i) {
        Id dirId = CardinalFromVector(n.corridor_dir_x[i],
                                      n.corridor_dir_y[i]);
        int bit = 0;
        switch (dirId) {
            case Id::DirNorth: bit = 0; break;
            case Id::DirEast:  bit = 1; break;
            case Id::DirSouth: bit = 2; break;
            case Id::DirWest:  bit = 3; break;
            default: continue;
        }
        if (mask & (1 << bit)) continue;
        mask |= (1 << bit);
        dirLen = AppendDirWord(dirList, sizeof(dirList), dirLen, dirId);
    }
    const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
    if (fmt && fmt[0] && dirList[0] != '\0') {
        std::snprintf(n.label, sizeof(n.label), fmt, dirList);
    } else {
        n.label[0] = '\0';
    }
    n.sig = 6 | ((mask & 0xff) << 8) | ((n.corridor_count & 0xff) << 16);
}

// Phase 3: cluster corridor endpoints into junction nodes.
void BuildJunctionNodes() {
    g_graph.junction_count = 0;
    for (int i = 0; i < g_graph.corridor_count; ++i) {
        const CorridorCell& c = g_graph.corridors[i];
        // Two endpoints along axis at overlap_start / overlap_end.
        struct EP {
            float x, y;
            float dir_x, dir_y;   // direction from endpoint toward corridor centre
        };
        EP eps[2];
        eps[0].x = c.midline_x + c.axis_x * c.overlap_start;
        eps[0].y = c.midline_y + c.axis_y * c.overlap_start;
        eps[0].dir_x = c.axis_x;   // start endpoint points toward +axis (centre)
        eps[0].dir_y = c.axis_y;
        eps[1].x = c.midline_x + c.axis_x * c.overlap_end;
        eps[1].y = c.midline_y + c.axis_y * c.overlap_end;
        eps[1].dir_x = -c.axis_x;  // end endpoint points toward -axis
        eps[1].dir_y = -c.axis_y;

        for (int e = 0; e < 2; ++e) {
            // Find an existing node within cluster radius.
            int found = -1;
            float bestD2 = kJunctionClusterRadiusM *
                           kJunctionClusterRadiusM;
            for (int n = 0; n < g_graph.junction_count; ++n) {
                float dx = g_graph.junctions[n].pos_x - eps[e].x;
                float dy = g_graph.junctions[n].pos_y - eps[e].y;
                float d2 = dx * dx + dy * dy;
                if (d2 < bestD2) {
                    bestD2 = d2;
                    found  = n;
                }
            }
            JunctionNode* node = nullptr;
            if (found < 0) {
                if (g_graph.junction_count >= kMaxJunctions) continue;
                node = &g_graph.junctions[g_graph.junction_count++];
                node->pos_x = eps[e].x;
                node->pos_y = eps[e].y;
                node->corridor_count = 0;
                node->label[0] = '\0';
                node->sig = 0;
            } else {
                node = &g_graph.junctions[found];
                // Re-centre as running average.
                float n_old = static_cast<float>(node->corridor_count);
                float n_new = n_old + 1.0f;
                node->pos_x = (node->pos_x * n_old + eps[e].x) / n_new;
                node->pos_y = (node->pos_y * n_old + eps[e].y) / n_new;
            }
            if (node->corridor_count <
                JunctionNode::kMaxCorridorsPerNode) {
                int slot = node->corridor_count++;
                node->corridor_idx[slot]   = i;
                node->corridor_dir_x[slot] = eps[e].dir_x;
                node->corridor_dir_y[slot] = eps[e].dir_y;
            }
        }
    }
}

// Point-in-corridor test using the corridor's axis + perp frame.
bool PointInCorridor(const CorridorCell& c, float px, float py,
                     float& outPerpDist) {
    float dx = px - c.midline_x;
    float dy = py - c.midline_y;
    float axisT = dx * c.axis_x + dy * c.axis_y;
    float perpT = dx * c.perp_x + dy * c.perp_y;
    if (axisT < c.overlap_start || axisT > c.overlap_end) return false;
    float halfW = c.width * 0.5f;
    if (perpT < -halfW || perpT > halfW) return false;
    outPerpDist = std::fabs(perpT);
    return true;
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

    // ---------------------------------------------------------------
    // Phase 1: consume Pillar 1's wall-surface clustering.
    //
    // The change-detector already runs union-find over the raw wall
    // edges at area-load time, merging collinear endpoint-sharing
    // edges across rooms into logical wall surfaces (see the "Wall
    // surface clustering" block in spatial_change_detector.cpp).
    // Each surface reduces to a single straight segment with two
    // extreme endpoints + unit direction + length — exactly what
    // walltopo previously rebuilt in-house with a weaker O(N²)
    // iterative-merge + dedup pipeline.
    //
    // Pillar 1 produces noticeably cleaner output than the old
    // in-house pass: same input (462 edges on Apartments) reduces to
    // 121 surfaces vs walltopo's 237 segments — roughly 2× more
    // aggressive merging, achieved by union-find with a looser 15°
    // collinearity threshold instead of 10° iterative sweeps.
    //
    // We still self-gate on the cache being ready: change_detector::
    // Tick runs BEFORE transitions::Tick in core_tick.cpp, but on
    // the area-change tick the surface table can still be empty if
    // BuildAreaWallCache faulted partway through; the retry path in
    // transitions::Tick re-calls BuildForArea each tick until both
    // caches populate.
    // ---------------------------------------------------------------
    int surfaceCount = acc::spatial::change_detector::GetWallSurfaceCount();
    if (surfaceCount <= 0) {
        acclog::Write("WallTopo",
                      "BuildForArea: Pillar 1 wall surfaces not ready — "
                      "leaving graph empty (will retry on next call)");
        return;
    }

    int segCount   = 0;
    int skippedShort      = 0;
    int skippedDegenerate = 0;
    for (int s = 0; s < surfaceCount && segCount < kMaxSegments; ++s) {
        acc::spatial::change_detector::WallSurfaceDesc desc;
        if (!acc::spatial::change_detector::GetWallSurfaceDesc(s, desc)) {
            ++skippedDegenerate;  // closed-loop / branching / overflow
            continue;
        }
        if (desc.length < kMinSegmentLengthM) {
            ++skippedShort;
            continue;
        }
        WallSegment& seg = g_graph.segments[segCount++];
        seg.a      = desc.a;
        seg.b      = desc.b;
        seg.dirx   = desc.dir_x;
        seg.diry   = desc.dir_y;
        seg.length = desc.length;
    }
    g_graph.segment_count = segCount;
    if (surfaceCount > kMaxSegments) {
        acclog::Write("WallTopo",
                      "BuildForArea: surface count %d exceeds cap %d — "
                      "truncating", surfaceCount, kMaxSegments);
    }

    acclog::Write("WallTopo",
                  "BuildForArea: area=%p consumed %d Pillar 1 surfaces -> "
                  "%d segments (skipped %d short, %d degenerate)",
                  area, surfaceCount, g_graph.segment_count,
                  skippedShort, skippedDegenerate);

    // ---------------------------------------------------------------
    // Phase 4: parallel-pair corridor detection.
    //
    // For each unordered pair (i, j) of segments:
    //   - test parallelism (within kCorridorParallelTolDeg)
    //   - compute perpendicular distance between their lines
    //   - if width is in [kCorridorMinWidthM, kCorridorMaxWidthM]
    //   - test projection overlap along the shared axis
    //   - if overlap ≥ kCorridorMinOverlapM → record corridor
    //
    // O(N²) over segments — fine for the few-dozen-segment scale of
    // K1 areas.
    // ---------------------------------------------------------------
    g_graph.corridor_count = 0;
    for (int i = 0; i < g_graph.segment_count; ++i) {
        const WallSegment& sa = g_graph.segments[i];
        for (int j = i + 1; j < g_graph.segment_count; ++j) {
            if (g_graph.corridor_count >= kMaxCorridors) break;
            const WallSegment& sb = g_graph.segments[j];

            if (!NearCollinear(sa.dirx, sa.diry, sb.dirx, sb.diry,
                               kCorridorParallelTolDeg)) {
                continue;
            }

            // Perpendicular distance between the two parallel lines.
            // Project sb.a onto sa's axis to get the orthogonal
            // component; that's the signed perpendicular distance.
            float t = 0.0f, perp = 0.0f;
            ProjectPointOnto(sa, sb.a.x, sb.a.y, t, perp);
            float width = std::fabs(perp);
            if (width < kCorridorMinWidthM || width > kCorridorMaxWidthM) {
                continue;
            }

            // Choose the corridor axis = sa.dir. Project both
            // segments' endpoints onto this axis (origin = sa.a) and
            // intersect the parameter ranges.
            float start = 0.0f, end = 0.0f;
            if (!ProjectionOverlap(sa, sb,
                                   sa.a.x, sa.a.y, sa.dirx, sa.diry,
                                   start, end)) {
                continue;
            }
            float overlap = end - start;
            if (overlap < kCorridorMinOverlapM) continue;

            // Record corridor. Midline = midpoint between the two
            // walls at the projected origin. Perpendicular unit
            // points from sa toward sb.
            float perpSign = (perp >= 0.0f) ? 1.0f : -1.0f;
            float perpX = -sa.diry * perpSign;
            float perpY =  sa.dirx * perpSign;

            CorridorCell& c = g_graph.corridors[g_graph.corridor_count++];
            c.seg_a = i;
            c.seg_b = j;
            c.axis_x = sa.dirx;
            c.axis_y = sa.diry;
            c.perp_x = perpX;
            c.perp_y = perpY;
            c.width  = width;
            c.midline_x = sa.a.x + perpX * (width * 0.5f);
            c.midline_y = sa.a.y + perpY * (width * 0.5f);
            c.overlap_start = start;
            c.overlap_end   = end;
            c.length        = overlap;
        }
        if (g_graph.corridor_count >= kMaxCorridors) {
            acclog::Write("WallTopo",
                          "BuildForArea: corridor cap %d reached, "
                          "remaining segments not paired",
                          kMaxCorridors);
            break;
        }
    }

    acclog::Write("WallTopo",
                  "BuildForArea: phase 4 found %d corridor candidates",
                  g_graph.corridor_count);

    // ---------------------------------------------------------------
    // Phase 4b: render labels for each corridor.
    // ---------------------------------------------------------------
    for (int i = 0; i < g_graph.corridor_count; ++i) {
        RenderCorridorLabel(g_graph.corridors[i]);
    }

    // ---------------------------------------------------------------
    // Phase 5: cluster corridor endpoints into junction nodes; render
    // each node's label (dead-end / junction / silent pass-through).
    // ---------------------------------------------------------------
    BuildJunctionNodes();
    for (int i = 0; i < g_graph.junction_count; ++i) {
        RenderJunctionLabel(g_graph.junctions[i]);
    }
    acclog::Write("WallTopo",
                  "BuildForArea: phase 5 clustered %d junction nodes",
                  g_graph.junction_count);

    g_graph.built = true;
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    acclog::Write("WallTopo",
                  "graph dump area=%p segments=%d corridors=%d "
                  "junctions=%d",
                  g_graph.area_owner, g_graph.segment_count,
                  g_graph.corridor_count, g_graph.junction_count);
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

    // Dump corridors — every candidate, since their count is bounded.
    int corLimit = g_graph.corridor_count > 32 ? 32 : g_graph.corridor_count;
    for (int i = 0; i < corLimit; ++i) {
        const CorridorCell& c = g_graph.corridors[i];
        acclog::Write("WallTopo",
                      "  cor[%d] segs=%d,%d width=%.2fm len=%.2fm "
                      "mid=(%.2f,%.2f) axis=(%.2f,%.2f)",
                      i, c.seg_a, c.seg_b, c.width, c.length,
                      c.midline_x, c.midline_y, c.axis_x, c.axis_y);
    }
    if (g_graph.corridor_count > 32) {
        acclog::Write("WallTopo",
                      "  ... (%d more corridors truncated)",
                      g_graph.corridor_count - 32);
    }

    // Dump junctions — show degree distribution + every labelled node.
    int deg1 = 0, deg2 = 0, deg3plus = 0;
    for (int i = 0; i < g_graph.junction_count; ++i) {
        int c = g_graph.junctions[i].corridor_count;
        if      (c == 1) ++deg1;
        else if (c == 2) ++deg2;
        else if (c >= 3) ++deg3plus;
    }
    acclog::Write("WallTopo",
                  "junction degrees: dead-end=%d pass-through=%d "
                  "junction=%d",
                  deg1, deg2, deg3plus);
    int juncLimit = g_graph.junction_count > 32
                        ? 32 : g_graph.junction_count;
    for (int i = 0; i < juncLimit; ++i) {
        const JunctionNode& n = g_graph.junctions[i];
        if (n.label[0] == '\0') continue;
        acclog::Write("WallTopo",
                      "  junc[%d] deg=%d pos=(%.2f,%.2f) label=\"%s\"",
                      i, n.corridor_count, n.pos_x, n.pos_y, n.label);
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    outSig = 0;
    if (!HasGraphForArea(area)) return false;

    // 1. Corridor footprint — point-in-rectangle. If multiple match
    //    (corridors can overlap at junctions), pick the one whose
    //    midline is closest.
    int   bestCor      = -1;
    float bestCorPerp  = 1e30f;
    for (int i = 0; i < g_graph.corridor_count; ++i) {
        float perp = 0.0f;
        if (PointInCorridor(g_graph.corridors[i],
                            worldPos.x, worldPos.y, perp)) {
            if (perp < bestCorPerp) {
                bestCorPerp = perp;
                bestCor     = i;
            }
        }
    }
    if (bestCor >= 0) {
        const CorridorCell& c = g_graph.corridors[bestCor];
        if (c.label[0] != '\0') {
            std::snprintf(outBuf, bufSize, "%s", c.label);
            outSig = c.sig;
            return true;
        }
    }

    // 2. Nearest junction node within kJunctionLookupRadiusM. This
    //    catches the case where the player sits at the meeting point
    //    of multiple corridors (where the corridor rectangles end /
    //    overlap) — they should hear the junction label, not the
    //    nearest corridor's.
    int   bestJunc = -1;
    float bestJuncD2 = kJunctionLookupRadiusM * kJunctionLookupRadiusM;
    for (int i = 0; i < g_graph.junction_count; ++i) {
        const JunctionNode& n = g_graph.junctions[i];
        if (n.label[0] == '\0') continue;  // degree-2 silent nodes
        float dx = worldPos.x - n.pos_x;
        float dy = worldPos.y - n.pos_y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestJuncD2) {
            bestJuncD2 = d2;
            bestJunc   = i;
        }
    }
    if (bestJunc >= 0) {
        const JunctionNode& n = g_graph.junctions[bestJunc];
        std::snprintf(outBuf, bufSize, "%s", n.label);
        outSig = n.sig;
        return true;
    }

    // 3. Fall back to "Offene Fläche" — the player is in space the
    //    decomposition didn't identify as a corridor or a junction.
    const char* fallback =
        acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
    if (fallback && fallback[0]) {
        std::snprintf(outBuf, bufSize, "%s", fallback);
        outSig = 2;
        return true;
    }
    return false;
}

}  // namespace acc::wall_topology
