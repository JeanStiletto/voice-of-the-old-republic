# probe_pathfind.cpp (388 lines)

Implementation of the pathfinding RE probe. Heavy structural dumping:
CPathfindInformation as raw hex + named-field decode, *pfi.paths? array
dereference with 16-byte stride interpretation, triple-scan heuristic
for other CExoArrayList-shaped triples in the PFI struct, and the
CSWSArea nav graph (path_points + path_connections counts + pointer +
raw sample data).

Key correction vs earlier attempts: CSWSCreature+0x340 is a pointer to
CPathfindInformation, not the struct itself (struct is 0x278 bytes,
lives behind the deref).

## Declarations (in source order)

- L18 — `namespace acc::probe_pathfind`
- L62 — `struct ProbeState`
  note: active flag + dispatchTick + creature/pfi snapshots + four fired-N booleans for the cascade checkpoints
- L80 — `bool IsForegroundOurs()`
- L89 — `uint32_t SafeReadU32(void* base, size_t offset, bool& ok)`
- L103 — `Vector SafeReadVector(void* base, size_t offset, bool& ok)`
- L117 — `size_t SafeBulkRead(void* src, void* dst, size_t len)`
  note: bulk memcpy with SEH fallback to byte-by-byte copy; returns bytes successfully read
- L139 — `void DumpPathfindInformation(const char* tag, void* pfi)`
  note: logs hex dump of 0x280-byte PFI struct + named-field decode + conditional deref of paths? array + triple-scan heuristic
- L227 — `void DumpAreaNavGraph(const char* tag, void* area)`
  note: logs path_points/path_connections counts + pointers + hex samples
- L274 — `void* DerefPathfindInfo(void* creature)`
  note: reads the CPathfindInformation* at creature+0x340 (it's a pointer, not inline struct)
- L282 — `void DumpCheckpoint(const char* checkpointTag)`
  note: cascade tick worker; detects creature/pfi pointer changes mid-cascade, calls DumpPathfindInformation
- L308 — `void PollWin32()`
- L364 — `void Tick()`
