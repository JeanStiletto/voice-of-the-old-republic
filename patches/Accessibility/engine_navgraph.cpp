// CSWSArea nav-graph reader — see engine_navgraph.h.
//
// Extracted 2026-05-13 from guidance_pathfind.cpp's anon-namespace
// SnapshotGraphMeta + LoadPoints + LoadConnections helpers. Behaviour is
// preserved verbatim; the only mechanical changes are the snapshot
// container (vector replaces static buffers, freeing the caller from
// kMax* assumptions outside this TU) and the namespace move.

#include "engine_navgraph.h"

#include <windows.h>
#include <cstring>

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
            snap.pos = *reinterpret_cast<const Vector*>(
                p + kPathPointPositionOffset);
            snap.csrOffset = *reinterpret_cast<const uint32_t*>(
                p + kPathPointCsrOffset);
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
    outLo = static_cast<int>(g.nodes[node].csrOffset);
    outHi = (node + 1 < nodeCount)
        ? static_cast<int>(g.nodes[node + 1].csrOffset)
        : connsCount;
    // Bounds-check against malformed CSR offsets.
    if (outLo < 0)             outLo = 0;
    if (outHi < outLo)         outHi = outLo;
    if (outHi > connsCount)    outHi = connsCount;
    if (outLo > connsCount)    outLo = connsCount;
}

}  // namespace acc::engine::navgraph
