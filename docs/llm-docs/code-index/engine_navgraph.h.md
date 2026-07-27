# engine_navgraph.h (48 lines)

CSWSArea nav-graph reader — shared snapshot helpers. Pure read,
SEH-guarded. Used by guidance_pathfind and wall_topology; the offset
constants live in guidance_pathfind.h (one canonical location). Documents
PathPoint (16 bytes: Vector position + uint32 csr_offset) and CSR
adjacency: neighbours of node N are path_connections[meta_N..meta_{N+1}),
symmetric/undirected edges.

## Declarations (in source order)

- L20 — `namespace acc::engine::navgraph`
- L22 — `struct PathPointSnapshot { Vector pos; uint32_t csrOffset; }`
- L29 — `struct NavGraphSnapshot { std::vector<PathPointSnapshot> nodes; std::vector<uint32_t> conns; }`
- L36 — `constexpr int kMaxNodes = 512`
- L37 — `constexpr int kMaxEdges = kMaxNodes * 8`
- L41 — `bool SnapshotNavGraph(void* area, NavGraphSnapshot& out)`
  note: true on non-empty snapshot; partial-node faults truncate (A* over the prefix still works, wall_topology degrades to "open area") rather than fail outright.
- L45 — `void NeighbourRange(const NavGraphSnapshot& g, int node, int& outLo, int& outHi)`
  note: bounds-checked against malformed CSR offsets; returns empty range instead of indexing out of bounds.
