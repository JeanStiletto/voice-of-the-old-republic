# spatial_wall_surfaces.h (92 lines)

Declares the wall cache + surface-clustering subsystem. The walkmesh stores
wall geometry as many short edges (~0.7-1m in Endar Spire corridors), which
would cause chatter as the player passes each edge endpoint; this clusters
adjacent collinear edges (sharing an endpoint within 5cm, direction agreeing
within cos(15 degrees)) into "surfaces" — deliberately merging across KOTOR's
internal .lyt "room" segmentation since a physical wall regularly spans
several. Each surface reduces to one straight segment (two extreme endpoints +
direction + length) for consumers like wall_topology.

## Declarations (in source order)

- L33 — `constexpr int kMaxWallEdges = 4096`
  note: sized for Endar Spire (405-908 observed edges); overflow logged once per area-change
- L34 — `constexpr int kMaxWallSurfaces = 1024`
- L44 — `struct WallSurfaceDesc { Vector a, b; float dir_x, dir_y, length; int edge_count; }`
  note: edge_count==0 flags a degenerate surface (closed loop / zero free endpoints)
- L57 — `void RebuildForArea(void* area)`
  note: rebuilds on area change; null area calls Clear() instead
- L61 — `void Clear()`
- L66 — `const acc::engine::WallEdge* GetWallBuffer()`
  note: borrowed static-storage pointer, valid until next RebuildForArea/Clear
- L72 — `const int* GetEdgeSurfaceIdBuffer()`
  note: parallel array to GetWallBuffer; -1 = pre-clustering or overflow-degraded
- L75 — `int GetWallCount()`
- L78 — `int GetSurfaceCount()`
- L83 — `bool GetSurfaceDesc(int idx, WallSurfaceDesc& outDesc)`
- L89 — `bool SegmentCrossesSurface(const Vector& a, const Vector& b, Vector& outHitPoint)`
