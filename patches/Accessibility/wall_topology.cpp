// Wall-topology decomposition — see wall_topology.h for goal + algorithm.
//
// EXPERIMENTAL — alternative-direction-calculation-system branch.
//
// Phase 1 (segment input) consumes Pillar 1's already-clustered wall
// surfaces from `acc::spatial::change_detector` instead of re-running
// its own merge pass over the raw edge list.
//
// Phase 2 (this revision, 2026-05-13) replaces the parallel-pair
// corridor sweep with walkmesh-face cell decomposition. The previous
// "two walls forming a 1.5-6m gap" model fell back to "Offene Fläche"
// in 100% of the WallTopo.Compare logs because real K1 corridors are
// wider than 6m and the player walks through the open middle, not
// between the wall fragments at the perimeter. The face-graph model
// uses the same triangulated walkmesh the engine itself uses for
// movement — connected walkable faces form perceptual cells whose
// shape (long+narrow vs small+square vs large+square) is the
// classification signal.

#include "wall_topology.h"

#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "engine_area.h"
#include "log.h"
#include "spatial_change_detector.h"
#include "strings.h"

namespace acc::wall_topology {

namespace {

// ---------------------------------------------------------------------
// Tunable parameters. Pull these out into a settings struct once the
// algorithm stabilises.
// ---------------------------------------------------------------------

// Material classifier. K1's surfacemat.2da uses small integer rows;
// row 7 is the "non-walkable" / wall-fill row in most areas we've
// inspected. Treat that as a hard boundary; everything else is
// walkable until in-game testing shows otherwise.
//
// TODO: replace this with a surfacemat.2da lookup once we wire one
// up. For now the magic constant is documented loudly enough that the
// next reader can refine it.
constexpr int kNonWalkableMaterial = 7;

bool MaterialIsWalkable(int materialId) {
    if (materialId < 0) return false;
    return materialId != kNonWalkableMaterial;
}

// Cell-classification thresholds. Tuned for KOTOR's metric-scale
// authoring (walkmesh triangles are typically 0.5-3m on a side, rooms
// run 5-20m, major corridors 15-40m).
//
// `kCorridorAspectRatio`: a cell whose long axis is at least this
// many times its short axis is corridor-shaped.
// `kCorridorMaxShortAxisM`: even high-aspect cells stop being
// "corridor" once they're wider than this — that's a long room, not a
// corridor.
// `kSmallCellMaxAreaSqM`: cells below this become "Raum" (small
// enclosed room) regardless of aspect ratio. Above it, the cell is
// "Offene Fläche" or corridor depending on aspect.
constexpr float kCorridorAspectRatio    = 2.5f;
constexpr float kCorridorMaxShortAxisM  = 6.0f;
constexpr float kSmallCellMaxAreaSqM    = 80.0f;

// Capacity for the cached face graph + cells. K1 areas have
// hand-authored walkmeshes; vanilla content tops out well below these.
constexpr int kMaxFaces = 8192;
constexpr int kMaxCells = 256;

// Spatial-index parameters for point-in-cell lookup. The grid covers
// the area's XY bounding box; each cell holds face indices that
// overlap that grid cell. 2m cell size matches typical face dimensions.
constexpr float kGridCellSize = 2.0f;
constexpr int   kGridWidth    = 96;
constexpr int   kGridHeight   = 96;
constexpr int   kMaxFacesPerGridCell = 32;

// ---------------------------------------------------------------------
// Cached state for the current area.
// ---------------------------------------------------------------------

struct AreaGraph {
    void*                       area_owner   = nullptr;
    bool                        built        = false;
    int                         face_count   = 0;
    acc::engine::WalkmeshFace   faces[kMaxFaces];
    int                         face_cell_id[kMaxFaces];

    int                         cell_count = 0;
    struct Cell {
        // Geometry summary.
        float min_x, min_y, max_x, max_y;
        float centroid_x, centroid_y;
        // PCA axis (primary orientation of the cell).
        float axis_x, axis_y;
        float long_extent;
        float short_extent;
        float area_sq_m;
        // Connectivity.
        int   face_count;
        int   exit_count;     // edges where neighbour is in a different cell
        // Pre-rendered localised label + a small signature for dedup.
        char  label[128];
        int   sig;
    };
    Cell cells[kMaxCells];

