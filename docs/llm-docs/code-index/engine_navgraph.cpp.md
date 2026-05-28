# engine_navgraph.cpp (166 lines)

Implementation of engine_navgraph.h. Leading comment notes this was extracted from guidance_pathfind.cpp; behaviour preserved verbatim, only the snapshot container changed (vector replaces static buffers).

## Declarations (in source order)

- L17 — `namespace acc::engine::navgraph`
- L19 — `namespace { ... }` (anonymous helpers)
- L22 — `template <typename T> bool SafeRead(void* base, size_t offset, T& out)`
  note: SEH-guarded scalar read; same pattern as engine_area
- L33 — `struct GraphMeta`
- L44 — `bool ReadMeta(void* area, GraphMeta& out)`
  note: validates pointer plausibility (0x00100000..0x80000000 range) to catch mid-teardown states
- L68 — `int LoadPoints(const GraphMeta& meta, std::vector<PathPointSnapshot>& out)`
  note: per-entry SEH; truncates on fault (partial snapshot still usable by A*)
- L98 — `int LoadConnections(const GraphMeta& meta, std::vector<uint32_t>& out)`
  note: bulk memcpy first, falls back to byte-by-byte SEH copy on fault
- L130 — `bool SnapshotNavGraph(void* area, NavGraphSnapshot& out)`
- L147 — `void NeighbourRange(const NavGraphSnapshot& g, int node, int& outLo, int& outHi)`
