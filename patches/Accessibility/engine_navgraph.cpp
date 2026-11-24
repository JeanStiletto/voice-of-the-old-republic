// CSWSArea nav-graph reader — see engine_navgraph.h.
//
// Extracted 2026-05-13 from guidance_pathfind.cpp's anon-namespace
// SnapshotGraphMeta + LoadPoints + LoadConnections helpers. Behaviour is
// preserved verbatim; the only mechanical changes are the snapshot
// container (vector replaces static buffers, freeing the caller from
// kMax* assumptions outside this TU) and the namespace move.

#include "engine_navgraph.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstring>

#include "engine_area.h"        // WallEdge, SegmentCrossesWalkmesh
#include "guidance_pathfind.h"  // canonical home of the offset constants
#include "log.h"

namespace acc::engine::navgraph {

namespace {

// SEH-guarded scalar read — same pattern as engine_area's helpers.
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

struct GraphMeta {
    uint32_t pointsCount;
    void*    pointsPtr;
    uint32_t connsCount;
    void*    connsPtr;
};

// Snapshot the nav graph metadata. Returns false if any of the four base
// fields fault, the graph is empty, or either heap pointer is implausibly
// low (catches mid-tear-down / uninitialised-area states that would
// otherwise crash the deref loop below).
bool ReadMeta(void* area, GraphMeta& out) {
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

// Read every path_points entry into the snapshot vector. Per-entry SEH
// fault stops the load (caller gets a truncated snapshot — usable, just
// missing tail nodes).
int LoadPoints(const GraphMeta& meta, std::vector<PathPointSnapshot>& out) {
    int n = static_cast<int>(meta.pointsCount);
    if (n > kMaxNodes) n = kMaxNodes;
    out.clear();
    out.reserve(n);

    auto* base = static_cast<const unsigned char*>(meta.pointsPtr);
    for (int i = 0; i < n; ++i) {
        const unsigned char* p = base + i * kPathPointStride;
        PathPointSnapshot snap{};
        bool ok = true;
        __try {
            // PathPoint is 2D: x (0x00), y (0x04), then the connections
            // COUNT (0x08) and first_connection (0x0c) — there is NO height
            // field. Reading a full Vector here would land the connections
            // count in pos.z as a denormal-garbage float; read x/y only and
            // leave z at 0. Consumers that need a floor height supply it
            // from context (e.g. the player's z).
            const float* xy = reinterpret_cast<const float*>(
                p + kPathPointPositionOffset);
            snap.pos.x = xy[0];
            snap.pos.y = xy[1];
            snap.pos.z = 0.0f;
            snap.csrOffset = *reinterpret_cast<const uint32_t*>(
                p + kPathPointCsrOffset);
            snap.connCount = *reinterpret_cast<const uint32_t*>(
                p + kPathPointCountOffset);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ok = false;
        }
        if (!ok) {
            acclog::Write("NavGraph", "SEH on path_points[%d] — truncating graph", i);
            return i;
        }
        out.push_back(snap);
    }
    return n;
}

// Read the flat connections array. Bulk memcpy first; on fault, fall
// back to byte-by-byte SEH-safe copy and stop at the first faulted byte.
int LoadConnections(const GraphMeta& meta, std::vector<uint32_t>& out) {
    int n = static_cast<int>(meta.connsCount);
    if (n > kMaxEdges) n = kMaxEdges;
    out.assign(n, 0);
    if (n == 0) return 0;

    __try {
        std::memcpy(out.data(), meta.connsPtr, n * sizeof(uint32_t));
        return n;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Bulk faulted — fall through to byte-by-byte.
    }
    const unsigned char* src = static_cast<const unsigned char*>(meta.connsPtr);
    unsigned char*       dst = reinterpret_cast<unsigned char*>(out.data());
    for (int i = 0; i < n; ++i) {
        for (int b = 0; b < 4; ++b) {
            __try {
                dst[i * 4 + b] = src[i * 4 + b];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                acclog::Write("NavGraph",
                              "SEH on path_connections[%d] byte %d — "
                              "truncating edges", i, b);
                out.resize(i);
                return i;
            }
        }
    }
    return n;
}

}  // namespace

bool SnapshotNavGraph(void* area, NavGraphSnapshot& out) {
    out.nodes.clear();
    out.conns.clear();
    if (!area) return false;

    GraphMeta meta;
    if (!ReadMeta(area, meta)) return false;

    int loaded = LoadPoints(meta, out.nodes);
    if (loaded <= 0) {
        out.nodes.clear();
        return false;
    }
    LoadConnections(meta, out.conns);
    return true;
}

void NeighbourRange(const NavGraphSnapshot& g, int node,
                    int& outLo, int& outHi) {
    const int nodeCount  = static_cast<int>(g.nodes.size());
    const int connsCount = static_cast<int>(g.conns.size());
    if (node < 0 || node >= nodeCount) {
        outLo = outHi = 0;
        return;
    }
    // first + count, exactly as the engine's successor generator reads it
    // (see the header: a bridged point's run lives at the array's end).
    // 64-bit sum so a garbage count can't wrap past the bounds checks.
    const long long lo = g.nodes[node].csrOffset;
    const long long hi = lo + g.nodes[node].connCount;
    outLo = lo > connsCount ? connsCount : static_cast<int>(lo);
    outHi = hi > connsCount ? connsCount : static_cast<int>(hi);
    // Bounds-check against malformed offsets.
    if (outLo < 0)             outLo = 0;
    if (outHi < outLo)         outHi = outLo;
    if (outHi > connsCount)    outHi = connsCount;
    if (outLo > connsCount)    outLo = connsCount;
}

namespace {

// A link is usable only if a body fits along it: centre line plus two
// parallel lines kBridgeClearanceM either side must all be wall-clear.
// 2D (ignoreZ) because path points carry no height — it can only over-block.
bool LinkBlocked(const WallEdge* walls, int wallCount,
                 const Vector& a, const Vector& b) {
    Vector hit{};
    if (SegmentCrossesWalkmesh(walls, wallCount, a, b, hit, /*ignoreZ=*/true)) return true;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return false;
    const float nx = -dy / len * kBridgeClearanceM;
    const float ny =  dx / len * kBridgeClearanceM;
    for (int side = -1; side <= 1; side += 2) {
        Vector pa = {a.x + side * nx, a.y + side * ny, 0.0f};
        Vector pb = {b.x + side * nx, b.y + side * ny, 0.0f};
        if (SegmentCrossesWalkmesh(walls, wallCount, pa, pb, hit, /*ignoreZ=*/true)) return true;
    }
    return false;
}

}  // namespace

// No __try in here (C2712: it owns vectors). The only engine-touching call is
// SegmentCrossesWalkmesh, which reads our own wall cache, not engine memory.
IslandBridgeStats BridgeIslands(NavGraphSnapshot& g,
                                const WallEdge* walls, int wallCount,
                                std::vector<IslandBridge>* outBridges) {
    IslandBridgeStats st;
    if (outBridges) outBridges->clear();
    const int n = static_cast<int>(g.nodes.size());
    if (n <= 1) {
        st.islandsBefore = st.islandsAfter = n;
        return st;
    }

    // Union-find over the authored links. Out-of-range neighbour ids (a
    // graph truncated at kMaxNodes) are ignored here and kept verbatim below.
    std::vector<int> parent(n);
    for (int i = 0; i < n; ++i) parent[i] = i;
    auto find = [&](int x) {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    };
    std::vector<std::vector<uint32_t>> adj(n);
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = 0;
        NeighbourRange(g, i, lo, hi);
        for (int e = lo; e < hi; ++e) {
            uint32_t v = g.conns[e];
            adj[i].push_back(v);
            if (v < static_cast<uint32_t>(n)) parent[find(i)] = find(static_cast<int>(v));
        }
    }
    int islands = 0;
    for (int i = 0; i < n; ++i) if (find(i) == i) ++islands;
    st.islandsBefore = st.islandsAfter = islands;
    if (islands <= 1) return st;  // K1 and every intact K2 graph end here
    if (!walls || wallCount <= 0) {
        st.skippedNoWalls = true;
        return st;
    }

