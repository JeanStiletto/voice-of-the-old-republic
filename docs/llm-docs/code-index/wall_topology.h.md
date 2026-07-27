# wall_topology.h (104 lines)

Single source of truth for "spoken label for the region at world position
P" — consumed by transitions (player), view_mode (cursor), and
map_ui_cursor (inverse-projected), so all three agree on corridor vs
junction vs dead-end. Algorithm: classify per-node by CSR-adjacency
degree on the engine's own nav graph (path_points/path_connections, the
same graph the engine trusts for pathfinding), then collapse hub clusters
and straight corridor chains into perceptual regions. Replaced an earlier
per-.lyt-room walkmesh-probe classifier that over-segmented KOTOR into
corridor-sized cells with frequent centroid-in-wall failures.

## Declarations (in source order)

- L23 — `namespace acc::wall_topology`
- L30-37 — `enum Kind` — DeadEnd=0, Corridor=1, Junction=2, OpenArea=4, Platz=5, Room=6
  note: value 3 is unused (a retired kind); KindRoom (merged small space) and KindPlatz (merged big space) render the SAME neutral "Bereich" label — the split only drives transitions.cpp's immediate-vs-deferred announce distinction, never surfaced to the player
- L44-45 — `kClusterIdNone = -1` (no fact yet) / `kClusterIdOpenArea = -2` (synthetic neutral-"Bereich" fallback, stable identity)
- L49 — `void BuildForArea(void* area)`
  note: idempotent on same area pointer; no-ops until the wall-edge cache is populated
- L54 — `void MaybeRefreshDoors(void* area)`
  note: re-snapshots doors until the count stabilises (initial snapshot can race a partially-populated server-object array)
- L61 — `void AttachLandmarksToDoors(void* area)`
  note: also invoked by transitions.cpp after a mid-session landmark-cache rebuild, since map notes can be script-enabled long after area entry
- L63 — `bool HasGraphForArea(void* area)`
- L65 — `void Reset()`
- L87 — `bool LookupAt(void* area, const Vector& worldPos, std::string& outLabel, int& outSig, int& outClusterId, bool allowDiagLog = true, bool requireWallReachable = true)`
  note: outSig low byte = Kind; outClusterId = UFFind root (stable region-change trigger key); requireWallReachable=false for the map cursor (LOS gating would wrongly reject off-walkmesh cursor positions); outLabel cleared unconditionally, even on a false return
- L98 — `bool GetClusterInfo(void* area, int clusterId, int& outKind, float& outExtentM)`
  note: footprint extent = longest bounding-box side of member nodes, metres; lets transitions.cpp dwell-gate sub-perceptual clusters without a second LookupAt
- L101 — `void DumpGraphToLog()`
