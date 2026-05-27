# spatial_change_detector.cpp (~700 lines)

Pillar 1 T1/T2 change detector implementation. Wall cache (kMaxWallEdges=4096), wall surface clustering (union-find on endpoint-sharing + collinearity), per-sector and per-object distance tracking, T2 foremost-in-front cone. Rebuilds on area change.

## Declarations (in source order)

- L20 — `namespace acc::spatial::change_detector`
- (anonymous namespace: wall cache g_walls[kMaxWallEdges], surface cluster tables, per-object distance state, T1/T2 fire tracking)
- `void Tick()` (public)
- `bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount)` (public)
- `int GetWallSurfaceCount()` (public)
- `bool GetWallSurfaceDesc(int idx, WallSurfaceDesc& outDesc)` (public)
- `int GetEdgeSurfaceId(int edgeIdx)` (public)
- `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)` (public)
