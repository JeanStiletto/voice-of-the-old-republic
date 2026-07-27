# wall_topology.cpp (3404 lines)

Implements the "Path 3" nav-graph perceptual-region classifier: consumes
the engine's own per-area path_points/path_connections (the same graph
the engine uses for AI pathfinding), classifies each node by CSR-
adjacency degree (1=dead-end, 2 same-room=corridor, 2 cross-room=door
transition, >=3=junction), then merges hub clusters + absorbs straggler
nodes + collapses straight corridor chains into perceptual regions via a
union-find (UFFind/UFUnite). Absorbed the retired region_classifier's
walkmesh-probe primitives (ProbeWall/ProbeDistance/WalkmeshAgreesDeadEnd)
as a secondary sanity check on top of pure graph degree — e.g. a
graph-degree-1 dead-end only renders as one if the walkmesh alcove
signature agrees. Door snapshots stabilize over several ticks (a
partially-populated server-object array can drop late doors), and a
landmark-to-door attach pass folds waypoint map-note names into door exit
labels. Two prior approaches (face-graph flood-fill, parallel-pair sweep)
failed empirically on K1's authoring style; see commit history on this
branch. Talks to: engine_navgraph (NavGraphSnapshot), engine_area
(AreaObjectIterator, SegmentCrossesWalkmesh), spatial_change_detector
(GetCachedWalls), transitions (IterateLandmarks/MarkLandmarkClaimedByDoor).

## Declarations (in source order)

- L55/73 — `ProbeWall`/`ProbeDistance` — single-ray casts against the cached perimeter wall cache
- L104 — `bool IsAlcoveAlongAxis(...)` — dead-end alcove geometry signature check
- L145 — `void ProbeClearance8(...)` — 8-directional clearance probe (door diagnostics)
- L159-349 — tuning constants: snap radius (15m), Z-merge tolerance, straggler-absorb
  radius (16m), corridor straight-line cosine threshold, open-area ray count/radius,
  room-bounded thresholds, axis-elongation ratio (1.8), large-area extent (40m),
  local `kKind*` aliases mirroring the header's `enum Kind`
- L359 — `struct DoorRecord` / L379 `kMaxDoors=128`
- L390 — `struct TransitionRecord` / L398-403 `kMaxTransitions=16`, attach radius (12m)
- L405 — `struct AreaGraph` — the module's per-area cached decomposition (nodes, clusters, door/transition snapshots, node_label/node_pos/node_filtered arrays consulted by LookupAt)
- L466-508 — door-snapshot stability state (2-tick streak required, 60-tick retry cap)
- L508 — `int OctantBit(Id dir)` / L543 `kOctantEmitOrder[8]` — 8-way compass bit mapping for junction direction lists
- L551 — `int Degree(g, node)` — CSR-adjacency degree, the core classification signal
- L568 — `void ComputeNodeShapeFeatures(...)` — per-node geometric features feeding classification
- L674 — `void SnapshotDoors(area)` — scans area doors into DoorRecord[]
- L733 — `void LogDoorSnapshotDetails(area)` — 4-cardinal probe distances per door (diagnostic)
- L822 — `void SnapshotTransitionTriggers(area)` — area-transition trigger capture
- L887 — `std::string RenderTransitionExit(trigIdx, centroid)`
- L924 — `int FindDoorOnEdge(a, b)` — matches a graph edge to a door by geometry
- L976 — `struct EdgeResult` + classification counters (s_class_clear/door/blocked, caveat hit counts)
- L997 — `int FindDoorNearPoint(p, maxDistM)`
- L1129 — `std::string RenderDoorDirection(doorIdx, dirWord)`
- L1193 — `std::string RenderCorridorAxis(bitA, bitB, doorIdx)`
- L1261 — `bool WalkmeshAgreesDeadEnd(deadEndPos, parentPos)` — 4-ray alcove agreement gate
- L1270-1291 — `s_uf_parent[]`, `UFFind(x)`, `UFUnite(a,b)` — union-find backing cluster ids
- L1292/1305 — `AppendListEntry`/`DirEntry` — direction-list text composition helpers
- L1321 — `bool ComputeCentroidAxis(...)` — elongation axis for a merged cluster's centroid
- L1356 — `int AxisOctantMask(axisId)`
- L1403 — `void ClassifyCluster(...)` — THE core per-cluster labeler: junction octant-list
  rendering vs the merged-"Bereich" area path (door rewrites kept as navigational
  anchors, wall-curve degree-1 artefacts dropped), ~530 lines covering junction/
  corridor/dead-end/open-area/platz/room branches
- L1934 — `void LogNavWallCrossings(g)` — diagnostic cross-reference log
- L2045 — `void LogTopologyMetrics(g, ...)` — per-area summary stats
- L2126 — `void LogClusterMemberAdjacency(g, ...)` — diagnostic
- L2206 — `void AttachLandmarksToDoors(area)` — public; folds waypoint map-note names into matching door exit labels
- L2305 — `void Reset()` — public; clears the cached AreaGraph
- L2319 — `bool HasGraphForArea(area)` — public
- L2325 — `void MaybeRefreshDoors(area)` — public; re-snapshots until the door count holds for kDoorStabilityRequiredStreak ticks
- L2398 — `void BuildForArea(area)` — public; the ~780-line graph build: degree
  classification, hub-cluster merge, straggler absorption, corridor-chain collapse,
  door snapshot + transition-trigger snapshot, landmark attach
- L3181 — `void DumpGraphToLog()` — public
- L3197 — `bool GetClusterInfo(area, clusterId, outKind, outExtentM)` — public
- L3212 — `bool LookupAt(area, worldPos, outLabel, outSig, outClusterId, allowDiagLog, requireWallReachable)` — public
  note: nearest-node search is 2D only (graph nodes are always z=0 — including dz broke elevated areas like Taris Sewers, where the upper platform alone exceeds the 15m snap radius); tracks a wall-filtered "rescue" candidate separately from the reachable primary so a walled-off alcove can still resolve when no other labelled cluster is nearby