    // Spatial index for point-in-cell lookup. `face_lists[cell]` is a
    // run of face indices stored densely; `cell_offset[cell]` /
    // `cell_count_in[cell]` index into it.
    float origin_x, origin_y;
    int   cell_offset[kGridWidth * kGridHeight];
    int   cell_count_in[kGridWidth * kGridHeight];
    int   face_lists[kGridWidth * kGridHeight * kMaxFacesPerGridCell];
};

AreaGraph g_graph;

// ---------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------

float TriangleArea2D(const Vector& a, const Vector& b, const Vector& c) {
    float ux = b.x - a.x, uy = b.y - a.y;
    float vx = c.x - a.x, vy = c.y - a.y;
    return 0.5f * std::fabs(ux * vy - uy * vx);
}

void TriangleCentroid2D(const Vector& a, const Vector& b, const Vector& c,
                        float& outX, float& outY) {
    outX = (a.x + b.x + c.x) / 3.0f;
    outY = (a.y + b.y + c.y) / 3.0f;
}

// 4-way cardinal from an XY unit-ish vector. Engine frame:
// 0° = +X = East, +Y = North.
acc::strings::Id CardinalFromVector(float dx, float dy) {
    using acc::strings::Id;
    if (std::fabs(dx) > std::fabs(dy)) {
        return dx >= 0.0f ? Id::DirEast : Id::DirWest;
    }
    return dy >= 0.0f ? Id::DirNorth : Id::DirSouth;
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

// 2D point-in-triangle test using barycentric coordinates with a
// tiny tolerance to absorb float drift near edges.
bool PointInTriangle2D(const Vector& a, const Vector& b, const Vector& c,
                       float px, float py) {
    float v0x = c.x - a.x, v0y = c.y - a.y;
    float v1x = b.x - a.x, v1y = b.y - a.y;
    float v2x = px  - a.x, v2y = py  - a.y;
    float dot00 = v0x * v0x + v0y * v0y;
    float dot01 = v0x * v1x + v0y * v1y;
    float dot02 = v0x * v2x + v0y * v2y;
    float dot11 = v1x * v1x + v1y * v1y;
    float dot12 = v1x * v2x + v1y * v2y;
    float denom = dot00 * dot11 - dot01 * dot01;
    if (denom > -1e-12f && denom < 1e-12f) return false;
    float invDenom = 1.0f / denom;
    float u = (dot11 * dot02 - dot01 * dot12) * invDenom;
    float v = (dot00 * dot12 - dot01 * dot02) * invDenom;
    constexpr float eps = 1e-4f;
    return u >= -eps && v >= -eps && (u + v) <= 1.0f + eps;
}

// ---------------------------------------------------------------------
// Phase 2A: flood-fill connected walkable faces into cells.
// ---------------------------------------------------------------------

void BuildCellsFromFaceGraph() {
    int n = g_graph.face_count;
    for (int i = 0; i < n; ++i) g_graph.face_cell_id[i] = -1;
    g_graph.cell_count = 0;

    // Scratch BFS queue. Sized to face count.
    static int s_queue[kMaxFaces];

    for (int start = 0; start < n; ++start) {
        if (g_graph.face_cell_id[start] != -1) continue;
        if (!MaterialIsWalkable(g_graph.faces[start].material_id)) continue;

        if (g_graph.cell_count >= kMaxCells) {
            acclog::Write("WallTopo",
                          "BuildCellsFromFaceGraph: cell cap %d reached, "
                          "remaining faces unassigned", kMaxCells);
            break;
        }
        int cellId = g_graph.cell_count++;
        int head = 0, tail = 0;
        s_queue[tail++] = start;
        g_graph.face_cell_id[start] = cellId;
        while (head < tail) {
            int f = s_queue[head++];
            const auto& face = g_graph.faces[f];
            for (int e = 0; e < 3; ++e) {
                int nb = face.adj[e];
                if (nb < 0 || nb >= n) continue;
                if (g_graph.face_cell_id[nb] != -1) continue;
                if (!MaterialIsWalkable(g_graph.faces[nb].material_id)) continue;
                g_graph.face_cell_id[nb] = cellId;
                if (tail < kMaxFaces) s_queue[tail++] = nb;
            }
        }
    }
}

// ---------------------------------------------------------------------
// Phase 2B: classify each cell — shape, exits, label.
// ---------------------------------------------------------------------

void ClassifyCells() {
    using acc::strings::Id;
    int n = g_graph.face_count;

    for (int c = 0; c < g_graph.cell_count; ++c) {
        AreaGraph::Cell& cell = g_graph.cells[c];
        cell.min_x =  1e30f; cell.min_y =  1e30f;
        cell.max_x = -1e30f; cell.max_y = -1e30f;
        cell.centroid_x = 0.0f; cell.centroid_y = 0.0f;
        cell.axis_x = 1.0f; cell.axis_y = 0.0f;
        cell.long_extent = 0.0f;
        cell.short_extent = 0.0f;
        cell.area_sq_m = 0.0f;
        cell.face_count = 0;
        cell.exit_count = 0;
        cell.label[0] = '\0';
        cell.sig = 0;
    }

    // First pass: bbox + face-centroid sum + face area, weighted by
    // area for the cell centroid + PCA. Area-weighting avoids letting
    // a cell's tiny triangulation slivers dominate the geometric
    // summary.
    static float s_sum_xx[kMaxCells];
    static float s_sum_xy[kMaxCells];
    static float s_sum_yy[kMaxCells];
    for (int c = 0; c < g_graph.cell_count; ++c) {
        s_sum_xx[c] = s_sum_xy[c] = s_sum_yy[c] = 0.0f;
    }
    for (int f = 0; f < n; ++f) {
        int cid = g_graph.face_cell_id[f];
        if (cid < 0) continue;
        const auto& face = g_graph.faces[f];
        float fa = TriangleArea2D(face.v[0], face.v[1], face.v[2]);
        float fcx = 0.0f, fcy = 0.0f;
        TriangleCentroid2D(face.v[0], face.v[1], face.v[2], fcx, fcy);
        auto& cell = g_graph.cells[cid];
        for (int k = 0; k < 3; ++k) {
            if (face.v[k].x < cell.min_x) cell.min_x = face.v[k].x;
            if (face.v[k].y < cell.min_y) cell.min_y = face.v[k].y;
            if (face.v[k].x > cell.max_x) cell.max_x = face.v[k].x;
            if (face.v[k].y > cell.max_y) cell.max_y = face.v[k].y;
        }
        cell.area_sq_m  += fa;
        cell.face_count += 1;
        cell.centroid_x += fcx * fa;  // area-weighted; normalised below
        cell.centroid_y += fcy * fa;

        // Count exits while we're walking edges.
        for (int e = 0; e < 3; ++e) {
            int nb = face.adj[e];
            if (nb < 0 || nb >= n) continue;
            if (g_graph.face_cell_id[nb] != cid) {
                cell.exit_count += 1;
            }
        }
    }

    // Normalise centroid + compute PCA in a second pass that knows
    // the cell centroid.
    for (int c = 0; c < g_graph.cell_count; ++c) {
        auto& cell = g_graph.cells[c];
        if (cell.area_sq_m > 1e-6f) {
            cell.centroid_x /= cell.area_sq_m;
            cell.centroid_y /= cell.area_sq_m;
        }
    }
    for (int f = 0; f < n; ++f) {
        int cid = g_graph.face_cell_id[f];
        if (cid < 0) continue;
        const auto& face = g_graph.faces[f];
        float fa = TriangleArea2D(face.v[0], face.v[1], face.v[2]);
        float fcx = 0.0f, fcy = 0.0f;
        TriangleCentroid2D(face.v[0], face.v[1], face.v[2], fcx, fcy);
        const auto& cell = g_graph.cells[cid];
        float dx = fcx - cell.centroid_x;
        float dy = fcy - cell.centroid_y;
        s_sum_xx[cid] += fa * dx * dx;
        s_sum_xy[cid] += fa * dx * dy;
        s_sum_yy[cid] += fa * dy * dy;
    }

    for (int c = 0; c < g_graph.cell_count; ++c) {
        auto& cell = g_graph.cells[c];

        // PCA on the 2×2 covariance — closed form. The principal
        // eigenvector gives the cell's long axis.
        float a = s_sum_xx[c];
        float b = s_sum_xy[c];
        float d = s_sum_yy[c];
        float tr   = a + d;
        float det  = a * d - b * b;
        float disc = std::sqrt(std::fmax(0.0f, tr * tr * 0.25f - det));
        float l1 = tr * 0.5f + disc;   // larger eigenvalue
        float l2 = tr * 0.5f - disc;   // smaller
        if (l1 < 0.0f) l1 = 0.0f;
        if (l2 < 0.0f) l2 = 0.0f;

        // Eigenvector for l1. Use the more numerically stable branch.
        float ex, ey;
        if (std::fabs(b) > 1e-6f) {
            ex = l1 - d;
            ey = b;
        } else if (a >= d) {
            ex = 1.0f; ey = 0.0f;
        } else {
            ex = 0.0f; ey = 1.0f;
        }
        float emag = std::sqrt(ex * ex + ey * ey);
        if (emag > 1e-6f) {
            cell.axis_x = ex / emag;
            cell.axis_y = ey / emag;
        }

        // Extent = 2 * sqrt(eigenvalue / area) gives the standard
        // deviation along each axis; doubled for a coarse "half-width"
        // → "full width" expansion. Matches the player's perception of
        // cell width better than the bbox dimensions (which over-state
        // diagonal cells).
        float weight = std::fmax(cell.area_sq_m, 1e-3f);
        cell.long_extent  = 2.0f * std::sqrt(l1 / weight);
        cell.short_extent = 2.0f * std::sqrt(l2 / weight);

        // ---- Classification -----------------------------------------
        const char* axisStr = (std::fabs(cell.axis_x) > std::fabs(cell.axis_y))
                                  ? acc::strings::Get(Id::AxisEastWest)
                                  : acc::strings::Get(Id::AxisNorthSouth);
        bool axisIsEW = (std::fabs(cell.axis_x) > std::fabs(cell.axis_y));

        bool isCorridor =
            cell.short_extent > 1e-3f &&
            cell.short_extent < kCorridorMaxShortAxisM &&
            (cell.long_extent / cell.short_extent) >= kCorridorAspectRatio;
        bool isSmallRoom = cell.area_sq_m < kSmallCellMaxAreaSqM;

        // Exit count drives the dead-end / junction variant. A cell
        // with 0 or 1 *cell-level* exits is a dead end; the exit_count
        // we tracked above is edge-level (a thick portal counts 3+).
        // Convert to cell-level by walking faces once more — cheaper to
        // compute as a set of neighbour cell ids.
        int neighbourCells[8];
        int neighbourCellCount = 0;
        for (int f = 0; f < n && neighbourCellCount < 8; ++f) {
            if (g_graph.face_cell_id[f] != c) continue;
            const auto& face = g_graph.faces[f];
            for (int e = 0; e < 3; ++e) {
                int nb = face.adj[e];
                if (nb < 0 || nb >= n) continue;
                int ncid = g_graph.face_cell_id[nb];
                if (ncid == c || ncid < 0) continue;
                bool seen = false;
                for (int k = 0; k < neighbourCellCount; ++k) {
                    if (neighbourCells[k] == ncid) { seen = true; break; }
                }
                if (!seen && neighbourCellCount < 8) {
                    neighbourCells[neighbourCellCount++] = ncid;
                }
            }
        }

        if (isCorridor) {
            const char* fmt = acc::strings::Get(Id::FmtMapCursorCorridor);
            if (fmt && fmt[0] && axisStr && axisStr[0]) {
                std::snprintf(cell.label, sizeof(cell.label),
                              fmt, axisStr, cell.short_extent);
            }
            int kindByte = axisIsEW ? 4 : 3;
            int w        = static_cast<int>(cell.short_extent + 0.5f);
            cell.sig = (kindByte & 0xff) | ((w & 0xff) << 8);
        } else if (neighbourCellCount >= 3) {
            // Junction. Build a comma-separated direction list from
            // the vectors centroid → each neighbour-cell centroid.
            char dirList[96] = {0};
            size_t dirLen = 0;
            int mask = 0;
            for (int k = 0; k < neighbourCellCount; ++k) {
                const auto& nb = g_graph.cells[neighbourCells[k]];
                float ddx = nb.centroid_x - cell.centroid_x;
                float ddy = nb.centroid_y - cell.centroid_y;
                Id dirId = CardinalFromVector(ddx, ddy);
                int bit = 0;
                switch (dirId) {
                    case Id::DirNorth: bit = 0; break;
                    case Id::DirEast:  bit = 1; break;
                    case Id::DirSouth: bit = 2; break;
                    case Id::DirWest:  bit = 3; break;
                    default: continue;
                }
                if (mask & (1 << bit)) continue;
                mask |= (1 << bit);
                dirLen = AppendDirWord(dirList, sizeof(dirList),
                                       dirLen, dirId);
            }
            const char* fmt =
                acc::strings::Get(Id::FmtMapCursorJunctionDirs);
            if (fmt && fmt[0] && dirList[0] != '\0') {
                std::snprintf(cell.label, sizeof(cell.label),
                              fmt, dirList);
            }
            cell.sig = 6 | ((mask & 0xff) << 8) |
                       ((neighbourCellCount & 0xff) << 16);
        } else if (neighbourCellCount == 1) {
            // Dead-end pointing at the single neighbour cell.
            const auto& nb = g_graph.cells[neighbourCells[0]];
            float ddx = nb.centroid_x - cell.centroid_x;
            float ddy = nb.centroid_y - cell.centroid_y;
            Id dirId = CardinalFromVector(ddx, ddy);
            const char* dir = acc::strings::Get(dirId);
            const char* fmt = acc::strings::Get(Id::FmtMapCursorDeadEnd);
            if (fmt && fmt[0] && dir && dir[0]) {
                std::snprintf(cell.label, sizeof(cell.label),
                              fmt, dir);
            }
            cell.sig = 5 | (static_cast<int>(dirId) << 8);
        } else if (isSmallRoom) {
            // Small enclosed cell with 0 or 2 exits — read as a room.
            // No localised "Raum" key yet; fall back to "Offene Fläche"
            // until we add one. Sig distinguishes for log post-mortem.
            const char* fallback =
                acc::strings::Get(Id::MapCursorOpenArea);
            if (fallback && fallback[0]) {
                std::snprintf(cell.label, sizeof(cell.label),
                              "%s", fallback);
            }
            cell.sig = 7;
        } else {
            // Large + low-aspect = open area.
            const char* fallback =
                acc::strings::Get(Id::MapCursorOpenArea);
            if (fallback && fallback[0]) {
                std::snprintf(cell.label, sizeof(cell.label),
                              "%s", fallback);
            }
            cell.sig = 2;
        }
    }
}

// ---------------------------------------------------------------------
// Phase 2C: spatial index — coarse uniform grid over the area bbox.
// ---------------------------------------------------------------------

void BuildSpatialIndex() {
    int total = kGridWidth * kGridHeight;
    for (int i = 0; i < total; ++i) {
        g_graph.cell_offset[i]   = 0;
        g_graph.cell_count_in[i] = 0;
    }

    // Find area bbox from face vertices.
    float minX =  1e30f, minY =  1e30f;
    float maxX = -1e30f, maxY = -1e30f;
    for (int f = 0; f < g_graph.face_count; ++f) {
        const auto& face = g_graph.faces[f];
        for (int k = 0; k < 3; ++k) {
            if (face.v[k].x < minX) minX = face.v[k].x;
            if (face.v[k].y < minY) minY = face.v[k].y;
            if (face.v[k].x > maxX) maxX = face.v[k].x;
            if (face.v[k].y > maxY) maxY = face.v[k].y;
        }
    }
    g_graph.origin_x = minX;
    g_graph.origin_y = minY;

    // First pass: count entries per grid cell.
    auto faceGridRange = [&](const acc::engine::WalkmeshFace& face,
                             int& gx0, int& gy0, int& gx1, int& gy1) {
        float fminX = face.v[0].x, fminY = face.v[0].y;
        float fmaxX = face.v[0].x, fmaxY = face.v[0].y;
        for (int k = 1; k < 3; ++k) {
            if (face.v[k].x < fminX) fminX = face.v[k].x;
            if (face.v[k].y < fminY) fminY = face.v[k].y;
            if (face.v[k].x > fmaxX) fmaxX = face.v[k].x;
            if (face.v[k].y > fmaxY) fmaxY = face.v[k].y;
        }
        gx0 = static_cast<int>((fminX - g_graph.origin_x) / kGridCellSize);
        gy0 = static_cast<int>((fminY - g_graph.origin_y) / kGridCellSize);
        gx1 = static_cast<int>((fmaxX - g_graph.origin_x) / kGridCellSize);
        gy1 = static_cast<int>((fmaxY - g_graph.origin_y) / kGridCellSize);
        if (gx0 < 0) gx0 = 0;
        if (gy0 < 0) gy0 = 0;
        if (gx1 >= kGridWidth)  gx1 = kGridWidth  - 1;
        if (gy1 >= kGridHeight) gy1 = kGridHeight - 1;
    };

    for (int f = 0; f < g_graph.face_count; ++f) {
        if (g_graph.face_cell_id[f] < 0) continue;  // skip non-walkable
        int gx0, gy0, gx1, gy1;
        faceGridRange(g_graph.faces[f], gx0, gy0, gx1, gy1);
        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                int c = gy * kGridWidth + gx;
                if (g_graph.cell_count_in[c] < kMaxFacesPerGridCell) {
                    g_graph.cell_count_in[c]++;
                }
            }
        }
    }

