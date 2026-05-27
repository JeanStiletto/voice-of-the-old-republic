# engine_navgraph.h (48 lines)

CSWSArea nav-graph reader — shared snapshot helpers. Pure read, SEH-guarded. Offset constants live in guidance_pathfind.h (one canonical location). Describes PathPoint (16 bytes) and CSR adjacency layout.

## Declarations (in source order)

- L20 — `namespace acc::engine::navgraph`
- L22 — `struct PathPointSnapshot`
- L28 — `struct NavGraphSnapshot`
  note: nodes = PathPointSnapshots, conns = flat CSR adjacency uint32 array
- L36 — `constexpr int kMaxNodes = 512`
- L37 — `constexpr int kMaxEdges = kMaxNodes * 8`
- L41 — `bool SnapshotNavGraph(void* area, NavGraphSnapshot& out)`
  note: true on non-empty snapshot; partial-node faults truncate rather than fail
- L45 — `void NeighbourRange(const NavGraphSnapshot& g, int node, int& outLo, int& outHi)`
  note: bounds-checked against malformed CSR offsets; returns empty range rather than indexing out of bounds
