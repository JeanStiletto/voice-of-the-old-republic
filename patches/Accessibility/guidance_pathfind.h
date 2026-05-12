// A* pathfinder over the per-area static nav graph.
//
// Layer: guidance/ (depends on engine_offsets.h's Vector; reads the engine's
// CSWSArea pure data, no engine re-entry). Phase 5 lay-off 2 — the
// computation half of Pillar 3 Mode B (audio beacon). The result is
// consumed by guidance_beacon (waypoint cue emission) and
// guidance_description (turn-by-turn route readout).
//
// Engine data source (verified 2026-05-11 via probe_pathfind, fields locked
// in memory `project_kotor_nav_graph_layout`):
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
// where meta_K = path_points[K].csr_offset; for the last node the
// implicit upper bound is path_connections_count. Sample data confirms
// symmetric undirected edges.
//
// Engine constraint reminder: the engine refuses to plot a path for the
// PC (memory `project_addmovetopoint_leader_broken`) — that's why we
// re-solve. The result is consumed by our own beacon, not handed back
// to the engine.
//
// First-cut scope (per docs/navsystem-progress.md Phase 5):
//   - Single-area only. Cross-area routing (transitioning through a door
//     that loads another module) is deferred; the user re-fires Ctrl+-
//     in the new area after a transition.
//   - Up to ~200 nodes per area (observed: Endar Spire start = 51,
//     samples reach 104). Linear scan + open/closed sets implemented as
//     fixed-size arrays — no heap churn per call.

#pragma once

#include <vector>

#include "engine_offsets.h"  // Vector

namespace acc::guidance {

// Compute the shortest walkmesh-respecting path from `start` to `goal`
// over `area`'s static nav graph. On success, `outWaypoints` is populated
// with the sequence of world-space positions the user should walk
// through, in order. The last entry is always the original `goal`
// (appended as the terminal anchor so the consumer doesn't have to
// special-case the last segment). The user's current position is NOT
// prepended.
//
// First-entry semantics: the raw A* output starts with the nav-graph
// node nearest `start`, which can sit behind / sideways of the player
// when they're mid-corridor between two graph nodes (first beacon
// played "8m behind me"). After A* we run a string-pulling pass that
// uses `start` as the implicit pre-path anchor: triples (anchor, B, C)
// where anchor→C is walkmesh-clear collapse to (anchor, C), dropping
// any backwards-first-hop and any redundant graph-node bounces along
// the route. So in practice the first entry is the first turn-point
// the user actually needs to hear about — typically a doorway, junction,
// or corner, not the nearest graph node.
//
// Smoothing uses the wall cache from `spatial::change_detector::
// GetCachedWalls` (Pillar 1). If the cache isn't ready yet (rare — only
// before the change-detector has ticked once after area enter), the
// raw A* output is returned unsmoothed; degraded behaviour matches the
// pre-smoothing version, never wrong, just less useful for that one
// call.
//
// Returns:
//   true  — non-empty waypoint sequence written to outWaypoints (degenerate
//           case "start == goal" returns a single-point path with just the
//           goal; consumers handle "already at destination").
//   false — area pointer null, graph empty, no path found, or any read
//           faulted during traversal. outWaypoints is cleared in every
//           failure case.
//
// Side effects: none. Cost: O(N * log N) on the open set (linear-array
// implementation; N typically < 200 so the constant beats a heap),
// plus one SEH-guarded read per CSWSArea field at entry.
//
// SEH semantics: every nav-graph read is __try-wrapped; a faulted read at
// any layer returns false cleanly. The function never raises.
bool ComputePath(void* area,
                 const Vector& start,
                 const Vector& goal,
                 std::vector<Vector>& outWaypoints);

}  // namespace acc::guidance

// CSWSArea nav graph offsets. Locked memory `project_kotor_nav_graph_layout`;
// verified live via probe_pathfind. PathPoint stride = 16 bytes
// (Vector + csr offset). Connections are a flat array of ulong (4 bytes).
constexpr size_t kAreaPathPointsCountOffset      = 0x238;
constexpr size_t kAreaPathPointsPtrOffset        = 0x23c;
constexpr size_t kAreaPathConnectionsCountOffset = 0x240;
constexpr size_t kAreaPathConnectionsPtrOffset   = 0x244;

constexpr size_t kPathPointStride                = 0x10;  // 16 bytes
constexpr size_t kPathPointPositionOffset        = 0x00;  // Vector
constexpr size_t kPathPointCsrOffset             = 0x0c;  // uint32
