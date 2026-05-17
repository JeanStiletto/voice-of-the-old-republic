// Shared per-area region cache + shape classifier.
//
// Layer: spatial/ (consumes engine_area's walkmesh-edge cache via
// spatial_change_detector + engine_area room ops + guidance_pathfind's
// path-graph offsets + strings for localised text).
//
// Single source of truth for "what's the spoken label for the region
// around this world position?". Three consumers:
//
//   1. `acc::map_ui_cursor` — feeds the in-map virtual cursor's world
//      coordinate (inverse-projected from map pixels).
//   2. `acc::view_mode`     — feeds the locked virtual cursor that lives
//      in world space when view-mode is active.
//   3. `acc::transitions`   — feeds the player's live world position so
//      walking through the area speaks the same region labels the map
//      cursor announces when the player parks on them.
//
// All three share the same cache: built once per area on area-enter
// (`BuildCacheForArea`) and queried via `LookupShapeAt` or
// `LookupRoomShape`. The cache stores one classification per .lyt-room
// (centroid sample + path-graph-adjacency union for junctions/dead-ends);
// text-equality dedup on the caller side collapses adjacent rooms whose
// labels happen to be identical, so the .lyt-room partition (which over-
// segments KOTOR areas into corridor-sized cells) doesn't translate into
// audible spam.
//
// Decision history:
//   - 2026-05-12 — chose per-area pre-classification over per-position
//     probing (cache stability over micro-granularity; the user explicitly
//     asked for "not position-dependent"). Documented in
//     map_ui_cursor.cpp comment block.
//   - 2026-05-13 — extracted shape cache + classifier from map_ui_cursor
//     into this shared module so the in-world (walking) and view-mode
//     adapters can read the same cache the map cursor uses, building the
//     cache once on area-enter rather than once per map-open. Walking-
//     adapter motivation: doors-open-later topology shifts are already
//     covered by the path-graph adjacency union (closed doors look like
//     walls to the 4-ray probe but the graph already includes the
//     connection across them).

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"  // Vector

