# spatial_wall_surfaces.cpp (476 lines)

Implements wall caching + surface clustering. `RebuildForArea` calls
`engine::BuildAreaWallCache` twice (once into the real buffer, once with
null/0 to get the true pre-filter discovered count for overflow detection),
then runs `ClusterEdgesIntoSurfaces` (O(N^2 alpha(N)) union-find over
`EdgesAreSameSurface` pair tests — fine at ~900 edges, runs once per area
load) and `BuildSurfaceDescriptors` (reduces each cluster to a straight
segment by finding its two degree-1 endpoints; classifies anomalies — closed
loops, Y/X-forks, multi-elevation 3D features — with sampled diagnostic log
dumps). `SegmentCrossesSurface` does 2D XY segment-vs-segment intersection
against every non-degenerate surface, returning the closest hit — mirrors the
audio wall-cue model exactly (same clustered representation).

## Declarations (in source order)

- L14 — `acc::engine::WallEdge g_walls[kMaxWallEdges]; int g_wall_count`
- L20 — `int g_edge_surface_id[kMaxWallEdges]`
- L23 — `int g_surface_count`
- L28 — `WallSurfaceDesc g_surface_descriptors[kMaxWallSurfaces]`
- L36 — `constexpr float kSurfaceCollinearityCosThreshold = 0.966f`
  note: cos(15deg); tight enough to keep 90deg L-junctions distinct
- L41 — `constexpr float kEndpointTolMeters = 0.05f` (+ kEndpointTolSquared)
- L49 — `float DistanceSquaredXY(const Vector& a, const Vector& b)`
- L61 — `bool EdgesAreSameSurface(const WallEdge& e1, const WallEdge& e2)`
  note: shared endpoint (XY, within tol) AND collinear direction; degenerate (near-zero-length) edges accepted
- L93 — `int UfFind(int i)` / L101 `void UfUnion(int i, int j)`
  note: iterative union-by-rank + path compression over g_uf_parent/g_uf_rank
- L121 — `void ClusterEdgesIntoSurfaces()`
  note: cross-room merging is intentional (.lyt segmentation != physical wall); overflow degrades extra clusters into surface 0
- L183 — `constexpr int kMaxPointsPerSurface = 64`
- L185 — `void BuildSurfaceDescriptors()`
  note: per-surface endpoint dedup table; free-endpoint count 2 = happy path segment, else anomaly-classified (0/1/3/4/5+/overflow) with multi-elevation (Z spread > 0.5m) tracked separately as "not a bug"; samples first 4 anomalies with edge dumps to log
- L368 — `void Clear()`
- L373 — `void RebuildForArea(void* area)`
  note: overflow check compares pre-seam-filter discovered count vs kMaxWallEdges
- L400-413 — `GetWallBuffer / GetEdgeSurfaceIdBuffer / GetWallCount / GetSurfaceCount`
- L416 — `bool GetSurfaceDesc(int idx, WallSurfaceDesc& outDesc)`
- L424 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
  note: parametric 2D line intersection (t,u in [0,1]); returns the smallest-t hit across all surfaces
