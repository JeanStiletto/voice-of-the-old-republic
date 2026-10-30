// Single source of truth for "spoken label for the region at world
// position P". Consumed by transitions (player), view_mode (cursor),
// map_ui_cursor (inverse-projected). One decomposition per area; all
// three can't disagree about corridor vs junction vs dead-end.
//
// Algorithm: classify per-node by CSR-adjacency degree on the engine's
// nav graph, then collapse hub clusters and straight corridor chains
// into perceptual regions. Implementation lives in room_topology.cpp.
//
// Replaced an earlier per-.lyt-room walkmesh-probe classifier — .lyt-
// rooms over-segment KOTOR into corridor-sized cells with frequent
// centroid-in-wall failures. The nav graph is what the engine itself
// trusts for path solving.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine_offsets.h"

namespace acc::room_topology {

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
//   OpenArea — synthetic neutral-"Bereich" fallback label. Stable
//              identity so the entry transition fires exactly once.
// (A runtime coverage-gap probe with its own sentinel ids lived here
// briefly in 2026-08; replaced by build-time walkmesh seeding — see
// SeedCoverageGapNodes in room_topology.cpp — which gives gap regions
// real nodes and real cluster ids instead of a parallel announce path.)
constexpr int kClusterIdNone      = -1;
constexpr int kClusterIdOpenArea  = -2;

// One exit entry of a region label, exactly as it appears in the label
// text ("Nord", "Tür Süd-West nach X"), and the world point it leads to —
// what 3D exit descriptions (positional_speech::SpeakExitList) place it at.
// Recorded while the label is built; the label itself never changes.
// Empty text = an exit the label names only through its axis word
// ("Korridor Nord-Süd", "Bereich Ost-West"): it has a real point but no
// entry of its own, so 3D speech skips it and only camera_orient (N) uses it.
struct ExitMark {
    std::string text;
    Vector      pos;
};

// Idempotent on same area pointer. Requires the wall cache to be
// populated; no-ops with empty graph until then.
//
// Callers are expected to re-invoke each tick until HasGraphForArea is
// true (transitions.cpp does), because the engine can have an area live
// before its nav-graph data is readable. That retry is capped: an area
// whose graph never becomes readable is abandoned after ~2s of ticks
// rather than re-probed every frame forever. Call ResetNavGraphRetry on
// genuine area entry so an abandoned area recovers when re-entered.
void BuildForArea(void* area);

// Clears the per-area nav-graph retry latch, re-arming BuildForArea for
// an area it previously gave up on. Call from the area-change path only —
// calling it every tick would restore the unbounded retry it exists to
// bound.
void ResetNavGraphRetry();

// Re-snapshots doors until the door set stabilises (initial snapshot
// can race a partially-populated server-object array). Commits when the
// count holds for N ticks. No-op once committed.
void MaybeRefreshDoors(void* area);

// Landmark→door matching pass. Runs inside BuildForArea; also invoked
// by transitions.cpp after a mid-session landmark-cache rebuild (map
// notes can be script-enabled long after area entry) so late-arriving
// landmarks still claim their door labels. Safe to re-run: iterates
// the current landmark cache against the current door snapshot.
void AttachLandmarksToDoors(void* area);

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
// zTrusted: worldPos.z is a real in-world height (player position).
// Enables the vertical filters: candidates whose calibrated floor
// height differs from worldPos.z by more than the same-floor tolerance
// (kWallCrossZToleranceM) are rejected like wall-blocked ones, and the
// wall test runs with the node lifted to its floor height so the
// z-guard inside SegmentCrossesWalkmesh sees the ledge walls between
// levels (Ebon-Hawk hull: the raised spine corridor's label leaked to
// a notch 3m below through a ledge the z=0 endpoint made invisible).
// Callers with synthetic / possibly-stale z (map cursor inverse
// projection, view-mode cursor) leave it false and keep pure-2D snap.
// NOTE: distance stays 2D in all cases — putting dz into the DISTANCE
// against the always-zero node z is the variant that broke Taris
// Sewers; the veto compares against per-node calibrated floor heights
// instead.
//
// False on no-graph / outside bounding box / no matching region.
//
// outLabel receives the full region label — never truncated. It is
// cleared on every call (including the false-returning paths), so callers
// can read it unconditionally.
//
// outExits (optional) receives the label's exit entries with their world
// points, in label order; cleared on every call like outLabel. Empty for
// labels without exits (the open-area fallback, a roomy area with no
// axis). Axis-only labels carry their two ends as empty-text marks.
bool LookupAt(void* area, const Vector& worldPos,
              std::string& outLabel, int& outSig,
              int& outClusterId,
              bool allowDiagLog = true,
              bool requireWallReachable = true,
              bool zTrusted = false,
              std::vector<ExitMark>* outExits = nullptr);

// What transitions.cpp needs to time a cluster's announce without a
// second label resolution.
struct ClusterInfo {
    int    kind      = -1;     // see Kind
    // Longest bounding-box side of the member nodes, metres. Zero for a
    // single node. Drives the dwell gate and the Großer-Bereich flag.
    float  extentM   = -1.0f;
    // How wide the space is on the floor: the larger of extentM and the
    // narrowest wall-to-wall width through the members (the clearance
    // probe). Gives a single-node crossing its real size. Drives the
    // centre rule's "near" radius.
    float  footprintM = -1.0f;
    Vector centre    = {0.0f, 0.0f, 0.0f};  // what its exit words are measured from
    // Big enough to speak as "Großer Bereich" (the noun swap applies only
    // to area labels; the flag is extent alone).
    bool   largeArea = false;
};

// False (out left at its defaults) on sentinel ids, unknown ids, or no
// graph.
bool GetClusterInfo(void* area, int clusterId, ClusterInfo& out);

void DumpGraphToLog();

}  // namespace acc::room_topology
