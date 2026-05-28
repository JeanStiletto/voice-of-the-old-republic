# wall_topology.cpp (2890 lines)

Wall-topology decomposition implementation — Path 3 nav-graph classifier. Consumes engine path_points/path_connections, classifies by CSR-adjacency degree, collapses hub clusters and straight corridor chains into perceptual regions. Includes walkmesh-probe helpers (ProbeWall, ProbeDistance, WalkmeshAgreesDeadEnd) absorbed from the retired region_classifier.

## Declarations (in source order)

- L34 — `namespace acc::wall_topology`
- L53 — `static float ProbeWall(const acc::engine::WallEdge* walls, int wallCount, const Vector& origin, float dx, float dy)` (anonymous namespace)
- L71 — `static float ProbeDistance(const Vector& pos, float dx, float dy)` (anonymous namespace)
  note: returns kProbeLenWu on clear ray, -1.0 on empty cache
- (internal graph-build, cluster-merge, door-snap, landmark-attach, LookupAt helpers — all in anonymous namespace)
- `void BuildForArea(void* area)` (public; definition inside namespace acc::wall_topology)
- `void MaybeRefreshDoors(void* area)` (public)
- `bool HasGraphForArea(void* area)` (public)
- `void Reset()` (public)
- `bool LookupAt(void* area, const Vector& worldPos, char* outBuf, size_t bufSize, int& outSig, int& outClusterId, bool allowDiagLog, bool requireWallReachable)` (public)
- `void DumpGraphToLog()` (public)
