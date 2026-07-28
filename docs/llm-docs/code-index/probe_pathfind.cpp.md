# probe_pathfind.cpp (389 lines)

Diagnostic probe locating where the engine stores its computed pathfinding
solution. F9 dispatches `guidance::WalkTo` to a point 10m ahead of the player
along current heading, then dumps `CSWSCreature::path_find_info`
(`CPathfindInformation*` at +0x340 — a pointer, not the struct itself; an
earlier probe pass wrongly dumped the pointer slot as if it were the struct)
across a tick cascade (t+100/500/1500/3500ms) to diff which bytes the
pathfinder populates. Also dumps the per-area nav graph
(`CSWSArea.path_points`/`path_connections`, paired ulong-count + pointer, NOT
a CExoArrayList) and runs a heuristic "triple-scan" over the PFI struct
looking for any {ptr,size,alloc}-shaped CExoArrayList elsewhere in the layout.
Feeds directly into the Pillar 3 design fork: read the engine's own path
solution vs. re-solving A* ourselves.

## Declarations (in source order)

- L22 — `constexpr int kVK_F9 = 0x78`
- L29 — `constexpr size_t kCreaturePathFindInfoPtrOffset = 0x340`
- L34 — `constexpr size_t kPfiDumpLen = 0x280`
  note: CPathfindInformation is 0x278 bytes per swkotor.exe.h:9621; dumps a bit beyond to catch tail data.
- L44-48 — CPathfindInformation candidate offsets `kPfiStartPointOffset=0x60; kPfiEndPointOffset=0x74; kPfiPathCountOffset=0x8c; kPfiPathsPtrOffset=0x90; kPfiCreatureObjIdOffset=0x2c`
  note: `?`-suffixed Ghidra guess-typed fields — logged, not trusted, until data confirms.
- L57-60 — CSWSArea nav-graph offsets `kAreaPathPointsCountOffset=0x238; kAreaPathPointsPtrOffset=0x23c; kAreaPathConnectionsCountOffset=0x240; kAreaPathConnectionsPtrOffset=0x244`
- L62-75 — `struct ProbeState { active, dispatchTick, creatureAtPress, pfiAtPress, fired100/500/1500/3500 }; ProbeState g_state; bool g_prevF9`
- L79 — `bool IsForegroundOurs()`
- L89 — `uint32_t SafeReadU32(void* base, size_t offset, bool& ok)`
- L103 — `Vector SafeReadVector(void* base, size_t offset, bool& ok)`
- L117 — `size_t SafeBulkRead(void* src, void* dst, size_t len)`
  note: tries a bulk SEH-guarded memcpy, falls back to byte-by-byte on fault to salvage a partial read.
- L139 — `void DumpPathfindInformation(const char* tag, void* pfi)`
  note: bulk hex-dumps the struct, decodes named fields, and conditionally dereferences pfi.paths? if it looks like a plausible heap pointer with a small count.
- L227 — `void DumpAreaNavGraph(const char* tag, void* area)`
- L274 — `void* DerefPathfindInfo(void* creature)`
- L282 — `void DumpCheckpoint(const char* checkpointTag)`
  note: logs if the creature or PFI pointer changed mid-cascade; disarms if the player creature is lost.
- L308 — `void PollWin32()`
  note: F9 = ProbePathfind; computes a 10m-ahead target from player yaw, dispatches WalkTo, arms the checkpoint cascade.
- L364 — `void Tick()`
  note: fires DumpCheckpoint at t+100/500/1500ms, and a final one at t+3500ms that also disarms.
