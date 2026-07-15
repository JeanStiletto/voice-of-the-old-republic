// Wall-topology decomposition — see wall_topology.h for goal + algorithm.
//
// EXPERIMENTAL — alternative-direction-calculation-system branch.
//
// Path 3 (2026-05-13) consumes the engine's per-area nav graph (the same
// path_points / path_connections fields the engine itself uses for AI
// pathfinding) and classifies each node by its CSR-adjacency degree:
//   - degree 1               → dead end pointing at its single neighbour
//   - degree 2, same room    → corridor (axis defined by both neighbours)
//   - degree 2, cross-room   → transition (doorway between two rooms)
//   - degree ≥ 3             → junction (comma-separated octant list)
//
// Previous revisions (face-graph flood-fill, parallel-pair sweep) all
// failed empirically on K1's authoring style — see commit history on this
// branch. The nav graph is what BioWare ships, and the engine itself
// trusts it for path solving, so we're operating on the same ground truth
// the level designers were aware of.

#include "wall_topology.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "engine_area.h"        // AreaObjectIterator, GetObjectKind, GameObjectKind::Door, GetObjectPosition, kDoorTransitionDestOffset, SegmentCrossesWalkmesh
#include "engine_navgraph.h"
#include "engine_reads.h"       // ExtractTextOrStrRef — transition-destination loc-string read
#include "log.h"
#include "spatial_change_detector.h"  // GetCachedWalls — seam-filtered perimeter cache
#include "strfmt.h"             // Format — heap-backed printf, never truncates announcements
#include "strings.h"
#include "transitions.h"        // IterateLandmarks + MarkLandmarkClaimedByDoor — landmark→door matching pass

namespace acc::wall_topology {

namespace {

// Walkmesh-probe primitives.
//
// Single-ray casts against the cached perimeter walls. Used for two
// gates inside this module:
//   - door diagnostics (BuildForArea logs 4-cardinal probe distances at
//     each door so we can see how the walkmesh looks at the threshold);
//   - dead-end alcove agreement (WalkmeshAgreesDeadEnd spins the 4-ray
//     probe along the graph-edge axis and checks the alcove signature
//     before believing a graph-degree-1 dead-end claim).
//
// Lived in region_classifier until 2026-05-27; absorbed here when the
// region module was retired in favour of pure nav-graph classification.

constexpr float kProbeLenWu = 25.0f;

float ProbeWall(const acc::engine::WallEdge* walls, int wallCount,
                const Vector& origin, float dx, float dy) {
    Vector b;
    b.x = origin.x + dx * kProbeLenWu;
    b.y = origin.y + dy * kProbeLenWu;
    b.z = origin.z;
    Vector hit;
    if (acc::engine::SegmentCrossesWalkmesh(walls, wallCount,
                                            origin, b, hit)) {
        float ddx = hit.x - origin.x;
        float ddy = hit.y - origin.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    return kProbeLenWu;
}

// Distance to first wall along (dx,dy). Returns kProbeLenWu when the
// ray clears the probe range, -1.0 when the wall cache is empty.
float ProbeDistance(const Vector& pos, float dx, float dy) {
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        return -1.0f;
    }
    float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 1e-6f) return -1.0f;
    return ProbeWall(walls, wallCount, pos, dx / mag, dy / mag);
}

// 4-ray alcove test rotated to align with (forwardX,forwardY): true
// when the forward probe clears 2m AND the three perpendiculars/back
// all hit a wall within 2m. Fail-open if the wall cache isn't ready.
bool IsAlcoveAlongAxis(const Vector& pos, float forwardX, float forwardY) {
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        return true;  // no data — fail open
    }
    float magSq = forwardX * forwardX + forwardY * forwardY;
    if (magSq < 1e-6f) return true;
    float inv = 1.0f / std::sqrt(magSq);
    float fx  = forwardX * inv;
    float fy  = forwardY * inv;
    float px  = fy;
    float py  = -fx;
    float dF = ProbeWall(walls, wallCount, pos,  fx,  fy);
    float dB = ProbeWall(walls, wallCount, pos, -fx, -fy);
    float dR = ProbeWall(walls, wallCount, pos,  px,  py);
    float dL = ProbeWall(walls, wallCount, pos, -px, -py);
    int shortCount = 0;
    if (dB <= 2.0f) ++shortCount;
    if (dR <= 2.0f) ++shortCount;
    if (dL <= 2.0f) ++shortCount;
    return shortCount == 3 && dF > 2.0f;
}

// 8-ray clearance probe. Casts kProbeLenWu rays on the 8 octants from
// `pos` against the cached perimeter walls, filling outRays[8] in octant
// order (E, NE, N, NW, W, SW, S, SE — matching OctantFromVector bucket
// numbering). Each entry is the distance to the first wall hit, or
// kProbeLenWu when the ray clears the probe range. Pure diagnostic for
// now — the clearance-dump uses it to characterise per-node openness so
// we can pick a principled "is this an open space" statistic before
// wiring any clearance-driven merge.
void ProbeClearance8(const acc::engine::WallEdge* walls, int wallCount,
                     const Vector& pos, float outRays[8]) {
    static const float kDirX[8] = { 1.0f,  0.70710678f, 0.0f, -0.70710678f,
                                   -1.0f, -0.70710678f, 0.0f,  0.70710678f};
    static const float kDirY[8] = { 0.0f,  0.70710678f, 1.0f,  0.70710678f,
                                    0.0f, -0.70710678f,-1.0f, -0.70710678f};
    for (int k = 0; k < 8; ++k) {
        outRays[k] = ProbeWall(walls, wallCount, pos, kDirX[k], kDirY[k]);
    }
}

// Tunable parameters.

// Re-use the navgraph cap so per-node arrays line up with the snapshot.
constexpr int kMaxNodes = acc::engine::navgraph::kMaxNodes;

// Snap radius for runtime LookupAt: any player position more than this
// many world units (KOTOR = metres) from the nearest graph node falls
// back to the neutral "Bereich" label. 15m matches the typical room-diagonal in
// vanilla content while still catching the central plaza / exterior
// case where the nav graph is sparse. Tune from log evidence.
constexpr float kMaxSnapM = 15.0f;

// Clustering runs in two passes (see BuildForArea). The old six merge
// passes were two operations wearing six coats, so they fold to:
//   Pass 1 — core merge: ONE nav-edge walk that unions a clear, door-free
//            edge whose endpoints are the same kind. Two rules: space
//            (both are a place the player stands in — degree-≥3 hub or
//            open/room by probe) and corridor (both straight degree-2).
//            Space↔passage never merge. The old junction / room / open
//            rules collapsed into the single space rule — separating them
//            only left door-less interiors fragmented at each class seam.
//   Pass 2 — straggler absorb: fold a degree-≤2 corner/spoke that matched
//            no core rule into an adjacent core (graph-adjacent, or bbox-
//            contained for graph-unattached nodes). The old hub + bbox
//            absorption passes.
// The constants below are the gates, tagged by the rule that uses them.
//
// Pass 1 / space rule — vertical gate. The planar distance gate is the
// unified space cap (kOpenMergeRadiusM, applied as kSpaceCapSq in the
// Pass 1 body); this Z gate stops multi-floor stacks (Sith base, Endar
// Spire decks) from collapsing vertically-aligned nodes. The old separate
// 8m junction cap is gone — merging two degree-≥3 hubs is just one case of
// the space rule now (this still collapses the Dias-Apartment pattern of
// two Kreuzungen ~6m apart into one perceptual hub).
constexpr float kMergeMaxZM = 1.0f;

// (The old density-based merge — pack ≥N nav nodes within 3m → one
// cluster — was retired 2026-06-09. It was a crude proxy for "small
// enclosed room"; the clearance probe now measures room-shape directly
// and the space rule subsumes it.)

// Pass 2 / straggler absorb — folds a degree-≤2 corner/spoke into an
// adjacent multi-node core (the merged successor of the old hub + bbox
// absorption passes). It targets the K1 "two-hub star" small-room pattern
// (Endar Spire quarters: two deg-4 nodes merge into a core, but the
// surrounding degree-1/2 patrol spokes would label independently — 5
// shape readings as the player walks) and the building-corner stragglers
// in open areas. kStragglerAbsorbMaxM is a sanity cap on the corner→core
// edge length — adjacency (a real nav edge) is the connector, not
// distance; 16m covers the Oberstadt store corners (9.9m off the frontage)
// with headroom while excluding cross-plaza edges. Tune from harvested
// absorb logs. (The target-must-be-multi-node and door-veto gates that
// keep corridors from being eaten live in the pass body.)
constexpr float kStragglerAbsorbMaxM    = 16.0f;

// Pass 1 / corridor rule — collapse straight degree-2 chain runs
// into one cluster. K1 corridors are authored as 5-15 degree-2 patrol
// nodes ~3-5m apart along a single axis; without merging each becomes
// its own cluster and a 15m hallway re-announces "Korridor Ost-West"
// 3+ times as the player walks it. The 2026-05-21 evening logs showed
// "Korridor Ost-West" spoken 13x, "Korridor Nord, West" 12x, etc.
// (docs/room-shape-improvements.md item 1).
//
// A node is "straight" iff its two outgoing edge vectors point in
// roughly opposite directions (cos(angle) <= kCorridorStraightCosMax).
// Two adjacent straight degree-2 nodes union. Sharp bends (~90° kinks)
// fail the straightness test and break the chain — an L stays as two
// corridors meeting at the bend, which matches the perceptual model (the
// player feels the corridor turn, so it reads as two directions).
//
// Tolerance is +/- 35 deg of straight (cos -0.82): loose enough to keep a
// gently curving hall as ONE corridor (a series of ~30 deg patrol-node
// bends used to shatter into one cluster per node — the Ebon Hawk
// crew-quarters approach split into four), tight enough that a real ~90 deg
// corner still breaks. The +/-35 cut also tracks navigability: in a narrow
// hall a bend past ~35 deg drifts you into the wall if you hold the end
// bearing, so those genuinely need to stay separate turn cues. Tune from
// log evidence — raise toward -0.9 (+/-25 deg) if gentle curves over-merge,
// drop toward -0.7 (+/-45 deg) if real halls still fragment.
//
// A *sharp* bend isn't left as its own one-node cluster, though: the
// corner-fold step (right after the Pass 1 core merge) folds it into the
// adjacent corridor it continues straightest into — its straight edge
// extends that corridor, its turning edge becomes the boundary to the next
// one. So an L reads as corridor-A then corridor-B (two axes meeting at the
// turn), never as a lone "corner" announcement wedged between them.
//
// Gates beyond per-node straightness (shared by every Pass 1 rule):
//   - door-on-edge veto (preserves authored room boundaries);
//   - wall-on-edge veto (cheap insurance — shouldn't fire on real
//     corridor neighbours, but covers degree-2 nodes the engine
//     places across a wall in adjacency tables).
// A straight node adjacent to a junction hub can't merge into it: the hub
// is space (degree-≥3 or open/room) and a corridor node is passage, and
// passage↔space never merge, so the corridor stays a distinct core.
constexpr float kCorridorStraightCosMax = -0.82f;

// Pass 1 / open rule — the large-space analog of the corridor and room
// rules: it folds the several nav nodes of one open plaza/chamber into a
// single cluster, so the player hears one place instead of flipping
// between adjacent Kreuzungen (the Slums central-plaza re-fire — clusters
// 110/118 swap on every short loop).
//
// A node is "open" when at least kOpenMinRays of its 8 clearance rays
// (cast at the calibrated floor height — see the clearance block) clear
// kOpenRayDistM. Two open nodes that are GRAPH-ADJACENT (share a nav
// edge), within kOpenMergeRadiusM, mutually wall-clear and door-free
// union into one open cluster. Adjacency is the connector — proximity
// alone bridged junction throats (the Oberstadt store/cantina over-merge);
// the engine's edges define what is actually contiguous. See the Pass 1
// body for the full rationale + Slums adjacency audit.
//
// Threshold rationale (measured 2026-06-09, Slums vs Manaan Östliches
// Zentrum): a pure corridor X-junction has at most as many long rays as
// it has arms, so its over-8m count tops out at ~3-5; a genuinely open
// room needs its surrounding walls pulled back and reaches 6-8. Setting
// kOpenMinRays=6 admits real rooms and rejects corridor intersections by
// construction — confirmed both directions on real content (Slums
// courtyard junctions 6-8 → merge; Manaan corridor junctions 3-5 →
// stay). The boundary lives at 5 (≈7m spaces) — nuisance, not safety.
//
// kOpenMergeRadiusM=16m is now a secondary sanity cap on the length of an
// open→open graph edge we'll coalesce across (adjacency is the primary
// connector). It covers every real Slums plaza edge (the largest open
// open-coalesce gap there is 16.0m, node 110↔118 is 15.3m and is a direct
// nav edge) while leaving long cross-plaza edges (e.g. Oberstadt 11↔18 at
// 23.7m) as separate spaces — current behaviour. The earlier "raise 12→16
// to un-strand node 110" tuning is now moot: 110 was never stranded in the
// graph, only in the old proximity form. The archway residual (two open
// rooms joined by a wide door-less opening merging across it) is now gated
// by adjacency too — they only merge if the engine authored an edge across
// the opening. Tune the cap from harvested cluster sizes if a real plaza
// turns out to need a longer adjacent edge.
constexpr float kOpenRayDistM     = 8.0f;
constexpr int   kOpenMinRays      = 6;
constexpr float kOpenMergeRadiusM = 16.0f;

// Shape-diagnostic thresholds (the per-node class log, NOT behaviour).
// Reconnaissance for the planned generalized shape-classifier: the open
// band uses 8m/6-rays; rooms and corridors sit at a smaller scale, so
// they get their own measured cuts here. Pure logging — only nodeOpen
// (above) drives merges. Tune from the harvested class distribution.
//   kStructRayDistM   — "arm/exit" scale for corridor (2 opposite) and
//                       junction (3+) detection. Corridor nodes between
//                       close junctions don't reach 8m, so 5m is the cut.
//   kRoomNearM        — below this a ray is "wall-hugging", not openness.
//   kRoomBoundedMaxM  — a room must be enclosed within this (no long arm).
//   kRoomMinMidRays   — a real room is evenly open: this many rays must
//                       land in the [near, open) mid-band. Separates a
//                       room interior from a corner/doorway node (mostly
//                       sub-near rays + one escape).
constexpr float kStructRayDistM   = 5.0f;
constexpr float kRoomNearM        = 2.0f;
constexpr float kRoomBoundedMaxM  = 10.0f;
constexpr int   kRoomMinMidRays   = 3;

// Room-class probe. A node is "room" when it sits in a bounded, evenly-open
// space: 0-1 rays clear the open scale, max ≤ kRoomBoundedMaxM, and
// ≥ kRoomMinMidRays rays land in the [near, open) mid-band (the evenness
// test that separates a real room interior from a wall-hugging corner /
// doorway node). This flag, like nodeOpen, no longer drives a merge rule of
// its own — it marks the node as SPACE for the unified Pass 1 space rule.
//
// (Replaced the old density pass (1b): density — ≥N nav nodes within 3m —
// was a crude proxy for "packed small room"; the clearance probe measures
// room-shape directly. The validated 2026-06-09 harvest fires it where
// small rooms are (Ebon Hawk 21, Apartments 9) and not outdoors (Slums 3).)

// Area geometry cue: the only shape we speak for a "Bereich" is its long
// axis, and only when the space is clearly elongated. At the cluster
// centroid the full extent across each of the 4 axes is the sum of its
// two opposite rays; the space is "elongated" when the longest axis
// exceeds kAxisElongRatio × the perpendicular one. Square / round / open
// spaces fall below the ratio and get no axis word (just "Bereich").
constexpr float kAxisElongRatio = 1.8f;

// Large-area threshold: a cluster whose member-node bounding box has its
// longer side ≥ this many metres speaks as "Großer Bereich" instead of
// "Bereich". Pure label swap — axis, exits, kind and delay are unchanged;
// it just sets the expectation that wall/object cues are sparse across a
// space this big (the Kashyyyk Great Walkway's main cluster spans ~160 m,
// normal merged rooms ~20-30 m). Tuned conservatively so only genuinely
// large open spaces trip it; raise/lower after in-game feedback.
constexpr float kLargeAreaExtentMeters = 40.0f;

// Kind values now live in the public header (wall_topology.h) so
// transitions.cpp can branch on Platz for the delayed-announce path.
// Aliases here keep the local code compact.
constexpr int kKindDeadEnd  = KindDeadEnd;
constexpr int kKindCorridor = KindCorridor;
constexpr int kKindJunction = KindJunction;
constexpr int kKindOpenArea = KindOpenArea;
constexpr int kKindPlatz    = KindPlatz;
constexpr int kKindRoom     = KindRoom;

// Cached state for the current area.

// Per-area door snapshot. Captured on BuildForArea (alongside the
// nav-graph snapshot) so the per-edge "is there a door between these
// two nav points?" query doesn't have to re-iterate game_objects[] +
// SEH-read every time. Transition destination is the CExoLocString at
// CSWSDoor +0x3c8 — non-empty when this door is an area-transition
// trigger (cross-area door), empty for normal in-area doors.
struct DoorRecord {
    Vector pos;
    char   transitionDest[64];
    // CSWSDoor.loc_name @+0x39c — the door's own authored localized
    // name (e.g. "Lift", "Sicherheitstür", "Tür zu Bastilas Quartier").
    // Substituted as the noun in the FmtMapCursorDoor* formats; when
    // empty, the localized generic ("Tür"/"Door") falls back so output
    // matches the pre-loc-name behaviour for unnamed doors. Same field
    // Pillar 4 cycle narration speaks when Q/E lands on a door.
    char   locName[64];
    // Attached landmark map-note (e.g. "Zur Oberstadt"), populated by
    // AttachLandmarksToDoors during BuildForArea when a CSWCWaypoint
    // with has_map_note sits within kLandmarkDoorMatchMaxM of this
    // door's position. Preferred over transitionDest in cluster labels
    // (see RenderDoorDirection) — the landmark name is the content
    // author's canonical phrasing and reads cleaner than the raw
    // area-transition string. Empty when no landmark matched.
    char   landmarkName[64];
};

