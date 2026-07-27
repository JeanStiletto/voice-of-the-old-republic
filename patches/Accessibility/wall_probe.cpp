// wall probing — single- and multi-ray casts against the cached walls.
//
// Split out of room_topology.cpp by the Phase-1 structure pass
// (refactoring candidate 12). See wall_probe.h for why this is its own
// system: the file it came from is really room topology, and the two had
// grown entangled under one name.
//
// Self-contained by construction - the probes read the perimeter-wall
// cache directly and hold no state of their own. ProbeWall stays private
// here; the three published entry points are the only ones room topology
// (or anything else) needs.

#include "wall_probe.h"

#include <cmath>

#include "engine_area.h"               // WallEdge, SegmentCrossesWalkmesh
#include "engine_offsets.h"            // Vector
#include "spatial_change_detector.h"   // GetCachedWalls — seam-filtered cache

namespace acc::wall_probe {

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

// Dead-end shape gate, a 4-ray probe rotated to align with
// (forwardX,forwardY) — the direction from the degree-1 node toward its
// graph parent, i.e. the way in. True when the forward ray is open (the
// entrance) while the node is boxed in on the other three sides. Two shapes
// qualify:
//   - tight alcove      — all three non-forward rays within kDeadEndBackM.
//   - corridor terminus — a close wall behind (back ray within kDeadEndBackM)
//     with the perpendicular walls merely bounded to corridor width
//     (kDeadEndSideM). This is the blocked end of a hallway: forward = the
//     long corridor you came down, back = the end wall a metre or two away,
//     sides = the corridor walls (~2.5-2.8m). The old alcove-only test
//     (all three rays within 2m) rejected these because a corridor is wider
//     than an alcove, so the terminus got filtered and LookupAt spoke the
//     corridor body's axis ("Ost-West") while the player stood on its dead
//     end instead of "Sackgasse" (2026-07-19 Endar Spire node[5]:
//     rays E=17.2 back-W=1.4 sides N=2.8/S=2.5).
// A wall-curve artefact in an open area fails both: it sits ALONG a wall, so
// its back and/or a side ray runs off into open space beyond kDeadEndSideM.
// Fail-open if the wall cache isn't ready.
bool IsAlcoveAlongAxis(const Vector& pos, float forwardX, float forwardY) {
    // Entrance must be open; terminal wall close behind; sides bounded to
    // corridor width (not open). kDeadEndSideM > kDeadEndBackM is what lets
    // a corridor end through while still requiring the node be enclosed on
    // three sides. Tune from the clearance-dump rays if a real terminus is
    // still filtered.
    constexpr float kDeadEndForwardM = 2.0f;
    constexpr float kDeadEndBackM    = 2.0f;
    constexpr float kDeadEndSideM    = 4.0f;

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
    return dF > kDeadEndForwardM &&
           dB <= kDeadEndBackM &&
           dR <= kDeadEndSideM &&
           dL <= kDeadEndSideM;
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

}  // namespace acc::wall_probe
