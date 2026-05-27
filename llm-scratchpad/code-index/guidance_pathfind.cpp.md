# guidance_pathfind.cpp (371 lines)

Implementation of the A* pathfinder over the engine's static per-area nav graph. Includes nearest-node resolution, A* with linear open-set scan, parent-chain reconstruction, and a string-pulling smoothing pass that uses the cached walkmesh walls.

## Declarations (in source order)

- L14 — `namespace acc::guidance`
- L16 — `namespace { // anonymous`
- L28 — `float DistXY(const Vector& a, const Vector& b)`
- L34 — `float DistXYSq(const Vector& a, const Vector& b)`
- L42 — `int FindNearestNode(const std::vector<PathPointSnapshot>& nodes, const Vector& target)`
  note: linear scan; N <= kMaxNodes
- L60 — `int FindBlockingEdge(const acc::engine::WallEdge* walls, int wallCount, const Vector& a, const Vector& b, float& outT)`
  note: diagnostic only (smoothing-pass logging); finds the closest walkmesh edge that blocks a→b; returns -1 when clear
- L98 — `} // namespace (anonymous)`
- L100 — `bool ComputePath(void* area, const Vector& start, const Vector& goal, std::vector<Vector>& outWaypoints)`
  note: inline struct AStarNode at L157; static s_state[kMaxNodes] scratch buffer at L164; string-pull smoothing pass at L278
