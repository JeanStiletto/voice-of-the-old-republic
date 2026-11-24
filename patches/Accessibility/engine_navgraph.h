// CSWSArea nav-graph reader — shared snapshot helpers.
//
// Pure read, SEH-guarded. Used by guidance_pathfind and room_topology
// (the offset constants live in guidance_pathfind.h — one canonical
// location).
//
// PathPoint (16 bytes): x, y, connection count (+0x08), first connection
// (+0x0c). Neighbours of node N are
//   path_connections[first_N .. first_N + count_N - 1]
// — exactly how the engine's successor generator reads them. Authored .pth
// data stores the runs in point order (all 198 K2 module graphs checked
// 2026-11-24), but BridgeIslands relocates a bridged point's run to the end
// of the array, so readers must never derive a run end from the next point.
// Edges are symmetric undirected.

#pragma once

#include <cstdint>
#include <vector>

#include "engine_offsets.h"

namespace acc::engine {
struct WallEdge;  // engine_area.h
}

namespace acc::engine::navgraph {

struct PathPointSnapshot {
    Vector   pos;
    uint32_t csrOffset;   // first connection
    uint32_t connCount;   // connection count
};

// Neighbour run for node N: conns[csrOffset .. csrOffset + connCount).
struct NavGraphSnapshot {
    std::vector<PathPointSnapshot> nodes;
    std::vector<uint32_t>          conns;
};

// Observed worst case ~104 nodes; 512 cap. Late nodes truncate (A* over
// the prefix still works; room_topology degrades to "open area").
constexpr int kMaxNodes = 512;
constexpr int kMaxEdges = kMaxNodes * 8;

// True on non-empty snapshot. Partial-node faults truncate rather than
// fail. Output cleared on every failure path.
bool SnapshotNavGraph(void* area, NavGraphSnapshot& out);

// Bounds-checked against malformed CSR offsets — returns empty range
// rather than indexing out of bounds.
void NeighbourRange(const NavGraphSnapshot& g, int node,
                    int& outLo, int& outHi);

// ----- Island bridging ------------------------------------------------------
//
// Every K1 area's path-point graph is one connected network; many K2 areas
// ship theirs split into islands (offline scan of every module, 2026-11-24:
// 901MAL Malachor surface 11 islands with none dominant, 232TEL Telos
// underground base 7, 303NAR 12, 851NIH 11 ...). The engine plans long walks
// over this same graph (PlotPathInArea -> PlotPathPointPath when the area has
// path points), so neither it nor our A* can route island to island, and the
// player hears "no path" for places one step away (patch-20261124-143027.log:
// the Malachor exit route started 2.4 m from the player's island).
//
// Joins islands with the shortest walkmesh-clear links first (minimum-
// spanning order: a link is only added while its two ends are still in
// different islands, so each merge costs exactly one link and no island
// pair gets a redundant shortcut). Never touches a graph that is already one
// network — every K1 area and every single-island K2 area is left byte-for-
// byte unchanged. Links within one island are never added.
//
// The wall test is the static walkmesh perimeter only, 2D (nodes carry no
// height). Doors are not part of it: a closed door still bounds a real
// walkable connection. A link must be clear along its centre line AND along
// two parallel lines kBridgeClearanceM to either side — the engine walks a
// path-point link as a straight line with the creature's body around it
// (personal space 0.35 m for every player appearance in both games), so a
// link only a zero-width line fits through would jam the walker.
constexpr float kBridgeClearanceM = 0.5f;
//
// 40 m, validated offline against every split K2 area (walkmesh replica +
// a stepped ground-truth walk along each link with body width, 2026-11-24;
// tools/re-scripts/pathgraph_bridge_check.py): 111 links, all on real
// walkable floor with body clearance (the width test swapped an 851NIH
// link that only a zero-width line fit through for a wider one). Every
// area but 232TEL is one network by 12 m; 232TEL's two halves share one
// floor but every gap under 35 m runs through a wall (the link crosses a
// hall). Areas whose leftover islands sit on separate floor (903MAL,
// 101PER, 103PER, cutscene points 1300 m off in 501OND/512OND/950COR) stay
// split — the wall test rejects every candidate. Because links go shortest
// first and stop once islands merge, the cap only matters where nothing
// shorter exists.
constexpr float kMaxIslandBridgeM = 40.0f;

struct IslandBridge {
    int   a;
    int   b;
    float distM;
};

struct IslandBridgeStats {
    int   islandsBefore = 0;
    int   islandsAfter  = 0;
    int   bridges       = 0;
    int   wallRejects   = 0;   // candidates within range blocked by a wall
    float longestM      = 0.0f;
    bool  skippedNoWalls = false;  // >1 island but no wall cache: left as-is
};

// Mutates `g` in place: the connection array keeps its authored prefix
// verbatim and each bridged point's run is re-appended at the end with the
// new link added (only those points' first/count change). With no walls it
// refuses to bridge — an untested link could run through a wall.
// `outBridges` (optional) receives every added link for logging.
IslandBridgeStats BridgeIslands(NavGraphSnapshot& g,
                                const WallEdge* walls, int wallCount,
                                std::vector<IslandBridge>* outBridges = nullptr);

// Bridge the ENGINE's own path graph for `area`, so the engine planner
// (walk-to, use/talk approach, map-pin walks, companions following) and every
// mod consumer that snapshots it all route over one linked network.
//
// Safe by construction:
//   - Intact graphs (one island — every K1 area) are never written.
//   - A truncated snapshot (over kMaxNodes / kMaxEdges) is never written.
//   - The new connection array comes from the engine's own operator new, so
//     ~CSWSArea's _free releases it normally. The old array is leaked on
//     purpose (a few KB per area entry): nothing can ever dangle into it.
//   - Its prefix is the old array verbatim and only bridged points get a new
//     first/count, written AFTER the array swap — every intermediate state is
//     a valid graph.
//   - Idempotent: once written the graph is one island, so a repeat call (or
//     a retry tick) is a no-op; re-entering the area reloads the .pth and
//     bridges again.
enum class BridgeWrite {
    Intact,    // one island (or empty) — nothing to do
    Written,   // links added to the engine graph
    Partial,   // links added but islands remain (separate floors)
    NoLinks,   // islands, but no walkmesh-clear link within range
    NoWalls,   // islands, wall cache not ready — caller should retry
    Failed,    // unreadable / truncated / fault — engine graph untouched
};
BridgeWrite EnsureAreaIslandsBridged(void* area,
                                     const WallEdge* walls, int wallCount);

}  // namespace acc::engine::navgraph
