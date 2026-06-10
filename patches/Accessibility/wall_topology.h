// Single source of truth for "spoken label for the region at world
// position P". Consumed by transitions (player), view_mode (cursor),
// map_ui_cursor (inverse-projected). One decomposition per area; all
// three can't disagree about corridor vs junction vs dead-end.
//
// Algorithm: classify per-node by CSR-adjacency degree on the engine's
// nav graph, then collapse hub clusters and straight corridor chains
// into perceptual regions. Implementation lives in wall_topology.cpp.
//
// Replaced an earlier per-.lyt-room walkmesh-probe classifier — .lyt-
// rooms over-segment KOTOR into corridor-sized cells with frequent
// centroid-in-wall failures. The nav graph is what the engine itself
// trusts for path solving.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "engine_offsets.h"

namespace acc::wall_topology {

// sig & 0xff = kind. Transitions defers the announce for KindPlatz (big
// merged spaces — the player may be crossing through). KindRoom is a
// merged small space that announces immediately. Both render the same
// neutral "Bereich" label; the kind split exists only to drive that
// delay distinction, never surfaced to the player.
enum Kind {
    KindDeadEnd     = 0,
    KindCorridor    = 1,
    KindJunction    = 2,
    KindOpenArea    = 4,
    KindPlatz       = 5,
    KindRoom        = 6,
};

// Real cluster ids are non-negative UFFind roots. Sentinels:
//   None     — no graph / outside snap radius / all candidates filtered.
//              "No fact yet, do nothing."
//   OpenArea — synthetic "Offene Fläche" label. Stable identity so the
//              entry transition fires exactly once.
constexpr int kClusterIdNone     = -1;
constexpr int kClusterIdOpenArea = -2;

// Idempotent on same area pointer. Requires the wall cache to be
// populated; no-ops with empty graph until then.
void BuildForArea(void* area);

// Re-snapshots doors until the door set stabilises (initial snapshot
// can race a partially-populated server-object array). Commits when the
// count holds for N ticks. No-op once committed.
void MaybeRefreshDoors(void* area);

bool HasGraphForArea(void* area);

void Reset();

// outSig packs kind in low byte + kind-specific bits above.
// outClusterId is the UFFind root — stable trigger key for "did the
// player change perceptual region", much better signal than .lyt-room
// change.
//
// allowDiagLog: speech-time call sites use default; per-tick probe in
// transitions::Tick passes false to avoid 60fps log spam (bounded sites
// still emit on real transitions).
//
// requireWallReachable: in-world adapters (walking, view mode) want LOS
// gating so they don't speak a cluster through a wall. The map UI
// cursor passes false — cursor positions can sit just off-walkmesh with
// a wall between cursor and road cluster, so LOS wrongly rejects every
// nearby node. Map cursor wants distance-only snap.
//
// False on no-graph / outside bounding box / no matching region.
//
// outLabel receives the full region label — never truncated. It is
// cleared on every call (including the false-returning paths), so callers
// can read it unconditionally.
bool LookupAt(void* area, const Vector& worldPos,
              std::string& outLabel, int& outSig,
              int& outClusterId,
              bool allowDiagLog = true,
              bool requireWallReachable = true);

void DumpGraphToLog();

}  // namespace acc::wall_topology
