# spatial_change_detector.h (62 lines)

Public interface for Pillar 1 Triggers 1+2 (spatial audio-cue change
detection): a per-tick scan over cached walls (from spatial_wall_surfaces) and
Pillar-4-filtered objects. Trigger 1 = 360-degree distance-delta per
world-frame sector (Front/Left/Back/Right bins), capped per tick. Trigger 2 =
player-relative foremost-in-front (+-45 degrees) debounce, silent when the
cone is clear. Re-exports `wall_surfaces::WallSurfaceDesc` and thin wrapper
accessors for legacy callers; new code should call `wall_surfaces::` directly.

## Declarations (in source order)

- L43 — `void Tick()`
  note: self-gates on player+area resolved; rebuilds wall cache + resets per-feature state on area change
- L47 — `using acc::spatial::wall_surfaces::WallSurfaceDesc`
- L51 — `bool GetCachedWalls(const engine::WallEdge*& outBuf, int& outCount)`
  note: borrowed pointer into static storage, valid for area lifetime; false until first Tick
- L54 — `int GetEdgeSurfaceId(int edgeIdx)`
- L59 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
  note: mirrors the audio wall-cue model — portal-seam phantoms absorbed by clustering are invisible here too
