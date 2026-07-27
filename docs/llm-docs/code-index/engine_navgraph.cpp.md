# engine_navgraph.cpp (175 lines)

Implementation of engine_navgraph.h. Extracted 2026-05-13 from
guidance_pathfind.cpp's anonymous-namespace helpers; behaviour preserved
verbatim — the only mechanical changes are the snapshot container (vector
replaces static buffers) and the namespace move.

## Declarations (in source order)

- L17 — `namespace acc::engine::navgraph`
- L19 — `namespace { ... }` (anonymous helpers)
- L22 — `template <typename T> bool SafeRead(void* base, size_t offset, T& out)`
  note: SEH-guarded scalar read, same pattern as engine_area's helpers.
- L33 — `struct GraphMeta { uint32_t pointsCount; void* pointsPtr; uint32_t connsCount; void* connsPtr; }`
- L44 — `bool ReadMeta(void* area, GraphMeta& out)`
  note: rejects heap pointers below 0x00100000 or >= 0x80000000 — catches mid-tear-down / uninitialised-area states before the deref loop below.
- L68 — `int LoadPoints(const GraphMeta& meta, std::vector<PathPointSnapshot>& out)`
  note: PathPoint is 2D only (x@0x00, y@0x04, csr_offset@0x08c) — NO height field; reading a full Vector would land the connections count in pos.z as denormal garbage. Per-entry SEH fault truncates the load.
- L107 — `int LoadConnections(const GraphMeta& meta, std::vector<uint32_t>& out)`
  note: bulk memcpy first; on fault falls back to byte-by-byte SEH-safe copy, stopping at the first faulted byte.
- L139 — `bool SnapshotNavGraph(void* area, NavGraphSnapshot& out)`
- L156 — `void NeighbourRange(const NavGraphSnapshot& g, int node, int& outLo, int& outHi)`
