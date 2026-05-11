#include "guidance_pathfind.h"

#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#include "log.h"

namespace acc::guidance {

namespace {

// Per-call A* working set. We allocate one statically so the per-area
// node count caps at kMaxNodes — observed worst case ~104 nodes, headroom
// to 512. The arrays are touched per-fire from a single OnUpdate tick,
// no reentrancy.
constexpr int kMaxNodes = 512;

struct PathPointSnapshot {
    Vector   pos;
    uint32_t csrOffset;
};

// SEH-guarded scalar reads — same pattern as the engine_area helpers.
template <typename T>
bool SafeRead(void* base, size_t offset, T& out) {
    __try {
        out = *reinterpret_cast<const T*>(
            reinterpret_cast<const unsigned char*>(base) + offset);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Snapshot the area's nav graph metadata. Returns false if any of the
// four base fields fault, the graph is empty, or the heap pointers are
// implausibly low (catches mid-tear-down or uninitialised-area states
// that would otherwise crash the deref loop below). Counts are clamped
// to kMaxNodes — we drop late-listed nodes rather than fail.
struct GraphMeta {
    uint32_t pointsCount;
    void*    pointsPtr;
    uint32_t connsCount;
    void*    connsPtr;
};

bool SnapshotGraphMeta(void* area, GraphMeta& out) {
    uint32_t pointsCount = 0;
    uint32_t pointsPtrU  = 0;
    uint32_t connsCount  = 0;
    uint32_t connsPtrU   = 0;
    if (!SafeRead(area, kAreaPathPointsCountOffset,      pointsCount)) return false;
    if (!SafeRead(area, kAreaPathPointsPtrOffset,        pointsPtrU))  return false;
    if (!SafeRead(area, kAreaPathConnectionsCountOffset, connsCount))  return false;
    if (!SafeRead(area, kAreaPathConnectionsPtrOffset,   connsPtrU))   return false;

    if (pointsCount == 0 || connsCount == 0) return false;
    if (pointsPtrU < 0x00100000u || pointsPtrU >= 0x80000000u) return false;
    if (connsPtrU  < 0x00100000u || connsPtrU  >= 0x80000000u) return false;

    out.pointsCount = pointsCount;
    out.pointsPtr   = reinterpret_cast<void*>(static_cast<uintptr_t>(pointsPtrU));
    out.connsCount  = connsCount;
    out.connsPtr    = reinterpret_cast<void*>(static_cast<uintptr_t>(connsPtrU));
    return true;
}

// Read all path_points into the snapshot array. Each entry is 16 bytes
// (Vector + csr offset). Returns count actually loaded (≤ kMaxNodes).
// SEH fault on any single node skips it and stops the load — partial
// graph is still usable for finding the start/goal node, but A* over a
// truncated graph may report "no path" where a complete graph would
// succeed; acceptable degradation.
int LoadPoints(const GraphMeta& meta, PathPointSnapshot* out) {
    int n = static_cast<int>(meta.pointsCount);
    if (n > kMaxNodes) n = kMaxNodes;

    auto* base = static_cast<const unsigned char*>(meta.pointsPtr);
    for (int i = 0; i < n; ++i) {
        const unsigned char* p = base + i * kPathPointStride;
        Vector   pos{0, 0, 0};
        uint32_t csr = 0;
        bool ok = true;
        __try {
            pos = *reinterpret_cast<const Vector*>(p + kPathPointPositionOffset);
            csr = *reinterpret_cast<const uint32_t*>(p + kPathPointCsrOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        if (!ok) {
            acclog::Write("Pathfind", "SEH on path_points[%d] — truncating graph", i);
            return i;
        }
        out[i].pos       = pos;
        out[i].csrOffset = csr;
    }
    return n;
}

// Read the flat connections array. Stored as uint32; we cap at kMaxEdges
// for the same reason as kMaxNodes.
constexpr int kMaxEdges = kMaxNodes * 8;  // 8 neighbours/node average
                                          // headroom; observed ratio ~2

int LoadConnections(const GraphMeta& meta, uint32_t* out) {
    int n = static_cast<int>(meta.connsCount);
    if (n > kMaxEdges) n = kMaxEdges;
    __try {
        std::memcpy(out, meta.connsPtr, n * sizeof(uint32_t));
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Bulk faulted — fall back to byte-by-byte SEH-safe copy. Same
        // pattern as probe_pathfind::SafeBulkRead.
    }
    const unsigned char* src = static_cast<const unsigned char*>(meta.connsPtr);
    unsigned char*       dst = reinterpret_cast<unsigned char*>(out);
    for (int i = 0; i < n; ++i) {
        for (int b = 0; b < 4; ++b) {
            __try {
                dst[i * 4 + b] = src[i * 4 + b];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("Pathfind", "SEH on path_connections[%d] byte %d "
                              "— truncating edges", i, b);
                return i;
            }
        }
    }
    return n;
}

// Euclidean distance (2D in XY plane; KOTOR areas are layout-flat enough
// that Z doesn't materially affect path costs and the walkmesh edges in
// engine_area also work in XY). Used both as edge weight and as A* heuristic.
float DistXY(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

float DistXYSq(const Vector& a, const Vector& b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Find the index of the path_point nearest to `target` in XY. Linear
// scan — N ≤ kMaxNodes, trivial cost per call.
int FindNearestNode(const PathPointSnapshot* nodes, int nodeCount,
                    const Vector& target) {
    if (nodeCount <= 0) return -1;
    int best = 0;
    float bestSq = DistXYSq(nodes[0].pos, target);
    for (int i = 1; i < nodeCount; ++i) {
        float d = DistXYSq(nodes[i].pos, target);
        if (d < bestSq) {
            bestSq = d;
            best   = i;
        }
    }
    return best;
}

// Resolve node N's CSR-adjacency neighbour range. Lower bound is
// nodes[N].csrOffset; upper bound is nodes[N+1].csrOffset (or
// connsCount for the last node).
void NeighbourRange(const PathPointSnapshot* nodes, int nodeCount,
                    int connsCount, int n,
                    int& outLo, int& outHi) {
    outLo = static_cast<int>(nodes[n].csrOffset);
    outHi = (n + 1 < nodeCount)
        ? static_cast<int>(nodes[n + 1].csrOffset)
        : connsCount;
    // Bounds check — defend against malformed CSR data (out-of-range
    // offsets shouldn't happen for well-formed graphs, but bail
    // cleanly rather than crash A*).
    if (outLo < 0)            outLo = 0;
    if (outHi < outLo)        outHi = outLo;
    if (outHi > connsCount)   outHi = connsCount;
    if (outLo > connsCount)   outLo = connsCount;
}

}  // namespace

bool ComputePath(void* area,
                 const Vector& start,
                 const Vector& goal,
                 std::vector<Vector>& outWaypoints) {
    outWaypoints.clear();
    if (!area) {
        acclog::Write("Pathfind", "ComputePath: area=null");
        return false;
    }

    GraphMeta meta;
    if (!SnapshotGraphMeta(area, meta)) {
        acclog::Write("Pathfind", "ComputePath: nav graph empty / unreadable");
        return false;
    }

    // Static buffers to avoid per-call heap churn. Reused across calls
    // (the patch is single-threaded; only one ComputePath in flight).
    static PathPointSnapshot s_nodes[kMaxNodes];
    static uint32_t          s_conns[kMaxEdges];

    int nodeCount = LoadPoints(meta, s_nodes);
    if (nodeCount <= 0) {
        acclog::Write("Pathfind", "ComputePath: no nodes loaded");
        return false;
    }
    int connsCount = LoadConnections(meta, s_conns);

    acclog::Write("Pathfind", "graph nodes=%d conns=%d "
                  "start=(%.2f,%.2f,%.2f) goal=(%.2f,%.2f,%.2f)",
                  nodeCount, connsCount,
                  start.x, start.y, start.z,
                  goal.x,  goal.y,  goal.z);

    // Degenerate: start within reach of goal (0.5m). Skip the graph search
    // entirely; emit a single-point path so consumers can speak "already
    // at destination" without a special-case.
    if (DistXY(start, goal) < 0.5f) {
        outWaypoints.push_back(goal);
        acclog::Write("Pathfind", "ComputePath: start == goal (single-point path)");
        return true;
    }

    int startNode = FindNearestNode(s_nodes, nodeCount, start);
    int goalNode  = FindNearestNode(s_nodes, nodeCount, goal);
    if (startNode < 0 || goalNode < 0) {
        acclog::Write("Pathfind", "ComputePath: nearest-node resolution failed");
        return false;
    }

    // Degenerate: start and goal map to same nav-graph node. Path is just
    // the goal anchor.
    if (startNode == goalNode) {
        outWaypoints.push_back(goal);
        acclog::Write("Pathfind", "ComputePath: start/goal share node %d "
                      "(single-point path)", startNode);
        return true;
    }

    // A* over the static graph. Linear-array open set: kMaxNodes is small
    // enough that scanning for min-f beats heap maintenance. Closed flag
    // is part of the per-node state.
    struct AStarNode {
        float g       = std::numeric_limits<float>::infinity();
        float f       = std::numeric_limits<float>::infinity();
        int   parent  = -1;
        bool  closed  = false;
        bool  open    = false;
    };
    static AStarNode s_state[kMaxNodes];
    for (int i = 0; i < nodeCount; ++i) s_state[i] = AStarNode{};

    s_state[startNode].g    = 0.0f;
    s_state[startNode].f    = DistXY(s_nodes[startNode].pos, s_nodes[goalNode].pos);
    s_state[startNode].open = true;

    int expanded = 0;
    while (true) {
        // Pick the open-set entry with lowest f.
        int   best   = -1;
        float bestF  = std::numeric_limits<float>::infinity();
        for (int i = 0; i < nodeCount; ++i) {
            if (s_state[i].open && s_state[i].f < bestF) {
                bestF = s_state[i].f;
                best  = i;
            }
        }
        if (best < 0) {
            acclog::Write("Pathfind", "ComputePath: open set exhausted "
                          "(no path) expanded=%d", expanded);
            return false;
        }
        if (best == goalNode) break;  // found

        s_state[best].open   = false;
        s_state[best].closed = true;
        ++expanded;

        int lo = 0, hi = 0;
        NeighbourRange(s_nodes, nodeCount, connsCount, best, lo, hi);
        for (int e = lo; e < hi; ++e) {
            int nb = static_cast<int>(s_conns[e]);
            if (nb < 0 || nb >= nodeCount) continue;
            if (s_state[nb].closed) continue;

            float tentativeG = s_state[best].g +
                               DistXY(s_nodes[best].pos, s_nodes[nb].pos);
            if (tentativeG < s_state[nb].g) {
                s_state[nb].parent = best;
                s_state[nb].g      = tentativeG;
                s_state[nb].f      = tentativeG +
                                     DistXY(s_nodes[nb].pos,
                                            s_nodes[goalNode].pos);
                s_state[nb].open   = true;
            }
        }

        // Defensive: in a malformed graph the search could loop forever.
        // Bound by 2 × nodeCount.
        if (expanded > 2 * nodeCount) {
            acclog::Write("Pathfind", "ComputePath: aborting after %d expansions "
                          "(probable malformed graph)", expanded);
            return false;
        }
    }

    // Reconstruct path goal → start by following parents, then reverse.
    int   chain[kMaxNodes];
    int   len = 0;
    int   cur = goalNode;
    while (cur >= 0 && len < kMaxNodes) {
        chain[len++] = cur;
        if (cur == startNode) break;
        cur = s_state[cur].parent;
    }
    if (len == 0 || chain[len - 1] != startNode) {
        acclog::Write("Pathfind", "ComputePath: parent chain broken "
                      "(len=%d last=%d expected start=%d)",
                      len, len ? chain[len - 1] : -1, startNode);
        return false;
    }

    outWaypoints.reserve(len + 1);
    for (int i = len - 1; i >= 0; --i) {
        outWaypoints.push_back(s_nodes[chain[i]].pos);
    }
    // Append the original goal as the terminal anchor — the last
    // path_point is `goalNode` (closest node to goal), but the user
    // actually wants to end up AT goal, not at the nearest nav node.
    outWaypoints.push_back(goal);

    acclog::Write("Pathfind", "ComputePath: solved len=%d expanded=%d "
                  "startNode=%d goalNode=%d (waypoints incl. goal=%zu)",
                  len, expanded, startNode, goalNode, outWaypoints.size());
    return true;
}

}  // namespace acc::guidance