namespace acc::region {

// Maximum rooms the cache can hold. Sized for the largest vanilla KOTOR
// area (Manaan East Central is ~52 rooms; Endar Spire / Taris bays are
// smaller). 64 leaves headroom for mod content without ballooning the
// fixed-size storage. If a future area exceeds this we log + truncate at
// build time — never silently lose rooms.
constexpr int kMaxRooms = 64;

// Source of the room's representative point — only surfaced for the
// area-enter build log so we can spot rooms that fall back to
// path-points (typically "void" rooms whose CSWSRoom.surface_mesh is
// null).
enum class RepSource : uint8_t {
    Walkmesh = 0,
    PathPoint = 1,
};

// Build (or rebuild) the shape cache for `area`. Idempotent on the same
// area pointer — if the cache is already built for `area` the call is a
// silent no-op so callers can invoke it eagerly without coordinating.
//
// Walks every CSWSRoom in the area, classifies its shape at a
// representative point, then augments junction/dead-end labels with
// path-graph adjacency (so a closed door that the walkmesh probe reads
// as a wall still contributes its direction to the room's exit set).
//
// Requires `acc::spatial::change_detector::GetCachedWalls()` to be
// populated for the area — if the wall cache isn't ready yet the build
// silently leaves the cache empty and the call can be retried on a
// later tick.
void BuildCacheForArea(void* area);

// True iff a non-empty cache exists for `area`. Returns false when
// `area` is null, when the cache has never been built, or when the
// cache was built for a different area.
bool HasCacheForArea(void* area);

// Reset all cache state. Called when the player loses an area (mid-load
// hand-off, main-menu return). Cheap; safe to call from any tick.
void Reset();

// Direct cache lookup by .lyt-room index. Returns false on:
//   - cache not built / built for a different area
//   - roomIdx outside [0, kMaxRooms)
//   - room wasn't populated at build time (e.g. surface_mesh null AND
//     no path-graph point landed inside it)
//
// On success, copies the localised label into outBuf and writes the
// classification signature into outSig. Signature byte layout:
//   byte 0: kind (1=off-path / 2=open area / 3=corridor NS /
//                  4=corridor EW / 5=dead-end / 6=junction)
//   byte 1: kind-dependent — long-axis index for dead-ends, direction
//           bitmask for junctions, quantised primary axis for corridors
//   byte 2: kind-dependent — secondary metric (quantised axis-sum, etc.)
bool LookupRoomShape(void* area, int roomIdx,
                     char* outBuf, size_t bufSize, int& outSig);

// Lookup with world-position fallback chain:
//   1. `GetRoomAtIndexed(area, world) → idx` (cache hit if populated).
//   2. Miss → snap to the nearest cached room's representative point
//      (`rep[r]`) and reuse its label. Same room from any nearby
//      position; transitions still announce because the nearest changes.
//
// On success outRoomIdx is set to the resolved room index (the original
// `GetRoomAtIndexed` result, *not* the snap-target). Pass nullptr if the
// caller doesn't care. Caller is responsible for fog-of-war gating
// before invoking — the cache itself doesn't know whether a cell has
// been revealed.
bool LookupShapeAt(void* area, const Vector& world,
                   char* outBuf, size_t bufSize, int& outSig,
                   int* outRoomIdx = nullptr);

// Walkmesh-shape kind from the 4-ray probe at a position. Values match the
// `kind` byte that `LookupRoomShape` / `LookupShapeAt` pack into outSig's
// low byte (documented above). Exposed as a typed enum so non-cache
// consumers (wall_topology, future shape gates) can sanity-check graph-
// derived classifications against the actual walkmesh geometry without
// going through the per-room cache.
enum class ShapeKind : uint8_t {
    Unknown    = 0,  // wall cache not ready / probe failed (treat as
                     // "trust the caller's other signal")
    OffPath    = 1,
    OpenArea   = 2,
    CorridorNS = 3,
    CorridorEW = 4,
    DeadEnd    = 5,
    Junction   = 6,
};

// One-shot walkmesh probe at `pos`. Pulls the cached wall edges from
// spatial::change_detector internally, runs the same 4-ray classifier the
// per-room cache uses, returns just the kind byte. Returns ShapeKind::
// Unknown when the wall cache isn't built yet — callers should NOT use
// Unknown as a "geometry says X" signal; treat it as "no data, fall back
// to whatever you'd do without me".
ShapeKind ProbeShapeAt(const Vector& pos);

// Raw single-ray walkmesh probe. Casts from `pos` in direction `(dx,dy)`
// (need not be unit-length; normalised internally) and returns the
// distance in metres at which it first crosses a walkmesh edge, or
// `kProbeLenWu` (25m) if the ray clears the probe range. Used for ad-
// hoc geometry queries — e.g. wall_topology's per-door diagnostic that
// reports how far walkmesh extends in 4 directions from a door.
//
// Returns -1.0 when the wall cache isn't built yet (caller should
// treat -1 as "no data").
float ProbeDistance(const Vector& pos, float dx, float dy);

// Rotated-axis alcove test. Probes at `pos` with the 4-ray pattern (one
// "forward" along the supplied axis, two perpendicular, one back) and
// returns true when the result matches the alcove signature: forward
// probe > 2m AND the other three ≤ 2m.
//
// Designed for wall_topology to check whether a graph-degree-1 nav node
// is a real alcove the player can walk into. The default `ProbeShapeAt`
// uses cardinal N/E/S/W axes and misses alcoves whose entrance is on a
// diagonal (e.g. NW) — those read as junctions because the cardinal
// rays clip walls at oblique angles. Spinning the probe set to align
// with the actual graph-edge direction (parent → dead-end) removes that
// bias.
//
// Returns true if the wall cache isn't available (fail-open: callers
// should not over-filter on missing data).
bool IsAlcoveAlongAxis(const Vector& pos, float forwardX, float forwardY);

}  // namespace acc::region