constexpr int kMaxDoors = 128;

struct AreaGraph {
    void*       area_owner   = nullptr;
    bool        built        = false;
    int         node_count   = 0;
    Vector      node_pos    [kMaxNodes];
    std::string node_label  [kMaxNodes];
    int         node_sig    [kMaxNodes];
    int         node_kind   [kMaxNodes];  // see kKind* constants
    // Frozen UFFind root per node, snapshotted at the end of BuildForArea
    // after all merge passes complete. Used as the perceptual-region
    // trigger key by transitions::Tick — walking inside one cluster
    // keeps the id constant, so cluster-change is a clean signal for
    // "the player just entered a different region".
    int         node_cluster_id[kMaxNodes];
    // Per-node "filtered" flag. Set to true on degree-1 clusters whose
    // walkmesh-shape gate rejected the alcove claim. The label is still
    // rendered (Sackgasse + direction) so the LookupAt rescue path can
    // emit it when no unfiltered candidate sits within range — recovers
    // the rare Bucket-3 case (alcove dropped by the geometry gate, no
    // labelled cluster nearby → would otherwise speak the neutral "Bereich").
    // The primary scan in LookupAt skips filtered nodes, so wall-curve
    // graph artefacts in inhabited regions stay silent (the core-merge and
    // straggler-absorb passes also absorb most such artefacts before the gate
    // runs; surviving filtered singletons are graph-isolated and almost
    // always genuine alcoves).
    bool        node_filtered  [kMaxNodes];
    int         door_count   = 0;
    DoorRecord  doors       [kMaxDoors];
};

AreaGraph g_graph;

// Door-snapshot stability tracking. The initial SnapshotDoors call from
// BuildForArea races the engine's per-area object population: doors whose
// handles haven't been registered yet in CGameObjectArray are silently
// skipped by AreaObjectIterator (the resolver returns "miss" on an
// unregistered id, see project_csws_area_handles.md). Pre-fix we'd lock
// in that incomplete door set for the rest of the session and not even
// re-entering the area recovered — only a full game restart.
//
// MaybeRefreshDoors re-snapshots each tick until the count stays
// constant for `kDoorStabilityRequiredStreak` ticks in a row, then
// commits. A hard cap exists for the edge case of script-spawned doors
// that genuinely arrive late: rather than retry forever, we commit
// whatever we have and log that we hit the cap.
//
// Cluster-classification (ClassifyCluster's FindDoorOnEdge calls) runs
// once during BuildForArea against whatever door set was visible at
// that moment. Door-set updates after the initial build don't
// retro-classify clusters. This is accepted lossy behaviour: any
// late-arriving door still shows up as a "Tür" exit through the
// per-edge query, and the worst case is that a cluster's classification
// is one door short — strictly better than missing the door entirely as
// today.
constexpr int kDoorStabilityRequiredStreak = 2;
constexpr int kDoorRetryCapTicks           = 60;

struct DoorStabilityState {
    bool committed   = false;
    int  last_count  = -1;
    int  streak      = 0;
    int  retry_ticks = 0;
};
DoorStabilityState g_doors_stability;

// Direction helpers.

// 8-way octant classifier. Engine frame: +X = East, +Y = North. Returns
// the Id corresponding to the octant the (dx,dy) vector points into.
acc::strings::Id OctantFromVector(float dx, float dy) {
    using acc::strings::Id;
    // atan2 gives radians in (-pi, pi] measured from +X (East) counter-
    // clockwise. Convert to a 0..8 bucket where 0 = East, 1 = Northeast,
    // …, 7 = Southeast.
    float theta = std::atan2(dy, dx);
    // Snap to nearest 45° bucket. Add half-bucket for round-to-nearest.
    constexpr float kPi  = 3.14159265358979323846f;
    constexpr float kStep = kPi / 4.0f;  // 45 degrees
    int bucket = static_cast<int>(std::floor((theta + kStep * 0.5f) / kStep));
    bucket = ((bucket % 8) + 8) % 8;
    switch (bucket) {
        case 0: return Id::DirEast;
        case 1: return Id::DirNortheast;
        case 2: return Id::DirNorth;
        case 3: return Id::DirNorthwest;
        case 4: return Id::DirWest;
        case 5: return Id::DirSouthwest;
        case 6: return Id::DirSouth;
        case 7: return Id::DirSoutheast;
    }
    return Id::DirEast;
}

// Map an Id::Dir* to a stable bit index 0..7 (for dedup masks). Order
// matches OctantFromVector's bucket numbering: E=0, NE=1, N=2, NW=3,
// W=4, SW=5, S=6, SE=7.
int OctantBit(acc::strings::Id dir) {
    using acc::strings::Id;
    switch (dir) {
        case Id::DirEast:      return 0;
        case Id::DirNortheast: return 1;
        case Id::DirNorth:     return 2;
        case Id::DirNorthwest: return 3;
        case Id::DirWest:      return 4;
        case Id::DirSouthwest: return 5;
        case Id::DirSouth:     return 6;
        case Id::DirSoutheast: return 7;
        default: return -1;
    }
}

// Reverse of OctantBit: bit index → direction Id. Used when emitting
// directions in canonical order rather than encounter order.
acc::strings::Id BitToOctant(int bit) {
    using acc::strings::Id;
    switch (bit) {
        case 0: return Id::DirEast;
        case 1: return Id::DirNortheast;
        case 2: return Id::DirNorth;
        case 3: return Id::DirNorthwest;
        case 4: return Id::DirWest;
        case 5: return Id::DirSouthwest;
        case 6: return Id::DirSouth;
        case 7: return Id::DirSoutheast;
    }
    return Id::DirEast;
}

// Order in which we emit direction words at junctions / plazas. N
// first then clockwise (NE, E, SE, S, SW, W, NW) — feels natural for
// a player who orients on a compass and reads exits clockwise.
constexpr int kOctantEmitOrder[8] = {
    2, 1, 0, 7, 6, 5, 4, 3
};

// Cluster classification (degree-driven, with junction-merge support).

// Per-node degree from the snapshot. Tiny helper — every classifier
// path needs this and it keeps the call sites self-documenting.
int Degree(const acc::engine::navgraph::NavGraphSnapshot& g, int node) {
    int lo = 0, hi = 0;
    acc::engine::navgraph::NeighbourRange(g, node, lo, hi);
    return hi - lo;
}

// Per-node shape features from the 8-ray clearance probe. Fills the
// openness + room flags and the calibrated floor height that the
// open-coalesce and room-coalesce passes consume, and logs the per-node
// clearance/shape profile for ongoing harvest. Runs once at the top of
// BuildForArea, before any merge pass, so every pass sees the same flags.
//
// nodes are stored at z=0 (logically 2D) but walls sit at the real floor
// height; a z=0 ray runs under every wall and the 2m z-guard rejects
// them all, so each node's probe is lifted to the z of its nearest wall
// endpoint. See the shape-band constant block for the thresholds; the
// raw rays + every feature are logged so thresholds stay re-pickable.
void ComputeNodeShapeFeatures(const acc::engine::navgraph::NavGraphSnapshot& g,
                              int n, bool* nodeOpen, bool* nodeRoom,
                              float* nodeFloorZ) {
    for (int i = 0; i < n; ++i) {
        nodeOpen[i] = false; nodeRoom[i] = false; nodeFloorZ[i] = 0.0f;
    }
    const acc::engine::WallEdge* cw = nullptr;
    int cwCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(cw, cwCount) ||
        !cw || cwCount <= 0) {
        acclog::Write("WallTopo",
                      "  clearance-dump: no wall cache — open/room coalesce "
                      "skipped (no shape data)");
        return;
    }
    acclog::Write("WallTopo",
                  "  clearance-dump: per-node 8-ray shape "
                  "(feeds open + room coalesce)");
    for (int i = 0; i < n; ++i) {
        Vector probePos = g.nodes[i].pos;
        float bestEndSq = 1e30f;
        for (int w = 0; w < cwCount; ++w) {
            float ex0 = cw[w].a.x - probePos.x;
            float ey0 = cw[w].a.y - probePos.y;
            float s0  = ex0 * ex0 + ey0 * ey0;
            if (s0 < bestEndSq) { bestEndSq = s0; probePos.z = cw[w].a.z; }
            float ex1 = cw[w].b.x - probePos.x;
            float ey1 = cw[w].b.y - probePos.y;
            float s1  = ex1 * ex1 + ey1 * ey1;
            if (s1 < bestEndSq) { bestEndSq = s1; probePos.z = cw[w].b.z; }
        }
        nodeFloorZ[i] = probePos.z;
        float r[8];
        ProbeClearance8(cw, cwCount, probePos, r);
        float mn = r[0], mx = r[0];
        int mnBit = 0, mxBit = 0;
        for (int k = 1; k < 8; ++k) {
            if (r[k] < mn) { mn = r[k]; mnBit = k; }
            if (r[k] > mx) { mx = r[k]; mxBit = k; }
        }
        int openRays = 0, structN = 0, midN = 0, shortN = 0;
        int longBits = 0, structBits = 0;
        float sum = 0.0f;
        for (int k = 0; k < 8; ++k) {
            sum += r[k];
            if (r[k] > kOpenRayDistM)   { ++openRays; longBits   |= (1 << k); }
            if (r[k] > kStructRayDistM) { ++structN;  structBits |= (1 << k); }
            if (r[k] >= kRoomNearM && r[k] < kOpenRayDistM) ++midN;
            if (r[k] < kRoomNearM) ++shortN;
        }
        float mean = sum / 8.0f;
        nodeOpen[i] = (openRays >= kOpenMinRays);
        nodeRoom[i] = (openRays <= 1) && (mx <= kRoomBoundedMaxM) &&
                      (midN >= kRoomMinMidRays);

        // Tentative shape class — diagnostic label only (see the shape-band
        // constants). Connector bands count genuine long arms (>8m); rooms
        // are caught by mid-band evenness, not arm count.
        const char* shape;
        if (openRays >= kOpenMinRays) {
            shape = "open";
        } else if (openRays >= 3) {
            shape = "junction";
        } else if (openRays == 2) {
            int b0 = -1, b1 = -1;
            for (int k = 0; k < 8; ++k) {
                if (longBits & (1 << k)) { if (b0 < 0) b0 = k; else b1 = k; }
            }
            int d = (b0 >= 0 && b1 >= 0) ? (b1 - b0) : 0;
            if (d > 4) d = 8 - d;
            shape = (d >= 3) ? "corridor" : "corner";
        } else if (openRays == 1) {
            shape = nodeRoom[i] ? "room" : "deadend";
        } else {
            shape = nodeRoom[i] ? "room" : "corner";
        }

        acclog::Write(
            "WallTopo",
            "    clearance node[%d] (%.1f,%.1f) deg=%d probeZ=%.1f "
            "min=%.1f@%d max=%.1f@%d mean=%.1f openN=%d structN=%d "
            "midN=%d shortN=%d open=%d room=%d structbits=0x%02x class=%s "
            "rays[E,NE,N,NW,W,SW,S,SE]="
            "%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
            i, g.nodes[i].pos.x, g.nodes[i].pos.y, Degree(g, i), probePos.z,
            mn, mnBit, mx, mxBit, mean, openRays, structN,
            midN, shortN, nodeOpen[i] ? 1 : 0, nodeRoom[i] ? 1 : 0,
            structBits, shape,
            r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7]);
    }
}

// Door snapshot + edge-near-door geometry.

// Collect every CSWSDoor in the area into g_graph.doors. Reads world
// position (+0x90) + transition destination (CExoLocString @+0x3c8 if
// non-empty). Idempotent within a BuildForArea call — relies on the
// caller to have Reset'd the cache.
//
// All reads are SEH-bounded through the underlying engine helpers.
// Faulted reads just skip the door; the snapshot truncates rather than
// fails so a partial set is still usable for the edge-near-door query.
//
// Collection-only — no logging. Callers handle the summary line (cadence
// differs between the initial build and the per-tick refresh loop) and
// the per-door diagnostic dump via LogDoorSnapshotDetails.
void SnapshotDoors(void* area) {
    g_graph.door_count = 0;
    if (!area) return;

    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    while ((obj = iter.Next()) != nullptr) {
        if (g_graph.door_count >= kMaxDoors) {
            acclog::Write("WallTopo",
                          "SnapshotDoors: hit kMaxDoors=%d — truncating",
                          kMaxDoors);
            break;
        }
        int kind = acc::engine::GetObjectKind(obj);
        if (kind != static_cast<int>(acc::engine::GameObjectKind::Door)) {
            continue;
        }
        Vector pos;
        if (!acc::engine::GetObjectPosition(obj, pos)) continue;

        DoorRecord& rec = g_graph.doors[g_graph.door_count];
        rec.pos                 = pos;
        rec.transitionDest[0]   = '\0';
        rec.locName[0]          = '\0';
        rec.landmarkName[0]     = '\0';
        // Transition destination CExoLocString lives at +0x3c8 (text)
        // with the strref at +0x3cc; ExtractTextOrStrRef walks both.
        // Empty result is fine — most doors aren't transitions.
        acc::engine::ExtractTextOrStrRef(
            obj,
            kDoorTransitionDestOffset,
            kDoorTransitionDestOffset + 4,
            rec.transitionDest, sizeof(rec.transitionDest));
        // CSWSDoor.loc_name @+0x39c — same CExoLocString layout. Used
        // as the door noun in cluster labels (Pillar 4 cycle narration
        // already reads this field). Empty for unnamed doors → caller
        // falls back to the localized generic word.
        acc::engine::ExtractTextOrStrRef(
            obj,
            kDoorLocNameOffset,
            kDoorLocNameOffset + 4,
            rec.locName, sizeof(rec.locName));
        ++g_graph.door_count;
    }
}

// Per-door diagnostic dump. Position + transition string + geometry
// probe block (nearest nav-graph node, approach direction, .lyt-room
// in front/behind, 4 cardinal walkmesh probes). Requires the nav
// graph to already be built (uses g_graph.node_pos) and the wall
// cache populated (ProbeDistance needs it).
void LogDoorSnapshotDetails(void* area) {
    int withTransition = 0;
    for (int i = 0; i < g_graph.door_count; ++i) {
        if (g_graph.doors[i].transitionDest[0]) ++withTransition;
    }
    acclog::Write("WallTopo",
                  "SnapshotDoors: collected %d doors (%d transition)",
                  g_graph.door_count, withTransition);
    for (int i = 0; i < g_graph.door_count; ++i) {
        const DoorRecord& d = g_graph.doors[i];
        acclog::Write("WallTopo",
                      "  door[%d] pos=(%.1f,%.1f,%.1f) locName=\"%s\" "
                      "transition=\"%s\"",
                      i, d.pos.x, d.pos.y, d.pos.z,
                      d.locName[0] ? d.locName : "(none)",
                      d.transitionDest[0] ? d.transitionDest : "(none)");

        // Diagnostic: what's the geometry around this door? We answer:
        //   1. Which .lyt-room is the door IN (engine resolves point→room)
        //   2. Which .lyt-room sits 2m PAST the door (back side)
        //   3. Walkmesh probe distances on 4 cardinal axes from the
        //      door position (room walkmesh excludes door collision
        //      meshes, so probes can pass through where the door is)
        //
        // The approach direction is estimated from the nearest nav-
        // graph node — the player walks toward the door from a corridor
        // node, so (door - nearestNode) is the "into" direction. The
        // 2m past-the-door point lives along that same axis. Comparing
        // front_room to back_room tells us whether the door has any
        // walkable interior behind it (front != back AND back is
        // valid → stub room exists) or is pure decoration (back == -1
        // or back == front).
        int nearestNode = -1;
        float bestSq = 1e30f;
        for (int n = 0; n < g_graph.node_count; ++n) {
            float ndx = g_graph.node_pos[n].x - d.pos.x;
            float ndy = g_graph.node_pos[n].y - d.pos.y;
            float d2 = ndx * ndx + ndy * ndy;
            if (d2 < bestSq) { bestSq = d2; nearestNode = n; }
        }
        Vector approachVec = {0.0f, 0.0f, 0.0f};
        if (nearestNode >= 0) {
            approachVec.x = d.pos.x - g_graph.node_pos[nearestNode].x;
            approachVec.y = d.pos.y - g_graph.node_pos[nearestNode].y;
            float mag = std::sqrt(approachVec.x * approachVec.x +
                                  approachVec.y * approachVec.y);
            if (mag > 1e-3f) {
                approachVec.x /= mag;
                approachVec.y /= mag;
            }
        }
        Vector behindPos;
        behindPos.x = d.pos.x + approachVec.x * 2.0f;
        behindPos.y = d.pos.y + approachVec.y * 2.0f;
        behindPos.z = d.pos.z;

        int frontRoom = -1, backRoom = -1;
        acc::engine::GetRoomAtIndexed(area, d.pos,     frontRoom);
        acc::engine::GetRoomAtIndexed(area, behindPos, backRoom);

        float dN = ProbeDistance(d.pos,  0.0f,  1.0f);
        float dE = ProbeDistance(d.pos,  1.0f,  0.0f);
        float dS = ProbeDistance(d.pos,  0.0f, -1.0f);
        float dW = ProbeDistance(d.pos, -1.0f,  0.0f);

        acclog::Write(
            "WallTopo",
            "    door[%d] diag: nearestNode=%d (%.1f,%.1f) approach=(%.2f,%.2f) "
            "front_room=%d back_room=%d (2m past at %.1f,%.1f) | "
            "probes N=%.1fm E=%.1fm S=%.1fm W=%.1fm",
            i, nearestNode,
            nearestNode >= 0 ? g_graph.node_pos[nearestNode].x : 0.0f,
            nearestNode >= 0 ? g_graph.node_pos[nearestNode].y : 0.0f,
            approachVec.x, approachVec.y,
            frontRoom, backRoom, behindPos.x, behindPos.y,
            dN, dE, dS, dW);
    }
}


