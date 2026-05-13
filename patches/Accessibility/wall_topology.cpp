// Wall-topology decomposition — see wall_topology.h for goal + algorithm.
//
// EXPERIMENTAL — alternative-direction-calculation-system branch.
//
// Path 3 (2026-05-13) consumes the engine's per-area nav graph (the same
// path_points / path_connections fields the engine itself uses for AI
// pathfinding) and classifies each node by its CSR-adjacency degree:
//   - degree 1               → dead end pointing at its single neighbour
//   - degree 2, same room    → corridor (axis defined by both neighbours)
//   - degree 2, cross-room   → transition (doorway between two rooms)
//   - degree ≥ 3             → junction (comma-separated octant list)
//
// Previous revisions (face-graph flood-fill, parallel-pair sweep) all
// failed empirically on K1's authoring style — see commit history on this
// branch. The nav graph is what BioWare ships, and the engine itself
// trusts it for path solving, so we're operating on the same ground truth
// the level designers were aware of.

#include "wall_topology.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_navgraph.h"
#include "log.h"
#include "strings.h"

namespace acc::wall_topology {

namespace {

// ---------------------------------------------------------------------
// Tunable parameters.
// ---------------------------------------------------------------------

// Re-use the navgraph cap so per-node arrays line up with the snapshot.
constexpr int kMaxNodes = acc::engine::navgraph::kMaxNodes;

// Snap radius for runtime LookupAt: any player position more than this
// many world units (KOTOR = metres) from the nearest graph node falls
// back to "Offene Fläche". 15m matches the typical room-diagonal in
// vanilla content while still catching the central plaza / exterior
// case where the nav graph is sparse. Tune from log evidence.
constexpr float kMaxSnapM = 15.0f;

// Junction-merge thresholds. Two directly-connected nav-graph nodes
// that are both degree-≥3 and pass these gates are merged into one
// composite cluster: the cluster's announce describes the union of
// their external exits, computed from the centroid of the cluster
// members. K1 places nav-graph nodes densely around hub areas (Dias
// Apartment at Taris Apartments has 2 junctions 6m apart, both reading
// as Kreuzung in sequence); merging collapses these to one perceptual
// "hub" announce.
//
// - kMergeMaxDistanceM: planar distance gate. 8m matches K1's typical
//   inter-junction spacing in hub clusters; wider corridors should not
//   merge their endpoints.
// - kMergeMaxZM: vertical gate. Stops multi-floor stacks (Sith base,
//   Endar Spire decks) from collapsing vertically-aligned junctions.
constexpr float kMergeMaxDistanceM = 8.0f;
constexpr float kMergeMaxZM        = 1.0f;

// Kind values now live in the public header (wall_topology.h) so
// transitions.cpp can branch on Platz for the delayed-announce path.
// Aliases here keep the local code compact.
constexpr int kKindDeadEnd  = KindDeadEnd;
constexpr int kKindCorridor = KindCorridor;
constexpr int kKindJunction = KindJunction;
constexpr int kKindOpenArea = KindOpenArea;
constexpr int kKindPlatz    = KindPlatz;

// ---------------------------------------------------------------------
// Cached state for the current area.
// ---------------------------------------------------------------------

struct AreaGraph {
    void*   area_owner   = nullptr;
    bool    built        = false;
    int     node_count   = 0;
    Vector  node_pos    [kMaxNodes];
    char    node_label  [kMaxNodes][96];
    int     node_sig    [kMaxNodes];
    int     node_kind   [kMaxNodes];  // see kKind* constants
};

AreaGraph g_graph;

// ---------------------------------------------------------------------
// Direction helpers.
// ---------------------------------------------------------------------

// 8-way octant classifier. Engine frame: +X = East, +Y = North. Returns
// the Id corresponding to the octant the (dx,dy) vector points into.
acc::strings::Id OctantFromVector(float dx, float dy) {
    using acc::strings::Id;
    // atan2 gives radians in (-pi, pi] measured from +X (East) counter-
    // clockwise. Convert to a 0..8 bucket where 0 = East, 1 = Northeast,
    // …, 7 = Southeast.
    float theta = std::atan2(dy, dx);
    // Snap to nearest 45° bucket. Add half-bucket for round-to-nearest.
    constexpr float kPi  = 3.14159265358979323846f;
    constexpr float kStep = kPi / 4.0f;  // 45 degrees
    int bucket = static_cast<int>(std::floor((theta + kStep * 0.5f) / kStep));
    bucket = ((bucket % 8) + 8) % 8;
    switch (bucket) {
        case 0: return Id::DirEast;
        case 1: return Id::DirNortheast;
        case 2: return Id::DirNorth;
        case 3: return Id::DirNorthwest;
        case 4: return Id::DirWest;
        case 5: return Id::DirSouthwest;
        case 6: return Id::DirSouth;
        case 7: return Id::DirSoutheast;
    }
    return Id::DirEast;
}

// "Norded-out" corridor axis. A corridor axis is a line, not a vector —
// both endpoints are valid. To avoid emitting "Korridor Süd" when the
// player would experience the same axis as "Korridor Nord" from the
// other end, always flip the vector to point toward the northern half
// of the world (engine +Y). At dy == 0 we tie-break to East so a pure
// East-West corridor always reads as "Korridor Ost", never "Korridor
// West". Documented here in source for the inevitable "why does it
// never say south?" question.
acc::strings::Id NordedOutAxisOctant(float dx, float dy) {
    if (dy < 0.0f || (dy == 0.0f && dx < 0.0f)) {
        dx = -dx;
        dy = -dy;
    }
    return OctantFromVector(dx, dy);
}

// Append a direction word to a comma-separated list. Returns the new
// length of `dirList`.
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

// Map an Id::Dir* to a stable bit index 0..7 (for dedup masks). Order
// matches OctantFromVector's bucket numbering: E=0, NE=1, N=2, NW=3,
// W=4, SW=5, S=6, SE=7.
int OctantBit(acc::strings::Id dir) {
    using acc::strings::Id;
    switch (dir) {
        case Id::DirEast:      return 0;
        case Id::DirNortheast: return 1;
        case Id::DirNorth:     return 2;
        case Id::DirNorthwest: return 3;
        case Id::DirWest:      return 4;
        case Id::DirSouthwest: return 5;
        case Id::DirSouth:     return 6;
        case Id::DirSoutheast: return 7;
        default: return -1;
    }
}

// Reverse of OctantBit: bit index → direction Id. Used when emitting
// directions in canonical order rather than encounter order.
acc::strings::Id BitToOctant(int bit) {
    using acc::strings::Id;
    switch (bit) {
        case 0: return Id::DirEast;
        case 1: return Id::DirNortheast;
        case 2: return Id::DirNorth;
        case 3: return Id::DirNorthwest;
        case 4: return Id::DirWest;
        case 5: return Id::DirSouthwest;
        case 6: return Id::DirSouth;
        case 7: return Id::DirSoutheast;
    }
    return Id::DirEast;
}

// Order in which we emit direction words at junctions / plazas. N
// first then clockwise (NE, E, SE, S, SW, W, NW) — feels natural for
// a player who orients on a compass and reads exits clockwise.
constexpr int kOctantEmitOrder[8] = {
    2, 1, 0, 7, 6, 5, 4, 3
};

// ---------------------------------------------------------------------
// Cluster classification (degree-driven, with junction-merge support).
// ---------------------------------------------------------------------

// Per-node degree from the snapshot. Tiny helper — every classifier
// path needs this and it keeps the call sites self-documenting.
int Degree(const acc::engine::navgraph::NavGraphSnapshot& g, int node) {
    int lo = 0, hi = 0;
    acc::engine::navgraph::NeighbourRange(g, node, lo, hi);
    return hi - lo;
}

// Union-find over node ids. Used to merge directly-connected junctions
// into single perceptual clusters. Reused for every BuildForArea
// invocation (reset at the start of the function).
int s_uf_parent[kMaxNodes];

int UFFind(int x) {
    while (s_uf_parent[x] != x) {
        s_uf_parent[x] = s_uf_parent[s_uf_parent[x]];  // path halving
        x = s_uf_parent[x];
    }
    return x;
}

void UFUnite(int a, int b) {
    int ra = UFFind(a);
    int rb = UFFind(b);
    if (ra == rb) return;
    // Use smaller index as the cluster root so the dump output is
    // stable across runs (the root is also used as the cluster's id).
    if (ra < rb) s_uf_parent[rb] = ra;
    else         s_uf_parent[ra] = rb;
}

// Append one direction entry to `dirList`. When `markDeadEnd` is set,
// the direction word is wrapped via FmtMapCursorJunctionDeadEndExit
// ("%s (Sackgasse)") so the user knows that exit doesn't lead onward
// (option 1 from the 2026-05-13 Dias-cluster discussion — junctions
// whose edges include degree-1 stubs should call them out).
size_t AppendDirEntry(char* dirList, size_t bufSize, size_t dirLen,
                      acc::strings::Id dirId, bool markDeadEnd) {
    const char* word = acc::strings::Get(dirId);
    if (!word || !word[0]) return dirLen;

    char entry[64];
    if (markDeadEnd) {
        const char* fmt = acc::strings::Get(
            acc::strings::Id::FmtMapCursorJunctionDeadEndExit);
        if (fmt && fmt[0]) {
            std::snprintf(entry, sizeof(entry), fmt, word);
        } else {
            std::snprintf(entry, sizeof(entry), "%s", word);
        }
    } else {
        std::snprintf(entry, sizeof(entry), "%s", word);
    }

    if (dirLen > 0 && dirLen + 2 < bufSize) {
        dirList[dirLen++] = ',';
        dirList[dirLen++] = ' ';
        dirList[dirLen]   = '\0';
    }
    size_t remaining = bufSize > dirLen ? bufSize - dirLen : 0;
    if (remaining == 0) return dirLen;
    int n = std::snprintf(dirList + dirLen, remaining, "%s", entry);
    if (n > 0) {
        size_t advanced = static_cast<size_t>(n) < remaining
                              ? static_cast<size_t>(n)
                              : (remaining > 0 ? remaining - 1 : 0);
        dirLen += advanced;
    }
    return dirLen;
}

// Classify a cluster (1 or more nodes) by its centroid and the list of
// external neighbour nodes (nodes outside this cluster that share an
// edge with some member). `clusterSize` is the count of nodes in the
// cluster — used only to distinguish singleton junctions ("Kreuzung")
// from merged ones ("Platz"). Renders the label + kind + sig into the
// out-params.
//
//   externalCount == 0  → isolated / open area
//   externalCount == 1  → dead-end (direction = centroid → that exit)
//   externalCount == 2  → corridor (axis between the two exits,
//                                   cardinal cases use "Nord-Süd" /
//                                   "Ost-West" axis words)
//   externalCount >= 3  → junction (singleton) or Platz (merged).
//                         8-octant bucketing with passable-wins:
//                         per-octant aggregation marks the octant as
//                         dead-end only when EVERY exit in it leads
//                         to a degree-1 neighbour. Any passable exit
//                         in the octant suppresses the marker — fixes
//                         the order-dependent first-wins bug from the
//                         initial revision.
void ClassifyCluster(const acc::engine::navgraph::NavGraphSnapshot& g,
                     const Vector& centroid, int clusterSize,
                     const int* externalNbs, int externalCount,
                     char* outLabel, size_t outLabelSize,
                     int& outKind, int& outSig) {
    using acc::strings::Id;
    if (outLabelSize > 0) outLabel[0] = '\0';
    outKind = kKindOpenArea;
    outSig  = kKindOpenArea;
    if (outLabelSize == 0) return;
    int n = static_cast<int>(g.nodes.size());

    if (externalCount == 0) {
        const char* s = acc::strings::Get(Id::MapCursorOpenArea);
        if (s && s[0]) std::snprintf(outLabel, outLabelSize, "%s", s);
        return;
    }
    if (externalCount == 1) {
        int nb = externalNbs[0];
        if (nb < 0 || nb >= n) return;
        float dx = g.nodes[nb].pos.x - centroid.x;
        float dy = g.nodes[nb].pos.y - centroid.y;
        Id dir = OctantFromVector(dx, dy);
        const char* word = acc::strings::Get(dir);
        const char* fmt  = acc::strings::Get(Id::FmtMapCursorDeadEnd);
        if (fmt && fmt[0] && word && word[0]) {
            std::snprintf(outLabel, outLabelSize, fmt, word);
        }
        outKind = kKindDeadEnd;
        int octBit = OctantBit(dir);
        outSig = (kKindDeadEnd & 0xff) | ((octBit & 0xff) << 8);
        return;
    }
    if (externalCount == 2) {
        int nbA = externalNbs[0];
        int nbB = externalNbs[1];
        if (nbA < 0 || nbA >= n || nbB < 0 || nbB >= n) return;
        float dx = g.nodes[nbB].pos.x - g.nodes[nbA].pos.x;
        float dy = g.nodes[nbB].pos.y - g.nodes[nbA].pos.y;
        Id dir = NordedOutAxisOctant(dx, dy);
        Id wordId = dir;
        if (dir == Id::DirNorth) wordId = Id::AxisNorthSouth;
        else if (dir == Id::DirEast) wordId = Id::AxisEastWest;
        const char* word = acc::strings::Get(wordId);
        const char* fmt  = acc::strings::Get(Id::FmtMapCursorCorridorDir);
        if (fmt && fmt[0] && word && word[0]) {
            std::snprintf(outLabel, outLabelSize, fmt, word);
        }
        outKind = kKindCorridor;
        int octBit = OctantBit(dir);
        outSig = (kKindCorridor & 0xff) | ((octBit & 0xff) << 8);
        return;
    }

    // 3+ external edges = junction or Platz. First pass: per-octant
    // aggregation. octantHasExit[bit] is set whenever any external
    // neighbour bucket-classifies into `bit`. octantAllDeadEnd[bit]
    // starts true and gets cleared the moment a non-dead-end exit
    // appears in that octant — so it stays true only when EVERY exit
    // in the bucket is a degree-1 stub. This fixes the order-dependent
    // first-wins issue where a passable exit could be silently masked
    // by a co-located dead-end (or vice versa).
    bool octantHasExit[8]    = {false, false, false, false,
                                false, false, false, false};
    bool octantAllDeadEnd[8] = {true, true, true, true,
                                true, true, true, true};
    for (int k = 0; k < externalCount; ++k) {
        int nb = externalNbs[k];
        if (nb < 0 || nb >= n) continue;
        float dx = g.nodes[nb].pos.x - centroid.x;
        float dy = g.nodes[nb].pos.y - centroid.y;
        Id dir = OctantFromVector(dx, dy);
        int bit = OctantBit(dir);
        if (bit < 0) continue;
        octantHasExit[bit] = true;
        if (Degree(g, nb) != 1) octantAllDeadEnd[bit] = false;
    }

    // Second pass: emit octants in canonical order (N, NE, E, SE, S,
    // SW, W, NW). Stable across runs; matches the way a compass-oriented
    // player scans for exits clockwise.
    char dirList[96] = {0};
    size_t dirLen = 0;
    int mask = 0;
    int deadEndMask = 0;
    for (int idx = 0; idx < 8; ++idx) {
        int bit = kOctantEmitOrder[idx];
        if (!octantHasExit[bit]) continue;
        mask |= (1 << bit);
        bool markDeadEnd = octantAllDeadEnd[bit];
        if (markDeadEnd) deadEndMask |= (1 << bit);
        Id dirId = BitToOctant(bit);
        dirLen = AppendDirEntry(dirList, sizeof(dirList), dirLen,
                                dirId, markDeadEnd);
    }

    bool isPlatz = clusterSize > 1;
    Id fmtId = isPlatz ? Id::FmtMapCursorPlazaDirs
                       : Id::FmtMapCursorJunctionDirs;
    int kind = isPlatz ? kKindPlatz : kKindJunction;

    const char* fmt = acc::strings::Get(fmtId);
    if (fmt && fmt[0] && dirList[0] != '\0') {
        std::snprintf(outLabel, outLabelSize, fmt, dirList);
    } else {
        const char* bare = acc::strings::Get(Id::MapCursorJunction);
        if (bare && bare[0]) {
            std::snprintf(outLabel, outLabelSize, "%s", bare);
        }
    }
    outKind = kind;
    outSig  = (kind & 0xff) |
              ((mask & 0xff) << 8) |
              ((deadEndMask & 0xff) << 16);
}

}  // namespace

void Reset() {
    g_graph.area_owner = nullptr;
    g_graph.built      = false;
    g_graph.node_count = 0;
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

    acc::engine::navgraph::NavGraphSnapshot g;
    if (!acc::engine::navgraph::SnapshotNavGraph(area, g)) {
        acclog::Write("WallTopo",
                      "BuildForArea: nav graph empty / unreadable (areaPtr=%p) "
                      "— leaving graph unbuilt", area);
        return;
    }

    int n = static_cast<int>(g.nodes.size());
    if (n > kMaxNodes) n = kMaxNodes;
    g_graph.node_count = n;
    for (int i = 0; i < n; ++i) {
        g_graph.node_pos[i]      = g.nodes[i].pos;
        g_graph.node_label[i][0] = '\0';
        g_graph.node_sig[i]      = 0;
        g_graph.node_kind[i]     = kKindOpenArea;
    }

    // Pass 1: union-find merge of directly-connected degree-≥3 nodes
    // within the distance + z gates. The cluster classifier in pass 2
    // then treats each cluster (singletons included) as one perceptual
    // place — adjacent same-octant junctions collapse to a single
    // "Kreuzung" announce with the union of their external exits.
    for (int i = 0; i < n; ++i) s_uf_parent[i] = i;
    int mergeEdges = 0;
    for (int i = 0; i < n; ++i) {
        if (Degree(g, i) < 3) continue;
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        for (int e = lo; e < hi; ++e) {
            int j = static_cast<int>(g.conns[e]);
            if (j < 0 || j >= n) continue;
            if (j <= i) continue;  // process each unordered pair once
            if (Degree(g, j) < 3) continue;
            float dx = g.nodes[i].pos.x - g.nodes[j].pos.x;
            float dy = g.nodes[i].pos.y - g.nodes[j].pos.y;
            float dz = g.nodes[i].pos.z - g.nodes[j].pos.z;
            if (dx * dx + dy * dy >
                kMergeMaxDistanceM * kMergeMaxDistanceM) continue;
            if (std::fabs(dz) > kMergeMaxZM) continue;
            UFUnite(i, j);
            ++mergeEdges;
        }
    }

    // Pass 2: per-cluster classification. For each cluster root (the
    // smallest node id in its union-find class), compute the centroid,
    // collect external neighbours (dedup by node id), classify via
    // ClassifyCluster, and write the result to every member.
    int clusters = 0, multiNodeClusters = 0;
    int deadEnds = 0, corridors = 0, junctions = 0, openAreas = 0;
    for (int root = 0; root < n; ++root) {
        if (UFFind(root) != root) continue;
        ++clusters;

        // Centroid of all members.
        Vector centroid = {0.0f, 0.0f, 0.0f};
        int size = 0;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            centroid.x += g.nodes[m].pos.x;
            centroid.y += g.nodes[m].pos.y;
            centroid.z += g.nodes[m].pos.z;
            ++size;
        }
        if (size > 0) {
            centroid.x /= size;
            centroid.y /= size;
            centroid.z /= size;
        }
        if (size > 1) ++multiNodeClusters;

        // External neighbours: edges from any member to a non-member.
        // Dedup by node id so multi-edge connections to the same
        // outside node only register once.
        constexpr int kMaxExternal = 16;
        int externalNbs[kMaxExternal];
        int externalCount = 0;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(g, m, lo, hi);
            for (int e = lo; e < hi; ++e) {
                int nb = static_cast<int>(g.conns[e]);
                if (nb < 0 || nb >= n) continue;
                if (UFFind(nb) == root) continue;  // internal edge
                bool seen = false;
                for (int k = 0; k < externalCount; ++k) {
                    if (externalNbs[k] == nb) { seen = true; break; }
                }
                if (!seen && externalCount < kMaxExternal) {
                    externalNbs[externalCount++] = nb;
                }
            }
        }

