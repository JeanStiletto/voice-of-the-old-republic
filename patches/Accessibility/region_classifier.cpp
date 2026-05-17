// Shared shape cache + classifier — see region_classifier.h.
//
// Extracted 2026-05-13 from map_ui_cursor.cpp's BuildRoomShapeCache +
// ClassifyTerrainShape. Behaviour is byte-for-byte the same; the map
// cursor (and now view_mode + transitions) consume this cache instead
// of building their own.

#include "region_classifier.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "engine_navgraph.h"          // shared nav-graph snapshot
#include "log.h"
#include "spatial_change_detector.h"  // GetCachedWalls
#include "strings.h"

namespace acc::region {

namespace {

struct RoomShapeCache {
    void* area_owner = nullptr;
    bool  built      = false;
    int   room_count = 0;
    char   text[kMaxRooms][128]  = {};
    int    sig [kMaxRooms]       = {};
    bool   present[kMaxRooms]    = {};
    Vector rep[kMaxRooms]        = {};
};

RoomShapeCache g_cache;

int QuantiseMetres(float d) {
    if (d < 0.0f)  d = 0.0f;
    if (d > 25.0f) d = 25.0f;
    return static_cast<int>(d + 0.5f);
}

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

// 4-ray walkmesh classifier — see ClassifyTerrainShape in the original
// map_ui_cursor.cpp for the decision-tree rationale. Logic is preserved
// verbatim; the only mechanical change is that wall buffer / wall count
// come in as parameters rather than from a file-scope helper.
bool ClassifyTerrainShape(const acc::engine::WallEdge* walls, int wallCount,
                          const Vector& cursor,
                          char* outBuf, size_t bufSize, int& outSig) {
    if (!walls || wallCount <= 0 || !outBuf || bufSize < 8) return false;
    float dN = ProbeWall(walls, wallCount, cursor,  0.0f,  1.0f);
    float dE = ProbeWall(walls, wallCount, cursor,  1.0f,  0.0f);
    float dS = ProbeWall(walls, wallCount, cursor,  0.0f, -1.0f);
    float dW = ProbeWall(walls, wallCount, cursor, -1.0f,  0.0f);

    float axisNS = dN + dS;
    float axisEW = dE + dW;
    float minD = dN;
    if (dE < minD) minD = dE;
    if (dS < minD) minD = dS;
    if (dW < minD) minD = dW;
    float maxD = dN;
    if (dE > maxD) maxD = dE;
    if (dS > maxD) maxD = dS;
    if (dW > maxD) maxD = dW;

    using acc::strings::Id;

    auto pack = [](int kind, int a, int b) {
        return (kind & 0xff) | ((a & 0xff) << 8) | ((b & 0xff) << 16);
    };

    if (maxD <= 0.8f) {
        const char* s = acc::strings::Get(Id::MapCursorOffPath);
        if (s && s[0]) {
            std::snprintf(outBuf, bufSize, "%s", s);
            outSig = pack(1, 0, 0);
            return true;
        }
        return false;
    }

    if (axisNS >= 12.0f && axisEW >= 12.0f && minD >= 4.0f) {
        const char* s = acc::strings::Get(Id::MapCursorOpenArea);
        if (s && s[0]) {
            std::snprintf(outBuf, bufSize, "%s", s);
            outSig = pack(2, QuantiseMetres(axisNS), QuantiseMetres(axisEW));
            return true;
        }
        return false;
    }

    bool corridorNS = (axisNS >= 8.0f) && (axisEW > 0.0f) &&
                      (axisNS >= 2.2f * axisEW);
    bool corridorEW = (axisEW >= 8.0f) && (axisNS > 0.0f) &&
                      (axisEW >= 2.2f * axisNS);
    if (corridorNS || corridorEW) {
        const char* axisStr = acc::strings::Get(
            corridorNS ? Id::AxisNorthSouth : Id::AxisEastWest);
        float width = corridorNS ? axisEW : axisNS;
        const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridor);
        if (fmt && fmt[0] && axisStr && axisStr[0]) {
            std::snprintf(outBuf, bufSize, fmt, axisStr, width);
            outSig = pack(corridorNS ? 3 : 4, QuantiseMetres(width), 0);
            return true;
        }
        return false;
    }

