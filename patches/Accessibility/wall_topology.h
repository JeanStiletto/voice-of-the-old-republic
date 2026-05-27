// Wall-topology decomposition — the patch's single source of truth
// for "what's the spoken label for the region around this world
// position?". Three consumers:
//
//   1. transitions    — in-world walking adapter (player position).
//   2. view_mode      — locked virtual cursor in world space.
//   3. map_ui_cursor  — inverse-projected map cursor world position.
//
// All three share one decomposition, built once on area-enter, so the
// player, the view-mode cursor, and the map cursor can never disagree
// about whether they're on a corridor, in a junction, or at a dead-end.
//
// Algorithm (Path 3, 2026-05-13): consume the engine's per-area nav
// graph and classify each node by its CSR-adjacency degree, with
// downstream merge passes that collapse densely-authored hub clusters
// and straight corridor chains into single perceptual regions.
// Implementation details, merge-pass rationale, and tunable thresholds
// live in wall_topology.cpp.
//
// History: an earlier per-.lyt-room walkmesh-probe classifier
// (`region_classifier`) preceded this module. It was retired
// 2026-05-27 — .lyt-rooms over-segment KOTOR areas into corridor-sized
// cells, and centroid-in-wall failure modes left many rooms silent.
// The nav graph is what BioWare ships and what the engine itself
// trusts for path solving — same ground truth the level designers
// used.

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
// graph when the wall cache isn't ready (caller retries next tick).
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
// `outSig` (kind in low byte, kind-specific fields in upper bytes),
// and the perceptual cluster id into `outClusterId`.
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
// `requireWallReachable` (default true) gates each candidate cluster
// on a clear segment from `worldPos` to the candidate's nearest node.
// Defends in-world adapters (walking, view mode) against speaking the
// label of an adjacent corridor through a wall — the player has to
// physically reach the cluster, so LOS is the right gate. The map UI
// cursor passes `false`: it isn't an embodied probe (cursor positions
// can land just off the road's walkmesh, with a building wall between
// the cursor and the road cluster, so the filter wrongly rejects every
// nearby labelled node and the position falls through to "Offene
// Fläche"). The map UI cursor wants distance-only snap — keep the snap
// radius small so the cluster assignment still tracks position, just
// don't insist on LOS the cursor doesn't have.
//
// Returns false when the graph isn't built, the position sits outside
// the bounding box, or no region matches (and no open-area fallback).
bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig,
              int& outClusterId,
              bool allowDiagLog = true,
              bool requireWallReachable = true);

// Diagnostic: dump the current graph to the patch log. Useful for
// tuning thresholds while iterating on the algorithm.
void DumpGraphToLog();

}  // namespace acc::wall_topology