    // Layout offsets via prefix-sum, then reset counts so the fill
    // pass below can use them as write cursors.
    int cursor = 0;
    for (int i = 0; i < total; ++i) {
        g_graph.cell_offset[i] = cursor;
        cursor += g_graph.cell_count_in[i];
        g_graph.cell_count_in[i] = 0;
    }
    if (cursor > kGridWidth * kGridHeight * kMaxFacesPerGridCell) {
        cursor = kGridWidth * kGridHeight * kMaxFacesPerGridCell;
        acclog::Write("WallTopo",
                      "BuildSpatialIndex: face-list capacity exceeded; "
                      "truncating index (lookup may miss faces near "
                      "dense regions)");
    }

    // Second pass: fill the dense face-list array.
    for (int f = 0; f < g_graph.face_count; ++f) {
        if (g_graph.face_cell_id[f] < 0) continue;
        int gx0, gy0, gx1, gy1;
        faceGridRange(g_graph.faces[f], gx0, gy0, gx1, gy1);
        for (int gy = gy0; gy <= gy1; ++gy) {
            for (int gx = gx0; gx <= gx1; ++gx) {
                int c = gy * kGridWidth + gx;
                int slot = g_graph.cell_offset[c] + g_graph.cell_count_in[c];
                if (slot < kGridWidth * kGridHeight * kMaxFacesPerGridCell) {
                    g_graph.face_lists[slot] = f;
                    g_graph.cell_count_in[c]++;
                }
            }
        }
    }
}

}  // namespace