    int shortCount = 0;
    int longIdx = -1;
    float longLen = 0.0f;
    float arr[4] = {dN, dE, dS, dW};
    for (int i = 0; i < 4; ++i) {
        if (arr[i] <= 2.0f) ++shortCount;
        if (arr[i] > longLen) { longLen = arr[i]; longIdx = i; }
    }
    if (shortCount == 3 && longIdx >= 0 && longLen > 2.0f) {
        Id dirIds[4] = { Id::DirNorth, Id::DirEast,
                         Id::DirSouth, Id::DirWest };
        const char* dir = acc::strings::Get(dirIds[longIdx]);
        const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
        if (fmt && fmt[0] && dir && dir[0]) {
            std::snprintf(outBuf, bufSize, fmt, dir);
            outSig = pack(5, longIdx, QuantiseMetres(longLen));
            return true;
        }
        return false;
    }

    constexpr float kOpeningThresholdM = 2.0f;
    Id dirIds[4] = { Id::DirNorth, Id::DirEast, Id::DirSouth, Id::DirWest };

    auto appendDir = [&](char* dirList, size_t bufSize, size_t& dirLen,
                         int& sigDirMask, int i) {
        const char* dir = acc::strings::Get(dirIds[i]);
        if (!dir || !dir[0]) return;
        if (dirLen > 0 && dirLen + 2 < bufSize) {
            dirList[dirLen++] = ',';
            dirList[dirLen++] = ' ';
            dirList[dirLen]   = '\0';
        }
        size_t remaining = (bufSize > dirLen) ? (bufSize - dirLen) : 0;
        if (remaining == 0) return;
        int n = std::snprintf(dirList + dirLen, remaining, "%s", dir);
        if (n > 0) {
            size_t advanced = (static_cast<size_t>(n) < remaining)
                                  ? static_cast<size_t>(n)
                                  : (remaining - 1);
            dirLen += advanced;
        }
        sigDirMask |= (1 << i);
    };

    char dirList[96] = {0};
    size_t dirLen = 0;
    int sigDirMask = 0;
    for (int i = 0; i < 4; ++i) {
        if (arr[i] < kOpeningThresholdM) continue;
        appendDir(dirList, sizeof(dirList), dirLen, sigDirMask, i);
    }

    if (sigDirMask == 0) {
        int order[4] = {0, 1, 2, 3};
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                if (arr[order[j]] > arr[order[i]]) {
                    int tmp = order[i]; order[i] = order[j]; order[j] = tmp;
                }
            }
        }
        int listed = 0;
        for (int k = 0; k < 4 && listed < 2; ++k) {
            if (arr[order[k]] < 0.5f) break;
            appendDir(dirList, sizeof(dirList), dirLen, sigDirMask,
                      order[k]);
            ++listed;
        }
    }

    if (sigDirMask != 0) {
        const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
        if (fmt && fmt[0]) {
            std::snprintf(outBuf, bufSize, fmt, dirList);
            outSig = pack(6, sigDirMask,
                          QuantiseMetres(axisNS + axisEW));
            return true;
        }
    }
    const char* s = acc::strings::Get(Id::MapCursorJunction);
    if (s && s[0]) {
        std::snprintf(outBuf, bufSize, "%s", s);
        outSig = pack(6,
                      QuantiseMetres(axisNS),
                      QuantiseMetres(axisEW));
        return true;
    }
    return false;
}

}  // namespace

void Reset() {
    g_cache.area_owner = nullptr;
    g_cache.built      = false;
    g_cache.room_count = 0;
    for (int i = 0; i < kMaxRooms; ++i) {
        g_cache.text[i][0] = '\0';
        g_cache.sig[i]     = 0;
        g_cache.present[i] = false;
        g_cache.rep[i]     = {0, 0, 0};
    }
}

