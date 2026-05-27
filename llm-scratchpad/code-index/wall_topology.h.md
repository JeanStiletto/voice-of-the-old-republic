# wall_topology.h (81 lines)

Single source of truth for "spoken label for the region at world position P". Nav-graph topology classifier (degree → dead-end/corridor/junction/open-area/platz). Replaces earlier walkmesh/lyt-room classifier. Consumed by transitions, view_mode, and map_ui_cursor.

## Declarations (in source order)

- L22 — `namespace acc::wall_topology`
- L27 — `enum Kind`
  note: KindDeadEnd=0, KindCorridor=1, KindJunction=2, KindTransition=3 (retired), KindOpenArea=4, KindPlatz=5
- L41 — `constexpr int kClusterIdNone`
- L42 — `constexpr int kClusterIdOpenArea`
- L46 — `void BuildForArea(void* area)`
  note: idempotent on same area pointer; no-ops until wall cache populated
- L51 — `void MaybeRefreshDoors(void* area)`
  note: re-snapshots doors until count stabilises; commits after N ticks; no-op once committed
- L53 — `bool HasGraphForArea(void* area)`
- L55 — `void Reset()`
- L73 — `bool LookupAt(void* area, const Vector& worldPos, char* outBuf, size_t bufSize, int& outSig, int& outClusterId, bool allowDiagLog = true, bool requireWallReachable = true)`
  note: outSig low byte = Kind; outClusterId = UFFind root; requireWallReachable=false for map cursor (cursor can sit off-walkmesh)
- L79 — `void DumpGraphToLog()`