void Reset() {
    g_graph.area_owner = nullptr;
    g_graph.built      = false;
    g_graph.face_count = 0;
    g_graph.cell_count = 0;
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

    // Pull the walkmesh face cache directly from engine_area. We
    // build it once per area-load; the walkmesh is immutable so the
    // cache survives until the player leaves the area.
    int faceCount = acc::engine::BuildAreaFaceCache(
        area, g_graph.faces, kMaxFaces);
    if (faceCount <= 0) {
        acclog::Write("WallTopo",
                      "BuildForArea: face cache empty (areaPtr=%p) — "
                      "leaving graph unbuilt", area);
        return;
    }
    g_graph.face_count = faceCount;

    BuildCellsFromFaceGraph();
    ClassifyCells();
    BuildSpatialIndex();

    acclog::Write("WallTopo",
                  "BuildForArea: area=%p faces=%d cells=%d",
                  area, g_graph.face_count, g_graph.cell_count);

    g_graph.built = true;
    DumpGraphToLog();
}

void DumpGraphToLog() {
    if (!g_graph.built) {
        acclog::Write("WallTopo", "DumpGraphToLog: no graph built");
        return;
    }
    acclog::Write("WallTopo",
                  "graph dump area=%p faces=%d cells=%d",
                  g_graph.area_owner, g_graph.face_count,
                  g_graph.cell_count);

    // Tally cell shape buckets for telemetry.
    int corridors = 0, junctions = 0, deadEnds = 0;
    int smallRooms = 0, openAreas = 0, other = 0;
    for (int i = 0; i < g_graph.cell_count; ++i) {
        int sigKind = g_graph.cells[i].sig & 0xff;
        switch (sigKind) {
            case 3: case 4: ++corridors;   break;
            case 5:         ++deadEnds;    break;
            case 6:         ++junctions;   break;
            case 7:         ++smallRooms;  break;
            case 2:         ++openAreas;   break;
            default:        ++other;       break;
        }
    }
    acclog::Write("WallTopo",
                  "cell shapes: corridor=%d junction=%d dead-end=%d "
                  "small-room=%d open=%d other=%d",
                  corridors, junctions, deadEnds, smallRooms,
                  openAreas, other);

    // Largest few cells — usually the most informative entries (the
    // main hub, big corridors). Selection-sort top-N by face count
    // descending.
    constexpr int kTopN = 16;
    int order[kMaxCells];
    for (int i = 0; i < g_graph.cell_count; ++i) order[i] = i;
    int limit = g_graph.cell_count < kTopN ? g_graph.cell_count : kTopN;
    for (int i = 0; i < limit; ++i) {
        int best = i;
        for (int j = i + 1; j < g_graph.cell_count; ++j) {
            if (g_graph.cells[order[j]].face_count >
                g_graph.cells[order[best]].face_count) {
                best = j;
            }
        }
        if (best != i) {
            int t = order[i]; order[i] = order[best]; order[best] = t;
        }
        const auto& cell = g_graph.cells[order[i]];
        acclog::Write("WallTopo",
                      "  cell[%d] faces=%d area=%.1fm² long=%.1fm short=%.1fm "
                      "exits=%d centroid=(%.1f,%.1f) axis=(%.2f,%.2f) "
                      "label=\"%s\"",
                      order[i], cell.face_count, cell.area_sq_m,
                      cell.long_extent, cell.short_extent, cell.exit_count,
                      cell.centroid_x, cell.centroid_y,
                      cell.axis_x, cell.axis_y, cell.label);
    }
}

