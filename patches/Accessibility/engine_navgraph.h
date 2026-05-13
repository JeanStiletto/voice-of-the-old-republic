// CSWSArea nav-graph reader — shared snapshot helpers.
//
// Layer: engine/ (pure read-side, SEH-guarded; no engine re-entry). Extracted
// 2026-05-13 from guidance_pathfind.cpp; both that file and the
// region_classifier Phase A block had inlined the same field reads. Adding a
// third consumer (wall_topology Path 3) would have been a third copy.
//
// Engine data source (locked in memory `project_kotor_nav_graph_layout`,
// verified live via probe_pathfind):
//
//   CSWSArea
//     +0x238  ulong       path_points_count
//     +0x23c  PathPoint*  path_points          // 16-byte stride
//     +0x240  ulong       path_connections_count
//     +0x244  ulong*      path_connections     // flat CSR adjacency
//
//   PathPoint (16 bytes):
//     +0x00  Vector  position      (12 bytes, world coords)
//     +0x0c  uint32  csr_offset    (offset into path_connections array)
//
// CSR adjacency: node N's neighbours live at
//   path_connections[meta_N .. meta_{N+1}-1]
// where meta_K = path_points[K].csr_offset; for the last node the implicit
// upper bound is path_connections_count. Edges are symmetric undirected.
//
// All offset constants live in guidance_pathfind.h (the original home);
// this header re-uses them via include. Adding them in two places would
// drift; one canonical location wins.

#pragma once

#include <cstdint>
#include <vector>

#include "engine_offsets.h"  // Vector

namespace acc::engine::navgraph {

// One nav-graph node. Position is in world space; csrOffset is the index
// into the flat connections array where this node's neighbour run begins.
struct PathPointSnapshot {
    Vector   pos;
    uint32_t csrOffset;
};

// Snapshot of an area's nav graph. `nodes` is sized by nodes.size(); the
// neighbour run for node N is `conns[nodes[N].csrOffset .. nodes[N+1].
// csrOffset)`, with the last node's upper bound being `conns.size()`.
struct NavGraphSnapshot {
    std::vector<PathPointSnapshot> nodes;
    std::vector<uint32_t>          conns;
};

// Hard cap on the number of nodes loaded. Observed worst case ~104 nodes;
// 512 is comfortable headroom. Late-listed nodes are dropped (truncated
// snapshot still usable for A* over the prefix; wall_topology's
// degree-driven classifier degrades to "open area" for any node not in
// the snapshot).
constexpr int kMaxNodes = 512;

// Edge headroom (8 neighbours/node average; observed ratio ~2).
constexpr int kMaxEdges = kMaxNodes * 8;

// Capture the area's nav graph. Returns true on a non-empty graph with
// at least one node loaded. Returns false in every failure case (null
// area, faulted field reads, empty graph, implausible heap pointers
// caught as mid-tear-down). Output is cleared on every failure path so
// callers can ignore the bool and check nodes.empty() if they prefer.
//
// SEH semantics: every read is __try-wrapped. Partial-node faults
// truncate the snapshot rather than fail — a few missing late-listed
// nodes don't sink a path solve or classification pass.
bool SnapshotNavGraph(void* area, NavGraphSnapshot& out);

// Resolve node N's CSR-adjacency neighbour range into [outLo, outHi).
// Bounds-checked against malformed CSR offsets — returns an empty range
// if the data is inconsistent rather than indexing out-of-bounds.
void NeighbourRange(const NavGraphSnapshot& g, int node,
                    int& outLo, int& outHi);

}  // namespace acc::engine::navgraph