    // Every cross-island pair within range, shortest first.
    struct Cand { float d; int a; int b; };
    std::vector<Cand> cands;
    const float maxSq = kMaxIslandBridgeM * kMaxIslandBridgeM;
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            if (find(a) == find(b)) continue;
            float dx = g.nodes[a].pos.x - g.nodes[b].pos.x;
            float dy = g.nodes[a].pos.y - g.nodes[b].pos.y;
            float dSq = dx * dx + dy * dy;
            if (dSq <= maxSq) cands.push_back({std::sqrt(dSq), a, b});
        }
    }
    std::sort(cands.begin(), cands.end(),
              [](const Cand& x, const Cand& y) { return x.d < y.d; });

    std::vector<char> touched(n, 0);
    for (const Cand& c : cands) {
        int ra = find(c.a), rb = find(c.b);
        if (ra == rb) continue;  // an earlier, shorter link already merged them
        if (LinkBlocked(walls, wallCount, g.nodes[c.a].pos, g.nodes[c.b].pos)) {
            ++st.wallRejects;
            continue;
        }
        adj[c.a].push_back(static_cast<uint32_t>(c.b));
        adj[c.b].push_back(static_cast<uint32_t>(c.a));
        touched[c.a] = touched[c.b] = 1;
        parent[ra] = rb;
        ++st.bridges;
        --st.islandsAfter;
        if (c.d > st.longestM) st.longestM = c.d;
        if (outBridges) outBridges->push_back({c.a, c.b, c.d});
        if (st.islandsAfter <= 1) break;
    }
    if (st.bridges == 0) return st;

    // Keep the authored array verbatim; re-append only the bridged points'
    // runs (old neighbours + new links) at the end and repoint those points.
    // Untouched points keep their first/count, so the same layout can be
    // written into the engine with every intermediate state valid.
    for (int i = 0; i < n; ++i) {
        if (!touched[i]) continue;
        g.nodes[i].csrOffset = static_cast<uint32_t>(g.conns.size());
        g.nodes[i].connCount = static_cast<uint32_t>(adj[i].size());
        g.conns.insert(g.conns.end(), adj[i].begin(), adj[i].end());
    }
    return st;
}

