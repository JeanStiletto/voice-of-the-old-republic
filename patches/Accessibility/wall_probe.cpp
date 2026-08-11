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
#include "spatial_wall_surfaces.h"     // FloorZCandidatesAt — floor-tri cache

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

// Probe-height vote (see wall_probe.h). Nav-graph nodes carry z=0 (the
// .pth has no height) while SegmentCrossesWalkmesh rejects walls outside
// its z-guard — unlifted probes on any elevated floor report open space
// everywhere. This broke the alcove gate for EVERY degree-1 node on the
// Peragus Minenschächte floor (z≈3.4) and made the corridor dead-mark
// beyond-probe always clear (2026-08-11).
//
// The clearance dump's earlier per-caller lift used the single NEAREST
// endpoint's z, which latches onto multi-storey anomaly walls the moment
// a closer wall leaves the cache — opening a door reclassified a
// Sackgasse pocket as open space because its probe jumped to a leftover
// edge 4m overhead. So: vote instead of nearest. Endpoints within
// kZVoteRadiusM are bucketed into 1m height bands; the winner is the
// band with the most support including its two adjacent bands (floor
// edges straddling a band boundary must not split their vote and lose
// to a balcony). Fallbacks: nearest endpoint z when the radius is
// empty, pos.z when the cache is empty.
float ResolveProbeZ(const acc::engine::WallEdge* walls, int wallCount,
                    const Vector& pos) {
    constexpr float kZKeepCallerM = 2.0f;  // ≈ the walkmesh z-guard

    // Primary source: the walkmesh floor triangle under (x,y) — ground
    // truth, exact on ramps (plane interpolation), no statistics
    // involved. Multiple candidates = genuinely stacked storeys; a
    // caller z that sees one of them picks its storey, otherwise the
    // wall-band vote below breaks the tie. Off-mesh points (a centroid
    // synthesized outside the walkable surface) fall through to the
    // vote.
    float cand[4];
    int candCount = acc::spatial::wall_surfaces::FloorZCandidatesAt(
        pos.x, pos.y, cand, 4);
    if (candCount > 0) {
        for (int i = 0; i < candCount; ++i) {
            if (std::fabs(pos.z - cand[i]) <= kZKeepCallerM) return pos.z;
        }
        if (candCount == 1) return cand[0];
    }

    if (!walls || wallCount <= 0) {
        return (candCount > 0) ? cand[0] : pos.z;
    }
    constexpr float kZVoteRadiusM = 10.0f;
    constexpr float kZBandM       = 1.0f;
    constexpr int   kMaxBands     = 32;
    float bandSum[kMaxBands];
    int   bandCount[kMaxBands];
    int   bandKey[kMaxBands];
    int   bands = 0;
    float nearestZ  = pos.z;
    float nearestSq = 1e30f;
    const float radiusSq = kZVoteRadiusM * kZVoteRadiusM;
    for (int w = 0; w < wallCount; ++w) {
        for (int e = 0; e < 2; ++e) {
            const Vector& p = e ? walls[w].b : walls[w].a;
            float dx = p.x - pos.x;
            float dy = p.y - pos.y;
            float sq = dx * dx + dy * dy;
            if (sq < nearestSq) { nearestSq = sq; nearestZ = p.z; }
            if (sq > radiusSq) continue;
            int key = static_cast<int>(std::floor(p.z / kZBandM));
            int b = 0;
            for (; b < bands; ++b) {
                if (bandKey[b] == key) break;
            }
            if (b == bands) {
                if (bands >= kMaxBands) continue;
                bandKey[bands]   = key;
                bandSum[bands]   = 0.0f;
                bandCount[bands] = 0;
                ++bands;
            }
            bandSum[b]   += p.z;
            bandCount[b] += 1;
        }
    }
    if (bands == 0) return (candCount > 0) ? cand[0] : nearestZ;
    // Winner by supported count = own band + the two adjacent bands, so
    // a floor whose endpoints straddle a band boundary still outvotes a
    // sparse elevated band. The returned z is the mean over the same
    // supporting bands.
    int   bestSupport = -1;
    float bestSum     = 0.0f;
    int   bestCount   = 0;
    for (int b = 0; b < bands; ++b) {
        int   support = 0;
        float sum     = 0.0f;
        int   cnt     = 0;
        for (int o = 0; o < bands; ++o) {
            int d = bandKey[o] - bandKey[b];
            if (d < -1 || d > 1) continue;
            support += bandCount[o];
            sum     += bandSum[o];
            cnt     += bandCount[o];
        }
        if (support > bestSupport) {
            bestSupport = support;
            bestSum     = sum;
            bestCount   = cnt;
        }
    }
    float votedZ = bestSum / static_cast<float>(bestCount);
    // Stacked storeys: the vote only picks WHICH floor candidate wins;
    // the returned height is the exact triangle plane, not the vote's
    // wall-endpoint approximation. (The keep-caller rule already ran in
    // the candidate loop above.)
    if (candCount > 1) {
        int best = 0;
        for (int i = 1; i < candCount; ++i) {
            if (std::fabs(cand[i] - votedZ) < std::fabs(cand[best] - votedZ)) {
                best = i;
            }
        }
        return cand[best];
    }
    if (std::fabs(pos.z - votedZ) <= kZKeepCallerM) return pos.z;
    return votedZ;
}

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
    Vector origin = pos;
    origin.z = ResolveProbeZ(walls, wallCount, pos);
    return ProbeWall(walls, wallCount, origin, dx / mag, dy / mag);
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
    Vector origin = pos;
    origin.z = ResolveProbeZ(walls, wallCount, pos);
    float dF = ProbeWall(walls, wallCount, origin,  fx,  fy);
    float dB = ProbeWall(walls, wallCount, origin, -fx, -fy);
    float dR = ProbeWall(walls, wallCount, origin,  px,  py);
    float dL = ProbeWall(walls, wallCount, origin, -px, -py);
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
    Vector origin = pos;
    origin.z = ResolveProbeZ(walls, wallCount, pos);
    for (int k = 0; k < 8; ++k) {
        outRays[k] = ProbeWall(walls, wallCount, origin, kDirX[k], kDirY[k]);
    }
}

}  // namespace acc::wall_probe