// Door-on-edge test. Per design choice (c): door is "on" the segment
// AB iff its projection parameter t is in [0,1] AND its perpendicular
// distance from the segment is ≤ kMaxPerpM. Pure 2D — the engine drops
// z for path solving and so do we.
//
// Tuned values:
//   - kMaxPerpM = 1.5m  — KOTOR door panels are ~2m wide; 1.5m gives
//     us slack for nav points that hug the door frame on either side
//     without admitting doors in adjacent rooms 4m away.
//   - kEndpointSlackM = 1.0m — admits doors that sit slightly past
//     either endpoint, common when level designers place the door's
//     centre a metre or so beyond the flanking nav point.
//
// Returns the door index on hit (>=0), or -1 if no door qualifies. On
// multi-door hits returns the first match — typical edges only ever
// cross one door, so a more sophisticated picker isn't worth the code.
int FindDoorOnEdge(const Vector& a, const Vector& b) {
    if (g_graph.door_count <= 0) return -1;

    constexpr float kMaxPerpM       = 1.5f;
    constexpr float kEndpointSlackM = 1.0f;

    float abx = b.x - a.x;
    float aby = b.y - a.y;
    float abLenSq = abx * abx + aby * aby;
    if (abLenSq < 0.04f) {  // degenerate (<0.2m) — bail
        return -1;
    }
    float abLen      = std::sqrt(abLenSq);
    float slackT     = kEndpointSlackM / abLen;
    float maxPerpSq  = kMaxPerpM * kMaxPerpM;

    for (int i = 0; i < g_graph.door_count; ++i) {
        const Vector& d = g_graph.doors[i].pos;
        float adx = d.x - a.x;
        float ady = d.y - a.y;
        float t = (adx * abx + ady * aby) / abLenSq;
        if (t < -slackT || t > 1.0f + slackT) continue;
        float closestX = a.x + t * abx;
        float closestY = a.y + t * aby;
        float dxC = d.x - closestX;
        float dyC = d.y - closestY;
        float perpSq = dxC * dxC + dyC * dyC;
        if (perpSq <= maxPerpSq) {
            return i;
        }
    }
    return -1;
}

// Unified edge classifier — Clear / Door / Blocked.

// Edge verdict for a nav-graph segment AB. Drives both the Pass 1
// core-merge / Pass 2 absorb merge-veto gates and external-neighbour
// filtering in Pass 3 classification:
//   - kEdgeClear   → free movement; contributes its direction unmodified
//   - kEdgeDoor    → free movement through a CSWSDoor (doorIdx valid);
//                    contributes direction wrapped as "Tür X" / "Tür X
//                    nach DEST"
//   - kEdgeBlocked → the segment crosses immovable walkmesh geometry;
//                    the edge is dropped entirely (no merge, no
//                    external-edge contribution).
enum EdgeClass {
    kEdgeClear   = 0,
    kEdgeDoor    = 1,
    kEdgeBlocked = 2,
};

struct EdgeResult {
    EdgeClass kind;
    int       doorIdx;  // valid only when kind == kEdgeDoor
};

// Tallies for the end-of-build summary. Counts are multi-fire across
// passes (an edge classified by Pass 1 and again by Pass 2 contributes
// twice); the summary log line documents this. Reset at BuildForArea
// entry; we never read them outside the same BuildForArea call.
int s_class_clear     = 0;
int s_class_door      = 0;
int s_class_blocked   = 0;
int s_caveat1_hits    = 0;  // door inside walkmesh wall
int s_caveat2_hits    = 0;  // walk-clear edge between distinct .wok rooms

// Find the door nearest to `p` within `maxDistM`. Returns the door
// index or -1. Used as the belt-and-braces fallback in ClassifyEdge:
// if SegmentCrossesWalkmesh reports a wall hit AND a door lives within
// 1m of the hit point, treat the edge as Door rather than Blocked —
// catches the rare "door inside solid walkmesh" authoring case (memory
// caveat from the 2026-05-20 design discussion).
int FindDoorNearPoint(const Vector& p, float maxDistM) {
    if (g_graph.door_count <= 0) return -1;
    float maxSq  = maxDistM * maxDistM;
    int   bestIx = -1;
    float bestSq = maxSq;
    for (int i = 0; i < g_graph.door_count; ++i) {
        float dx = g_graph.doors[i].pos.x - p.x;
        float dy = g_graph.doors[i].pos.y - p.y;
        float d2 = dx * dx + dy * dy;
        if (d2 <= bestSq) { bestSq = d2; bestIx = i; }
    }
    return bestIx;
}

// Classify a nav-graph segment AB against walls + doors. Optional
// `areaForDiag` enables the caveat-2 cross-room logging — pass nullptr
// when caveat-2 noise would dominate (e.g. corridor-span tests where
// crossing rooms is by design). Logs caveat-1 hits unconditionally
// because they're rare and high-signal.
//
// Multi-fire note: a graph edge (i, j) may be classified from multiple
// passes (Pass 1 core-merge + Pass 2 absorb vetoes, then Pass 3
// external-edge collection). All
// fires hit the same logic and tally the same counters; the summary
// line documents the multiplication so a reader doesn't mistake it for
// per-edge truth. Caveat-1 / caveat-2 LOG ENTRIES include their caller
// context so the same edge appearing multiple times reads naturally.
EdgeResult ClassifyEdge(void* areaForDiag,
                        const Vector& a, const Vector& b,
                        const char* callerCtx) {
    EdgeResult r{kEdgeClear, -1};

    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    bool haveWalls = acc::spatial::change_detector::GetCachedWalls(
        walls, wallCount);
    Vector hit{};
    bool wallHit = haveWalls &&
                   acc::engine::SegmentCrossesWalkmesh(walls, wallCount,
                                                      a, b, hit);

    if (wallHit) {
        // Caveat 1: walkmesh hole convention says doors sit in
        // walkable gaps. If the wall hit is within 1m of a known
        // door, the convention failed for this door (or the seam
        // filter mis-classified a door-frame edge). Reclassify as
        // Door so we keep the actionable label.
        int nearestDoor = FindDoorNearPoint(hit, 1.0f);
        if (nearestDoor >= 0) {
            ++s_caveat1_hits;
            float ddx = g_graph.doors[nearestDoor].pos.x - hit.x;
            float ddy = g_graph.doors[nearestDoor].pos.y - hit.y;
            float dist = std::sqrt(ddx * ddx + ddy * ddy);
            acclog::Write(
                "WallTopo",
                "ClassifyEdge HOLE-IN-WALL [%s] — segment (%.1f,%.1f) -> "
                "(%.1f,%.1f): wall hit at (%.1f,%.1f), door[%d] at "
                "(%.1f,%.1f) %.2fm away, transition=\"%s\" → "
                "reclassifying Blocked → Door",
                callerCtx ? callerCtx : "?",
                a.x, a.y, b.x, b.y,
                hit.x, hit.y,
                nearestDoor,
                g_graph.doors[nearestDoor].pos.x,
                g_graph.doors[nearestDoor].pos.y,
                dist,
                g_graph.doors[nearestDoor].transitionDest[0]
                    ? g_graph.doors[nearestDoor].transitionDest
                    : "(none)");
            r.kind    = kEdgeDoor;
            r.doorIdx = nearestDoor;
            ++s_class_door;
            return r;
        }
        r.kind = kEdgeBlocked;
        ++s_class_blocked;
        return r;
    }

    // Walkmesh-clear. Check whether a door sits on the segment for
    // label rewriting.
    int doorIdx = FindDoorOnEdge(a, b);
    if (doorIdx >= 0) {
        r.kind    = kEdgeDoor;
        r.doorIdx = doorIdx;
        ++s_class_door;
        return r;
    }

    // Clear, no door. Caveat 2: log when the two endpoints sit in
    // different .wok rooms. Noisy in hubs (plazas span 4 .wok rooms),
    // but the pattern of WHICH edges fire repeatedly across areas is
    // the diagnostic signal — see commit comment for the read pattern.
    if (areaForDiag) {
        int roomA = -1, roomB = -1;
        acc::engine::GetRoomAtIndexed(areaForDiag, a, roomA);
        acc::engine::GetRoomAtIndexed(areaForDiag, b, roomB);
        if (roomA != roomB && roomA >= 0 && roomB >= 0) {
            ++s_caveat2_hits;
            float ex = b.x - a.x, ey = b.y - a.y;
            float dist = std::sqrt(ex * ex + ey * ey);
            acclog::Write(
                "WallTopo",
                "ClassifyEdge CROSS-ROOM-CLEAR [%s] — (%.1f,%.1f,room=%d) "
                "-> (%.1f,%.1f,room=%d), distance=%.1fm, no wall, no door. "
                "Possible undetected archway / .wok seam noise.",
                callerCtx ? callerCtx : "?",
                a.x, a.y, roomA, b.x, b.y, roomB, dist);
        }
    }

    ++s_class_clear;
    return r;
}

// Render the door-flavoured replacement of a direction word. Single
// source of truth for the dead-end / corridor / junction-octant
// rewrites. Each format takes (noun, direction[, extra]):
//   - noun = door's authored CSWSDoor.loc_name when non-empty (e.g.
//     "Lift", "Sicherheitstür"); falls back to the localized generic
//     "Tür"/"Door". Doors named with the generic word collapse to the
//     same output as the fallback, which is fine — no deny-list needed.
// Selection priority for the WHICH format:
//   1. landmarkName non-empty  → "%s %s, %s" (FmtMapCursorDoorLandmark)
//      Used when AttachLandmarksToDoors matched a CSWCWaypoint to this
//      door. Reads cleaner than the area-transition string and lets the
//      proximity-landmark path (transitions::TickProximityLandmarks)
//      suppress a separate redundant announce.
//   2. transitionDest non-empty → "%s %s nach %s" (FmtMapCursorDoorTransition)
//      Engine-derived cross-area destination text (e.g. "Taris -
//      Südliche Oberstadt"). Used when no landmark is attached.
//   3. bare door form → "%s %s" (FmtMapCursorDoor)
std::string RenderDoorDirection(int doorIdx, const char* dirWord) {
    using acc::strings::Id;
    if (!dirWord || !dirWord[0]) return std::string();
    const char* landmark = "";
    const char* dest     = "";
    const char* locName  = "";
    if (doorIdx >= 0 && doorIdx < g_graph.door_count) {
        landmark = g_graph.doors[doorIdx].landmarkName;
        dest     = g_graph.doors[doorIdx].transitionDest;
        locName  = g_graph.doors[doorIdx].locName;
    }
    const char* noun =
        (locName && locName[0]) ? locName
                                : acc::strings::Get(Id::MapCursorDoorNoun);
    if (!noun || !noun[0]) noun = "Tür";  // last-ditch fallback if strings table missing

    // Designer-authored doors often duplicate the landmark string into
    // CSWSDoor.loc_name (e.g. Taris Versteck NW exit: loc_name and the
    // companion waypoint's map-note both read "Zum Apartmentkomplex").
    // Without this dedup the FmtMapCursorDoorLandmark expansion
    // "%s %s, %s" prints the same word twice — "Zum Apartmentkomplex
    // Nord-West, Süd, Zum Apartmentkomplex". When noun == landmark
    // (case-insensitive), drop the landmark suffix and fall through to
    // the transitionDest or bare-door path.
    bool landmarkEqualsNoun = false;
    if (landmark && landmark[0] && noun && noun[0]) {
        landmarkEqualsNoun = (_stricmp(landmark, noun) == 0);
    }

    if (landmark && landmark[0] && !landmarkEqualsNoun) {
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDoorLandmark);
        if (fmt && fmt[0]) return acc::strfmt::Format(fmt, noun, dirWord, landmark);
    }
    if (dest && dest[0]) {
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDoorTransition);
        if (fmt && fmt[0]) return acc::strfmt::Format(fmt, noun, dirWord, dest);
    }
    const char* fmt = acc::strings::Get(Id::FmtMapCursorDoor);
    if (fmt && fmt[0]) return acc::strfmt::Format(fmt, noun, dirWord);
    return acc::strfmt::Format("%s %s", noun, dirWord);
}

// Render a corridor's axis label into outBuf. Branches by corridor
// symmetry so the spoken form stays terse for clean axes but exposes
// both endpoints when the corridor turns:
//   - opposite octants (bit XOR == 4) = symmetric corridor → single
//     label. Cardinal pairs (N+S, E+W) use the dedicated axis words
//     ("Nord-Süd" / "Ost-West"); diagonal pairs (NE+SW, NW+SE) use
//     the northern-half octant word ("Nord-Ost" / "Nord-West") —
//     diagonals are symmetric enough that the abbreviation reads
//     naturally.
//   - non-opposite octants = asymmetric / L-shaped corridor → both
//     direction words rendered, more-northern-first ("Korridor West,
//     Süd-Ost"). Caught the "Korridor West" failure mode where the
//     old single-octant axis collapsed an asymmetric corridor to a
//     single direction and lost the other endpoint.
// When `doorIdx` >= 0 the picked label is wrapped via
// RenderDoorDirection so corridors that pass through doors read as
// "Tür <axis>" / "Tür <axis> nach <DEST>".
std::string RenderCorridorAxis(int bitA, int bitB, int doorIdx) {
    using acc::strings::Id;
    if (bitA < 0 || bitB < 0 || bitA == bitB) return std::string();

    if ((bitA ^ bitB) == 4) {
        Id wordId;
        if (bitA == 2 || bitB == 2) {
            wordId = Id::AxisNorthSouth;  // N <-> S
        } else if (bitA == 0 || bitB == 0) {
            wordId = Id::AxisEastWest;    // E <-> W
        } else {
            // Diagonal pair: NE(1)+SW(5) or NW(3)+SE(7). Northern
            // half always has the lower bit number in these pairs.
            int northBit = (bitA < bitB) ? bitA : bitB;
            wordId = BitToOctant(northBit);
        }
        const char* word = acc::strings::Get(wordId);
        if (!word || !word[0]) return std::string();
        if (doorIdx >= 0) return RenderDoorDirection(doorIdx, word);
        const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridorDir);
        if (fmt && fmt[0]) return acc::strfmt::Format(fmt, word);
        return std::string();
    }

    // Non-opposite octants: render both endpoints. Order by
    // y-component descending, x-component descending on ties — keeps
    // the more-northern, then more-eastern, end first.
    static const int octant_y[8] = { 0,  1,  2,  1,  0, -1, -2, -1};
    static const int octant_x[8] = { 2,  1,  0, -1, -2, -1,  0,  1};
    int first = bitA, second = bitB;
    if (octant_y[bitB] > octant_y[first] ||
        (octant_y[bitB] == octant_y[first] &&
         octant_x[bitB] >  octant_x[first])) {
        first = bitB;
        second = bitA;
    }
    const char* wordA = acc::strings::Get(BitToOctant(first));
    const char* wordB = acc::strings::Get(BitToOctant(second));
    if (!wordA || !wordA[0] || !wordB || !wordB[0]) return std::string();

    std::string combo = acc::strfmt::Format("%s, %s", wordA, wordB);

    if (doorIdx >= 0) return RenderDoorDirection(doorIdx, combo.c_str());
    const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridorDir);
    if (fmt && fmt[0]) return acc::strfmt::Format(fmt, combo.c_str());
    return std::string();
}

// "Is this node a *real* dead-end the player can step into, or is it a
// graph artefact pinned to a wall curve in a bigger open area?"
//
// The nav graph is hand-authored by BioWare's level designers for AI
// patrol routing. They drop degree-1 nodes at wall bumps, room corners,
// and patrol terminators — many of which don't correspond to anything a
// player could walk into. The graph alone can't tell these apart from
// genuine recesses (alcoves with content); both look like degree-1
// nodes. The walkmesh CAN tell them apart: a real alcove has 3 short
// probes + 1 long probe (walls on 3 sides, entrance on the 4th); a wall-
// curve artefact looks like a corridor / junction / open area at the
// node's position.
//
// We probe along the actual graph-edge axis (dead-end → parent) rather
// than the cardinal compass. A diagonally-aligned alcove (entrance
// facing NW) reads as a junction under the cardinal probe because the
// rays clip walls obliquely; spinning the 4-probe to align with the
// edge eliminates that bias.
//
// Returns true when the walkmesh agrees with "alcove shape", or when
// the wall cache isn't available yet (fail open — trust the graph
// rather than over-filter on missing data). Returns false only when
// the walkmesh data is present AND it contradicts the alcove claim.
//
// Conservative by design: matches the user's "rather too much info than
// hide map realities" stance — we only filter when we have direct
// geometric evidence the dead-end is meaningless.
bool WalkmeshAgreesDeadEnd(const Vector& deadEndPos, const Vector& parentPos) {
    float fx = parentPos.x - deadEndPos.x;
    float fy = parentPos.y - deadEndPos.y;
    return IsAlcoveAlongAxis(deadEndPos, fx, fy);
}

// Union-find over node ids. Used to merge directly-connected junctions
// into single perceptual clusters. Reused for every BuildForArea
// invocation (reset at the start of the function).
int s_uf_parent[kMaxNodes];

int UFFind(int x) {
    while (s_uf_parent[x] != x) {
        s_uf_parent[x] = s_uf_parent[s_uf_parent[x]];  // path halving
        x = s_uf_parent[x];
    }
    return x;
}

void UFUnite(int a, int b) {
    int ra = UFFind(a);
    int rb = UFFind(b);
    if (ra == rb) return;
    // Use smaller index as the cluster root so the dump output is
    // stable across runs (the root is also used as the cluster's id).
    if (ra < rb) s_uf_parent[rb] = ra;
    else         s_uf_parent[ra] = rb;
}

