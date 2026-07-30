// walkmesh wall-edge extraction.
//
// Split out of engine_area.cpp by the Phase-1 structure pass (refactoring
// candidate 3). This is the Pillar-1 geometry foundation: it reads room
// collision meshes, transforms face edges to world space, and builds the
// per-area wall cache that room_topology.cpp consumes. Nothing here
// touches the object model or the map surface - it is pure geometry over
// CSWSArea's room list.
//
// Declarations stay in engine_area.h - splitting that header is a separate,
// deferred item, so every existing includer is unaffected.

#include "engine_area.h"

#include <windows.h>
#include <cstdint>

#include "log.h"
#include "engine_rebase.h"

namespace acc::engine {

namespace {

typedef void (__thiscall* PFN_CollisionMeshLocalToWorld)(void* this_,
                                                         Vector* output,
                                                         Vector* localPoint);

// Min XY-length² (~5cm) to skip near-vertical edges that lack a meaningful
// 2D footprint. K1 walkmeshes contain near-vertical step-side edges with
// sub-cm horizontal drift; those break downstream clustering and aren't
// navigable walls in 2D. Matches Pillar 1's kEndpointTolMeters.
constexpr float kMinEdgeXYLengthSq = 2.5e-3f;

// Read a CSWSRoom's surface_mesh pointer. Returns nullptr if the room slot
// itself is null/garbage or surface_mesh hasn't been populated. SEH-guarded.
void* GetRoomSurfaceMesh(void* room) {
    if (!room) return nullptr;
    __try {
        return *reinterpret_cast<void**>(
            reinterpret_cast<unsigned char*>(room) + kRoomSurfaceMeshOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Read the three uint32 vertex indices for face `f` from a contiguous
// face-index array. SEH-guarded — returns false on bad pointer. Output
// is left untouched on fault.
bool ReadFaceVertexIndices(unsigned char* faces, uint32_t f, uint32_t outV[3]) {
    __try {
        auto* face = reinterpret_cast<uint32_t*>(
            faces + static_cast<size_t>(f) * kWalkmeshFaceStride);
        outV[0] = face[0];
        outV[1] = face[1];
        outV[2] = face[2];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Transform a pair of local vertices through the room's
// CCollisionMesh::LocalToWorld. SEH-guarded; on fault falls back to the
// local copies (correct when world_coords=1, the common runtime case
// for room walkmeshes).
void TransformEdgeEndpoints(void* surfaceMesh,
                            PFN_CollisionMeshLocalToWorld fn,
                            const Vector& localA, const Vector& localB,
                            Vector& outWorldA, Vector& outWorldB) {
    Vector la = localA;
    Vector lb = localB;
    outWorldA = la;
    outWorldB = lb;
    __try {
        fn(surfaceMesh, &outWorldA, &la);
        fn(surfaceMesh, &outWorldB, &lb);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outWorldA = la;
        outWorldB = lb;
    }
}

// Walk every triangle of one room's surface mesh, emit a WallEdge for each
// adjacency==-1 side. Returns the number of edges this room contributed (to
// the running total — written to outBuf only while there's space).
int ScanRoomWallEdges(void* surfaceMesh, int roomId,
                      WallEdge* outBuf, int maxEdges, int alreadyWritten) {
    if (!surfaceMesh) return 0;
    auto* mesh = reinterpret_cast<unsigned char*>(surfaceMesh);

    Vector*   vertices     = nullptr;
    uint32_t  faceCount    = 0;
    void*     faceIndices  = nullptr;
    uint32_t* materials    = nullptr;
    int*      adjacencies  = nullptr;   // flat int[faceCount*3]

    __try {
        vertices = *reinterpret_cast<Vector**>(
            mesh + kCollisionMeshVerticesOffset);
        faceCount = *reinterpret_cast<uint32_t*>(
            mesh + kCollisionMeshFaceCountOffset);
        faceIndices = *reinterpret_cast<void**>(
            mesh + kCollisionMeshFacesOffset);
        materials = *reinterpret_cast<uint32_t**>(
            mesh + kCollisionMeshMaterialsOffset);
        // SurfaceMeshAdjacency lives on the wrapping CSWRoomSurfaceMesh,
        // not on the embedded CSWCollisionMesh — offset is +0x88 from the
        // surface_mesh base (which IS the wrapper). Each entry is
        // `int indices[3]` (12 bytes); we treat the whole thing as a flat
        // int array indexed via f*3 + e to avoid a local struct typedef.
        adjacencies = *reinterpret_cast<int**>(
            mesh + kSurfaceMeshAdjacenciesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }

    if (!vertices || !faceIndices || !adjacencies || faceCount == 0) {
        return 0;
    }

    auto fnLocalToWorld = reinterpret_cast<PFN_CollisionMeshLocalToWorld>(
        kAddrCollisionMeshLocalToWorld);

    int emitted = 0;
    auto* faces = reinterpret_cast<unsigned char*>(faceIndices);

    for (uint32_t f = 0; f < faceCount; ++f) {
        uint32_t v[3] = {0, 0, 0};
        if (!ReadFaceVertexIndices(faces, f, v)) continue;
        int adj[3] = {0, 0, 0};
        __try {
            adj[0] = adjacencies[f * 3 + 0];
            adj[1] = adjacencies[f * 3 + 1];
            adj[2] = adjacencies[f * 3 + 2];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            continue;
        }

        // surfacemat.2da row for this face — captured once per face (all
        // three potential edges share the same material).
        int materialId = -1;
        __try {
            materialId = static_cast<int>(materials[f]);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            materialId = -1;
        }

        for (int e = 0; e < 3; ++e) {
            if (adj[e] != -1) continue;  // interior edge — has a neighbour
            uint32_t va = v[e];
            uint32_t vb = v[(e + 1) % 3];

            Vector localA = {0, 0, 0}, localB = {0, 0, 0};
            __try {
                localA = vertices[va];
                localB = vertices[vb];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }

            Vector worldA, worldB;
            TransformEdgeEndpoints(surfaceMesh, fnLocalToWorld,
                                   localA, localB, worldA, worldB);

            float xy_dx = worldB.x - worldA.x;
            float xy_dy = worldB.y - worldA.y;
            if (xy_dx * xy_dx + xy_dy * xy_dy < kMinEdgeXYLengthSq) {
                continue;
            }

            int slot = alreadyWritten + emitted;
            if (outBuf && slot < maxEdges) {
                outBuf[slot].a           = worldA;
                outBuf[slot].b           = worldB;
                outBuf[slot].room_id     = roomId;
                outBuf[slot].material_id = materialId;
            }
            ++emitted;
        }
    }
    return emitted;
}

}  // namespace

// Global triangle-edge index used by the portal-coincidence filter in
// BuildAreaWallCache. Every triangle edge from every room is recorded
// here regardless of adjacency value; the filter then asks, for each
// emitted adjacency=-1 wall edge, "does any *other* room have a
// coincident edge in its triangle list?" — if yes, the walkmesh
// extends across into that other room, so the edge is a portal and
// gets dropped.
//
// Sized at 16384 for headroom over K1's largest observed area
// (~6000 triangle edges in dense Taris). 16384 × 28 B = ~460 KB static
// — comfortable for our patch's memory budget. Module-static + the
// single-threaded patch model means no reentrancy concerns; each
// BuildAreaWallCache invocation overwrites the contents.
constexpr int kMaxGlobalTriEdges = 16384;
static Vector s_globalEdgeA[kMaxGlobalTriEdges];
static Vector s_globalEdgeB[kMaxGlobalTriEdges];
static int    s_globalEdgeRoom[kMaxGlobalTriEdges];

// Mirror of ScanRoomWallEdges but emits every triangle edge regardless
// of adjacency value. The 5cm² XY-length filter is retained — vertical
// / near-vertical edges aren't useful as 2D portal matches and bloat
// the index. Returns the count of edges this room contributed to the
// running total (written to the global arrays only while there's space).
static int ScanRoomAllTriangleEdges(void* surfaceMesh, int roomId,
                             int alreadyWritten) {
    if (!surfaceMesh) return 0;
    auto* mesh = reinterpret_cast<unsigned char*>(surfaceMesh);

    Vector*   vertices    = nullptr;
    uint32_t  faceCount   = 0;
    void*     faceIndices = nullptr;
    __try {
        vertices    = *reinterpret_cast<Vector**>(
            mesh + kCollisionMeshVerticesOffset);
        faceCount   = *reinterpret_cast<uint32_t*>(
            mesh + kCollisionMeshFaceCountOffset);
        faceIndices = *reinterpret_cast<void**>(
            mesh + kCollisionMeshFacesOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!vertices || !faceIndices || faceCount == 0) return 0;

    auto fnLocalToWorld = reinterpret_cast<PFN_CollisionMeshLocalToWorld>(
        kAddrCollisionMeshLocalToWorld);
    auto* faces = reinterpret_cast<unsigned char*>(faceIndices);

    int emitted = 0;
    for (uint32_t f = 0; f < faceCount; ++f) {
        uint32_t v[3] = {0, 0, 0};
        if (!ReadFaceVertexIndices(faces, f, v)) continue;

        for (int e = 0; e < 3; ++e) {
            uint32_t va = v[e];
            uint32_t vb = v[(e + 1) % 3];
            Vector localA = {0, 0, 0}, localB = {0, 0, 0};
            __try {
                localA = vertices[va];
                localB = vertices[vb];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
            Vector worldA, worldB;
            TransformEdgeEndpoints(surfaceMesh, fnLocalToWorld,
                                   localA, localB, worldA, worldB);
            float xy_dx = worldB.x - worldA.x;
            float xy_dy = worldB.y - worldA.y;
            if (xy_dx * xy_dx + xy_dy * xy_dy < kMinEdgeXYLengthSq) continue;

            int slot = alreadyWritten + emitted;
            if (slot < kMaxGlobalTriEdges) {
                s_globalEdgeA[slot]    = worldA;
                s_globalEdgeB[slot]    = worldB;
                s_globalEdgeRoom[slot] = roomId;
            }
            ++emitted;
        }
    }
    return emitted;
}

int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges) {
    if (!area) return 0;

    void*    rooms     = nullptr;
    uint32_t roomCount = 0;
    __try {
        auto* base = reinterpret_cast<unsigned char*>(area);
        rooms     = *reinterpret_cast<void**>(base + kAreaRoomsOffset);
        roomCount = *reinterpret_cast<uint32_t*>(base + kAreaRoomCountOffset);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    if (!rooms || roomCount == 0) return 0;

    auto* roomBase = reinterpret_cast<unsigned char*>(rooms);
    int total = 0;
    for (uint32_t i = 0; i < roomCount; ++i) {
        void* room = roomBase + static_cast<size_t>(i) * kRoomStride;
        void* sm   = GetRoomSurfaceMesh(room);
        if (!sm) continue;
        int contributed = ScanRoomWallEdges(sm, static_cast<int>(i),
                                            outBuf, maxEdges, total);
        total += contributed;
    }

    // Count-only probe — no buffer to filter. Return pre-filter total so
    // callers can detect "would have overflowed the buffer" telemetry.
    if (!outBuf || maxEdges <= 0) return total;

    // Portal filtering — the "edges of walkable areas" rule.
    //
    // KOTOR's walkmesh joins rooms via portals, not per-triangle
    // adjacency. When room A and room B share a walkable boundary,
    // room A's triangle on its side marks the boundary edge with
    // adjacency=-1 (no neighbour within A). In *some* cases room B
    // does the same → the old symmetric "both sides adj=-1" seam pair
    // caught it. But K1 also has plenty of asymmetric portals where
    // only one side has adj=-1; the other side has a finite-adjacency
    // value pointing at room B's own neighbour. The old filter missed
    // these — they survived as phantom walls.
    //
    // The right rule: a wall exists only where *no* triangle (in any
    // room) sits on the other side of an adj=-1 edge. To check that,
    // we look for a coincident edge in any *other* room's triangle
    // list, regardless of that edge's own adjacency value. Hit → the
    // walkmesh extends across into another room → portal → drop. No
    // hit → boundary of the walkable union → real wall → keep.
    //
    // This is a strict superset of the old symmetric pair filter
    // (matches between adj=-1 edges still match, plus new asymmetric
    // matches). Replaces the old filter outright.
    //
    // Cost: O(N · G) where N = adj=-1 walls in outBuf (~500-1000) and
    // G = total triangle edges across all rooms (~3000-6000). 5M cheap
    // float compares per area-load; once per area-enter.
    int written = (total < maxEdges) ? total : maxEdges;

    // Build the global triangle-edge index — every triangle edge from
    // every room, regardless of adjacency value.
    int globalCount = 0;
    for (uint32_t i = 0; i < roomCount; ++i) {
        void* room = roomBase + static_cast<size_t>(i) * kRoomStride;
        void* sm   = GetRoomSurfaceMesh(room);
        if (!sm) continue;
        int contributed = ScanRoomAllTriangleEdges(sm, static_cast<int>(i),
                                                   globalCount);
        globalCount += contributed;
    }
    bool globalOverflowed = false;
    if (globalCount > kMaxGlobalTriEdges) {
        acclog::Write("AreaWalls",
            "global edge index OVERFLOW: discovered=%d kMaxGlobalTriEdges=%d "
            "— portal filter may miss matches against the tail (raise the cap)",
            globalCount, kMaxGlobalTriEdges);
        globalCount = kMaxGlobalTriEdges;
        globalOverflowed = true;
    }

    // Endpoint match tolerance. LocalToWorld involves matrix math, so
    // a pair of "identical" edges from two different rooms may not be
    // bit-equal — allow ~1cm of slack per coordinate. Squared so we
    // can compare against squared distance and avoid a sqrt.
    constexpr float kSeamEpsSq = 1e-4f;  // ~1cm² in world units
    auto coincident = [&](const Vector& p, const Vector& q) {
        float dx = p.x - q.x, dy = p.y - q.y, dz = p.z - q.z;
        return (dx * dx + dy * dy + dz * dz) < kSeamEpsSq;
    };

    // Per-wall portal flag. Static so we don't burden the stack at the
    // BuildAreaWallCache scope; single-threaded patch ⇒ no reentrancy.
    constexpr int kMaxPortalFlags = 8192;
    static bool s_isPortal[kMaxPortalFlags];
    int flagN = (written < kMaxPortalFlags) ? written : kMaxPortalFlags;
    for (int i = 0; i < flagN; ++i) s_isPortal[i] = false;

    int portalMatches = 0;
    for (int i = 0; i < flagN; ++i) {
        const WallEdge& e = outBuf[i];
        for (int g = 0; g < globalCount; ++g) {
            if (s_globalEdgeRoom[g] == e.room_id) continue;
            bool match =
                (coincident(e.a, s_globalEdgeA[g]) &&
                 coincident(e.b, s_globalEdgeB[g])) ||
                (coincident(e.a, s_globalEdgeB[g]) &&
                 coincident(e.b, s_globalEdgeA[g]));
            if (match) {
                s_isPortal[i] = true;
                ++portalMatches;
                break;  // one match suffices to classify as portal
            }
        }
    }

    int kept = 0;
    for (int i = 0; i < flagN; ++i) {
        if (s_isPortal[i]) continue;
        if (kept != i) outBuf[kept] = outBuf[i];
        ++kept;
    }
    // Tail beyond kMaxPortalFlags (only possible if maxEdges >
    // kMaxPortalFlags AND the area has that many edges) — unfilterable,
    // append unchanged. Pathological; we log if it ever happens.
    if (written > flagN) {
        int tail = written - flagN;
        for (int i = 0; i < tail; ++i) {
            outBuf[kept + i] = outBuf[flagN + i];
        }
        kept += tail;
        acclog::Write("AreaWalls",
            "portal filter exceeded flag buffer (written=%d flagN=%d) — "
            "%d trailing edges unfiltered",
            written, flagN, tail);
    }

    acclog::Write("AreaWalls",
        "portal filter: emitted=%d -> kept=%d (dropped %d as portals via "
        "coincidence against %d global edges%s)",
        written, kept, written - kept, globalCount,
        globalOverflowed ? " [OVERFLOWED]" : "");

    // Same-room duplicate dedup. K1's walkmesh has two recurring
    // patterns where the per-room scan above emits a wall edge twice
    // from the same room:
    //
    //   1. Exact 3D duplicate. Two faces in one room both have
    //      adjacency=-1 on the same physical edge with different
    //      `materials[]` entries (non-manifold authoring). Both edges
    //      have the same 3D endpoints (possibly reversed direction).
    //
    //   2. Step / slanted-face pair. The bottom edge of a step is
    //      stored as a flat wall at Z=0, AND the slanted face of the
    //      step is stored as a separate wall going from the step's
    //      top (Z=2.25 typical) down to the same Z=0 foot. The two
    //      edges share one 3D endpoint exactly (the foot where flat
    //      meets slanted) and the other endpoint matches in XY but
    //      not Z. Same 2D footprint either way.
    //
    // We dedup both: drop the second copy per matched pair, keep the
    // first. material_id isn't read by any production path (logging
    // only), so the surviving copy's material is irrelevant.
    //
    // The match rule is:
    //   - 2D XY footprints match (either direction), AND
    //   - at least one endpoint pair matches in full 3D within
    //     `kSeamEpsSq` tolerance.
    //
    // The 3D-shared-endpoint requirement distinguishes step/slope
    // pairs (always share the foot at Z=0) from genuine multi-floor
    // geometry — lower-corridor wall vs upper-corridor wall at the
    // same XY but with both endpoints at different Z. Multi-floor
    // pairs have no 3D-shared endpoint and survive this pass, which
    // matters when an area has decks / balconies / overhead walkways
    // (Endar Spire, Manaan habitat domes, etc.).
    //
    // The cross-room seam pass above explicitly skips same-room pairs
    // because dropping BOTH copies (the cross-room rule for portal
    // seams) would lose the wall entirely. Here we drop just ONE.
    //
    // Diagnostic motivation: per-edge anomaly dumps from patch-
    // 20260513-080349 + -081234 showed the two patterns above. After
    // this pass, Pillar 1's clustering produces single straight-
    // segment surfaces instead of 2-edge "lens" anomalies, and
    // walltopo gets clean inputs.
    //
    // O(N²) over `kept`. Same cost class as the cross-room pass; runs
    // once per area-load.
    auto xyCoincident = [&](const Vector& p, const Vector& q) {
        float dx = p.x - q.x, dy = p.y - q.y;
        return (dx * dx + dy * dy) < kSeamEpsSq;
    };
    int sameRoomDups = 0;
    int i = 0;
    while (i < kept) {
        int j = i + 1;
        while (j < kept) {
            if (outBuf[i].room_id != outBuf[j].room_id) {
                ++j;
                continue;
            }
            // 2D footprint match in either direction.
            bool xyMatch =
                (xyCoincident(outBuf[i].a, outBuf[j].a) &&
                 xyCoincident(outBuf[i].b, outBuf[j].b)) ||
                (xyCoincident(outBuf[i].a, outBuf[j].b) &&
                 xyCoincident(outBuf[i].b, outBuf[j].a));
            // At least one endpoint pair shared exactly in 3D —
            // distinguishes step/slope pairs (always share the foot)
            // from multi-floor walls (no 3D endpoint in common).
            bool sharesEndpoint3D =
                coincident(outBuf[i].a, outBuf[j].a) ||
                coincident(outBuf[i].a, outBuf[j].b) ||
                coincident(outBuf[i].b, outBuf[j].a) ||
                coincident(outBuf[i].b, outBuf[j].b);
            if (xyMatch && sharesEndpoint3D) {
                outBuf[j] = outBuf[--kept];   // swap-remove
                ++sameRoomDups;
            } else {
                ++j;
            }
        }
        ++i;
    }
    if (sameRoomDups > 0) {
        acclog::Write("AreaWalls",
            "same-room dedup: dropped %d duplicate wall edges "
            "(non-manifold faces / step+slanted-face pairs; multi-floor "
            "walls preserved)",
            sameRoomDups);
    }

    return kept;
}


bool SegmentCrossesWalkmesh(const WallEdge* walls,
                            int wallCount,
                            const Vector& a,
                            const Vector& b,
                            Vector& outHitPoint,
                            bool ignoreZ) {
    if (!walls || wallCount <= 0) return false;

    // Movement direction in 2D. abx/aby form the player segment; if both
    // are ~0 the cursor isn't actually moving and there's nothing to
    // test (avoids the divide-by-zero in the parametric formula below).
    float abx = b.x - a.x;
    float aby = b.y - a.y;
    if (abx * abx + aby * aby < 1e-10f) return false;

    bool   anyHit       = false;
    float  bestT        = 1e30f;
    Vector bestHit      = a;

    for (int i = 0; i < wallCount; ++i) {
        const WallEdge& w = walls[i];
        float cdx = w.b.x - w.a.x;
        float cdy = w.b.y - w.a.y;

        // Standard 2D segment-segment intersection in the XY plane.
        // Solve for parametric t (along a→b) and u (along w.a→w.b):
        //   a + t * (b - a) == w.a + u * (w.b - w.a)
        // → denom = abx*cdy - aby*cdx (≈ 0 means the segments are
        //   parallel; treat as no hit — sliding-along is fine for our
        //   cursor scale).
        float denom = abx * cdy - aby * cdx;
        if (denom > -1e-8f && denom < 1e-8f) continue;

        float dx = w.a.x - a.x;
        float dy = w.a.y - a.y;
        float t  = (dx * cdy - dy * cdx) / denom;
        float u  = (dx * aby - dy * abx) / denom;

        if (t < 0.0f || t > 1.0f) continue;
        if (u < 0.0f || u > 1.0f) continue;

        // 3D guard: the XY test alone treats a wall on another floor as a
        // blocker whenever its 2D projection lands on a→b. Wall edges are
        // walkmesh floor-boundary edges, so their z tracks the floor they
        // bound; reject the hit when the wall edge sits more than a
        // same-floor tolerance from the ray at the crossing point. This is
        // the entire remaining over-block population (validated via the
        // nav-graph crosscheck) — every runtime false-block occurred at
        // elevated z against a ground-floor wall.
        //
        // Skipped when ignoreZ: callers with untrustworthy endpoint z (the
        // waypoint smoother feeds 2D nav nodes) must run pure 2D, or the
        // guard silently drops real same-floor walls when the ray's z is
        // bogus and routes the path straight through them.
        if (!ignoreZ) {
            float rayZ  = a.z   + t * (b.z   - a.z);
            float wallZ = w.a.z + u * (w.b.z - w.a.z);
            float dz = rayZ - wallZ;
            if (dz < 0.0f) dz = -dz;
            if (dz > kWallCrossZToleranceM) continue;
        }

        if (t < bestT) {
            bestT      = t;
            bestHit.x  = a.x + t * abx;
            bestHit.y  = a.y + t * aby;
            bestHit.z  = a.z + t * (b.z - a.z);
            anyHit     = true;
        }
    }

    if (anyHit) outHitPoint = bestHit;
    return anyHit;
}

}  // namespace acc::engine
