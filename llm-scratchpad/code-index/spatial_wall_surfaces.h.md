# spatial_wall_surfaces.h (92 lines)

Wall cache + surface clustering — owns the per-area wall edge buffer (g_walls[kMaxWallEdges]) and the union-find clustering output (per-edge surface id buffer + per-surface descriptors). Built once per area via RebuildForArea, consumed by spatial_change_detector (T1/T2), wall_topology, guidance_pathfind, view_mode, audio_footstep_suppress, map_ui_cursor.

## Declarations (in source order)

- L26 — `namespace acc::spatial::wall_surfaces`
- L32 — `constexpr int kMaxWallEdges = 4096` — Endar Spire range, headroom for open areas
- L33 — `constexpr int kMaxWallSurfaces = 1024`
- L45 — `struct WallSurfaceDesc` — a/b extreme endpoints, dir_x/y, length, edge_count (==0 flags degenerate)
- L56 — `void RebuildForArea(void* area)` — cache build + clustering + descriptors; silent on null (calls Clear)
- L60 — `void Clear()` — resets to no-area state
- L64 — `const acc::engine::WallEdge* GetWallBuffer()` — borrowed pointer, lifetime = next Rebuild/Clear
- L69 — `const int* GetEdgeSurfaceIdBuffer()` — parallel array to wall buffer
- L72 — `int GetWallCount()`
- L75 — `int GetSurfaceCount()`
- L80 — `bool GetSurfaceDesc(int idx, WallSurfaceDesc& outDesc)` — false on degenerate / out of range
- L86 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
