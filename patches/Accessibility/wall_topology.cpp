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

#include "engine_area.h"        // AreaObjectIterator, GetObjectKind, GameObjectKind::Door, GetObjectPosition, kDoorTransitionDestOffset
#include "engine_navgraph.h"
#include "engine_reads.h"       // ExtractTextOrStrRef — transition-destination loc-string read
#include "log.h"
#include "region_classifier.h"  // ProbeShapeAt — walkmesh gate on graph-only dead-ends
#include "strings.h"

namespace acc::wall_topology {

namespace {

// ---------------------------------------------------------------------
// Tunable parameters.
// ---------------------------------------------------------------------

// Re-use the navgraph cap so per-node arrays line up with the snapshot.
constexpr int kMaxNodes = acc::engine::navgraph::kMaxNodes;

// Snap radius for runtime LookupAt: any player position more than this
// many world units (KOTOR = metres) from the nearest graph node falls
// back to "Offene Fläche". 15m matches the typical room-diagonal in
// vanilla content while still catching the central plaza / exterior
// case where the nav graph is sparse. Tune from log evidence.
constexpr float kMaxSnapM = 15.0f;

// Junction-merge thresholds. Two directly-connected nav-graph nodes
// that are both degree-≥3 and pass these gates are merged into one
// composite cluster: the cluster's announce describes the union of
// their external exits, computed from the centroid of the cluster
// members. K1 places nav-graph nodes densely around hub areas (Dias
// Apartment at Taris Apartments has 2 junctions 6m apart, both reading
// as Kreuzung in sequence); merging collapses these to one perceptual
// "hub" announce.
//
// - kMergeMaxDistanceM: planar distance gate. 8m matches K1's typical
//   inter-junction spacing in hub clusters; wider corridors should not
//   merge their endpoints.
// - kMergeMaxZM: vertical gate. Stops multi-floor stacks (Sith base,
//   Endar Spire decks) from collapsing vertically-aligned junctions.
constexpr float kMergeMaxDistanceM = 8.0f;
constexpr float kMergeMaxZM        = 1.0f;

// Kind values now live in the public header (wall_topology.h) so
// transitions.cpp can branch on Platz for the delayed-announce path.
// Aliases here keep the local code compact.
constexpr int kKindDeadEnd  = KindDeadEnd;
constexpr int kKindCorridor = KindCorridor;
constexpr int kKindJunction = KindJunction;
constexpr int kKindOpenArea = KindOpenArea;
constexpr int kKindPlatz    = KindPlatz;

// ---------------------------------------------------------------------
// Cached state for the current area.
// ---------------------------------------------------------------------

// Per-area door snapshot. Captured on BuildForArea (alongside the
// nav-graph snapshot) so the per-edge "is there a door between these
// two nav points?" query doesn't have to re-iterate game_objects[] +
// SEH-read every time. Transition destination is the CExoLocString at
// CSWSDoor +0x3c8 — non-empty when this door is an area-transition
// trigger (cross-area door), empty for normal in-area doors.
struct DoorRecord {
    Vector pos;
    char   transitionDest[64];
};

constexpr int kMaxDoors = 128;

struct AreaGraph {
    void*       area_owner   = nullptr;
    bool        built        = false;
    int         node_count   = 0;
    Vector      node_pos    [kMaxNodes];
    char        node_label  [kMaxNodes][96];
    int         node_sig    [kMaxNodes];
    int         node_kind   [kMaxNodes];  // see kKind* constants
    int         door_count   = 0;
    DoorRecord  doors       [kMaxDoors];
};

AreaGraph g_graph;

// ---------------------------------------------------------------------
// Direction helpers.
// ---------------------------------------------------------------------

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

// "Norded-out" corridor axis. A corridor axis is a line, not a vector —
// both endpoints are valid. To avoid emitting "Korridor Süd" when the
// player would experience the same axis as "Korridor Nord" from the
// other end, always flip the vector to point toward the northern half
// of the world (engine +Y). At dy == 0 we tie-break to East so a pure
// East-West corridor always reads as "Korridor Ost", never "Korridor
// West". Documented here in source for the inevitable "why does it
// never say south?" question.
acc::strings::Id NordedOutAxisOctant(float dx, float dy) {
    if (dy < 0.0f || (dy == 0.0f && dx < 0.0f)) {
        dx = -dx;
        dy = -dy;
    }
    return OctantFromVector(dx, dy);
}

// Append a direction word to a comma-separated list. Returns the new
// length of `dirList`.
size_t AppendDirWord(char* dirList, size_t bufSize, size_t dirLen,
                     acc::strings::Id dirId) {
    const char* word = acc::strings::Get(dirId);
    if (!word || !word[0]) return dirLen;
    if (dirLen > 0 && dirLen + 2 < bufSize) {
        dirList[dirLen++] = ',';
        dirList[dirLen++] = ' ';
        dirList[dirLen]   = '\0';
    }
    size_t remaining = bufSize > dirLen ? bufSize - dirLen : 0;
    if (remaining == 0) return dirLen;
    int n = std::snprintf(dirList + dirLen, remaining, "%s", word);
    if (n > 0) {
        size_t advanced = static_cast<size_t>(n) < remaining
                              ? static_cast<size_t>(n)
                              : (remaining > 0 ? remaining - 1 : 0);
        dirLen += advanced;
    }
    return dirLen;
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

// ---------------------------------------------------------------------
// Cluster classification (degree-driven, with junction-merge support).
// ---------------------------------------------------------------------

// Per-node degree from the snapshot. Tiny helper — every classifier
// path needs this and it keeps the call sites self-documenting.
int Degree(const acc::engine::navgraph::NavGraphSnapshot& g, int node) {
    int lo = 0, hi = 0;
    acc::engine::navgraph::NeighbourRange(g, node, lo, hi);
    return hi - lo;
}

// ---------------------------------------------------------------------
// Door snapshot + edge-near-door geometry.
// ---------------------------------------------------------------------

// Collect every CSWSDoor in the area into g_graph.doors. Reads world
// position (+0x90) + transition destination (CExoLocString @+0x3c8 if
// non-empty). Idempotent within a BuildForArea call — relies on the
// caller to have Reset'd the cache.
//
// All reads are SEH-bounded through the underlying engine helpers.
// Faulted reads just skip the door; the snapshot truncates rather than
// fails so a partial set is still usable for the edge-near-door query.
void SnapshotDoors(void* area) {
    g_graph.door_count = 0;
    if (!area) return;

    acc::engine::AreaObjectIterator iter(area);
    void* obj = nullptr;
    int withTransition = 0;
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
        // Transition destination CExoLocString lives at +0x3c8 (text)
        // with the strref at +0x3cc; ExtractTextOrStrRef walks both.
        // Empty result is fine — most doors aren't transitions.
        if (acc::engine::ExtractTextOrStrRef(
                obj,
                kDoorTransitionDestOffset,
                kDoorTransitionDestOffset + 4,
                rec.transitionDest, sizeof(rec.transitionDest)) &&
            rec.transitionDest[0]) {
            ++withTransition;
        }
        ++g_graph.door_count;
    }
    acclog::Write("WallTopo",
                  "SnapshotDoors: collected %d doors (%d transition)",
                  g_graph.door_count, withTransition);
    for (int i = 0; i < g_graph.door_count; ++i) {
        const DoorRecord& d = g_graph.doors[i];
        acclog::Write("WallTopo",
                      "  door[%d] pos=(%.1f,%.1f,%.1f) transition=\"%s\"",
                      i, d.pos.x, d.pos.y, d.pos.z,
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

        float dN = acc::region::ProbeDistance(d.pos,  0.0f,  1.0f);
        float dE = acc::region::ProbeDistance(d.pos,  1.0f,  0.0f);
        float dS = acc::region::ProbeDistance(d.pos,  0.0f, -1.0f);
        float dW = acc::region::ProbeDistance(d.pos, -1.0f,  0.0f);

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

// Render the door-flavoured replacement of a direction word. Picks the
// transition format when the matched door carries a destination name,
// otherwise the bare "Tür %s" form. Single source of truth for the
// dead-end / corridor / junction-octant rewrites.
void RenderDoorDirection(int doorIdx,
                         const char* dirWord,
                         char* outBuf, size_t bufSize) {
    using acc::strings::Id;
    if (!outBuf || bufSize == 0 || !dirWord || !dirWord[0]) {
        if (outBuf && bufSize > 0) outBuf[0] = '\0';
        return;
    }
    const char* dest =
        (doorIdx >= 0 && doorIdx < g_graph.door_count)
            ? g_graph.doors[doorIdx].transitionDest
            : "";
    if (dest && dest[0]) {
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDoorTransition);
        if (fmt && fmt[0]) {
            std::snprintf(outBuf, bufSize, fmt, dirWord, dest);
            return;
        }
    }
    const char* fmt = acc::strings::Get(Id::FmtMapCursorDoor);
    if (fmt && fmt[0]) {
        std::snprintf(outBuf, bufSize, fmt, dirWord);
    } else {
        std::snprintf(outBuf, bufSize, "%s", dirWord);
    }
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
    return acc::region::IsAlcoveAlongAxis(deadEndPos, fx, fy);
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

// Append one direction entry to `dirList`. When `markDeadEnd` is set,
// the direction word is wrapped via FmtMapCursorJunctionDeadEndExit
// ("Sackgasse %s") so the user knows that exit doesn't lead onward
// (option 1 from the 2026-05-13 Dias-cluster discussion — junctions
// whose edges include degree-1 stubs should call them out). Prefix
// form mirrors the door rewrite ("Tür %s nach %s") so every special
// exit reads NOUN-then-direction within the junction list.
size_t AppendDirEntry(char* dirList, size_t bufSize, size_t dirLen,
                      acc::strings::Id dirId, bool markDeadEnd) {
    const char* word = acc::strings::Get(dirId);
    if (!word || !word[0]) return dirLen;

    char entry[64];
    if (markDeadEnd) {
        const char* fmt = acc::strings::Get(
            acc::strings::Id::FmtMapCursorJunctionDeadEndExit);
        if (fmt && fmt[0]) {
            std::snprintf(entry, sizeof(entry), fmt, word);
        } else {
            std::snprintf(entry, sizeof(entry), "%s", word);
        }
    } else {
        std::snprintf(entry, sizeof(entry), "%s", word);
    }

    if (dirLen > 0 && dirLen + 2 < bufSize) {
        dirList[dirLen++] = ',';
        dirList[dirLen++] = ' ';
        dirList[dirLen]   = '\0';
    }
    size_t remaining = bufSize > dirLen ? bufSize - dirLen : 0;
    if (remaining == 0) return dirLen;
    int n = std::snprintf(dirList + dirLen, remaining, "%s", entry);
    if (n > 0) {
        size_t advanced = static_cast<size_t>(n) < remaining
                              ? static_cast<size_t>(n)
                              : (remaining > 0 ? remaining - 1 : 0);
        dirLen += advanced;
    }
    return dirLen;
}

// Classify a cluster (1 or more nodes) by its centroid and the list of
// external neighbour nodes (nodes outside this cluster that share an
// edge with some member). `clusterSize` is the count of nodes in the
// cluster — used only to distinguish singleton junctions ("Kreuzung")
// from merged ones ("Platz"). Renders the label + kind + sig into the
// out-params.
//
//   externalCount == 0  → isolated / open area
//   externalCount == 1  → dead-end (direction = centroid → that exit)
//   externalCount == 2  → corridor (axis between the two exits,
//                                   cardinal cases use "Nord-Süd" /
//                                   "Ost-West" axis words)
//   externalCount >= 3  → junction (singleton) or Platz (merged).
//                         8-octant bucketing with passable-wins:
//                         per-octant aggregation marks the octant as
//                         dead-end only when EVERY exit in it leads
//                         to a degree-1 neighbour. Any passable exit
//                         in the octant suppresses the marker — fixes
//                         the order-dependent first-wins bug from the
//                         initial revision.
void ClassifyCluster(const acc::engine::navgraph::NavGraphSnapshot& g,
                     const Vector& centroid, int clusterSize,
                     const int* externalNbs,
                     const int* externalSrcs,
                     int externalCount,
                     char* outLabel, size_t outLabelSize,
                     int& outKind, int& outSig) {
    using acc::strings::Id;
    if (outLabelSize > 0) outLabel[0] = '\0';
    outKind = kKindOpenArea;
    outSig  = kKindOpenArea;
    if (outLabelSize == 0) return;
    int n = static_cast<int>(g.nodes.size());

    if (externalCount == 0) {
        const char* s = acc::strings::Get(Id::MapCursorOpenArea);
        if (s && s[0]) std::snprintf(outLabel, outLabelSize, "%s", s);
        return;
    }
    if (externalCount == 1) {
        int nb = externalNbs[0];
        if (nb < 0 || nb >= n) return;

        // Walkmesh-shape gate: a degree-1 graph node only emits a
        // "Sackgasse" label when the walkmesh at the node's position
        // shows the alcove signature (forward > 2m + 3 short rays) when
        // the 4-ray probe is rotated to align with the parent direction.
        // Wall-curve artefacts in big open areas / corridors get filtered
        // here — they're degree-1 in the graph (one patrol-anchor
        // connection) but the local geometry doesn't form a recess the
        // player can walk into.
        if (!WalkmeshAgreesDeadEnd(centroid, g.nodes[nb].pos)) {
            outKind = kKindOpenArea;
            outSig  = kKindOpenArea & 0xff;
            // Leave outLabel empty — LookupAt skips empty-label nodes,
            // so the next-nearest labelled neighbour wins.
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: degree-1 at (%.1f,%.1f,%.1f) FAILED "
                "walkmesh-shape gate (parent=%d at %.1f,%.1f) — dropping "
                "Sackgasse label",
                centroid.x, centroid.y, centroid.z,
                nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y);
            return;
        }
        float dx = g.nodes[nb].pos.x - centroid.x;
        float dy = g.nodes[nb].pos.y - centroid.y;
        Id dir = OctantFromVector(dx, dy);
        const char* word = acc::strings::Get(dir);

        // Door-on-edge check: if a CSWSDoor sits on the segment from
        // this dead-end's centroid to its single neighbour, the label
        // shifts from "Sackgasse, X" to "Tür X" (or "Tür X nach DEST"
        // for transition doors). Door wins over Sackgasse — more
        // actionable for the player.
        int doorIdx = FindDoorOnEdge(centroid, g.nodes[nb].pos);
        if (doorIdx >= 0 && word && word[0]) {
            RenderDoorDirection(doorIdx, word, outLabel, outLabelSize);
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: degree-1 at (%.1f,%.1f,%.1f) HIT door "
                "idx=%d on edge → \"%s\"",
                centroid.x, centroid.y, centroid.z, doorIdx, outLabel);
        } else {
            const char* fmt  = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0] && word && word[0]) {
                std::snprintf(outLabel, outLabelSize, fmt, word);
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
        float dx = g.nodes[nbB].pos.x - g.nodes[nbA].pos.x;
        float dy = g.nodes[nbB].pos.y - g.nodes[nbA].pos.y;
        Id dir = NordedOutAxisOctant(dx, dy);
        Id wordId = dir;
        if (dir == Id::DirNorth) wordId = Id::AxisNorthSouth;
        else if (dir == Id::DirEast) wordId = Id::AxisEastWest;
        const char* word = acc::strings::Get(wordId);

        // Door-on-edge: corridor segment passes through a door → relabel.
        // Query is across the whole corridor (nbA → nbB) because the
        // cluster node sits between them; a door anywhere on that span
        // is the one the player would walk through.
        int doorIdx = FindDoorOnEdge(g.nodes[nbA].pos, g.nodes[nbB].pos);
        if (doorIdx >= 0 && word && word[0]) {
            RenderDoorDirection(doorIdx, word, outLabel, outLabelSize);
            acclog::Write(
                "WallTopo",
                "ClassifyCluster: degree-2 corridor at (%.1f,%.1f,%.1f) "
                "HIT door idx=%d on segment → \"%s\"",
                centroid.x, centroid.y, centroid.z, doorIdx, outLabel);
        } else {
            const char* fmt  = acc::strings::Get(Id::FmtMapCursorCorridorDir);
            if (fmt && fmt[0] && word && word[0]) {
                std::snprintf(outLabel, outLabelSize, fmt, word);
            }
        }
        outKind = kKindCorridor;
        int octBit = OctantBit(dir);
        outSig = (kKindCorridor & 0xff) | ((octBit & 0xff) << 8);
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

        // Door test runs regardless of dead-end / wall-curve status —
        // a door is the player-actionable signal even when the nav
        // node behind it is a wall-curve patrol anchor.
        bool hasDoor = false;
        if (octantDoorIdx[bit] < 0) {
            int doorIdx = FindDoorOnEdge(edgeStart, g.nodes[nb].pos);
            if (doorIdx >= 0) {
                octantDoorIdx[bit] = doorIdx;
                hasDoor = true;
                acclog::Write(
                    "WallTopo",
                    "ClassifyCluster: junction at (%.1f,%.1f) octant=%d "
                    "edge from member %d (%.1f,%.1f) -> nb %d (%.1f,%.1f) "
                    "HIT door[%d] at (%.1f,%.1f)",
                    centroid.x, centroid.y, bit,
                    src, edgeStart.x, edgeStart.y,
                    nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                    doorIdx,
                    g_graph.doors[doorIdx].pos.x,
                    g_graph.doors[doorIdx].pos.y);
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

    // Second pass: emit octants in canonical order (N, NE, E, SE, S,
    // SW, W, NW). Stable across runs; matches the way a compass-oriented
    // player scans for exits clockwise.
    char dirList[96] = {0};
    size_t dirLen = 0;
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
                char doorEntry[96];
                RenderDoorDirection(octantDoorIdx[bit], dirWord,
                                    doorEntry, sizeof(doorEntry));
                if (dirLen > 0 && dirLen + 2 < sizeof(dirList)) {
                    dirList[dirLen++] = ',';
                    dirList[dirLen++] = ' ';
                    dirList[dirLen]   = '\0';
                }
                size_t remaining = sizeof(dirList) - dirLen;
                int written = std::snprintf(dirList + dirLen, remaining,
                                            "%s", doorEntry);
                if (written > 0) {
                    size_t adv = static_cast<size_t>(written) < remaining
                                     ? static_cast<size_t>(written)
                                     : (remaining > 0 ? remaining - 1 : 0);
                    dirLen += adv;
                }
            }
            continue;
        }

        bool markDeadEnd = octantAllDeadEnd[bit];
        if (markDeadEnd) deadEndMask |= (1 << bit);
        dirLen = AppendDirEntry(dirList, sizeof(dirList), dirLen,
                                dirId, markDeadEnd);
    }

    bool isPlatz = clusterSize > 1;
    Id fmtId = isPlatz ? Id::FmtMapCursorPlazaDirs
                       : Id::FmtMapCursorJunctionDirs;
    int kind = isPlatz ? kKindPlatz : kKindJunction;

    const char* fmt = acc::strings::Get(fmtId);
    if (fmt && fmt[0] && dirList[0] != '\0') {
        std::snprintf(outLabel, outLabelSize, fmt, dirList);
    } else {
        const char* bare = acc::strings::Get(Id::MapCursorJunction);
        if (bare && bare[0]) {
            std::snprintf(outLabel, outLabelSize, "%s", bare);
        }
    }
    outKind = kind;
    outSig  = (kind & 0xff) |
              ((mask & 0xff) << 8) |
              ((deadEndMask & 0xff) << 16);
}

}  // namespace

void Reset() {
    g_graph.area_owner = nullptr;
    g_graph.built      = false;
    g_graph.node_count = 0;
    g_graph.door_count = 0;
}

bool HasGraphForArea(void* area) {
    return area != nullptr &&
           g_graph.built &&
           g_graph.area_owner == area;
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
    if (n > kMaxNodes) n = kMaxNodes;
    g_graph.node_count = n;
    for (int i = 0; i < n; ++i) {
        g_graph.node_pos[i]      = g.nodes[i].pos;
        g_graph.node_label[i][0] = '\0';
        g_graph.node_sig[i]      = 0;
        g_graph.node_kind[i]     = kKindOpenArea;
    }

    // Snapshot doors before classifying clusters — ClassifyCluster's
    // FindDoorOnEdge calls consume g_graph.doors[].
    SnapshotDoors(area);

    // Pass 1: union-find merge of directly-connected degree-≥3 nodes
    // within the distance + z gates. The cluster classifier in pass 2
    // then treats each cluster (singletons included) as one perceptual
    // place — adjacent same-octant junctions collapse to a single
    // "Kreuzung" announce with the union of their external exits.
    //
    // Door-on-edge veto: a nav edge that crosses a CSWSDoor separates
    // two distinct visual rooms (often a security door into a small
    // loot room ~12m deep — see tar_m02aa cases for door[1]/[5]/[6]).
    // Sighted players read each side as its own space; merging hides
    // the door entirely. Skip the merge; the door surfaces as a "Tür
    // DIR" exit on each side via ClassifyCluster's external-edge loop.
    //
    // We tried .wok room-id veto first (build 22:59:21) but engine
    // rooms in K1 are authoring chunks, not visual rooms — central
    // hubs split into 4+ adjacent rooms with no real divider between
    // them, and even single visual rooms can be 2 .wok chunks. Door
    // presence is the more reliable visual-boundary signal.
    for (int i = 0; i < n; ++i) s_uf_parent[i] = i;
    int mergeEdges = 0;
    int mergeVetoedByDoor = 0;
    for (int i = 0; i < n; ++i) {
        if (Degree(g, i) < 3) continue;
        int lo = 0, hi = 0;
        acc::engine::navgraph::NeighbourRange(g, i, lo, hi);
        for (int e = lo; e < hi; ++e) {
            int j = static_cast<int>(g.conns[e]);
            if (j < 0 || j >= n) continue;
            if (j <= i) continue;  // process each unordered pair once
            if (Degree(g, j) < 3) continue;
            float dx = g.nodes[i].pos.x - g.nodes[j].pos.x;
            float dy = g.nodes[i].pos.y - g.nodes[j].pos.y;
            float dz = g.nodes[i].pos.z - g.nodes[j].pos.z;
            if (dx * dx + dy * dy >
                kMergeMaxDistanceM * kMergeMaxDistanceM) continue;
            if (std::fabs(dz) > kMergeMaxZM) continue;
            if (FindDoorOnEdge(g.nodes[i].pos, g.nodes[j].pos) >= 0) {
                ++mergeVetoedByDoor;
                acclog::Write(
                    "WallTopo",
                    "  merge VETOED (door on edge): node[%d] (%.1f,%.1f) "
                    "<-/-> node[%d] (%.1f,%.1f)",
                    i, g.nodes[i].pos.x, g.nodes[i].pos.y,
                    j, g.nodes[j].pos.x, g.nodes[j].pos.y);
                continue;
            }
            UFUnite(i, j);
            ++mergeEdges;
        }
    }

    // Pass 2: per-cluster classification. For each cluster root (the
    // smallest node id in its union-find class), compute the centroid,
    // collect external neighbours (dedup by node id), classify via
    // ClassifyCluster, and write the result to every member.
    int clusters = 0, multiNodeClusters = 0;
    int deadEnds = 0, corridors = 0, junctions = 0, openAreas = 0;
    for (int root = 0; root < n; ++root) {
        if (UFFind(root) != root) continue;
        ++clusters;

        // Centroid of all members.
        Vector centroid = {0.0f, 0.0f, 0.0f};
        int size = 0;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            centroid.x += g.nodes[m].pos.x;
            centroid.y += g.nodes[m].pos.y;
            centroid.z += g.nodes[m].pos.z;
            ++size;
        }
        if (size > 0) {
            centroid.x /= size;
            centroid.y /= size;
            centroid.z /= size;
        }
        if (size > 1) ++multiNodeClusters;

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
        constexpr int kMaxExternal = 16;
        int externalNbs[kMaxExternal];
        int externalSrcs[kMaxExternal];   // cluster member that owns this edge
        int externalCount = 0;
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(g, m, lo, hi);
            for (int e = lo; e < hi; ++e) {
                int nb = static_cast<int>(g.conns[e]);
                if (nb < 0 || nb >= n) continue;
                if (UFFind(nb) == root) continue;  // internal edge
                bool seen = false;
                for (int k = 0; k < externalCount; ++k) {
                    if (externalNbs[k] == nb) { seen = true; break; }
                }
                if (!seen && externalCount < kMaxExternal) {
                    externalNbs [externalCount] = nb;
                    externalSrcs[externalCount] = m;
                    ++externalCount;
                }
            }
        }

