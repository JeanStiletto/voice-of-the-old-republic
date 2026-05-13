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

// Build the wall-topology decomposition for `area`. Idempotent on the
// same area pointer. Requires `acc::spatial::change_detector::
// GetCachedWalls()` to be populated; silently no-ops with an empty
// graph when the wall cache isn't ready (caller retries next tick,
// same as region_classifier).
void BuildForArea(void* area);

// True iff a decomposition exists for `area`.
bool HasGraphForArea(void* area);

// Reset all state. Called when the player loses an area.
void Reset();

// Look up the region descriptor at `worldPos`. On success writes the
// localised label into `outBuf` and a small integer signature into
// `outSig` (same byte layout convention as region_classifier::
// LookupShapeAt — kind in low byte, kind-specific fields in upper
// bytes). Returns false when the graph isn't built, the position
// sits outside the bounding box, or no region matches.
bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig);

// Diagnostic: dump the current graph to the patch log. Useful for
// tuning thresholds while iterating on the algorithm.
void DumpGraphToLog();

}  // namespace acc::wall_topology
