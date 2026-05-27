// CSWSArea nav-graph reader — shared snapshot helpers.
//
// Pure read, SEH-guarded. Used by guidance_pathfind and wall_topology
// (the offset constants live in guidance_pathfind.h — one canonical
// location).
//
// PathPoint (16 bytes): Vector position + uint32 csr_offset.
// CSR adjacency: neighbours of node N at
//   path_connections[meta_N .. meta_{N+1}-1]
// where meta_K = path_points[K].csr_offset; last node's upper bound is
// path_connections_count. Edges are symmetric undirected.

#pragma once

#include <cstdint>
#include <vector>

#include "engine_offsets.h"

namespace acc::engine::navgraph {

struct PathPointSnapshot {
    Vector   pos;
    uint32_t csrOffset;
};

// Neighbour run for node N: conns[nodes[N].csrOffset .. nodes[N+1].csrOffset);
// last node's upper bound is conns.size().
struct NavGraphSnapshot {
    std::vector<PathPointSnapshot> nodes;
    std::vector<uint32_t>          conns;
};

// Observed worst case ~104 nodes; 512 cap. Late nodes truncate (A* over
// the prefix still works; wall_topology degrades to "open area").
constexpr int kMaxNodes = 512;
constexpr int kMaxEdges = kMaxNodes * 8;

// True on non-empty snapshot. Partial-node faults truncate rather than
// fail. Output cleared on every failure path.
bool SnapshotNavGraph(void* area, NavGraphSnapshot& out);

// Bounds-checked against malformed CSR offsets — returns empty range
// rather than indexing out of bounds.
void NeighbourRange(const NavGraphSnapshot& g, int node,
                    int& outLo, int& outHi);

}  // namespace acc::engine::navgraph
