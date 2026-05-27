# spatial_change_detector.h (70 lines)

Pillar 1 Triggers 1+2 — per-feature change detection. Maintains per-feature last_cued_distance (T1) and foremost-in-front debounce (T2). Exports the wall surface cache + clustering results for wall_topology and map_ui_cursor.

## Declarations (in source order)

- L28 — `namespace acc::spatial::change_detector`
- L32 — `void Tick()`
  note: self-gates on player+area resolved; rebuilds wall cache on area change
- L36 — `bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount)`
  note: borrowed pointer into static storage; valid for area lifetime only
- L44 — `struct WallSurfaceDesc`
  note: a/b = extreme endpoints; dir_x/y = direction; length; edge_count; consumed by wall_topology
- L53 — `int GetWallSurfaceCount()`
- L58 — `bool GetWallSurfaceDesc(int idx, WallSurfaceDesc& outDesc)`
- L61 — `int GetEdgeSurfaceId(int edgeIdx)`
- L66 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
  note: uses clustered (portal-seam-absorbed) surfaces, not raw edges
