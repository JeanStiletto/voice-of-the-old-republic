# spatial_wall_surfaces.cpp (~476 lines)

Owns: g_walls[kMaxWallEdges], g_wall_count, g_edge_surface_id[kMaxWallEdges], g_surface_count, g_surface_descriptors[kMaxWallSurfaces], union-find scratch (g_uf_parent/g_uf_rank). All in anonymous namespace.

## Declarations (in source order)

- L8 — `namespace acc::spatial::wall_surfaces`
- (anonymous namespace)
  - L14-L29 — cache + cluster storage
  - L33-L42 — clustering constants (kSurfaceCollinearityCosThreshold, kEndpointTolMeters/Squared)
  - L46 — `float DistanceSquaredXY(const Vector& a, const Vector& b)` — XY-only, ignores Z noise
  - L59 — `bool EdgesAreSameSurface(const acc::engine::WallEdge& e1, const acc::engine::WallEdge& e2)` — endpoint match + collinearity test
  - L86-L116 — union-find (g_uf_parent, g_uf_rank, UfFind, UfUnion)
  - L120 — `void ClusterEdgesIntoSurfaces()` — O(N²) pair test + densification, cross-room merging intentional
  - L165 — `constexpr int kMaxPointsPerSurface = 64`
  - L167 — `void BuildSurfaceDescriptors()` — reduce each cluster to a straight segment + anomaly diagnostics (closed-loop / Y-fork / X-fork / multi-elevation breakdown)
- L329 — `void Clear()` (public) — wall_count=0, surface_count=0
- L334 — `void RebuildForArea(void* area)` (public) — BuildAreaWallCache + Cluster + BuildDescriptors + overflow log
- L361 — `const acc::engine::WallEdge* GetWallBuffer()` (public)
- L365 — `const int* GetEdgeSurfaceIdBuffer()` (public)
- L369 — `int GetWallCount()` (public)
- L373 — `int GetSurfaceCount()` (public)
- L377 — `bool GetSurfaceDesc(int idx, WallSurfaceDesc& outDesc)` (public)
- L385 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)` (public)