// Append `entry` to a comma-separated list string, inserting ", " before
// it when the list is non-empty. No-op for empty entries.
void AppendListEntry(std::string& list, const std::string& entry) {
    if (entry.empty()) return;
    if (!list.empty()) list += ", ";
    list += entry;
}

// Render one direction entry. When `markDeadEnd` is set, the direction
// word is wrapped via FmtMapCursorJunctionDeadEndExit ("Sackgasse %s") so
// the user knows that exit doesn't lead onward (option 1 from the
// 2026-05-13 Dias-cluster discussion — junctions whose edges include
// degree-1 stubs should call them out). Prefix form mirrors the door
// rewrite ("Tür %s nach %s") so every special exit reads NOUN-then-
// direction within the junction list.
std::string DirEntry(acc::strings::Id dirId, bool markDeadEnd) {
    const char* word = acc::strings::Get(dirId);
    if (!word || !word[0]) return std::string();
    if (markDeadEnd) {
        const char* fmt = acc::strings::Get(
            acc::strings::Id::FmtMapCursorJunctionDeadEndExit);
        if (fmt && fmt[0]) return acc::strfmt::Format(fmt, word);
    }
    return std::string(word);
}

// Long-axis test for an area cluster. Probes 8 rays at the cluster
// centroid (calibrated floor z) and reports whether the space is clearly
// elongated and along which axis word. Returns false (no axis) for
// roughly square / round / open spaces — those speak as a bare "Bereich".
// The axis is the only geometry cue we surface; there is no size.
bool ComputeCentroidAxis(const Vector& centroid, float floorZ,
                         acc::strings::Id& outAxisId) {
    using acc::strings::Id;
    const acc::engine::WallEdge* cw = nullptr;
    int cwCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(cw, cwCount) ||
        !cw || cwCount <= 0) {
        return false;
    }
    Vector p = centroid;
    p.z = floorZ;
    float r[8];
    ProbeClearance8(cw, cwCount, p, r);
    // Full extent across each of the 4 axes = sum of the opposite rays.
    // Index 0=E-W, 1=NE-SW, 2=N-S, 3=NW-SE (octant pairs k and k+4).
    float ext[4];
    for (int a = 0; a < 4; ++a) ext[a] = r[a] + r[a + 4];
    int longA = 0;
    for (int a = 1; a < 4; ++a) if (ext[a] > ext[longA]) longA = a;
    int perpA = (longA + 2) % 4;  // 90° away
    if (ext[longA] < kAxisElongRatio * ext[perpA]) return false;
    switch (longA) {
        case 0: outAxisId = Id::AxisEastWest;   break;
        case 2: outAxisId = Id::AxisNorthSouth; break;
        case 1: outAxisId = BitToOctant(1);     break;  // NE-SW → "Nord-Ost"
        case 3: outAxisId = BitToOctant(3);     break;  // NW-SE → "Nord-West"
    }
    return true;
}

// The two opposite octant bits an elongation axis covers, or 0 for a
// non-axis Id. Used to drop a redundant exit list: an "Ost-West" area whose
// only exits are exactly Ost + West has an exit list that just echoes the
// axis word, so we speak "Bereich Ost-West" alone. The four values mirror
// the axisId cases ComputeCentroidAxis can emit.
int AxisOctantMask(acc::strings::Id axisId) {
    using acc::strings::Id;
    if (axisId == Id::AxisEastWest)   return (1 << 0) | (1 << 4);  // E + W
    if (axisId == Id::AxisNorthSouth) return (1 << 2) | (1 << 6);  // N + S
    if (axisId == Id::DirNortheast)   return (1 << 1) | (1 << 5);  // NE + SW
    if (axisId == Id::DirNorthwest)   return (1 << 3) | (1 << 7);  // NW + SE
    return 0;
}

// Classify a cluster (1 or more nodes) by its centroid and the list of
// external neighbour nodes (nodes outside this cluster that share an
// edge with some member). Renders the label + kind + sig into the
// out-params.
//
// areaHint: 0 = not an area (use the graph deadend/corridor/junction
// rendering below); 1 = room (immediate-announce); 2 = big area
// (delayed-announce). Non-zero short-circuits to the unified neutral
// "Bereich [axis]. Ausgänge: …" rendering — open spaces, merged rooms
// and merged big junctions all read the same way, geometry = long axis
// only when elongated, then the exits. centroidFloorZ feeds the axis
// probe.
//
// The graph path (areaHint == 0) runs for any cluster the caller did NOT
// flag as a probe-owned place: every singleton, plus multi-node hubs that
// merged by the space rule but carry no open/room geometry (a merged
// corridor-junction). It renders Sackgasse / Korridor / Kreuzung from the
// exit topology, never the merged-space "Platz". For a multi-node hub the
// centroid is the synthesized member midpoint and externalNbs is the union
// of every member's external exits, so the same per-octant logic applies:
//   externalCount == 1  → dead-end (direction = centroid → that exit)
//   externalCount == 2  → corridor (axis between the two exits,
//                                   cardinal cases use "Nord-Süd" /
//                                   "Ost-West" axis words)
//   externalCount >= 3  → junction ("Kreuzung").
//                         8-octant bucketing with passable-wins:
//                         per-octant aggregation marks the octant as
//                         dead-end only when EVERY exit in it leads
//                         to a degree-1 neighbour. Any passable exit
//                         in the octant suppresses the marker — fixes
//                         the order-dependent first-wins bug from the
//                         initial revision.
void ClassifyCluster(const acc::engine::navgraph::NavGraphSnapshot& g,
                     const Vector& centroid,
                     const int* externalNbs,
                     const int* externalSrcs,
                     const int* externalDoorIdx,
                     int externalCount,
                     int areaHint, float centroidFloorZ,
                     bool isLargeArea,
                     std::string& outLabel,
                     int& outKind, int& outSig,
                     bool& outFiltered) {
    using acc::strings::Id;
    outLabel.clear();
    outKind = kKindOpenArea;
    outSig  = kKindOpenArea;
    outFiltered = false;
    int n = static_cast<int>(g.nodes.size());

    // Area path: probe-owned merged spaces (open / room / merged big
    // junction). One neutral "Bereich" label, long-axis cue only when
    // elongated, then the exit list — door rewrites kept (the navigational
    // anchors), wall-curve degree-1 artefacts dropped.
    if (areaHint != 0) {
        bool octHas[8]  = {false, false, false, false, false, false, false, false};
        int  octDoor[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
        for (int k = 0; k < externalCount; ++k) {
            int nb = externalNbs[k];
            if (nb < 0 || nb >= n) continue;
            int bit = OctantBit(OctantFromVector(g.nodes[nb].pos.x - centroid.x,
                                                 g.nodes[nb].pos.y - centroid.y));
            if (bit < 0) continue;
            int door = externalDoorIdx ? externalDoorIdx[k] : -1;
            if (door >= 0) {
                if (octDoor[bit] < 0) octDoor[bit] = door;
                octHas[bit] = true;
                continue;
            }
            int deg = Degree(g, nb);
            int src = externalSrcs ? externalSrcs[k] : -1;
            Vector edgeStart = (src >= 0 && src < n) ? g.nodes[src].pos : centroid;
            bool realExit = (deg >= 2) ||
                            (deg == 1 &&
                             WalkmeshAgreesDeadEnd(g.nodes[nb].pos, edgeStart));
            if (realExit) octHas[bit] = true;
        }

        std::string dirList;
        int mask = 0;
        for (int idx = 0; idx < 8; ++idx) {
            int bit = kOctantEmitOrder[idx];
            if (!octHas[bit]) continue;
            mask |= (1 << bit);
            Id dirId = BitToOctant(bit);
            const char* dirWord = acc::strings::Get(dirId);
            if (octDoor[bit] >= 0 && dirWord && dirWord[0]) {
                AppendListEntry(dirList,
                                RenderDoorDirection(octDoor[bit], dirWord));
            } else {
                AppendListEntry(dirList, DirEntry(dirId, /*markDeadEnd=*/false));
            }
        }

        Id axisId = Id::AxisEastWest;
        bool elong = ComputeCentroidAxis(centroid, centroidFloorZ, axisId);
        // Large-footprint clusters swap the neutral "Bereich" noun for
        // "Großer Bereich" — expectation-setting only; axis/exits/kind
        // are identical. Large areas only take the areaHint != 0 path
        // anyway (they're merged spaces), so the swap lives here.
        const char* noun  = acc::strings::Get(isLargeArea ? Id::AreaNounLarge
                                                          : Id::AreaNoun);
        const char* axisW = elong ? acc::strings::Get(axisId) : nullptr;
        if (!noun || !noun[0]) noun = "Bereich";

        // Drop the exit list when it exactly repeats the elongation axis:
        // an elongated area whose only exits are the two ends of that axis
        // (both plain directions — a named door exit is always kept, its
        // destination is information the axis word doesn't carry) reads as
        // "Bereich Ost-West", not "Bereich Ost-West. Ausgänge: Ost, West".
        // Narrow by construction — only fires when the exit mask is exactly
        // the axis pair, so the common case (exits differ from elongation)
        // is untouched.
        bool axisCoversExits = false;
        if (elong) {
            int axisMask = AxisOctantMask(axisId);
            if (axisMask != 0 && mask == axisMask) {
                bool anyDoor = false;
                for (int bit = 0; bit < 8; ++bit)
                    if ((axisMask & (1 << bit)) && octDoor[bit] >= 0)
                        anyDoor = true;
                axisCoversExits = !anyDoor;
            }
        }
        bool haveExits = !dirList.empty() && !axisCoversExits;

        if (elong && axisW && axisW[0] && haveExits) {
            const char* fmt = acc::strings::Get(Id::FmtAreaAxisExits);
            if (fmt && fmt[0]) outLabel = acc::strfmt::Format(fmt, noun, axisW,
                                                              dirList.c_str());
        } else if (haveExits) {
            const char* fmt = acc::strings::Get(Id::FmtAreaExits);
            if (fmt && fmt[0]) outLabel = acc::strfmt::Format(fmt, noun,
                                                              dirList.c_str());
        } else if (elong && axisW && axisW[0]) {
            const char* fmt = acc::strings::Get(Id::FmtAreaAxisOnly);
            if (fmt && fmt[0]) outLabel = acc::strfmt::Format(fmt, noun, axisW);
        } else {
            outLabel = noun;
        }
        outKind = (areaHint == 1) ? kKindRoom : kKindPlatz;
        outSig  = (outKind & 0xff) | ((mask & 0xff) << 8);
        return;
    }

    // externalCount == 0 never reaches here: the caller forces areaHint != 0
    // for any zero-exit cluster (see BuildForArea's isArea test), so an
    // isolated cluster always takes the "Bereich" area path above.
    if (externalCount == 1) {
        int nb = externalNbs[0];
        if (nb < 0 || nb >= n) return;

        // Walkmesh-shape gate: a degree-1 graph node is treated as a
        // primary candidate only when the walkmesh at the node's
        // position shows the alcove signature (forward > 2m + 3 short
        // rays) under a 4-ray probe rotated to align with the parent
        // direction. Wall-curve artefacts in open areas / corridors fail
        // the gate — they're degree-1 in the graph (one patrol-anchor
        // connection) but the local geometry doesn't form a recess the
        // player can walk into.
        //
        // Pre-2026-05-22 we dropped the label entirely on gate failure,
        // which created Bucket-3 "Offene Fläche where it should be
        // Sackgasse" fires when no other labelled cluster was nearby
        // (player standing in a real alcove the gate misjudged). We now
        // render the label anyway and let LookupAt's rescue path use it
        // only when no unfiltered candidate sits within snap range —
        // primary scan still skips it, so wall-curve false positives
        // stay silent in the normal case.
        bool gateAgrees = WalkmeshAgreesDeadEnd(centroid, g.nodes[nb].pos);
        if (!gateAgrees) {
            outFiltered = true;
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: degree-1 at (%.1f,%.1f,%.1f) FAILED "
                "walkmesh-shape gate (parent=%d at %.1f,%.1f) — labelling "
                "as filtered Sackgasse (LookupAt rescue only)",
                centroid.x, centroid.y, centroid.z,
                nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y);
        }
        float dx = g.nodes[nb].pos.x - centroid.x;
        float dy = g.nodes[nb].pos.y - centroid.y;
        Id dir = OctantFromVector(dx, dy);
        const char* word = acc::strings::Get(dir);

        // Door verdict was precomputed during external-edge collection
        // (ClassifyEdge ran the wall + door tests once per real graph
        // edge). Read the cached doorIdx rather than re-querying — that
        // keeps walls + doors decided by the same primitive instead of
        // running parallel checks here.
        int doorIdx = externalDoorIdx ? externalDoorIdx[0] : -1;
        if (doorIdx >= 0 && word && word[0]) {
            outLabel = RenderDoorDirection(doorIdx, word);
            if (gateAgrees) {
                acclog::Write(
                    "WallTopo",
                    "ClassifyCluster: degree-1 at (%.1f,%.1f,%.1f) HIT door "
                    "idx=%d on edge → \"%s\"",
                    centroid.x, centroid.y, centroid.z, doorIdx,
                    outLabel.c_str());
            }
        } else {
            const char* fmt  = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0] && word && word[0]) {
                outLabel = acc::strfmt::Format(fmt, word);
            }
        }
        outKind = kKindDeadEnd;
        int octBit = OctantBit(dir);
        outSig = (kKindDeadEnd & 0xff) | ((octBit & 0xff) << 8);
        return;
    }
    if (externalCount == 2) {
        int nbA = externalNbs[0];
        int nbB = externalNbs[1];
        if (nbA < 0 || nbA >= n || nbB < 0 || nbB >= n) return;
        // Octant per endpoint, relative to the centroid (cluster
        // member's position). RenderCorridorAxis picks the right
        // form — single axis word for symmetric corridors, two-end
        // form for L-shaped / asymmetric corridors.
        int bitA = OctantBit(OctantFromVector(g.nodes[nbA].pos.x - centroid.x,
                                              g.nodes[nbA].pos.y - centroid.y));
        int bitB = OctantBit(OctantFromVector(g.nodes[nbB].pos.x - centroid.x,
                                              g.nodes[nbB].pos.y - centroid.y));
        // Prefer the door verdicts precomputed on the actual graph
        // edges (member→nbA, member→nbB). If neither edge carried a
        // door, fall back to the corridor-span test for doors that
        // sit off both graph edges but still on the spanning line.
        int doorIdx = -1;
        if (externalDoorIdx) {
            doorIdx = externalDoorIdx[0];
            if (doorIdx < 0) doorIdx = externalDoorIdx[1];
        }
        if (doorIdx < 0) {
            doorIdx = FindDoorOnEdge(g.nodes[nbA].pos, g.nodes[nbB].pos);
        }
        outLabel = RenderCorridorAxis(bitA, bitB, doorIdx);
        if (doorIdx >= 0) {
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: degree-2 corridor at (%.1f,%.1f,%.1f) "
                "HIT door idx=%d on segment → \"%s\"",
                centroid.x, centroid.y, centroid.z, doorIdx, outLabel.c_str());
        }
        outKind = kKindCorridor;
        int sigBit = (bitA < bitB) ? bitA : bitB;
        outSig = (kKindCorridor & 0xff) | ((sigBit & 0xff) << 8) |
                 ((((bitA < bitB) ? bitB : bitA) & 0xff) << 16);
        return;
    }

    // 3+ external edges = junction or Platz. First pass: per-octant
    // aggregation. octantHasExit[bit] is set whenever any external
    // neighbour bucket-classifies into `bit`. octantAllDeadEnd[bit]
    // starts true and gets cleared the moment a non-dead-end exit
    // appears in that octant — so it stays true only when EVERY exit
    // in the bucket is a degree-1 stub. This fixes the order-dependent
    // first-wins issue where a passable exit could be silently masked
    // by a co-located dead-end (or vice versa).
    bool octantHasExit[8]    = {false, false, false, false,
                                false, false, false, false};
    bool octantAllDeadEnd[8] = {true, true, true, true,
                                true, true, true, true};
    // First-door-per-octant: if any external edge in an octant crosses
    // a CSWSDoor, store its index here. The emit loop swaps the plain
    // direction word for the "Tür DIR" / "Tür DIR nach DEST" rewrite
    // and skips the (Sackgasse) marker for that octant — door wins.
    int octantDoorIdx[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    // Per-neighbour classification accumulates into per-octant counters.
    // Per the user's "don't announce nonsense" rule:
    //   - real onward exit (degree ≥ 2)     → octant emits direction
    //   - real dead-end alcove (degree == 1, walkmesh agrees) → emits
    //                                          direction, may carry
    //                                          (Sackgasse) marker
    //   - wall-curve (degree == 1, walkmesh disagrees) → contributes
    //                                          NOTHING — direction is
    //                                          dropped entirely when the
    //                                          octant has no other
    //                                          contributors
    // octantHasExit[bit] is set only by real onward exits or real dead-
    // ends (or doors — see below). octantAllDeadEnd[bit] is cleared by
    // real onward exits, leaving the marker only for octants whose
    // exits are exclusively real dead-end alcoves.
    for (int k = 0; k < externalCount; ++k) {
        int nb = externalNbs[k];
        if (nb < 0 || nb >= n) continue;
        float dx = g.nodes[nb].pos.x - centroid.x;
        float dy = g.nodes[nb].pos.y - centroid.y;
        Id dir = OctantFromVector(dx, dy);
        int bit = OctantBit(dir);
        if (bit < 0) continue;

        int deg = Degree(g, nb);
        // For the alcove test, the "parent" anchoring the rotated probe
        // axis is the cluster member that owns this edge. Falls back to
        // the centroid when externalSrcs is unavailable.
        int src = externalSrcs ? externalSrcs[k] : -1;
        Vector edgeStart = (src >= 0 && src < n)
                               ? g.nodes[src].pos : centroid;

        bool isOnwardExit  = (deg >= 2);
        bool isRealDeadEnd = (deg == 1) &&
                             WalkmeshAgreesDeadEnd(g.nodes[nb].pos, edgeStart);
        bool isWallCurve   = (deg == 1) && !isRealDeadEnd;

        // Door verdict precomputed during external-edge collection
        // (one ClassifyEdge call per graph edge, walls + doors decided
        // together). Read the cached doorIdx so we don't run a second
        // parallel door test here.
        bool hasDoor = false;
        int precomputedDoor = externalDoorIdx ? externalDoorIdx[k] : -1;
        if (octantDoorIdx[bit] < 0) {
            if (precomputedDoor >= 0) {
                octantDoorIdx[bit] = precomputedDoor;
                hasDoor = true;
                acclog::Write(
                    "WallTopo",
                    "ClassifyCluster: junction at (%.1f,%.1f) octant=%d "
                    "edge from member %d (%.1f,%.1f) -> nb %d (%.1f,%.1f) "
                    "HIT door[%d] at (%.1f,%.1f)",
                    centroid.x, centroid.y, bit,
                    src, edgeStart.x, edgeStart.y,
                    nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                    precomputedDoor,
                    g_graph.doors[precomputedDoor].pos.x,
                    g_graph.doors[precomputedDoor].pos.y);
            }
        } else {
            hasDoor = true;
        }

        // Direction only appears in the label when SOMETHING real
        // exists in this octant. Wall-curves alone don't qualify.
        if (isOnwardExit || isRealDeadEnd || hasDoor) {
            octantHasExit[bit] = true;
        }
        // (Sackgasse) marker survives only when no onward exit shares
        // the octant. Mixed real-dead-end + wall-curve still gets the
        // marker (the real dead-end carries it); mixed real-dead-end +
        // onward exit doesn't (the onward exit dominates).
        if (isOnwardExit) octantAllDeadEnd[bit] = false;

        if (isWallCurve && !hasDoor) {
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: junction at (%.1f,%.1f) octant=%d "
                "neighbour %d (%.1f,%.1f) FAILED rotated walkmesh gate "
                "(parent=%d at %.1f,%.1f) — not contributing to octant",
                centroid.x, centroid.y, bit,
                nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                src, edgeStart.x, edgeStart.y);
        }
    }

    // Demote gate: count octants the player can actually walk into
    // (onward neighbours + doors). Pure-dead-end stubs and wall-curve
    // octants are annotations, not choices. If the effective exit count
    // drops below 3 (octant collisions or walkmesh gates having eaten
    // the rest), the cluster isn't a junction — it's a corridor or
    // dead-end. Demote so the spoken type matches the decision space
    // and drop dead-end stubs from the demoted label (they're noise in
    // a small space — what the player flagged as overdescription in
    // the loot rooms behind security doors).
    int realExitCount = 0;
    int realExitBits[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int firstDoorBit = -1;
    for (int bit = 0; bit < 8; ++bit) {
        bool hasOnward = octantHasExit[bit] && !octantAllDeadEnd[bit];
        bool hasDoor   = octantDoorIdx[bit] >= 0;
        if (!hasOnward && !hasDoor) continue;
        realExitBits[realExitCount++] = bit;
        if (hasDoor && firstDoorBit < 0) firstDoorBit = bit;
    }

    if (realExitCount == 1) {
        int bit = realExitBits[0];
        Id dirId = BitToOctant(bit);
        const char* dirWord = acc::strings::Get(dirId);
        if (octantDoorIdx[bit] >= 0) {
            outLabel = RenderDoorDirection(octantDoorIdx[bit],
                                           dirWord ? dirWord : "");
        } else {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0] && dirWord && dirWord[0]) {
                outLabel = acc::strfmt::Format(fmt, dirWord);
            }
        }
        outKind = kKindDeadEnd;
        outSig  = (kKindDeadEnd & 0xff) | ((bit & 0xff) << 8);
        acclog::Write(
            "WallTopo",
            "ClassifyCluster: junction at (%.1f,%.1f) DEMOTED to Sackgasse "
            "(real-exits=1) → \"%s\"",
            centroid.x, centroid.y, outLabel.c_str());
        return;
    }

    if (realExitCount == 2) {
        int bitA = realExitBits[0];
        int bitB = realExitBits[1];
        int doorIdx = (firstDoorBit >= 0)
                          ? octantDoorIdx[firstDoorBit]
                          : -1;
        outLabel = RenderCorridorAxis(bitA, bitB, doorIdx);
        outKind = kKindCorridor;
        int sigBitLo = (bitA < bitB) ? bitA : bitB;
        int sigBitHi = (bitA < bitB) ? bitB : bitA;
        outSig = (kKindCorridor & 0xff) |
                 ((sigBitLo & 0xff) << 8) |
                 ((sigBitHi & 0xff) << 16);
        acclog::Write(
            "WallTopo",
            "ClassifyCluster: junction at (%.1f,%.1f) DEMOTED to Korridor "
            "(real-exits=2, octants=%d+%d) → \"%s\"",
            centroid.x, centroid.y, bitA, bitB, outLabel.c_str());
        return;
    }

    // Second pass: emit octants in canonical order (N, NE, E, SE, S,
    // SW, W, NW). Stable across runs; matches the way a compass-oriented
    // player scans for exits clockwise.
    std::string dirList;
    int mask = 0;
    int deadEndMask = 0;
    for (int idx = 0; idx < 8; ++idx) {
        int bit = kOctantEmitOrder[idx];
        if (!octantHasExit[bit]) continue;
        mask |= (1 << bit);
        Id dirId = BitToOctant(bit);

        // Door wins over Sackgasse: when an octant carries a door, the
        // (Sackgasse) annotation is suppressed and the direction word
        // is replaced with the "Tür DIR" form. The door is the player-
        // actionable information; Sackgasse is metadata about geometry
        // beyond the door that doesn't matter once we've named the
        // gateway.
        if (octantDoorIdx[bit] >= 0) {
            const char* dirWord = acc::strings::Get(dirId);
            if (dirWord && dirWord[0]) {
                AppendListEntry(dirList,
                                RenderDoorDirection(octantDoorIdx[bit], dirWord));
            }
            continue;
        }

        bool markDeadEnd = octantAllDeadEnd[bit];
        if (markDeadEnd) deadEndMask |= (1 << bit);
        AppendListEntry(dirList, DirEntry(dirId, markDeadEnd));
    }

    // Reached by a hub (singleton or merged) with no open/room geometry, so
    // this path renders "Kreuzung". A hub that IS open/room was flagged a
    // probe-owned place by the caller and took the "Bereich" area path above.
    const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
    if (fmt && fmt[0] && !dirList.empty()) {
        outLabel = acc::strfmt::Format(fmt, dirList.c_str());
    } else {
        const char* bare = acc::strings::Get(Id::MapCursorJunction);
        if (bare && bare[0]) {
            outLabel = bare;
        }
    }
    outKind = kKindJunction;
    outSig  = (kKindJunction & 0xff) |
              ((mask & 0xff) << 8) |
              ((deadEndMask & 0xff) << 16);
}