bool HasCacheForArea(void* area) {
    return area != nullptr &&
           g_cache.built &&
           g_cache.area_owner == area;
}

namespace {

// SEH-isolated read of CSWSArea.room_count. Pulled out of
// BuildCacheForArea because that function now holds C++ objects with
// destructors (the navgraph snapshot's vectors), which MSVC refuses to
// mix with __try in the same scope.
int ReadRoomCount(void* area) {
    __try {
        return static_cast<int>(*reinterpret_cast<uint32_t*>(
            reinterpret_cast<unsigned char*>(area) +
            kAreaRoomCountOffset));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

}  // namespace

void BuildCacheForArea(void* area) {
    if (!area) return;
    if (HasCacheForArea(area)) return;  // idempotent on the same area

    Reset();
    g_cache.area_owner = area;

    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        acclog::Write("Region",
                      "BuildCacheForArea: wall cache not ready — leaving "
                      "cache empty (will retry on next call)");
        return;
    }

    int roomCount = ReadRoomCount(area);
    if (roomCount > kMaxRooms) {
        acclog::Write("Region",
                      "BuildCacheForArea: area reports %d rooms — "
                      "truncating to cache capacity %d",
                      roomCount, kMaxRooms);
        roomCount = kMaxRooms;
    }
    g_cache.room_count = roomCount;

    // -------------------------------------------------------------
    // Phase A — resolve every path point's containing room (used
    // both as a representative-point fallback for "void" rooms and
    // as the seed for the adjacency-edge build in Phase C).
    uint64_t adjacency[kMaxRooms] = {0};
    int      pointCountByRoom[kMaxRooms] = {0};
    Vector   sampleByRoom[kMaxRooms]     = {};
    bool     hasSample[kMaxRooms]        = {};

    acc::engine::navgraph::NavGraphSnapshot navGraph;
    acc::engine::navgraph::SnapshotNavGraph(area, navGraph);
    const uint32_t pathPointsCount      = static_cast<uint32_t>(navGraph.nodes.size());
    const uint32_t pathConnectionsCount = static_cast<uint32_t>(navGraph.conns.size());

    constexpr uint32_t kMaxPathPoints = 512;
    int      pointRoom[kMaxPathPoints];
    for (uint32_t i = 0; i < kMaxPathPoints; ++i) {
        pointRoom[i] = -1;
    }

    for (uint32_t i = 0; i < pathPointsCount && i < kMaxPathPoints; ++i) {
        Vector pos = navGraph.nodes[i].pos;
        int roomIdx = -1;
        acc::engine::GetRoomAtIndexed(area, pos, roomIdx);
        pointRoom[i] = roomIdx;
        if (roomIdx >= 0 && roomIdx < kMaxRooms) {
            ++pointCountByRoom[roomIdx];
            if (!hasSample[roomIdx]) {
                sampleByRoom[roomIdx] = pos;
                hasSample[roomIdx]    = true;
            }
        }
    }

    // -------------------------------------------------------------
    // Phase B — classify every room.
    int populated = 0;
    int recovered = 0;
    for (int r = 0; r < roomCount; ++r) {
        Vector rep;
        int failReason = 0;
        bool gotRep = acc::engine::GetRoomRepresentativeWorld(
            area, r, rep, &failReason);
        const char* repSource = "walkmesh";

        if (!gotRep) {
            if (hasSample[r]) {
                rep = sampleByRoom[r];
                gotRep = true;
                repSource = "pathpoint";
                ++recovered;
                acclog::Write("Region",
                              "room %d walkmesh-rep failed (reason=%d) — "
                              "using path-point rep=(%.2f,%.2f,%.2f)",
                              r, failReason, rep.x, rep.y, rep.z);
            } else {
                acclog::Write("Region",
                              "room %d walkmesh-rep failed (reason=%d), "
                              "no path-point in this room — skipping",
                              r, failReason);
                continue;
            }
        }

        char text[128] = {0};
        int  sig = 0;
        if (ClassifyTerrainShape(walls, wallCount, rep,
                                 text, sizeof(text), sig)) {
            // kind=1 ("Wand" / off-path) means all four probe rays hit a
            // wall within 0.8m of the representative point — i.e. the
            // sample landed inside a wall, not that the .lyt-room is
            // genuinely unwalkable. K1 ships rooms whose walkmesh-
            // derived centroid falls outside the walkable surface
            // (thin transition strips, L-shaped rooms with centroid in
            // the void). Treat these as unclassified so downstream
            // consumers — transitions / view-mode / map-cursor snap —
            // skip them rather than speaking a misleading "Wand". They
            // still keep `rep` populated (used by map-cursor's nearest-
            // room snap) but `present=false` makes them invisible to
            // direct-index lookups + filters them out of the dedup
            // walk. Patch-20260512-223500 had room 1 fire "Wand" 11×
            // in 4 minutes from a tight corridor crossing — pure
            // probe-failure noise.
            int kind = sig & 0xff;
            if (kind == 1) {
                g_cache.rep[r]     = rep;
                g_cache.present[r] = false;
                acclog::Write("Region",
                              "room %d shape probe kind=1 (Wand) at "
                              "rep=(%.2f,%.2f,%.2f) — marking unclassified "
                              "(probe-failure suppression)",
                              r, rep.x, rep.y, rep.z);
                continue;
            }
            std::snprintf(g_cache.text[r], sizeof(g_cache.text[r]),
                          "%s", text);
            g_cache.sig[r]     = sig;
            g_cache.present[r] = true;
            g_cache.rep[r]     = rep;
            ++populated;
            acclog::Write("Region",
                          "room %d shape=\"%s\" sig=%d rep=(%.2f,%.2f,%.2f) "
                          "source=%s",
                          r, text, sig, rep.x, rep.y, rep.z, repSource);
        }
    }

    // -------------------------------------------------------------
    // Phase C — build adjacency edges from path connections.
    int adjEdges = 0;
    if (pathPointsCount > 0 && pathConnectionsCount > 0) {
        for (uint32_t i = 0; i < pathPointsCount && i < kMaxPathPoints; ++i) {
            int roomA = pointRoom[i];
            if (roomA < 0 || roomA >= kMaxRooms) continue;
            int lo = 0, hi = 0;
            acc::engine::navgraph::NeighbourRange(
                navGraph, static_cast<int>(i), lo, hi);
            for (int k = lo; k < hi; ++k) {
                uint32_t j = navGraph.conns[k];
                if (j >= pathPointsCount || j >= kMaxPathPoints) continue;
                int roomB = pointRoom[j];
                if (roomB < 0 || roomB >= kMaxRooms) continue;
                if (roomA == roomB) continue;
                if (!(adjacency[roomA] & (1ULL << roomB))) {
                    adjacency[roomA] |= (1ULL << roomB);
                    ++adjEdges;
                }
            }
        }
    }

    acclog::Write("Region",
                  "Adjacency: pathPoints=%u pathConns=%u edges=%d "
                  "recovered=%d",
                  pathPointsCount, pathConnectionsCount, adjEdges,
                  recovered);

    // Union policy: walkmesh probe is source of truth for "is there an
    // opening this direction?"; path graph is source of truth for "which
    // room does that opening lead to". Adjacency may ADD directions but
    // never REMOVE them. See the original map_ui_cursor.cpp BuildRoom-
    // ShapeCache comment for the room-5-regression rationale.
    using acc::strings::Id;
    Id cardinalIds[4] = {
        Id::DirNorth, Id::DirEast, Id::DirSouth, Id::DirWest
    };

    int overrides = 0;
    for (int r = 0; r < roomCount; ++r) {
        if (!g_cache.present[r]) continue;
        int kind = g_cache.sig[r] & 0xff;
        if (kind != 5 && kind != 6) continue;

        int wmMask = 0;
        int sigByte1 = (g_cache.sig[r] >> 8) & 0xff;
        if (kind == 5) {
            if (sigByte1 >= 0 && sigByte1 < 4) {
                wmMask = (1 << sigByte1);
            }
        } else {
            wmMask = sigByte1 & 0x0f;
        }

        int adjMask = 0;
        int adjCount = 0;
        Vector myRep = g_cache.rep[r];
        for (int b = 0; b < kMaxRooms; ++b) {
            if (!(adjacency[r] & (1ULL << b))) continue;
            if (b >= roomCount) continue;
            if (!g_cache.present[b]) continue;
            ++adjCount;
            float dx = g_cache.rep[b].x - myRep.x;
            float dy = g_cache.rep[b].y - myRep.y;
            int dirIdx;
            if (std::fabs(dx) > std::fabs(dy)) {
                dirIdx = (dx > 0.0f) ? 1 : 3;
            } else {
                dirIdx = (dy > 0.0f) ? 0 : 2;
            }
            adjMask |= (1 << dirIdx);
        }

        int addedMask = adjMask & ~wmMask;
        int unionMask = wmMask | adjMask;

        const char* policy =
            (addedMask != 0) ? "adj-augment" :
            (adjCount == 0)  ? "walkmesh-only-no-adj" :
                               "walkmesh-only-adj-subset";
        acclog::Write("Region",
                      "room %d kind=%d wmMask=0x%X adjMask=0x%X "
                      "addedMask=0x%X adjCount=%d points=%d policy=%s",
                      r, kind, wmMask, adjMask, addedMask, adjCount,
                      pointCountByRoom[r], policy);

        if (addedMask == 0) continue;

        char dirList[96] = {0};
        size_t dirLen = 0;
        for (int d = 0; d < 4; ++d) {
            if (!(unionMask & (1 << d))) continue;
            const char* name = acc::strings::Get(cardinalIds[d]);
            if (!name || !name[0]) continue;
            if (dirLen > 0 && dirLen + 2 < sizeof(dirList)) {
                dirList[dirLen++] = ',';
                dirList[dirLen++] = ' ';
                dirList[dirLen]   = '\0';
            }
            size_t remaining = sizeof(dirList) - dirLen;
            int n = std::snprintf(dirList + dirLen, remaining, "%s", name);
            if (n > 0) {
                size_t advanced = (static_cast<size_t>(n) < remaining)
                                      ? static_cast<size_t>(n)
                                      : (remaining > 0 ? remaining - 1 : 0);
                dirLen += advanced;
            }
        }

        int popcount = 0;
        for (int d = 0; d < 4; ++d) {
            if (unionMask & (1 << d)) ++popcount;
        }

        if (popcount == 1) {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0]) {
                std::snprintf(g_cache.text[r], sizeof(g_cache.text[r]),
                              fmt, dirList);
                g_cache.sig[r] = (5) | ((unionMask & 0xff) << 8);
            }
        } else {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorJunctionDirs);
            if (fmt && fmt[0]) {
                std::snprintf(g_cache.text[r], sizeof(g_cache.text[r]),
                              fmt, dirList);
                g_cache.sig[r] = (6) | ((unionMask & 0xff) << 8) |
                                       ((adjCount & 0xff) << 16);
            }
        }
        ++overrides;
        acclog::Write("Region",
                      "room %d union-override → \"%s\" "
                      "(wmMask=0x%X adjMask=0x%X unionMask=0x%X)",
                      r, g_cache.text[r],
                      wmMask, adjMask, unionMask);
    }
    acclog::Write("Region",
                  "Union-augmentation applied to %d rooms", overrides);

    g_cache.built = true;
    acclog::Write("Region",
                  "BuildCacheForArea: built area=%p rooms=%d populated=%d",
                  area, roomCount, populated);
}