namespace {

// Engine write, SEH-guarded, POD-only (C2712). `fresh` has already been
// filled; this swaps it in and then repoints the bridged points. Validates
// that the area still holds the graph we snapshotted before touching it.
bool SwapInConnections(void* area, uint32_t expectPoints, uint32_t expectPointsPtr,
                       uint32_t expectConnsPtr, uint32_t* fresh, uint32_t freshCount,
                       const int* idx, const uint32_t* first, const uint32_t* count,
                       int touchedCount) {
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        if (*reinterpret_cast<uint32_t*>(base + kAreaPathPointsCountOffset) != expectPoints)  return false;
        if (*reinterpret_cast<uint32_t*>(base + kAreaPathPointsPtrOffset)   != expectPointsPtr) return false;
        if (*reinterpret_cast<uint32_t*>(base + kAreaPathConnectionsPtrOffset) != expectConnsPtr) return false;
        auto* points = reinterpret_cast<unsigned char*>(static_cast<uintptr_t>(expectPointsPtr));
        // Probe every point we will write BEFORE the swap — a fault here
        // leaves the engine graph untouched.
        for (int k = 0; k < touchedCount; ++k) {
            volatile uint32_t probe = *reinterpret_cast<uint32_t*>(
                points + idx[k] * kPathPointStride + kPathPointCountOffset);
            (void)probe;
        }
        // 1) New array: authored prefix identical, so every untouched
        //    point's (first, count) is valid against it immediately.
        //    Pointer first: the new array is a superset, so (new ptr, old
        //    count) is itself a valid state.
        *reinterpret_cast<uint32_t**>(base + kAreaPathConnectionsPtrOffset)  = fresh;
        *reinterpret_cast<uint32_t*>(base + kAreaPathConnectionsCountOffset) = freshCount;
        // 2) Repoint the bridged points at their appended runs.
        for (int k = 0; k < touchedCount; ++k) {
            unsigned char* p = points + idx[k] * kPathPointStride;
            *reinterpret_cast<uint32_t*>(p + kPathPointCsrOffset)   = first[k];
            *reinterpret_cast<uint32_t*>(p + kPathPointCountOffset) = count[k];
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

uint32_t* EngineAllocDwords(uint32_t count) {
    __try {
        auto opNew = reinterpret_cast<PFN_OperatorNew>(kAddrOperatorNew);
        return static_cast<uint32_t*>(opNew(static_cast<size_t>(count) * sizeof(uint32_t)));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

}  // namespace

BridgeWrite EnsureAreaIslandsBridged(void* area,
                                     const WallEdge* walls, int wallCount) {
    if (!area) return BridgeWrite::Failed;
    GraphMeta meta;
    if (!ReadMeta(area, meta)) return BridgeWrite::Intact;  // no graph at all

    NavGraphSnapshot g;
    if (!SnapshotNavGraph(area, g)) return BridgeWrite::Failed;
    const NavGraphSnapshot before = g;   // authored layout, for the write diff

    std::vector<IslandBridge> links;
    IslandBridgeStats st = BridgeIslands(g, walls, wallCount, &links);
    if (st.islandsBefore <= 1) return BridgeWrite::Intact;
    if (st.skippedNoWalls)     return BridgeWrite::NoWalls;

    acclog::Write("NavGraph", "engine graph: islands %d -> %d (links=%d longest=%.1fm "
                  "wallRejects=%d max=%.0fm clearance=%.2fm) areaPtr=%p",
                  st.islandsBefore, st.islandsAfter, st.bridges, st.longestM,
                  st.wallRejects, kMaxIslandBridgeM, kBridgeClearanceM, area);
    for (const IslandBridge& l : links) {
        acclog::Write("NavGraph", "  link node %d (%.1f,%.1f) <-> node %d (%.1f,%.1f) %.1fm",
                      l.a, g.nodes[l.a].pos.x, g.nodes[l.a].pos.y,
                      l.b, g.nodes[l.b].pos.x, g.nodes[l.b].pos.y, l.distM);
    }
    if (st.bridges == 0) return BridgeWrite::NoLinks;

    // Never write a truncated snapshot back — the tail it lost would vanish.
    if (static_cast<uint32_t>(before.nodes.size()) != meta.pointsCount ||
        static_cast<uint32_t>(before.conns.size()) != meta.connsCount) {
        acclog::Write("NavGraph", "engine graph NOT written: snapshot truncated "
                      "(points %u/%u conns %u/%u) — mod-side bridging only",
                      static_cast<unsigned>(before.nodes.size()), meta.pointsCount,
                      static_cast<unsigned>(before.conns.size()), meta.connsCount);
        return BridgeWrite::Failed;
    }

    std::vector<int>      idx;
    std::vector<uint32_t> first, count;
    for (int i = 0; i < static_cast<int>(g.nodes.size()); ++i) {
        if (g.nodes[i].csrOffset == before.nodes[i].csrOffset &&
            g.nodes[i].connCount == before.nodes[i].connCount) continue;
        idx.push_back(i);
        first.push_back(g.nodes[i].csrOffset);
        count.push_back(g.nodes[i].connCount);
    }

    const uint32_t freshCount = static_cast<uint32_t>(g.conns.size());
    uint32_t* fresh = EngineAllocDwords(freshCount);
    if (!fresh) {
        acclog::Write("NavGraph", "engine graph NOT written: allocation failed");
        return BridgeWrite::Failed;
    }
    std::memcpy(fresh, g.conns.data(), freshCount * sizeof(uint32_t));

    bool ok = SwapInConnections(
        area, meta.pointsCount,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(meta.pointsPtr)),
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(meta.connsPtr)),
        fresh, freshCount, idx.data(), first.data(), count.data(),
        static_cast<int>(idx.size()));
    if (!ok) {
        // `fresh` is never referenced by the engine on this path; leaking it
        // is cheaper than binding the engine's _free for one failure case.
        acclog::Write("NavGraph", "engine graph NOT written: area changed under "
                      "us or write faulted — engine graph untouched");
        return BridgeWrite::Failed;
    }
    acclog::Write("NavGraph", "engine graph written: conns %u -> %u, %d point(s) "
                  "repointed (old array left allocated by design)",
                  meta.connsCount, freshCount, static_cast<int>(idx.size()));
    return st.islandsAfter > 1 ? BridgeWrite::Partial : BridgeWrite::Written;
}

}  // namespace acc::engine::navgraph
