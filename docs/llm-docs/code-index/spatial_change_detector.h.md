# spatial_change_detector.h (65 lines)

Pillar 1 Triggers 1+2 — per-feature change detection. Maintains per-feature last_cued_distance (T1) and foremost-in-front debounce (T2). Re-exports the wall surface cache + clustering accessors (implemented in spatial_wall_surfaces.{h,cpp}) as thin wrappers for legacy callers.

## Declarations (in source order)

- L31 — `namespace acc::spatial::change_detector`
- L35 — `void Tick()`
  note: self-gates on player+area resolved; rebuilds wall cache on area change via wall_surfaces::RebuildForArea
- L39 — `using acc::spatial::wall_surfaces::WallSurfaceDesc;`
  note: struct now owned by spatial_wall_surfaces.h; re-exported here for legacy callers
- L43 — `bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount)`
  note: forwards to wall_surfaces::GetWallBuffer + GetWallCount
- L46 — `int GetWallSurfaceCount()`
  note: forwards to wall_surfaces::GetSurfaceCount
- L51 — `bool GetWallSurfaceDesc(int idx, WallSurfaceDesc& outDesc)`
  note: forwards to wall_surfaces::GetSurfaceDesc
- L54 — `int GetEdgeSurfaceId(int edgeIdx)`
  note: forwards to wall_surfaces::GetEdgeSurfaceIdBuffer
- L60 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
  note: forwards to wall_surfaces::SegmentCrossesSurface