// Diagnostic: nav-graph vs wall-cache crossing check.
//
// Confirms (and localizes) phantom walls using ONLY engine walkmesh +
// nav data — no .lyt-room topography enters this test. The engine's
// path_points / path_connections are the edges it uses for AI
// pathfinding, so an engine-authored nav edge is provably walkable. If
// such an edge crosses one of our cached "wall" edges, that wall is
// phantom by definition: the engine routes creatures straight through
// it.
//
// The crossing test is 3D-aware: a 2D-projection coincidence where the
// wall sits more than kMaxZcrossM above/below the nav edge (a wall on a
// different floor) is excluded, so this also measures how much of the
// runtime WALL-FILTERED noise is pure 2D-projection (would be fixed by
// a z-overlap guard) versus genuine same-floor phantoms (missed portal
// seams or straight-line-reachability mismatch).
//
// Crossings within kMinNodeClrM of either nav node are ignored — nav
// nodes routinely hug real walls, and a graze at the node endpoint is
// not the engine routing *through* a wall.
//
// Per-wall material_id + room_id are dumped: a phantom carrying a FLOOR
// surfacemat (e.g. Metal/Grass) points at a mis-emitted walkable seam;
// a phantom carrying a wall material points at the straight-line model
// crossing a real wall the path actually skirts. Read-only; never
// mutates the graph or the cache.
void LogNavWallCrossings(const acc::engine::navgraph::NavGraphSnapshot& g) {
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        acclog::Write("WallTopo",
                      "  navwall-crosscheck: no wall cache available — skipping");
        return;
    }

    // Shared with the production SegmentCrossesWalkmesh z-guard so the
    // diagnostic measures exactly what the runtime test will reject.
    constexpr float kMaxZcrossM   = acc::engine::kWallCrossZToleranceM;
    constexpr float kMinNodeClrM  = 0.5f;   // crossing must clear both nav nodes
    constexpr float kMinNodeClrSq = kMinNodeClrM * kMinNodeClrM;
    constexpr int   kMaxDump      = 40;

    int n = static_cast<int>(g.nodes.size());
    if (n > kMaxNodes) n = kMaxNodes;

    // Distinct-wall flag so we count phantom WALLS, not phantom crossings
    // (one wall may be crossed by several nav edges). 16 KB static, fine
    // for the single-threaded patch model.
    constexpr int kWallFlagCap = 16384;
    static bool s_crossed[kWallFlagCap];
    int wallN = (wallCount < kWallFlagCap) ? wallCount : kWallFlagCap;
    for (int i = 0; i < wallN; ++i) s_crossed[i] = false;

    int navEdges = 0, crossings = 0, zSkipped = 0, dumped = 0;

    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        for (int e = lo; e < hi; ++e) {
            int j = static_cast<int>(g.conns[e]);
            if (j <= i || j >= n) continue;  // each undirected edge once
            ++navEdges;
            const Vector& P = g.nodes[i].pos;
            const Vector& Q = g.nodes[j].pos;
            float abx = Q.x - P.x, aby = Q.y - P.y;
            if (abx * abx + aby * aby < 1e-6f) continue;

            for (int w = 0; w < wallN; ++w) {
                const acc::engine::WallEdge& we = walls[w];
                float cdx = we.b.x - we.a.x, cdy = we.b.y - we.a.y;
                float denom = abx * cdy - aby * cdx;
                if (denom > -1e-8f && denom < 1e-8f) continue;  // parallel
                float dx = we.a.x - P.x, dy = we.a.y - P.y;
                float t = (dx * cdy - dy * cdx) / denom;  // along nav edge
                float u = (dx * aby - dy * abx) / denom;  // along wall edge
                if (t < 0.0f || t > 1.0f) continue;
                if (u < 0.0f || u > 1.0f) continue;

                float cx = P.x + t * abx, cy = P.y + t * aby;
                float d0x = cx - P.x, d0y = cy - P.y;
                float d1x = cx - Q.x, d1y = cy - Q.y;
                if (d0x * d0x + d0y * d0y < kMinNodeClrSq) continue;
                if (d1x * d1x + d1y * d1y < kMinNodeClrSq) continue;

                // 3D guard: reject different-floor projection coincidences.
                float navZ  = P.z   + t * (Q.z   - P.z);
                float wallZ = we.a.z + u * (we.b.z - we.a.z);
                float adz = navZ - wallZ;
                if (adz < 0.0f) adz = -adz;
                if (adz > kMaxZcrossM) { ++zSkipped; continue; }

                ++crossings;
                s_crossed[w] = true;
                if (dumped < kMaxDump) {
                    ++dumped;
                    acclog::Write(
                        "WallTopo",
                        "  navwall-cross[%d]: nav node[%d](%.1f,%.1f,%.1f) -> "
                        "node[%d](%.1f,%.1f,%.1f) crosses wall[%d] "
                        "(%.1f,%.1f,%.1f)-(%.1f,%.1f,%.1f) room=%d mat=%d "
                        "at (%.1f,%.1f) dz=%.2f",
                        dumped, i, P.x, P.y, P.z, j, Q.x, Q.y, Q.z,
                        w, we.a.x, we.a.y, we.a.z, we.b.x, we.b.y, we.b.z,
                        we.room_id, we.material_id, cx, cy, adz);
                }
            }
        }
    }

    int distinctWalls = 0;
    for (int w = 0; w < wallN; ++w) if (s_crossed[w]) ++distinctWalls;

    acclog::Write(
        "WallTopo",
        "  navwall-crosscheck: navEdges=%d walls=%d -> crossings=%d "
        "distinct-phantom-walls=%d (z-skipped=%d at |dz|>%.1fm; "
        "node-clearance=%.1fm; dumped=%d, cap=%d)",
        navEdges, wallCount, crossings, distinctWalls, zSkipped,
        kMaxZcrossM, kMinNodeClrM, dumped, kMaxDump);
}

// Diagnostic-only: dump per-node topology metrics so we can pick a
// principled gate later. NO MERGE LOGIC USES THESE COUNTS — pure
// reconnaissance, called from BuildForArea's end-of-build diagnostics
// block alongside the other Log* dumps. For each node we log:
//   - degree      (existing nav-graph degree)
//   - triangles   (3-cycles touching the node — pairs of neighbours that
//                  are directly connected to each other)
//   - 4-cycles    (pairs of neighbours that share a common neighbour ≠ the
//                  node itself, not already counted as a triangle)
//   - 2-hop reach (distinct nodes reachable within 2 graph-hops, ex-self)
// Goal: separate "linear corridor" (triangles=0, 4-cycles=0, low reach)
// from "looped room patrol points" (triangles or 4-cycles ≥1) from
// "T-junction" (high reach, but triangles=0, 4-cycles=0). Run on Endar
// Spire quarters + corridors + bridge and Taris South Apartments to
// compare topology signatures before tuning any gate.
void LogTopologyMetrics(const acc::engine::navgraph::NavGraphSnapshot& g,
                        int n) {
    auto navIsConnected = [&](int a, int b) -> bool {
        if (a < 0 || a >= n || b < 0 || b >= n || a == b) return false;
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, a, lo, hi);
        for (int e = lo; e < hi; ++e) {
            if (static_cast<int>(g.conns[e]) == b) return true;
        }
        return false;
    };
    bool reachVisited[kMaxNodes] = {false};
    acclog::Write(
        "WallTopo",
        "  topology-dump: per-node degree / triangles / 4-cycles / "
        "2-hop reach (diagnostic only — no behaviour change)");
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        int deg = hi - lo;

        int triangles = 0;
        int fourCycles = 0;
        // For each unordered pair (Y, Z) of i's neighbours.
        for (int a = lo; a < hi; ++a) {
            int Y = static_cast<int>(g.conns[a]);
            if (Y < 0 || Y >= n || Y == i) continue;
            for (int b = a + 1; b < hi; ++b) {
                int Z = static_cast<int>(g.conns[b]);
                if (Z < 0 || Z >= n || Z == i || Z == Y) continue;
                if (navIsConnected(Y, Z)) {
                    ++triangles;
                    continue;  // triangle, not a 4-cycle through this pair
                }
                // 4-cycle: do Y and Z share a common neighbour W ≠ i?
                int yLo = 0, yHi = 0;
                acc::engine::navgraph::NeighbourRange(g, Y, yLo, yHi);
                bool found4 = false;
                for (int c = yLo; c < yHi && !found4; ++c) {
                    int W = static_cast<int>(g.conns[c]);
                    if (W < 0 || W >= n) continue;
                    if (W == i || W == Y || W == Z) continue;
                    if (navIsConnected(W, Z)) found4 = true;
                }
                if (found4) ++fourCycles;
            }
        }

        // 2-hop reach: BFS depth 2 from node i, count distinct.
        for (int k = 0; k < n; ++k) reachVisited[k] = false;
        reachVisited[i] = true;
        int reach = 0;
        for (int a = lo; a < hi; ++a) {
            int Y = static_cast<int>(g.conns[a]);
            if (Y < 0 || Y >= n || reachVisited[Y]) continue;
            reachVisited[Y] = true;
            ++reach;
            int yLo = 0, yHi = 0;
            acc::engine::navgraph::NeighbourRange(g, Y, yLo, yHi);
            for (int b = yLo; b < yHi; ++b) {
                int Z = static_cast<int>(g.conns[b]);
                if (Z < 0 || Z >= n || reachVisited[Z]) continue;
                reachVisited[Z] = true;
                ++reach;
            }
        }

        acclog::Write(
            "WallTopo",
            "    node[%d] (%.1f,%.1f) deg=%d tri=%d c4=%d reach2=%d",
            i, g.nodes[i].pos.x, g.nodes[i].pos.y,
            deg, triangles, fourCycles, reach);
    }
}