bool LookupRoomShape(void* area, int roomIdx,
                     char* outBuf, size_t bufSize, int& outSig) {
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    outSig = 0;
    if (!HasCacheForArea(area)) return false;
    if (roomIdx < 0 || roomIdx >= kMaxRooms) return false;
    if (!g_cache.present[roomIdx]) return false;
    std::snprintf(outBuf, bufSize, "%s", g_cache.text[roomIdx]);
    outSig = g_cache.sig[roomIdx];
    return true;
}

ShapeKind ProbeShapeAt(const Vector& pos) {
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        return ShapeKind::Unknown;
    }
    char buf[128];
    int sig = 0;
    if (!ClassifyTerrainShape(walls, wallCount, pos, buf, sizeof(buf), sig)) {
        return ShapeKind::Unknown;
    }
    return static_cast<ShapeKind>(sig & 0xff);
}

bool IsAlcoveAlongAxis(const Vector& pos, float forwardX, float forwardY) {
    const acc::engine::WallEdge* walls = nullptr;
    int wallCount = 0;
    if (!acc::spatial::change_detector::GetCachedWalls(walls, wallCount) ||
        !walls || wallCount <= 0) {
        return true;  // no data — fail open (caller's other signal wins)
    }
    float magSq = forwardX * forwardX + forwardY * forwardY;
    if (magSq < 1e-6f) return true;
    float inv = 1.0f / std::sqrt(magSq);
    float fx  = forwardX * inv;
    float fy  = forwardY * inv;
    // Perpendiculars: 90° CW = (fy, -fx); 90° CCW = (-fy, fx).
    float px = fy;
    float py = -fx;

    float dF = ProbeWall(walls, wallCount, pos,  fx,  fy);
    float dB = ProbeWall(walls, wallCount, pos, -fx, -fy);
    float dR = ProbeWall(walls, wallCount, pos,  px,  py);
    float dL = ProbeWall(walls, wallCount, pos, -px, -py);

    // Same threshold tuple `ClassifyTerrainShape` uses for the cardinal
    // dead-end check, just spun to align with the supplied forward axis:
    //   forward > 2.0m  AND  back/left/right all ≤ 2.0m
    int shortCount = 0;
    if (dB <= 2.0f) ++shortCount;
    if (dR <= 2.0f) ++shortCount;
    if (dL <= 2.0f) ++shortCount;
    return shortCount == 3 && dF > 2.0f;
}

