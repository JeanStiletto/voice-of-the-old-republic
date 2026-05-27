// A* pathfinder over the per-area static nav graph.
//
// Reads CSWSArea pure data — no engine re-entry. We re-solve because the
// engine refuses to plot a path for the PC (project_addmovetopoint_
// leader_broken). Output feeds guidance_beacon and guidance_description.
//
// CSWSArea nav graph (offsets at the bottom of this header):
//
//   PathPoint (16 bytes): Vector position + uint32 csr_offset.
//   CSR adjacency: node N's neighbours live at
//     path_connections[meta_N .. meta_{N+1}-1]
//   where meta_K = path_points[K].csr_offset; last node's upper bound
//   is path_connections_count. Edges are symmetric undirected.
//
// First-cut scope:
//   - Single area only — cross-area routing is deferred; the user re-fires
//     Ctrl+- after walking through a transition.
//   - ~200 nodes max — open/closed sets are fixed-size arrays.

#pragma once

#include <vector>

#include "engine_offsets.h"

namespace acc::guidance {

// Computes shortest walkmesh-respecting path. outWaypoints ends with the
// original `goal` appended as terminal anchor. `start` is NOT prepended.
//
// A* output starts at the nearest graph node, which can be sideways of
// the player mid-corridor. A string-pulling pass after A* uses `start`
// as the implicit pre-path anchor: triples (anchor, B, C) where
// anchor→C is walkmesh-clear collapse to (anchor, C). So the first
// returned entry is the first turn-point the user actually needs to
// hear about (doorway, junction, corner), not the nearest node.
//
// Smoothing uses spatial::change_detector::GetCachedWalls. If the cache
// isn't ready (only before the change-detector's first post-area tick),
// raw A* output is returned — degraded but never wrong.
//
// Returns:
//   true  — non-empty path. Degenerate "start == goal" returns just the
//           goal so callers handle "already there".
//   false — null area / empty graph / no path / any read faulted.
//           outWaypoints is cleared on every failure.
//
// O(N²) open-set scan; N < 200 in practice beats a heap. Every nav-graph
// read is __try-wrapped; the function never raises.
bool ComputePath(void* area,
                 const Vector& start,
                 const Vector& goal,
                 std::vector<Vector>& outWaypoints);

}  // namespace acc::guidance

// CSWSArea nav graph offsets (memory: project_kotor_nav_graph_layout).
constexpr size_t kAreaPathPointsCountOffset      = 0x238;
constexpr size_t kAreaPathPointsPtrOffset        = 0x23c;
constexpr size_t kAreaPathConnectionsCountOffset = 0x240;
constexpr size_t kAreaPathConnectionsPtrOffset   = 0x244;

constexpr size_t kPathPointStride                = 0x10;
constexpr size_t kPathPointPositionOffset        = 0x00;  // Vector
constexpr size_t kPathPointCsrOffset             = 0x0c;  // uint32
