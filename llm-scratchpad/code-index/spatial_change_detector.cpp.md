# spatial_change_detector.cpp (~1180 lines)

Pillar 1 T1/T2 change detector implementation. Per-sector and per-object distance tracking, T2 foremost-in-front cone, calibration on area change + view-mode enter/exit. Wall cache + surface clustering lives in spatial_wall_surfaces.{h,cpp}; this file reads through wall_surfaces:: accessors.

## Declarations (in source order)

- L22 — `namespace acc::spatial::change_detector`
- (anonymous namespace: g_surface_last_cued_at[kMaxWallSurfaces] runtime fire-stamp, sector tracking, object state table, T2 foremost state, geometry helper ClosestPointDistanceSquared, ObjectClassify helpers, WallSector enum + ClassifyRelativeBearing, OnAreaChange, CalibrateInRange)
- `void Tick()` (public) — orchestrates area-change rebuild + calibration + per-tick wall/object scan + T1 sector fires + T2 foremost-in-front
- `bool GetCachedWalls(const acc::engine::WallEdge*& outBuf, int& outCount)` (public) — forwards to wall_surfaces
- `int GetWallSurfaceCount()` (public) — forwards
- `bool GetWallSurfaceDesc(int idx, WallSurfaceDesc& outDesc)` (public) — forwards
- `int GetEdgeSurfaceId(int edgeIdx)` (public) — forwards
- `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)` (public) — forwards