        char label[96] = {0};
        int kind = kKindOpenArea, sig = 0;
        ClassifyCluster(g, centroid, size, externalNbs, externalCount,
                        label, sizeof(label), kind, sig);

        switch (kind) {
            case kKindDeadEnd:  ++deadEnds;  break;
            case kKindCorridor: ++corridors; break;
            case kKindJunction: ++junctions; break;
            case kKindPlatz:    ++junctions; break;  // tally w/ junctions
            default:            ++openAreas; break;
        }

        // Write to every cluster member.
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            std::strncpy(g_graph.node_label[m], label,
                         sizeof(g_graph.node_label[m]) - 1);
            g_graph.node_label[m][sizeof(g_graph.node_label[m]) - 1] = '\0';
            g_graph.node_kind[m] = kind;
            g_graph.node_sig[m]  = sig;
        }
    }

    g_graph.built = true;
    acclog::Write("WallTopo",
                  "BuildForArea: area=%p nodes=%d clusters=%d "
                  "merged-pairs=%d multi-node-clusters=%d "
                  "(dead=%d corridor=%d junction=%d open=%d)",
                  area, n, clusters, mergeEdges, multiNodeClusters,
                  deadEnds, corridors, junctions, openAreas);
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    for (int i = 0; i < g_graph.node_count; ++i) {
        int root = UFFind(i);
        acclog::Write("WallTopo",
                      "  node[%d] cluster=%d kind=%d sig=%d "
                      "pos=(%.1f,%.1f,%.1f) label=\"%s\"",
                      i, root, g_graph.node_kind[i], g_graph.node_sig[i],
                      g_graph.node_pos[i].x, g_graph.node_pos[i].y,
                      g_graph.node_pos[i].z, g_graph.node_label[i]);
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    outSig = 0;
    if (!HasGraphForArea(area)) return false;
    if (g_graph.node_count <= 0) return false;

    // 3D nearest-node — same-floor in single-floor areas, but the Z
    // term matters in stacked-floor maps (Sith base, Endar Spire decks,
    // Manaan). 2D nearest would snap to the wrong floor whenever the
    // nav graphs of two floors line up vertically. ~100 nodes, linear
    // scan is trivial.
    int best = -1;
    float bestSq = 1e30f;
    for (int i = 0; i < g_graph.node_count; ++i) {
        float dx = g_graph.node_pos[i].x - worldPos.x;
        float dy = g_graph.node_pos[i].y - worldPos.y;
        float dz = g_graph.node_pos[i].z - worldPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestSq) {
            bestSq = d2;
            best   = i;
        }
    }

    if (best < 0) return false;

    float bestDist = std::sqrt(bestSq);
    if (bestDist > kMaxSnapM) {
        // Outside the indexed region — fall through to "open area".
        const char* fallback =
            acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
        if (fallback && fallback[0]) {
            std::snprintf(outBuf, bufSize, "%s", fallback);
            outSig = kKindOpenArea & 0xff;
            return true;
        }
        return false;
    }

    if (g_graph.node_label[best][0] == '\0') {
        // Node has no rendered label (isolated / classification skipped)
        // — emit "Offene Fläche" rather than leaving the user silent.
        const char* fallback =
            acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
        if (fallback && fallback[0]) {
            std::snprintf(outBuf, bufSize, "%s", fallback);
            outSig = kKindOpenArea & 0xff;
            return true;
        }
        return false;
    }

    std::snprintf(outBuf, bufSize, "%s", g_graph.node_label[best]);
    outSig = g_graph.node_sig[best];
    return true;
}

}  // namespace acc::wall_topology