bool LookupShapeAt(void* area, const Vector& world,
                   char* outBuf, size_t bufSize, int& outSig,
                   int* outRoomIdx) {
    if (outRoomIdx) *outRoomIdx = -1;
    if (!outBuf || bufSize == 0) return false;
    outBuf[0] = '\0';
    outSig = 0;
    if (!HasCacheForArea(area)) return false;

    int roomIdx = -1;
    acc::engine::GetRoomAtIndexed(area, world, roomIdx);
    if (outRoomIdx) *outRoomIdx = roomIdx;

    if (roomIdx >= 0 && roomIdx < kMaxRooms && g_cache.present[roomIdx]) {
        std::snprintf(outBuf, bufSize, "%s", g_cache.text[roomIdx]);
        outSig = g_cache.sig[roomIdx];
        return true;
    }

    // Cache miss — snap to the nearest cached room's representative
    // point. Same stability story as the map cursor's pre-extraction
    // behaviour: a position on a portal seam between rooms (or in a
    // room whose surface_mesh was null at build time) resolves to a
    // stable label rather than re-classifying on every probe.
    float bestDist2 = 1e30f;
    int bestRoom = -1;
    for (int r = 0; r < g_cache.room_count; ++r) {
        if (!g_cache.present[r]) continue;
        float dx = g_cache.rep[r].x - world.x;
        float dy = g_cache.rep[r].y - world.y;
        float d2 = dx * dx + dy * dy;
        if (d2 < bestDist2) {
            bestDist2 = d2;
            bestRoom = r;
        }
    }
    if (bestRoom < 0) return false;
    std::snprintf(outBuf, bufSize, "%s", g_cache.text[bestRoom]);
    outSig = g_cache.sig[bestRoom];
    return true;
}

}  // namespace acc::region
