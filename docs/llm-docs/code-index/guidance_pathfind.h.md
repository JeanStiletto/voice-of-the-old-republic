# guidance_pathfind.h (65 lines)

Header for the A* nav-graph pathfinder. Documents the CSWSArea PathPoint/CSR
adjacency layout (offsets at the bottom), the single-area-only first-cut
scope (~200 node cap, fixed-size open/closed arrays), and the string-pulling
smoothing contract (anchored on `start`, not the first A* waypoint).

## Declarations (in source order)

- L26 — `namespace acc::guidance`
- L50 — `bool ComputePath(void* area, const Vector& start, const Vector& goal, std::vector<Vector>& outWaypoints)`
  note: never raises — every nav-graph read is SEH-wrapped; false clears outWaypoints.
- L58-61 — `kAreaPathPointsCountOffset` (0x238), `kAreaPathPointsPtrOffset` (0x23c), `kAreaPathConnectionsCountOffset` (0x240), `kAreaPathConnectionsPtrOffset` (0x244)
- L63-65 — `kPathPointStride` (0x10), `kPathPointPositionOffset` (0x00), `kPathPointCsrOffset` (0x0c)
