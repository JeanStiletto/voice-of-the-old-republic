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

// Kind bytes packed into the low byte of `sig`. Upper bytes vary by
// kind (mask, octant index, etc.) — purely for log post-mortem.
//
// kKindTransition is deliberately retired: empirical Oberstadt data
// (patch-20260513-102829.log) showed K1's .lyt-room boundaries don't
// correspond to doorways — they're authoring units. Cross-room
// degree-2 nodes were over-firing as "Türschwelle" along a 40m
// east-west street, swallowing every corridor announce via
// text-dedup. Real doorway detection would need wall-geometry
// constriction sensing, which is a separate problem.
constexpr int kKindDeadEnd  = 0;
constexpr int kKindCorridor = 1;
constexpr int kKindJunction = 2;
constexpr int kKindOpenArea = 4;

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

// ---------------------------------------------------------------------
// Per-node classification (degree-driven).
// ---------------------------------------------------------------------

void ClassifyDeadEnd(int node, int neighbour,
                     const acc::engine::navgraph::NavGraphSnapshot& g) {
    using acc::strings::Id;
    float dx = g.nodes[neighbour].pos.x - g.nodes[node].pos.x;
    float dy = g.nodes[neighbour].pos.y - g.nodes[node].pos.y;
    Id dir = OctantFromVector(dx, dy);
    const char* word = acc::strings::Get(dir);
    const char* fmt  = acc::strings::Get(Id::FmtMapCursorDeadEnd);
    if (fmt && fmt[0] && word && word[0]) {
        std::snprintf(g_graph.node_label[node],
                      sizeof(g_graph.node_label[node]),
                      fmt, word);
    }
    g_graph.node_kind[node] = kKindDeadEnd;
    int octBit = OctantBit(dir);
    g_graph.node_sig[node] = (kKindDeadEnd & 0xff) |
                             ((octBit & 0xff) << 8);
}

void ClassifyCorridor(int node, int nbA, int nbB,
                      const acc::engine::navgraph::NavGraphSnapshot& g) {
    using acc::strings::Id;
    float dx = g.nodes[nbB].pos.x - g.nodes[nbA].pos.x;
    float dy = g.nodes[nbB].pos.y - g.nodes[nbA].pos.y;
    Id dir = NordedOutAxisOctant(dx, dy);

    // A corridor axis is a line, not a direction. For the two cardinal
    // cases swap in the proper axis word ("Nord-Süd" / "Ost-West") so
    // the user doesn't read "Korridor Ost" as a one-way passage. The
    // diagonal cases stay as their hyphenated single-octant word
    // ("Nord-Ost", "Nord-West") on user direction — the hyphenated
    // form already reads less directional than the cardinals.
    Id wordId = dir;
    if (dir == Id::DirNorth) wordId = Id::AxisNorthSouth;
    else if (dir == Id::DirEast) wordId = Id::AxisEastWest;

    const char* word = acc::strings::Get(wordId);
    const char* fmt  = acc::strings::Get(Id::FmtMapCursorCorridorDir);
    if (fmt && fmt[0] && word && word[0]) {
        std::snprintf(g_graph.node_label[node],
                      sizeof(g_graph.node_label[node]),
                      fmt, word);
    }
    g_graph.node_kind[node] = kKindCorridor;
    int octBit = OctantBit(dir);
    g_graph.node_sig[node] = (kKindCorridor & 0xff) |
                             ((octBit & 0xff) << 8);
}

void ClassifyJunction(int node,
                      const acc::engine::navgraph::NavGraphSnapshot& g,
                      int lo, int hi) {
    using acc::strings::Id;
    char dirList[96] = {0};
    size_t dirLen = 0;
    int mask = 0;

    // Walk neighbours in the order the engine stored them — produces a
    // stable, repeatable order for the same area across runs (helps
    // when comparing logs).
    for (int e = lo; e < hi; ++e) {
        int nb = static_cast<int>(g.conns[e]);
        if (nb < 0 || nb >= static_cast<int>(g.nodes.size())) continue;
        float dx = g.nodes[nb].pos.x - g.nodes[node].pos.x;
        float dy = g.nodes[nb].pos.y - g.nodes[node].pos.y;
        Id dir = OctantFromVector(dx, dy);
        int bit = OctantBit(dir);
        if (bit < 0) continue;
        if (mask & (1 << bit)) continue;
        mask |= (1 << bit);
        dirLen = AppendDirWord(dirList, sizeof(dirList), dirLen, dir);
    }

    const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
    if (fmt && fmt[0] && dirList[0] != '\0') {
        std::snprintf(g_graph.node_label[node],
                      sizeof(g_graph.node_label[node]),
                      fmt, dirList);
    } else {
        // Fall back to the bare "Kreuzung" / "Junction" word when the
        // direction list came out empty (every neighbour mapped to the
        // same octant after dedup — unusual but possible at very
        // densely-packed nodes).
        const char* bare = acc::strings::Get(Id::MapCursorJunction);
        if (bare && bare[0]) {
            std::snprintf(g_graph.node_label[node],
                          sizeof(g_graph.node_label[node]),
                          "%s", bare);
        }
    }
    g_graph.node_kind[node] = kKindJunction;
    g_graph.node_sig[node]  = (kKindJunction & 0xff) |
                              ((mask & 0xff) << 8);
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

    int deadEnds = 0, corridors = 0, junctions = 0;
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        int degree = hi - lo;
        if (degree <= 0) {
            // Isolated node — no exits. Leave as "open area"; the
            // fallback path catches it.
            continue;
        }
        if (degree == 1) {
            int nb = static_cast<int>(g.conns[lo]);
            if (nb < 0 || nb >= n) continue;
            ClassifyDeadEnd(i, nb, g);
            ++deadEnds;
        } else if (degree == 2) {
            int nbA = static_cast<int>(g.conns[lo]);
            int nbB = static_cast<int>(g.conns[lo + 1]);
            if (nbA < 0 || nbA >= n || nbB < 0 || nbB >= n) continue;
            ClassifyCorridor(i, nbA, nbB, g);
            ++corridors;
        } else {
            ClassifyJunction(i, g, lo, hi);
            ++junctions;
        }
    }

    g_graph.built = true;
    acclog::Write("WallTopo",
                  "BuildForArea: area=%p nodes=%d (dead=%d corridor=%d "
                  "junction=%d)",
                  area, n, deadEnds, corridors, junctions);
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    for (int i = 0; i < g_graph.node_count; ++i) {
        acclog::Write("WallTopo",
                      "  node[%d] kind=%d sig=%d pos=(%.1f,%.1f,%.1f) "
                      "label=\"%s\"",
                      i, g_graph.node_kind[i], g_graph.node_sig[i],
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
