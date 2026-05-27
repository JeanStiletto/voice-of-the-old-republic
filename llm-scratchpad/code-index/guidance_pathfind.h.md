# guidance_pathfind.h (65 lines)

A* pathfinder header over the per-area static nav graph. Engine refuses to plot a path for the PC, so we re-solve. Output feeds guidance_beacon and guidance_description. Also declares the CSWSArea nav graph offset constants.

## Declarations (in source order)

- L26 — `namespace acc::guidance`
- L50 — `bool ComputePath(void* area, const Vector& start, const Vector& goal, std::vector<Vector>& outWaypoints);`
  note: appends original goal as terminal anchor; start is NOT prepended; string-pulling pass uses GetCachedWalls (degrades gracefully if unavailable); O(N²) open-set scan; every read is __try-wrapped
- L57 — `constexpr size_t kAreaPathPointsCountOffset      = 0x238;`
- L58 — `constexpr size_t kAreaPathPointsPtrOffset        = 0x23c;`
- L59 — `constexpr size_t kAreaPathConnectionsCountOffset = 0x240;`
- L60 — `constexpr size_t kAreaPathConnectionsPtrOffset   = 0x244;`
- L62 — `constexpr size_t kPathPointStride                = 0x10;`
- L63 — `constexpr size_t kPathPointPositionOffset        = 0x00;`
- L64 — `constexpr size_t kPathPointCsrOffset             = 0x0c;`