// Diagnostic-only: per multi-node cluster, dump member adjacency. For
// each member node, list its graph neighbours split into internal (same
// cluster) and external (cross-cluster), and mark the edge's ClassifyEdge
// verdict. Passes nullptr for the area diag so this re-walk doesn't
// double-emit caveat-2 lines already logged during collection. Reads the
// frozen union-find state, so call it after all merge passes complete.
void LogClusterMemberAdjacency(const acc::engine::navgraph::NavGraphSnapshot& g,
                               int n) {
    for (int root = 0; root < n; ++root) {
        if (UFFind(root) != root) continue;
        int size = 0;
        for (int m = 0; m < n; ++m) if (UFFind(m) == root) ++size;
        if (size < 2) continue;
        acclog::Write("WallTopo",
                      "  cluster[%d] size=%d label=\"%s\"",
                      root, size, g_graph.node_label[root].c_str());
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(g, m, lo, hi);
            for (int e = lo; e < hi; ++e) {
                int nb = static_cast<int>(g.conns[e]);
                if (nb < 0 || nb >= n) continue;
                bool internal = (UFFind(nb) == root);
                EdgeResult er = ClassifyEdge(/*areaForDiag=*/nullptr,
                                             g.nodes[m].pos,
                                             g.nodes[nb].pos,
                                             "dump");
                if (er.kind == kEdgeDoor) {
                    acclog::Write(
                        "WallTopo",
                        "    member[%d] (%.1f,%.1f) -> nb[%d] (%.1f,%.1f) "
                        "%s door[%d] transition=\"%s\"",
                        m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                        internal ? "INTERNAL" : "EXTERNAL",
                        er.doorIdx,
                        g_graph.doors[er.doorIdx].transitionDest[0]
                            ? g_graph.doors[er.doorIdx].transitionDest
                            : "(none)");
                } else if (er.kind == kEdgeBlocked) {
                    acclog::Write(
                        "WallTopo",
                        "    member[%d] (%.1f,%.1f) -> nb[%d] (%.1f,%.1f) "
                        "%s WALL-BLOCKED",
                        m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                        internal ? "INTERNAL" : "EXTERNAL");
                } else {
                    acclog::Write(
                        "WallTopo",
                        "    member[%d] (%.1f,%.1f) -> nb[%d] (%.1f,%.1f) "
                        "%s clear",
                        m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                        internal ? "INTERNAL" : "EXTERNAL");
                }
            }
        }
    }
}

}  // namespace

// Match each landmark waypoint (registered in transitions.cpp's per-area
// landmark cache during RebuildLandmarkCache) to the nearest door within
// kLandmarkDoorMatchMaxM. When a match lands, the door's landmarkName is
// populated and the landmark is flagged via MarkLandmarkClaimedByDoor so
// the proximity-fire path won't re-announce the same name a second later.
//
// Greedy first-come — if two landmarks contest the same door (rare in
// vanilla content, only seen at hub doors with multiple co-located
// waypoints), the first-iterated one wins and the second logs a
// conflict line. Tuning the rule beyond first-come needs evidence first.
//
// Threshold rationale: 3.0m. Empirical evidence from
// patch-20260522-141304.log:
//   "Zur Oberstadt" landmark @ (112.61, 83.34) ↔ door[4] @ (112.5, 81.8)
//   → 1.5m — well inside the gate.
// 3m gives 2x slack for authoring noise without admitting cross-cluster
// matches (corridor doors usually sit ≥6m from the next nearest door).
//
// Diagnostics: per-landmark match / unmatched line + summary line for
// rate analysis. Unmatched lines include the nearest-door distance so
// post-mortem can tell whether the threshold needs widening for a
// particular area or whether the landmark genuinely isn't door-shaped.
void AttachLandmarksToDoors(void* /*area*/) {
    constexpr float kLandmarkDoorMatchMaxM = 3.0f;
    constexpr float kMaxSq = kLandmarkDoorMatchMaxM * kLandmarkDoorMatchMaxM;

    if (g_graph.door_count <= 0) {
        acclog::Write("WallTopo",
                      "AttachLandmarks: no doors snapshotted — skipping");
        return;
    }

    int matched = 0, unmatched = 0, conflicts = 0;

    int cursor = 0;
    char name[128] = {0};
    Vector lmPos = {0.0f, 0.0f, 0.0f};
    int landmarkIdx = -1;
    while (acc::transitions::IterateLandmarks(
               cursor, name, sizeof(name), lmPos, landmarkIdx)) {
        // Linear scan over doors — door_count is tiny (≤128, in practice
        // <30 for vanilla areas), so the O(landmarks * doors) sweep is
        // a few hundred multiplies total per area build.
        int   bestDoor = -1;
        float bestSq   = 1e30f;
        for (int d = 0; d < g_graph.door_count; ++d) {
            float dx = g_graph.doors[d].pos.x - lmPos.x;
            float dy = g_graph.doors[d].pos.y - lmPos.y;
            float dsq = dx * dx + dy * dy;
            if (dsq < bestSq) {
                bestSq   = dsq;
                bestDoor = d;
            }
        }

        if (bestDoor < 0) continue;
        float bestDist = std::sqrt(bestSq);

        if (bestSq > kMaxSq) {
            // Nearest door beyond threshold — keep landmark out-of-band
            // for the proximity-fire path. Log the candidate distance so
            // post-mortem can tune the threshold per-area if needed.
            ++unmatched;
            acclog::Write(
                "WallTopo",
                "AttachLandmarks: UNMATCHED landmark[%d] '%s' "
                "lmPos=(%.2f,%.2f) — nearest door[%d] pos=(%.2f,%.2f) "
                "dist=%.2fm > %.1fm threshold (transition=\"%s\")",
                landmarkIdx, name,
                lmPos.x, lmPos.y,
                bestDoor,
                g_graph.doors[bestDoor].pos.x, g_graph.doors[bestDoor].pos.y,
                bestDist, kLandmarkDoorMatchMaxM,
                g_graph.doors[bestDoor].transitionDest[0]
                    ? g_graph.doors[bestDoor].transitionDest
                    : "(none)");
            continue;
        }

        // Within threshold. Greedy claim: first-iterated landmark wins.
        if (g_graph.doors[bestDoor].landmarkName[0] != '\0') {
            ++conflicts;
            acclog::Write(
                "WallTopo",
                "AttachLandmarks: CONFLICT landmark[%d] '%s' "
                "would match door[%d] (dist=%.2fm) but door already "
                "claimed by '%s' — skipping",
                landmarkIdx, name, bestDoor, bestDist,
                g_graph.doors[bestDoor].landmarkName);
            continue;
        }

        std::strncpy(g_graph.doors[bestDoor].landmarkName, name,
                     sizeof(g_graph.doors[bestDoor].landmarkName) - 1);
        g_graph.doors[bestDoor]
            .landmarkName[sizeof(g_graph.doors[bestDoor].landmarkName) - 1] = '\0';
        acc::transitions::MarkLandmarkClaimedByDoor(landmarkIdx);
        ++matched;
        acclog::Write(
            "WallTopo",
            "AttachLandmarks: matched landmark[%d] '%s' → door[%d] "
            "lmPos=(%.2f,%.2f) doorPos=(%.2f,%.2f) dist=%.2fm "
            "(was transitionDest=\"%s\")",
            landmarkIdx, name, bestDoor,
            lmPos.x, lmPos.y,
            g_graph.doors[bestDoor].pos.x, g_graph.doors[bestDoor].pos.y,
            bestDist,
            g_graph.doors[bestDoor].transitionDest[0]
                ? g_graph.doors[bestDoor].transitionDest
                : "(none)");
    }

    acclog::Write(
        "WallTopo",
        "AttachLandmarks: summary — matched=%d unmatched=%d conflicts=%d "
        "(landmarks scanned=%d, doors=%d, threshold=%.1fm)",
        matched, unmatched, conflicts,
        matched + unmatched + conflicts, g_graph.door_count,
        kLandmarkDoorMatchMaxM);
}

void Reset() {
    g_graph.area_owner = nullptr;
    g_graph.built      = false;
    g_graph.node_count = 0;
    g_graph.door_count = 0;
    s_class_clear   = 0;
    s_class_door    = 0;
    s_class_blocked = 0;
    s_caveat1_hits  = 0;
    s_caveat2_hits  = 0;
    g_doors_stability = DoorStabilityState{};
}

bool HasGraphForArea(void* area) {
    return area != nullptr &&
           g_graph.built &&
           g_graph.area_owner == area;
}

void MaybeRefreshDoors(void* area) {
    if (!HasGraphForArea(area))      return;
    if (g_doors_stability.committed) return;

    int prevCount = g_graph.door_count;

    // Preserve landmark attachments across the re-snapshot. SnapshotDoors
    // rebuilds g_graph.doors from scratch (clearing landmarkName), but
    // AttachLandmarksToDoors only runs once during BuildForArea — re-running
    // it here would re-log + re-claim every tick. Doors don't move in the
    // world, so carry the names over by position match. Harmless today (the
    // cluster labels are already baked to strings before this loop runs), but
    // keeps DoorRecord.landmarkName authoritative for any future runtime read.
    struct SavedLandmark { Vector pos; char name[64]; };
    SavedLandmark saved[kMaxDoors];
    int savedCount = g_graph.door_count;
    if (savedCount > kMaxDoors) savedCount = kMaxDoors;
    for (int i = 0; i < savedCount; ++i) {
        saved[i].pos = g_graph.doors[i].pos;
        std::strncpy(saved[i].name, g_graph.doors[i].landmarkName,
                     sizeof(saved[i].name) - 1);
        saved[i].name[sizeof(saved[i].name) - 1] = '\0';
    }

    SnapshotDoors(area);

    for (int i = 0; i < g_graph.door_count; ++i) {
        if (g_graph.doors[i].landmarkName[0]) continue;  // freshly set, keep
        for (int j = 0; j < savedCount; ++j) {
            if (!saved[j].name[0]) continue;
            float dx = saved[j].pos.x - g_graph.doors[i].pos.x;
            float dy = saved[j].pos.y - g_graph.doors[i].pos.y;
            float dz = saved[j].pos.z - g_graph.doors[i].pos.z;
            if (dx * dx + dy * dy + dz * dz < 0.01f) {  // same door (<0.1m)
                std::strncpy(g_graph.doors[i].landmarkName, saved[j].name,
                             sizeof(g_graph.doors[i].landmarkName) - 1);
                g_graph.doors[i]
                    .landmarkName[sizeof(g_graph.doors[i].landmarkName) - 1]
                    = '\0';
                break;
            }
        }
    }

    ++g_doors_stability.retry_ticks;

    if (g_graph.door_count == g_doors_stability.last_count) {
        ++g_doors_stability.streak;
    } else {
        // Door set changed — log the new details so we can grep
        // exactly what arrived (or, less likely, what disappeared).
        acclog::Write("WallTopo",
                      "MaybeRefreshDoors: count changed %d -> %d at retry tick %d",
                      prevCount, g_graph.door_count,
                      g_doors_stability.retry_ticks);
        LogDoorSnapshotDetails(area);
        g_doors_stability.last_count = g_graph.door_count;
        g_doors_stability.streak     = 1;
    }

    bool stable  = g_doors_stability.streak >= kDoorStabilityRequiredStreak;
    bool capHit  = g_doors_stability.retry_ticks >= kDoorRetryCapTicks;
    if (stable || capHit) {
        g_doors_stability.committed = true;
        acclog::Write("WallTopo",
                      "MaybeRefreshDoors: %s — final door_count=%d after %d retry tick%s",
                      capHit && !stable ? "cap reached" : "stable",
                      g_graph.door_count,
                      g_doors_stability.retry_ticks,
                      g_doors_stability.retry_ticks == 1 ? "" : "s");
    }
}

