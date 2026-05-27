// Pillar 1 Triggers 1+2 — per-feature change detection.
//
// One per-tick scan over walls (engine_area's cached perimeter edges) +
// objects (AreaObjectIterator filtered by the Pillar 4 six-category
// predicate). Maintains per-feature last_cued_distance (T1) +
// last_cued_at timestamp (T1/T2 shared) + foremost-in-front debounce (T2).
//
// Trigger 1 — 360° distance-delta. Fires when |Δd| to any in-range
// feature exceeds settings.pillar1.distanceDeltaThresholdMeters. Walls
// bin into 4 character-relative sectors; closest per sector contributes,
// capped at trigger1MaxWallCuesPerTick. Objects fire on per-handle
// crossing.
//
// Trigger 2 — foremost-in-front (±45°). Closest wall-or-object in the
// Front sector. Identity change + kT2QuietMs stability → one cue. Cone-
// clear = silence (turn_announce confirms rotation).
//
// T1/T2 coordination via shared last_cued_at. T1 stamps on fire; T2
// only fires when the foremost hasn't been recently stamped. Result:
// approach reads as T1-only; T2 only adds audio during stationary
// rotation or rotation across already-stamped features.

#pragma once

#include "engine_offsets.h"
#include "engine_area.h"

namespace acc::spatial::change_detector {

// Self-gates on player + area resolved; rebuilds the wall cache and
// resets per-feature state on area change. Silent on null player/area.
void Tick();

// Borrowed pointer into static storage, valid for the area lifetime.
// Don't retain across transitions. False until first Tick has populated.
bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount);

// One wall surface = chain of collinear endpoint-sharing edges
// clustered at area-load via union-find (~5cm endpoint match, ~15°
// direction match, across rooms).
//
// a/b = the two extreme endpoints (only ones used by exactly one edge).
// Consumed by wall_topology to skip a redundant merge pass.
struct WallSurfaceDesc {
    Vector a;
    Vector b;
    float  dir_x;
    float  dir_y;
    float  length;
    int    edge_count;
};

// 0 until first Tick has populated.
int GetWallSurfaceCount();

// False on out-of-range, before clustering runs, or for surfaces that
// couldn't reduce to a single straight segment (closed-loop / branching
// edge cases flagged with edge_count == 0).
bool GetWallSurfaceDesc(int idx, WallSurfaceDesc& outDesc);

// edgeIdx into GetCachedWalls. -1 on invalid input or pre-clustering.
int GetEdgeSurfaceId(int edgeIdx);

// 2D segment-vs-surface intersection. Mirrors the audio wall-cue model:
// portal-seam phantoms absorbed by clustering are invisible here, so
// cursors block where the audio would announce, not on absorbed fragments.
bool SegmentCrossesSurface(const Vector& a, const Vector& b,
                           Vector& outHitPoint);

}  // namespace acc::spatial::change_detector