bool LookupAt(void* area, const Vector& worldPos,
              char* outBuf, size_t bufSize, int& outSig) {
    if (outBuf && bufSize > 0) outBuf[0] = '\0';
    outSig = 0;
    if (!HasGraphForArea(area)) return false;

    int gx = static_cast<int>((worldPos.x - g_graph.origin_x) / kGridCellSize);
    int gy = static_cast<int>((worldPos.y - g_graph.origin_y) / kGridCellSize);
    if (gx < 0 || gy < 0 || gx >= kGridWidth || gy >= kGridHeight) {
        // Outside the indexed region — fall through to "open area".
        const char* fallback =
            acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
        if (fallback && fallback[0]) {
            std::snprintf(outBuf, bufSize, "%s", fallback);
            outSig = 2;
            return true;
        }
        return false;
    }

    int gridCell = gy * kGridWidth + gx;
    int base = g_graph.cell_offset[gridCell];
    int cnt  = g_graph.cell_count_in[gridCell];
    for (int i = 0; i < cnt; ++i) {
        int f = g_graph.face_lists[base + i];
        const auto& face = g_graph.faces[f];
        if (PointInTriangle2D(face.v[0], face.v[1], face.v[2],
                              worldPos.x, worldPos.y)) {
            int cid = g_graph.face_cell_id[f];
            if (cid < 0) continue;
            const auto& cell = g_graph.cells[cid];
            if (cell.label[0] != '\0') {
                std::snprintf(outBuf, bufSize, "%s", cell.label);
                outSig = cell.sig;
                return true;
            }
        }
    }

    // No face contained the point — player is standing on
    // non-walkable geometry or in the gap between faces. Fall back to
    // "Offene Fläche" so the user hears something rather than the
    // tier-collapse silence.
    const char* fallback =
        acc::strings::Get(acc::strings::Id::MapCursorOpenArea);
    if (fallback && fallback[0]) {
        std::snprintf(outBuf, bufSize, "%s", fallback);
        outSig = 2;
        return true;
    }
    return false;
}

}  // namespace acc::wall_topology