        char label[96] = {0};
        int kind = kKindOpenArea, sig = 0;
        ClassifyCluster(g, centroid, size, externalNbs, externalSrcs,
                        externalCount,
                        label, sizeof(label), kind, sig);

        switch (kind) {
            case kKindDeadEnd:  ++deadEnds;  break;
            case kKindCorridor: ++corridors; break;
            case kKindJunction: ++junctions; break;
            case kKindPlatz:    ++junctions; break;  // tally w/ junctions
            default:            ++openAreas; break;
        }

        // Write to every cluster member.
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            std::strncpy(g_graph.node_label[m], label,
                         sizeof(g_graph.node_label[m]) - 1);
            g_graph.node_label[m][sizeof(g_graph.node_label[m]) - 1] = '\0';
            g_graph.node_kind[m] = kind;
            g_graph.node_sig[m]  = sig;
        }
    }

    g_graph.built = true;
    acclog::Write("WallTopo",
                  "BuildForArea: area=%p nodes=%d clusters=%d "
                  "merged-pairs=%d merge-vetoed-by-door=%d "
                  "multi-node-clusters=%d "
                  "(dead=%d corridor=%d junction=%d open=%d)",
                  area, n, clusters, mergeEdges, mergeVetoedByDoor,
                  multiNodeClusters,
                  deadEnds, corridors, junctions, openAreas);

    // Diagnostic: per multi-node cluster, dump member adjacency. For
    // each member node, list its graph neighbours split into
    // internal (same cluster) and external (cross-cluster), and mark
    // any edge that crosses a door via FindDoorOnEdge. This exposes
    // whether dividing doors split a merged cluster into a "junction
    // side" (member with external neighbours other than the door) and
    // a "room side" (member whose only off-cluster path is through
    // the door itself).
    for (int root = 0; root < n; ++root) {
        if (UFFind(root) != root) continue;
        int size = 0;
        for (int m = 0; m < n; ++m) if (UFFind(m) == root) ++size;
        if (size < 2) continue;
        acclog::Write("WallTopo",
                      "  cluster[%d] size=%d label=\"%s\"",
                      root, size, g_graph.node_label[root]);
        for (int m = 0; m < n; ++m) {
            if (UFFind(m) != root) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(g, m, lo, hi);
            for (int e = lo; e < hi; ++e) {
                int nb = static_cast<int>(g.conns[e]);
                if (nb < 0 || nb >= n) continue;
                bool internal = (UFFind(nb) == root);
                int doorIdx = FindDoorOnEdge(g.nodes[m].pos,
                                             g.nodes[nb].pos);
                if (doorIdx >= 0) {
                    acclog::Write(
                        "WallTopo",
                        "    member[%d] (%.1f,%.1f) -> nb[%d] (%.1f,%.1f) "
                        "%s door[%d] transition=\"%s\"",
                        m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                        internal ? "INTERNAL" : "EXTERNAL",
                        doorIdx,
                        g_graph.doors[doorIdx].transitionDest[0]
                            ? g_graph.doors[doorIdx].transitionDest
                            : "(none)");
                } else {
                    acclog::Write(
                        "WallTopo",
                        "    member[%d] (%.1f,%.1f) -> nb[%d] (%.1f,%.1f) "
                        "%s no-door",
                        m, g.nodes[m].pos.x, g.nodes[m].pos.y,
                        nb, g.nodes[nb].pos.x, g.nodes[nb].pos.y,
                        internal ? "INTERNAL" : "EXTERNAL");
                }
            }
        }
    }

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
                      g_graph.node_pos[i].z, g_graph.node_label[i]);
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    outSig = 0;
    if (!HasGraphForArea(area)) return false;
    if (g_graph.node_count <= 0) return false;

    // 3D nearest-node — same-floor in single-floor areas, but the Z
    // term matters in stacked-floor maps (Sith base, Endar Spire decks,
    // Manaan). 2D nearest would snap to the wrong floor whenever the
    // nav graphs of two floors line up vertically. ~100 nodes, linear
    // scan is trivial.
    //
    // Skip nodes with empty labels — those are the walkmesh-shape gate's
    // filtered dead-ends. They have valid positions but no announce
    // text; treating them as "present for nearest-snap" would let them
    // steal lookups from real neighbours and emit "Offene Fläche" in
    // places the region classifier reads as "Kreuzung". The cleaner
    // behaviour is to make them transparent to LookupAt entirely — the
    // next-nearest labelled node takes over.
    int best = -1;
    float bestSq = 1e30f;
    for (int i = 0; i < g_graph.node_count; ++i) {
        if (g_graph.node_label[i][0] == '\0') continue;
        float dx = g_graph.node_pos[i].x - worldPos.x;
        float dy = g_graph.node_pos[i].y - worldPos.y;
        float dz = g_graph.node_pos[i].z - worldPos.z;
        float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < bestSq) {
            bestSq = d2;
            best   = i;
        }
    }

    if (best < 0) return false;

    float bestDist = std::sqrt(bestSq);
    if (bestDist > kMaxSnapM) {
        // Outside the indexed region — fall through to "open area".
        const char* fallback =
            acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
        if (fallback && fallback[0]) {
            std::snprintf(outBuf, bufSize, "%s", fallback);
            outSig = kKindOpenArea & 0xff;
            return true;
        }
        return false;
    }

    if (g_graph.node_label[best][0] == '\0') {
        // Node has no rendered label (isolated / classification skipped)
        // — emit "Offene Fläche" rather than leaving the user silent.
        const char* fallback =
            acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
        if (fallback && fallback[0]) {
            std::snprintf(outBuf, bufSize, "%s", fallback);
            outSig = kKindOpenArea & 0xff;
            return true;
        }
        return false;
    }

    std::snprintf(outBuf, bufSize, "%s", g_graph.node_label[best]);
    outSig = g_graph.node_sig[best];
    return true;
}

}  // namespace acc::wall_topology
