// Wall-topology decomposition — alternative to the room-cache classifier.
//
// EXPERIMENTAL — lives on the `alternative-direction-calculation-system`
// branch. Goal: build a graph of perceptual regions (corridors,
// junctions, dead-ends, open areas) directly from the perimeter-wall
// cache, without relying on .lyt-room partitions or per-point ray
// probing.
//
// Motivation (from 2026-05-13 design discussion):
//
//   The room-cache classifier inherits two failure modes from K1's
//   .lyt-room partitioning:
//
//     1. Centroid-in-wall: a room's geometric centroid can land inside
//        a wall (L-shaped rooms, concave layouts), producing a "Wand"
//        probe failure even though the room is walkable. We currently
//        suppress these, which means those rooms speak nothing.
//
//     2. Sliver landmarks: a .lyt-room can be a long thin transition
//        strip that intercepts the player on multiple unrelated paths
//        (Taris South Apartments room 8 covers a 50m sliver tied to
//        the "Zur Oberstadt" landmark). We've gated landmark speech on
//        proximity to the waypoint, but the underlying problem is
//        that .lyt-rooms are not perceptually meaningful regions.
//
//   A topological decomposition operates at the right level of
//   abstraction: corridors are *things*, junctions are *things*,
//   not collections of sample points that happen to classify
//   identically. Regions correspond directly to what the player
//   perceives.
//
// High-level algorithm:
//
//   1. SEGMENT GROUPING — collapse the raw wall-edge soup into longer
//      "wall segments". Adjacent edges that are near-collinear and
//      share endpoints merge into one polyline segment. Reduces a
//      few-hundred-edge cache to a few-dozen segments per area.
//
//   2. JUNCTION CLUSTERING — find clusters of segment endpoints
//      within a small radius of each other. Each cluster becomes a
//      "junction point" in the floor-plan graph.
//
//   3. CORRIDOR DETECTION — pairs of parallel segments separated by
//      corridor-width (2-6m) with overlapping projections are
//      candidate corridors. The space between them becomes a
//      "corridor cell" with width, length, and orientation.
//
//   4. CLASSIFICATION — assign labels:
//        - Corridor cell        → "Korridor {Axis}, {Length} Meter"
//        - Junction point (deg≥3) → "Kreuzung, {open directions}"
//        - Dead-end (deg=1)     → "Sackgasse, {direction}"
//        - Leftover open space  → "Offene Fläche"
//
//   5. SPATIAL INDEX — build a position-to-region index so runtime
//      lookup is O(1) or O(log N). Could be a uniform grid over the
//      area's bounding box, or a polygon-walk for each region.
//
// Module API mirrors region_classifier: one BuildForArea call on
// area-enter, then per-position LookupAt at runtime. Idempotent on
// the same area pointer.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::wall_topology {

// Kind codes packed into the low byte of the `sig` value returned by
// LookupAt. Consumers (transitions.cpp uses this to detect Platz
// clusters for the delayed-announce path) read the kind via
// `sig & 0xff`.
//
// kKindTransition is reserved but currently unused (cross-room
// degree-2 detection was retired 2026-05-13 after empirical evidence
// that K1's .lyt-room boundaries don't correspond to doorways).
enum Kind {
    KindDeadEnd     = 0,
    KindCorridor    = 1,
    KindJunction    = 2,
    KindTransition  = 3,
    KindOpenArea    = 4,
    KindPlatz       = 5,
};

// Cluster-id sentinels returned by LookupAt via outClusterId.
// Real cluster ids are non-negative UFFind roots (0..node_count-1).
//
// `kClusterIdNone` — no graph for this area, position outside the snap
// radius, or every candidate was wall-filtered. Callers using cluster-id
// as a trigger key should treat this as "no fact yet, do nothing".
//
// `kClusterIdOpenArea` — LookupAt synthesised an "Offene Fläche" label
// via the open-area fallback (no labelled candidates, or all blocked).
// Stable identity: a player walking from a labelled cluster into open
// space should fire exactly one announcement, then stay quiet until
// they enter another cluster. Distinct from real cluster ids so the
// trigger key compares correctly.
constexpr int kClusterIdNone     = -1;
constexpr int kClusterIdOpenArea = -2;

// Build the wall-topology decomposition for `area`. Idempotent on the
// same area pointer. Requires `acc::spatial::change_detector::
// GetCachedWalls()` to be populated; silently no-ops with an empty
// graph when the wall cache isn't ready (caller retries next tick,
// same as region_classifier).
void BuildForArea(void* area);

// Per-tick door re-snapshot until the door set stabilises. The initial
// SnapshotDoors call inside BuildForArea can race a partially-populated
// server-object array — doors whose handles haven't resolved yet are
// silently skipped by AreaObjectIterator. This call re-runs the door
// snapshot each tick and commits once the count repeats for N ticks
// (or a hard cap is hit). No-op once committed, when the area doesn't
// have a built graph yet, or when `area` doesn't match the cached
// owner.
void MaybeRefreshDoors(void* area);

// True iff a decomposition exists for `area`.
bool HasGraphForArea(void* area);

// Reset all state. Called when the player loses an area.
void Reset();

// Look up the region descriptor at `worldPos`. On success writes the
// localised label into `outBuf`, a small integer signature into
// `outSig` (same byte layout convention as region_classifier::
// LookupShapeAt — kind in low byte, kind-specific fields in upper
// bytes), and the perceptual cluster id into `outClusterId`.
//
// `outClusterId` is the UFFind root of the snapped node, stable for
// the lifetime of the build. Two adjacent labelled regions get
// distinct ids; walking inside one cluster keeps the id constant.
// Callers use this as the trigger key for "did the player change
// perceptual region" — much better signal than .lyt-room change.
//
// Sentinels: `kClusterIdNone` on no-graph / out-of-snap / all-blocked
// (no announcement); `kClusterIdOpenArea` when the open-area fallback
// label fires (one announcement per open-area entry). See enum docs.
//
// `allowDiagLog` (default true) controls the WALL-FILTERED /
// ALL-BLOCKED diagnostic emissions inside LookupAt. Speech-time call
// sites leave it default — those fire bounded by cluster transitions
// and we want full visibility. The per-tick trigger probe in
// transitions::Tick passes `false` so the same position evaluated
// every frame at 60 fps doesn't flood the log with identical lines.
// The bounded call sites still emit the same diagnostics whenever a
// real transition resolves, so no information is lost.
//
// Returns false when the graph isn't built, the position sits outside
// the bounding box, or no region matches (and no open-area fallback).
bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig,
              int& outClusterId,
              bool allowDiagLog = true);

// Diagnostic: dump the current graph to the patch log. Useful for
// tuning thresholds while iterating on the algorithm.
void DumpGraphToLog();

}  // namespace acc::wall_topology
