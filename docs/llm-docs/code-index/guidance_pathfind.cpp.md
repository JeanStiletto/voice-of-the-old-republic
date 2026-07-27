# guidance_pathfind.cpp (376 lines)

A* pathfinder over the per-area static PathPoint nav graph (CSR adjacency),
re-implemented because the engine refuses to plot a path for the player
creature. Snapshots the graph via engine_navgraph, finds nearest nodes to
start/goal, runs a linear-open-set A* (kMaxNodes small enough that scanning
beats a heap), reconstructs the waypoint chain, and appends the original goal
as terminal anchor. A post-A* string-pulling smoothing pass anchors on the
player's real start position (not the nearest graph node) and collapses any
triple (anchor, B, C) where anchor→C is walkmesh-clear — eliminating the
"first hop points backward/sideways" artifact of starting mid-corridor.
Smoothing uses spatial_change_detector's cached wall edges and degrades
gracefully (raw path) if the cache isn't ready yet. Feeds guidance_beacon and
guidance_description.

## Declarations (in source order)

- L14 — `namespace acc::guidance`
- L20-23 — `kMaxNodes`, `kMaxEdges` (from engine_navgraph), `using PathPointSnapshot`
- L28 — `float DistXY(const Vector&, const Vector&)`
- L34 — `float DistXYSq(const Vector&, const Vector&)`
- L42 — `int FindNearestNode(const std::vector<PathPointSnapshot>&, const Vector& target)`
- L68 — `int FindBlockingEdge(const WallEdge*, int wallCount, const Vector& a, const Vector& b, float& outT)`
  note: diagnostic-only — identifies which wall edge rejected a smoothing skip, for log inspection.
- L100 — `bool ComputePath(void* area, const Vector& start, const Vector& goal, std::vector<Vector>& outWaypoints)` — public
  note: degenerate start==goal or same-nearest-node returns a single-point path; A* bounded at 2×nodeCount expansions against malformed graphs.
- L157 — `struct AStarNode` (local to ComputePath) — g/f/parent/closed/open
- L164 — `static AStarNode s_state[kMaxNodes]`