void BuildForArea(void* area) {
    if (!area) return;
    if (HasGraphForArea(area)) return;

    Reset();
    g_graph.area_owner = area;

    acc::engine::navgraph::NavGraphSnapshot g;
    if (!acc::engine::navgraph::SnapshotNavGraph(area, g)) {
        acclog::Write("WallTopo",
                      "BuildForArea: nav graph empty / unreadable (areaPtr=%p) "
                      "— leaving graph unbuilt", area);
        return;
    }

    int n = static_cast<int>(g.nodes.size());
    if (n > kMaxNodes) {
        acclog::Write("WallTopo",
                      "BuildForArea: nav graph has %d nodes — truncating to "
                      "kMaxNodes=%d; %d node%s dropped from classification "
                      "(label/exit data for them will be missing)",
                      static_cast<int>(g.nodes.size()), kMaxNodes,
                      static_cast<int>(g.nodes.size()) - kMaxNodes,
                      static_cast<int>(g.nodes.size()) - kMaxNodes == 1
                          ? "" : "s");
        n = kMaxNodes;
    }
    g_graph.node_count = n;
    for (int i = 0; i < n; ++i) {
        g_graph.node_pos[i]        = g.nodes[i].pos;
        g_graph.node_label[i].clear();
        g_graph.node_sig[i]        = 0;
        g_graph.node_kind[i]       = kKindOpenArea;
        g_graph.node_cluster_id[i] = kClusterIdNone;
    }

    // Snapshot doors before classifying clusters — ClassifyCluster's
    // FindDoorOnEdge calls consume g_graph.doors[]. The initial snapshot
    // can race a partially-populated server-object array; the per-tick
    // MaybeRefreshDoors loop catches late-arriving doors. Initial
    // cluster classification is locked to whatever was visible now
    // (accepted lossy behaviour — late doors still show up as "Tür"
    // exits via the per-edge query).
    SnapshotDoors(area);
    LogDoorSnapshotDetails(area);
    // Attach landmark map-notes to doors before any cluster labels get
    // rendered — RenderDoorDirection consults DoorRecord.landmarkName and
    // labels are baked once during the classification passes below. Also
    // flags matched landmarks in the transitions cache so the
    // proximity-fire path won't double-announce.
    AttachLandmarksToDoors(area);
    g_doors_stability.last_count  = g_graph.door_count;
    g_doors_stability.streak      = 0;
    g_doors_stability.retry_ticks = 0;
    g_doors_stability.committed   = false;

    // Per-node shape features first, so every merge pass below can branch
    // on the same openness / room flags. They no longer drive separate
    // merge classes — they only decide, per node, whether it is "space"
    // (a place the player stands in) or "passage" (a link between places);
    // see the Pass 1 space/passage rule.
    bool  nodeOpen  [kMaxNodes];
    bool  nodeRoom  [kMaxNodes];
    float nodeFloorZ[kMaxNodes];
    ComputeNodeShapeFeatures(g, n, nodeOpen, nodeRoom, nodeFloorZ);

    // ===== Pass 1: core merge =====
    // Form perceptual cores by walking the nav graph once and unioning each
    // clear, door-free edge whose endpoints are the SAME kind of thing.
    // Internally there are now only two kinds — the merge layer no longer
    // separates junction / room / open (they were the same operation,
    // "union adjacent same-class nodes", and the open-vs-room seam left
    // door-less interiors fragmented: an Ebon Hawk hold split open↔throat↔
    // room into three regions). Collapsed to:
    //
    //   - SPACE   : a place the player stands in — degree-≥3 hub, or open /
    //               room by the clearance probe. Two space nodes union.
    //   - PASSAGE : a straight degree-2 link. Two straight nodes union into
    //               one corridor run (the L-shaped-corridor rule is carried
    //               over unchanged: a ~90° bend fails the straightness test
    //               and breaks the run, so the player still hears the turn).
    //
    // PASSAGE↔SPACE never merge — that boundary is exactly the line between
    // "I'm in a corridor" and "I'm in a room", and is what lets Pass 3 still
    // voice corridors, junctions and areas distinctly even though the merge
    // itself stopped distinguishing them.
    //
    // Each rule is gated on a clear, door-free edge: the door is tested in
    // 2D (FindDoorOnEdge); the wall is tested on the floor-z-lifted segment
    // (the nav nodes sit at z=0, so a z=0 segment would run under same-floor
    // walls). Adjacency — a real engine nav edge — is the connector, so two
    // spaces that merely face each other across a junction throat stay
    // separate (the Oberstadt store / cantina-avenue fix).
    for (int i = 0; i < n; ++i) s_uf_parent[i] = i;

    // Corridor straightness: a degree-2 node whose two edges point roughly
    // opposite. Precomputed so the corridor rule is a both-straight test in
    // the edge-walk; also reused by Pass 2 to keep corridor material out of
    // the straggler absorb, and by the corner-fold step (chainStraightCos
    // lets it pick the straightest neighbour to fold a bend into).
    bool  chainStraight   [kMaxNodes];
    float chainStraightCos[kMaxNodes];  // turn cos at this node; +1 if not deg-2
    for (int i = 0; i < n; ++i) {
        chainStraight[i]    = false;
        chainStraightCos[i] = 1.0f;
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        if (hi - lo != 2) continue;
        int nA = static_cast<int>(g.conns[lo]);
        int nB = static_cast<int>(g.conns[lo + 1]);
        if (nA < 0 || nA >= n || nB < 0 || nB >= n || nA == nB) continue;
        float ax = g.nodes[nA].pos.x - g.nodes[i].pos.x;
        float ay = g.nodes[nA].pos.y - g.nodes[i].pos.y;
        float bx = g.nodes[nB].pos.x - g.nodes[i].pos.x;
        float by = g.nodes[nB].pos.y - g.nodes[i].pos.y;
        float la = std::sqrt(ax * ax + ay * ay);
        float lb = std::sqrt(bx * bx + by * by);
        if (la < 1e-3f || lb < 1e-3f) continue;
        chainStraightCos[i] = (ax * bx + ay * by) / (la * lb);
        if (chainStraightCos[i] <= kCorridorStraightCosMax)
            chainStraight[i] = true;
    }

    // Wall cache for the edge clearness test (shared by Pass 1 and Pass 2).
    const acc::engine::WallEdge* gw = nullptr;
    int gwCount = 0;
    bool haveGW = acc::spatial::change_detector::GetCachedWalls(
                      gw, gwCount) && gw && gwCount > 0;

    int coreSpace = 0, coreCorridor = 0;
    int coreVetoDoor = 0, coreVetoWall = 0;
    // Unified space cap. The merge layer no longer separates junction (was
    // 8m) / room (8m) / open (16m); one generous adjacency cap covers all
    // three. A real nav edge plus the door + wall vetoes keep it honest, so
    // the 16m reach can't bridge a wall or an authored doorway.
    const float kSpaceCapSq = kOpenMergeRadiusM * kOpenMergeRadiusM;
    for (int i = 0; i < n; ++i) {
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        int degI = hi - lo;
        for (int e = lo; e < hi; ++e) {
            int j = static_cast<int>(g.conns[e]);
            if (j <= i || j >= n) continue;        // each undirected pair once
            if (UFFind(i) == UFFind(j)) continue;
            float dx = g.nodes[i].pos.x - g.nodes[j].pos.x;
            float dy = g.nodes[i].pos.y - g.nodes[j].pos.y;
            float dz = g.nodes[i].pos.z - g.nodes[j].pos.z;
            if (std::fabs(dz) > kMergeMaxZM) continue;   // multi-floor guard
            float d2 = dx * dx + dy * dy;

            // Decide the core kind before running the edge tests. Space =
            // degree-≥3 hub or open / room by the probe; passage = straight
            // degree-2. Space unions with space (one cap); straight unions
            // with straight (no cap — a chain collapses fully). Passage↔space
            // falls through and stays unmerged.
            int degJ = Degree(g, j);
            bool spaceI = (degI >= 3) || nodeOpen[i] || nodeRoom[i];
            bool spaceJ = (degJ >= 3) || nodeOpen[j] || nodeRoom[j];
            const char* kind = nullptr;
            if (spaceI && spaceJ && d2 <= kSpaceCapSq)         kind = "space";
            else if (chainStraight[i] && chainStraight[j])     kind = "corridor";
            if (!kind) continue;

            if (FindDoorOnEdge(g.nodes[i].pos, g.nodes[j].pos) >= 0) {
                ++coreVetoDoor;
                continue;
            }
            if (haveGW) {
                Vector a = g.nodes[i].pos, b = g.nodes[j].pos;
                float fz = 0.5f * (nodeFloorZ[i] + nodeFloorZ[j]);
                a.z = fz; b.z = fz;
                Vector hit{};
                if (acc::engine::SegmentCrossesWalkmesh(gw, gwCount, a, b, hit)) {
                    ++coreVetoWall;
                    continue;
                }
            }

            UFUnite(i, j);
            switch (kind[0]) {
                case 's': ++coreSpace;    break;
                default:  ++coreCorridor; break;
            }
            acclog::Write(
                "WallTopo",
                "  core-merge [%s]: node[%d] (%.1f,%.1f) + node[%d] (%.1f,%.1f) "
                "gap=%.1fm",
                kind, i, g.nodes[i].pos.x, g.nodes[i].pos.y,
                j, g.nodes[j].pos.x, g.nodes[j].pos.y, std::sqrt(d2));
        }
    }
    acclog::Write(
        "WallTopo",
        "  core-merge: space=%d corridor=%d "
        "vetoedByDoor=%d vetoedByWall=%d",
        coreSpace, coreCorridor,
        coreVetoDoor, coreVetoWall);

    // ===== Pass 1b: corner fold =====
    // A sharp bend (degree-2, not chainStraight) is the corner where two
    // corridor segments meet. Left alone it announces as its own one-node
    // cluster wedged between the two runs (the Ebon Hawk crew-quarters
    // 51° dogleg: "corridor / corner / corridor", three cues). Fold it into
    // the neighbour it continues STRAIGHTEST into — its straight edge extends
    // that corridor, its turning edge becomes the boundary to the next one.
    //
    // "Straightest" = the neighbour nb with the most-opposite edges (smallest
    // chainStraightCos), among the corner's degree-2 chainStraight neighbours
    // reachable across a clear, door-free edge. Folding into exactly one side
    // means the corner NEVER unions across its own bend, so the two corridors
    // stay distinct (each announced by its own axis) and the player turns at
    // the boundary — no merged corridor with a misleading end bearing. Runs
    // before Pass 2 so a corner prefers its straight corridor over being
    // absorbed into an adjacent space hub. Single pass: folding a corner
    // can't create a new corner.
    int cornerFolds = 0;
    for (int x = 0; x < n; ++x) {
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, x, lo, hi);
        if (hi - lo != 2) continue;        // degree-2 only
        if (chainStraight[x]) continue;    // straight nodes already chained
        int   bestNb  = -1;
        float bestCos = 2.0f;              // smaller = straighter pass-through
        for (int e = lo; e < hi; ++e) {
            int nb = static_cast<int>(g.conns[e]);
            if (nb < 0 || nb >= n || nb == x) continue;
            if (!chainStraight[nb]) continue;   // join must continue straight
            if (UFFind(nb) == UFFind(x)) continue;
            if (FindDoorOnEdge(g.nodes[x].pos, g.nodes[nb].pos) >= 0) continue;
            if (haveGW) {
                Vector a = g.nodes[x].pos, b = g.nodes[nb].pos;
                float fz = 0.5f * (nodeFloorZ[x] + nodeFloorZ[nb]);
                a.z = fz; b.z = fz;
                Vector hit{};
                if (acc::engine::SegmentCrossesWalkmesh(gw, gwCount, a, b, hit))
                    continue;
            }
            if (chainStraightCos[nb] < bestCos) {
                bestCos = chainStraightCos[nb];
                bestNb  = nb;
            }
        }
        if (bestNb >= 0) {
            acclog::Write(
                "WallTopo",
                "  corner-fold: node[%d] (%.1f,%.1f) bend-cos=%.2f -> corridor "
                "via node[%d] (%.1f,%.1f) straight-cos=%.2f",
                x, g.nodes[x].pos.x, g.nodes[x].pos.y, chainStraightCos[x],
                bestNb, g.nodes[bestNb].pos.x, g.nodes[bestNb].pos.y, bestCos);
            UFUnite(x, bestNb);
            ++cornerFolds;
        }
    }
    acclog::Write("WallTopo", "  corner-fold: folded=%d", cornerFolds);

    // ===== Pass 2: straggler absorb =====
    // Fold leftover corner / doorway nodes into the core they belong to.
    // The merged successor of the old hub-absorption + bbox-absorption
    // passes, run HERE (after every core merge, including open) so a corner
    // hanging off a plaza folds into the plaza, not a singleton. Two
    // admission rules, both preserving authored boundaries via the door
    // veto:
    //
    //   (a) graph-attached: a degree-≤2 singleton that is NOT corridor
    //       material (chainStraight) folds into an adjacent multi-node core
    //       across a clear, door-free edge within kStragglerAbsorbMaxM.
    //       Cascades to a fixpoint on LIVE membership, so a corner two hops
    //       from a core still folds in (Oberstadt store corner 24 → spoke
    //       14 → frontage core — the case the old hub pass structurally
    //       missed, since it froze its snapshot before building the core).
    //       Real corridors are safe: they are multi-node cores (Pass 1
    //       chained them) and chainStraight singletons are excluded, so the
    //       cascade can neither eat a corridor core nor propagate through a
    //       straight corridor node.
    //
    //   (b) graph-unattached: a remaining singleton whose XY sits inside a
    //       core's bounding box, with a member within kMergeMaxZM in Z and a
    //       wall-/door-clear segment to it. Catches nodes that only connect
    //       to door-vetoed neighbours (Endar Spire start node[3]/[6]).

    // (a) Adjacency cascade.
    int absorbAdj = 0, absorbAdjVetoDoor = 0, absorbAdjVetoWall = 0;
    {
        int sizeByRoot[kMaxNodes];
        const float kCapSq = kStragglerAbsorbMaxM * kStragglerAbsorbMaxM;
        bool changed = true;
        int  guard   = 0;
        while (changed && guard++ < n) {
            changed = false;
            for (int s = 0; s < n; ++s) sizeByRoot[s] = 0;
            for (int s = 0; s < n; ++s) {
                int r = UFFind(s);
                if (r >= 0 && r < n) ++sizeByRoot[r];
            }
            for (int x = 0; x < n; ++x) {
                if (Degree(g, x) > 2) continue;          // corner / spoke only
                if (chainStraight[x]) continue;          // corridor material
                if (sizeByRoot[UFFind(x)] >= 2) continue; // already in a core
                int lo = 0, hi = 0;
                acc::engine::navgraph::NeighbourRange(g, x, lo, hi);
                // Scan valid core-neighbours (clear, door-free, within cap)
                // and collect the distinct cores X reaches. A node that
                // reaches >= 2 distinct cores BRIDGES two areas — keep it as
                // its own announcing junction so both connections stay
                // audible (Oberstadt node 25 links the store frontage to the
                // cantina avenue; folding it into the avenue erased the
                // "Süd" exit cue and the avenue became unfindable from the
                // frontage). Only a pendant — exactly one reachable core —
                // folds in.
                int coreRootA = -1, coreNbA = -1;
                bool isBridge = false;
                for (int e = lo; e < hi && !isBridge; ++e) {
                    int j = static_cast<int>(g.conns[e]);
                    if (j < 0 || j >= n || j == x) continue;
                    int jr = UFFind(j);
                    if (sizeByRoot[jr] < 2) continue;  // neighbour is not a core
                    if (jr == UFFind(x)) continue;
                    float dx = g.nodes[x].pos.x - g.nodes[j].pos.x;
                    float dy = g.nodes[x].pos.y - g.nodes[j].pos.y;
                    if (dx * dx + dy * dy > kCapSq) continue;
                    if (FindDoorOnEdge(g.nodes[x].pos, g.nodes[j].pos) >= 0) {
                        ++absorbAdjVetoDoor;
                        continue;
                    }
                    if (haveGW) {
                        Vector a = g.nodes[x].pos, b = g.nodes[j].pos;
                        float fz = 0.5f * (nodeFloorZ[x] + nodeFloorZ[j]);
                        a.z = fz; b.z = fz;
                        Vector hit{};
                        if (acc::engine::SegmentCrossesWalkmesh(gw, gwCount,
                                                                a, b, hit)) {
                            ++absorbAdjVetoWall;
                            continue;
                        }
                    }
                    if (coreRootA < 0) { coreRootA = jr; coreNbA = j; }
                    else if (jr != coreRootA) isBridge = true;
                }
                if (isBridge) continue;        // keep bridge as its own junction
                if (coreRootA >= 0) {
                    acclog::Write(
                        "WallTopo",
                        "  absorb [adj]: node[%d] (%.1f,%.1f) deg=%d -> "
                        "core=%d (via node[%d] at %.1f,%.1f)",
                        x, g.nodes[x].pos.x, g.nodes[x].pos.y, Degree(g, x),
                        coreRootA, coreNbA,
                        g.nodes[coreNbA].pos.x, g.nodes[coreNbA].pos.y);
                    UFUnite(x, coreNbA);
                    ++absorbAdj;
                    changed = true;
                }
            }
        }
    }
    acclog::Write(
        "WallTopo",
        "  absorb [adj]: absorbed=%d vetoedByDoor=%d vetoedByWall=%d "
        "(cap=%.0fm)",
        absorbAdj, absorbAdjVetoDoor, absorbAdjVetoWall, kStragglerAbsorbMaxM);

    // (b) Bounding-box admission for remaining graph-unattached singletons.
    // Snapshot taken now, after the adjacency cascade, so the bbox reflects
    // the final cores. Single pass over a frozen snapshot — absorbing a
    // candidate does not recompute the bbox or re-test others (iteration
    // could chain-absorb a real corridor sticking out of a Platz).
    int bboxSnapRoot[kMaxNodes];
    int bboxSnapSize[kMaxNodes];
    for (int i = 0; i < n; ++i) {
        bboxSnapRoot[i] = UFFind(i);
        bboxSnapSize[i] = 0;
    }
    for (int i = 0; i < n; ++i) {
        int r = bboxSnapRoot[i];
        if (r >= 0 && r < n) ++bboxSnapSize[r];
    }

    struct ClusterBbox {
        float minX, maxX, minY, maxY;
        bool  valid;
    };
    ClusterBbox bboxByRoot[kMaxNodes];
    for (int i = 0; i < n; ++i) bboxByRoot[i].valid = false;
    for (int m = 0; m < n; ++m) {
        int root = bboxSnapRoot[m];
        if (root < 0 || root >= n) continue;
        if (bboxSnapSize[root] < 2) continue;
        ClusterBbox& bb = bboxByRoot[root];
        const Vector p = g.nodes[m].pos;
        if (!bb.valid) {
            bb.minX = bb.maxX = p.x;
            bb.minY = bb.maxY = p.y;
            bb.valid = true;
        } else {
            if (p.x < bb.minX) bb.minX = p.x;
            if (p.x > bb.maxX) bb.maxX = p.x;
            if (p.y < bb.minY) bb.minY = p.y;
            if (p.y > bb.maxY) bb.maxY = p.y;
        }
    }

    int bboxAbsorbed     = 0;
    int bboxVetoedByWall = 0;
    int bboxVetoedByDoor = 0;
    int bboxNoZMatch     = 0;
    int bboxAmbiguous    = 0;
    for (int x = 0; x < n; ++x) {
        if (bboxSnapSize[bboxSnapRoot[x]] != 1) continue;  // singletons only
        const Vector px = g.nodes[x].pos;

        int   bestRoot   = -1;
        int   bestMember = -1;
        float bestD2     = 1e30f;
        int   containing = 0;
        for (int root = 0; root < n; ++root) {
            const ClusterBbox& bb = bboxByRoot[root];
            if (!bb.valid) continue;
            if (px.x < bb.minX || px.x > bb.maxX) continue;
            if (px.y < bb.minY || px.y > bb.maxY) continue;

            int   nearestMember = -1;
            float nearestD2     = 1e30f;
            for (int m = 0; m < n; ++m) {
                if (bboxSnapRoot[m] != root) continue;
                float dz = g.nodes[m].pos.z - px.z;
                if (std::fabs(dz) > kMergeMaxZM) continue;
                float dx = g.nodes[m].pos.x - px.x;
                float dy = g.nodes[m].pos.y - px.y;
                float d2 = dx * dx + dy * dy;
                if (d2 < nearestD2) {
                    nearestD2     = d2;
                    nearestMember = m;
                }
            }
            if (nearestMember < 0) continue;

            ++containing;
            if (nearestD2 < bestD2) {
                bestD2     = nearestD2;
                bestRoot   = root;
                bestMember = nearestMember;
            }
        }

        if (bestRoot < 0) {
            for (int root = 0; root < n; ++root) {
                const ClusterBbox& bb = bboxByRoot[root];
                if (!bb.valid) continue;
                if (px.x < bb.minX || px.x > bb.maxX) continue;
                if (px.y < bb.minY || px.y > bb.maxY) continue;
                ++bboxNoZMatch;
                acclog::Write(
                    "WallTopo",
                    "  bbox-absorb VETOED (z): node[%d] (%.1f,%.1f,%.1f) "
                    "inside bbox of core=%d but no member within %.1fm in Z "
                    "— multi-floor protection",
                    x, px.x, px.y, px.z, root, kMergeMaxZM);
                break;
            }
            continue;
        }

        if (containing > 1) {
            ++bboxAmbiguous;
            acclog::Write(
                "WallTopo",
                "  bbox-absorb AMBIGUOUS: node[%d] (%.1f,%.1f) sits inside %d "
                "core bboxes; picking nearest (core=%d via member[%d] at %.1fm)",
                x, px.x, px.y, containing,
                bestRoot, bestMember, std::sqrt(bestD2));
        }

        EdgeResult er = ClassifyEdge(area, px, g.nodes[bestMember].pos,
                                     "bbox-absorb");
        if (er.kind == kEdgeBlocked) {
            ++bboxVetoedByWall;
            acclog::Write(
                "WallTopo",
                "  bbox-absorb VETOED (wall): node[%d] (%.1f,%.1f) inside "
                "core=%d bbox but segment to member[%d] (%.1f,%.1f) crosses "
                "a wall",
                x, px.x, px.y, bestRoot,
                bestMember, g.nodes[bestMember].pos.x,
                g.nodes[bestMember].pos.y);
            continue;
        }
        if (er.kind == kEdgeDoor) {
            ++bboxVetoedByDoor;
            acclog::Write(
                "WallTopo",
                "  bbox-absorb VETOED (door): node[%d] (%.1f,%.1f) inside "
                "core=%d bbox but segment to member[%d] (%.1f,%.1f) crosses "
                "door[%d]",
                x, px.x, px.y, bestRoot,
                bestMember, g.nodes[bestMember].pos.x,
                g.nodes[bestMember].pos.y, er.doorIdx);
            continue;
        }

        UFUnite(x, bestMember);
        ++bboxAbsorbed;
        acclog::Write(
            "WallTopo",
            "  bbox-absorb: node[%d] (%.1f,%.1f,%.1f) -> core=%d "
            "(nearest member[%d] at %.1f,%.1f, distance %.1fm)",
            x, px.x, px.y, px.z, bestRoot,
            bestMember, g.nodes[bestMember].pos.x,
            g.nodes[bestMember].pos.y, std::sqrt(bestD2));
    }
    acclog::Write(
        "WallTopo",
        "  bbox-absorb: absorbed=%d vetoedByWall=%d vetoedByDoor=%d "
        "vetoedByZ=%d ambiguous-bbox=%d",
        bboxAbsorbed, bboxVetoedByWall, bboxVetoedByDoor,
        bboxNoZMatch, bboxAmbiguous);

    // ===== Pass 3: per-cluster classification =====
    // For each cluster root (the
    // smallest node id in its union-find class), compute the centroid,
    // collect external neighbours (dedup by node id), classify via
    // ClassifyCluster, and write the result to every member.
    int clusters = 0, multiNodeClusters = 0;
    int deadEnds = 0, corridors = 0, junctions = 0, openAreas = 0;
    for (int root = 0; root < n; ++root) {
        if (UFFind(root) != root) continue;
        ++clusters;

        // Centroid of all members. Also tally member shape flags + the
        // calibrated floor z (averaged) so we can decide whether this
        // cluster is a probe-owned area and feed the axis probe.
        Vector centroid = {0.0f, 0.0f, 0.0f};
        int size = 0;
        bool hasOpen = false, hasRoom = false;
        float floorZSum = 0.0f;
        // Member-node bounding box (X/Y) → footprint extent for the
        // large-area label swap. Node positions, not walls, so it under-
        // reads true area, but the relative gap between a sprawling open
        // cluster (~160 m) and a normal merged room (~20-30 m) is huge,
        // so a coarse threshold separates them cleanly.
        float minX =  1e30f, minY =  1e30f;
        float maxX = -1e30f, maxY = -1e30f;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            centroid.x += g.nodes[m].pos.x;
            centroid.y += g.nodes[m].pos.y;
            centroid.z += g.nodes[m].pos.z;
            if (g.nodes[m].pos.x < minX) minX = g.nodes[m].pos.x;
            if (g.nodes[m].pos.x > maxX) maxX = g.nodes[m].pos.x;
            if (g.nodes[m].pos.y < minY) minY = g.nodes[m].pos.y;
            if (g.nodes[m].pos.y > maxY) maxY = g.nodes[m].pos.y;
            if (nodeOpen[m]) hasOpen = true;
            if (nodeRoom[m]) hasRoom = true;
            floorZSum += nodeFloorZ[m];
            ++size;
        }
        if (size > 0) {
            centroid.x /= size;
            centroid.y /= size;
            centroid.z /= size;
        }
        float centroidFloorZ = (size > 0) ? floorZSum / size : 0.0f;
        if (size > 1) ++multiNodeClusters;

        // Longer bounding-box side = footprint extent. Single-node
        // clusters have zero extent and can never be "large".
        float bboxExtent = 0.0f;
        if (size > 0) {
            float w = maxX - minX;
            float h = maxY - minY;
            bboxExtent = (w > h) ? w : h;
        }
        bool isLargeArea = bboxExtent >= kLargeAreaExtentMeters;

        // External neighbours: edges from any member to a non-member.
        // Dedup by node id so multi-edge connections to the same
        // outside node only register once. Capture the source cluster
        // member alongside the nb so ClassifyCluster's door-on-edge
        // test can use the ACTUAL graph edge (member.pos → nb.pos)
        // rather than the centroid → nb.pos blur. For multi-node Platz
        // clusters the centroid is a synthesized midpoint between
        // members; testing perpendicular distance from that midpoint
        // matches doors that aren't on any real graph edge, which is
        // why the Upper-City plaza was lighting up with "Tür Nord, Tür
        // Ost, Tür Süd, Tür West" all at once.
        //
        // Edge-class filtering: each member→nb graph edge runs through
        // ClassifyEdge (the unified wall + door primitive). Blocked
        // edges drop entirely — the graph offered an exit, but the
        // walkmesh says it crosses a wall, so the cluster simply
        // doesn't see that neighbour. Door verdicts are cached in
        // externalDoorIdx[] so ClassifyCluster doesn't run a second
        // door query per neighbour.
        constexpr int kMaxExternal = 16;
        int externalNbs[kMaxExternal];
        int externalSrcs[kMaxExternal];   // cluster member that owns this edge
        int externalDoorIdx[kMaxExternal];
        int externalCount = 0;
        int blockedExternal = 0;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(g, m, lo, hi);
            for (int e = lo; e < hi; ++e) {
                int nb = static_cast<int>(g.conns[e]);
                if (nb < 0 || nb >= n) continue;
                if (UFFind(nb) == root) continue;  // internal edge
                EdgeResult er = ClassifyEdge(area,
                                             g.nodes[m].pos, g.nodes[nb].pos,
                                             "ext-edge");
                if (er.kind == kEdgeBlocked) {
                    ++blockedExternal;
                    acclog::Write(
                        "WallTopo",
                        "  external BLOCKED: cluster=%d member[%d] (%.1f,%.1f) "
                        "-> nb[%d] (%.1f,%.1f) — segment crosses wall, "
                        "dropping from neighbour list",
                        root, m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y);
                    continue;
                }
                bool seen = false;
                for (int k = 0; k < externalCount; ++k) {
                    if (externalNbs[k] == nb) {
                        // Upgrade -1 doorIdx if a later edge to the same
                        // neighbour found a door the first didn't.
                        if (externalDoorIdx[k] < 0 &&
                            er.kind == kEdgeDoor) {
                            externalDoorIdx[k] = er.doorIdx;
                        }
                        seen = true;
                        break;
                    }
                }
                if (!seen) {
                    if (externalCount < kMaxExternal) {
                        externalNbs    [externalCount] = nb;
                        externalSrcs   [externalCount] = m;
                        externalDoorIdx[externalCount] =
                            (er.kind == kEdgeDoor) ? er.doorIdx : -1;
                        ++externalCount;
                    } else {
                        // Cap hit — this exit won't be voiced. Log it rather
                        // than drop silently so an under-reported plaza is
                        // visible in the harvest (raise kMaxExternal if real).
                        acclog::Write(
                            "WallTopo",
                            "  external CAP: cluster=%d already has "
                            "kMaxExternal=%d neighbours — dropping exit to "
                            "nb[%d] (%.1f,%.1f); this exit will be missing "
                            "from the label",
                            root, kMaxExternal, nb,
                            g.nodes[nb].pos.x, g.nodes[nb].pos.y);
                    }
                }
            }
        }

        // Announce decision — now independent of how the cluster merged.
        // A cluster speaks as "Bereich" only when the clearance probe says
        // it is a PLACE: any open-class member, a multi-node room cluster,
        // or an isolated cluster with no exits. Everything else takes the
        // graph path and is voiced by its exit topology — a tight merged
        // hub with no open/room geometry reads as "Kreuzung", a degree-2
        // run as "Korridor", a single exit as "Sackgasse". This is what
        // keeps junctions sounding like junctions and corridors like
        // corridors even though they merge by the same space rule.
        //
        // The old "size>1 && externalCount>=3 → area" clause was dropped on
        // purpose: a merged corridor-junction hub (space-class by degree
        // alone) must stay a Kreuzung, not become a Bereich.
        //   - any open-class member       → big area (delayed)
        //   - multi-node room cluster      → room  (immediate)
        //   - isolated cluster (no exits)  → area (open/room by flag)
        int areaHint = 0;  // 0 none, 1 room, 2 big
        bool isArea = (externalCount == 0) || hasOpen ||
                      (size > 1 && hasRoom);
        if (isArea) areaHint = (hasRoom && !hasOpen) ? 1 : 2;

        std::string label;
        int kind = kKindOpenArea, sig = 0;
        bool filtered = false;
        ClassifyCluster(g, centroid, externalNbs, externalSrcs,
                        externalDoorIdx, externalCount,
                        areaHint, centroidFloorZ, isLargeArea,
                        label, kind, sig, filtered);

        switch (kind) {
            case kKindDeadEnd:  ++deadEnds;  break;
            case kKindCorridor: ++corridors; break;
            case kKindJunction: ++junctions; break;
            case kKindPlatz:    ++junctions; break;  // tally w/ junctions
            case kKindRoom:     ++junctions; break;  // tally w/ junctions
            default:            ++openAreas; break;
        }

        if (isLargeArea) {
            acclog::Write("WallTopo",
                          "  large-area: cluster=%d size=%d extent=%.1fm "
                          "(>= %.1fm) -> '%s'",
                          root, size, bboxExtent, kLargeAreaExtentMeters,
                          label.c_str());
        }

        // Write to every cluster member.
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            g_graph.node_label[m]    = label;
            g_graph.node_kind[m]     = kind;
            g_graph.node_sig[m]      = sig;
            g_graph.node_filtered[m] = filtered;
        }
    }

    // Freeze the UFFind roots into node_cluster_id[] now that all merge
    // passes have completed. After this point UFFind itself is no
    // longer consulted at runtime — LookupAt reads node_cluster_id
    // directly, so subsequent path-halving on UFFind can't shift the
    // observed id under a caller comparing against a stored value.
    for (int i = 0; i < n; ++i) {
        g_graph.node_cluster_id[i] = UFFind(i);
    }

    g_graph.built = true;
    acclog::Write("WallTopo",
                  "BuildForArea: area=%p nodes=%d clusters=%d "
                  "core-merge(space=%d corridor=%d) "
                  "absorb(adj=%d bbox=%d) multi-node-clusters=%d "
                  "(dead=%d corridor=%d junction=%d open=%d)",
                  area, n, clusters,
                  coreSpace, coreCorridor,
                  absorbAdj, bboxAbsorbed, multiNodeClusters,
                  deadEnds, corridors, junctions, openAreas);

    // Edge-classification summary. Counts are multi-fire (each graph
    // edge is classified by every pass that examines it — Pass 1
    // core-merge + Pass 2 absorb vetoes, then Pass 3 external-edge
    // collection), so
    // reading them as raw per-edge truth would overcount. They're a
    // ratio/anomaly signal: blocked > 0 confirms the wall primitive
    // is catching at least some wall-crossing edges; caveat-1 hits > 0
    // signals K1 violates the walkmesh-holes-for-doors convention here
    // (rare, high-signal); caveat-2 candidates is a per-area noise
    // floor for "Clear edges between distinct .wok rooms".
    acclog::Write("WallTopo",
                  "  edge-classification summary (multi-counted across "
                  "passes): clear=%d door=%d blocked=%d | "
                  "caveat-1 (door-in-wall)=%d caveat-2 (cross-room "
                  "candidates)=%d",
                  s_class_clear, s_class_door, s_class_blocked,
                  s_caveat1_hits, s_caveat2_hits);

    // ===== End-of-build diagnostics (observation only, no behaviour) =====
    // Per-node topology signatures (degree / triangles / 4-cycles / reach)
    // for future gate tuning.
    LogTopologyMetrics(g, n);
    // Phantom-wall confirmation: cross-check the engine's own nav graph
    // against the cached walls (3D-aware). Any nav edge that crosses a
    // cached wall proves that wall is phantom — see LogNavWallCrossings.
    LogNavWallCrossings(g);
    // Per multi-node cluster, dump member adjacency + ClassifyEdge verdicts.
    LogClusterMemberAdjacency(g, n);
    // Final per-node cluster/kind/sig/label table.
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    for (int i = 0; i < g_graph.node_count; ++i) {
        int root = UFFind(i);
        acclog::Write("WallTopo",
                      "  node[%d] cluster=%d kind=%d sig=%d "
                      "pos=(%.1f,%.1f,%.1f) label=\"%s\"",
                      i, root, g_graph.node_kind[i], g_graph.node_sig[i],
                      g_graph.node_pos[i].x, g_graph.node_pos[i].y,
                      g_graph.node_pos[i].z, g_graph.node_label[i].c_str());
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              std::string& outLabel, int& outSig,
              int& outClusterId,
              bool allowDiagLog,
              bool requireWallReachable) {
    outLabel.clear();
    outSig = 0;
    outClusterId = kClusterIdNone;
    if (!HasGraphForArea(area)) return false;
    if (g_graph.node_count <= 0) return false;

    // 2D nearest-node. The engine stores all path-graph nodes at z=0
    // (the nav data is logically 2D — verified across every area in
    // logged play, including multi-elevation maps). Including dz in
    // the distance broke elevated areas: Taris Sewers spawns the
    // player at z≈28m on the upper platform, which alone exceeds the
    // 15m snap radius and pins every position to the neutral "Bereich"
    // fallback. Multi-floor disambiguation needs to come from room-id,
    // not z against an always-zero graph.
    //
    // Two parallel best-candidate slots:
    //   - `best`         : reachable, unfiltered (primary)
    //   - `bestFiltered` : reachable, walkmesh-gate-rejected (rescue)
    // Primary wins whenever a candidate sits within snap range. Rescue
    // is consulted only when no primary in range — recovers the
    // Bucket-3 "alcove dropped by the geometry gate, no other labelled
    // cluster nearby → would have emitted the neutral Bereich" case without
    // re-introducing wall-curve artefacts on inhabited terrain (those
    // get absorbed by the core-merge / straggler-absorb passes upstream,
    // or lose on distance to a real labelled cluster).
    //
    // Wall-reachability filter (added 2026-05-20, originally for the
    // Endar Spire starting room speaking corridor labels from the
    // *adjacent* hallway): test segment (worldPos → candidate.pos)
    // against the seam-filtered perimeter wall cache. Applied
    // identically to both primary and rescue scans.
    //
    // Track best-blocked separately so we can still log "would have
    // picked X but it's behind a wall" for diagnostic tuning.
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    bool haveWalls = acc::spatial::change_detector::GetCachedWalls(
        walls, wallCount);

    int   best           = -1;
    float bestSq         = 1e30f;
    int   bestFiltered   = -1;
    float bestFilteredSq = 1e30f;
    int   bestBlocked    = -1;
    float bestBlockedSq  = 1e30f;
    int   blockedSeen    = 0;
    for (int i = 0; i < g_graph.node_count; ++i) {
        if (g_graph.node_label[i].empty()) continue;
        float dx = g_graph.node_pos[i].x - worldPos.x;
        float dy = g_graph.node_pos[i].y - worldPos.y;
        float d2 = dx * dx + dy * dy;

        bool reachable = true;
        if (haveWalls && requireWallReachable) {
            Vector hitTmp{};
            reachable = !acc::engine::SegmentCrossesWalkmesh(
                walls, wallCount, worldPos, g_graph.node_pos[i], hitTmp);
        }

        if (g_graph.node_filtered[i]) {
            // Rescue-only candidate. Wall-filtered diagnostics ignore
            // these (they're not "would have been picked" by the
            // primary scan in any case).
            if (reachable && d2 < bestFilteredSq) {
                bestFilteredSq = d2;
                bestFiltered   = i;
            }
            continue;
        }

        if (reachable) {
            if (d2 < bestSq) {
                bestSq = d2;
                best   = i;
            }
        } else {
            ++blockedSeen;
            if (d2 < bestBlockedSq) {
                bestBlockedSq = d2;
                bestBlocked   = i;
            }
        }
    }

    // Diagnostic: log the snap decision whenever the wall filter
    // actually rejected a candidate that would have been closer than
    // our pick. Silent when the nearest candidate is already
    // reachable (no filtering happened). Called from
    // SpeakRoomChange / LogWallTopoComparison — both fire on room
    // transitions, not per-tick, so logging here is bounded.
    if (allowDiagLog &&
        bestBlocked >= 0 &&
        (best < 0 || bestBlockedSq < bestSq)) {
        float bDist = std::sqrt(bestBlockedSq);
        float pDist = best >= 0 ? std::sqrt(bestSq) : -1.0f;
        acclog::Write(
            "WallTopo",
            "LookupAt WALL-FILTERED at (%.1f,%.1f,%.1f): nearest "
            "candidate node[%d] (%.1f,%.1f) \"%s\" at %.1fm is behind "
            "a wall; picked node[%d] (%.1f,%.1f) \"%s\" at %.1fm "
            "instead (blocked-seen=%d)",
            worldPos.x, worldPos.y, worldPos.z,
            bestBlocked,
            g_graph.node_pos[bestBlocked].x,
            g_graph.node_pos[bestBlocked].y,
            g_graph.node_label[bestBlocked].c_str(),
            bDist,
            best,
            best >= 0 ? g_graph.node_pos[best].x : 0.0f,
            best >= 0 ? g_graph.node_pos[best].y : 0.0f,
            best >= 0 ? g_graph.node_label[best].c_str() : "(none)",
            pDist,
            blockedSeen);
    }
    // Sanity: every candidate filtered out is a strong signal the
    // wall cache or the player position is wrong. Surfaces overfire.
    if (allowDiagLog && best < 0 && blockedSeen > 0) {
        acclog::Write(
            "WallTopo",
            "LookupAt ALL-BLOCKED at (%.1f,%.1f,%.1f): every labelled "
            "node (%d candidates) was wall-filtered. Falling back to "
            "neutral Bereich. Possible overfire — check wall cache vs "
            "player position.",
            worldPos.x, worldPos.y, worldPos.z, blockedSeen);
    }

    // Primary wins when in range.
    if (best >= 0) {
        float bestDist = std::sqrt(bestSq);
        if (bestDist <= kMaxSnapM &&
            !g_graph.node_label[best].empty()) {
            outLabel     = g_graph.node_label[best];
            outSig       = g_graph.node_sig[best];
            outClusterId = g_graph.node_cluster_id[best];
            return true;
        }
    }

    // Rescue: no primary in range, but a filtered candidate is. Recovers
    // alcoves the walkmesh-shape gate misjudged when no labelled cluster
    // sits within snap radius. Merge passes upstream eliminate the
    // wall-curve case (artefacts inside inhabited regions get absorbed
    // by the core-merge / straggler-absorb passes), so surviving filtered
    // singletons are graph-isolated and almost always genuine alcoves.
    if (bestFiltered >= 0) {
        float fDist = std::sqrt(bestFilteredSq);
        if (fDist <= kMaxSnapM &&
            !g_graph.node_label[bestFiltered].empty()) {
            if (allowDiagLog) {
                float pDist = best >= 0 ? std::sqrt(bestSq) : -1.0f;
                acclog::Write(
                    "WallTopo",
                    "LookupAt RESCUE at (%.1f,%.1f,%.1f): no unfiltered "
                    "candidate in range (best primary %.1fm); using "
                    "filtered node[%d] (%.1f,%.1f) \"%s\" at %.1fm",
                    worldPos.x, worldPos.y, worldPos.z,
                    pDist,
                    bestFiltered,
                    g_graph.node_pos[bestFiltered].x,
                    g_graph.node_pos[bestFiltered].y,
                    g_graph.node_label[bestFiltered].c_str(),
                    fDist);
            }
            outLabel     = g_graph.node_label[bestFiltered];
            outSig       = g_graph.node_sig[bestFiltered];
            outClusterId = g_graph.node_cluster_id[bestFiltered];
            return true;
        }
    }

    // No primary, no rescue: emit the neutral "Bereich" when the graph
    // has at least one candidate (so the player gets one stable "you're
    // in an area" cue). Return false only when the graph is empty.
    if (best < 0 && bestFiltered < 0 && blockedSeen == 0) {
        return false;
    }
    const char* fallback =
        acc::strings::Get(acc::strings::Id::AreaNoun);
    if (fallback && fallback[0]) {
        outLabel = fallback;
        outSig = kKindOpenArea & 0xff;
        outClusterId = kClusterIdOpenArea;
        return true;
    }
    return false;
}

}  // namespace acc::wall_topology
