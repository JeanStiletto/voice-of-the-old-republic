# engine_area.cpp (1902 lines)

The largest engine_* module: per-area object iteration, handle resolution (server + client namespaces), object naming/tagging, door/waypoint/trigger sub-state, global-variable + save-load-flag reads, map-pin CRUD, area-map fog-of-war geometry, and the walkmesh wall-edge extraction pipeline (Pillar 1 foundation: per-room triangle scan, cross-room portal-coincidence filtering, same-room duplicate dedup) plus a segment-vs-walkmesh crossing test. Also owns `MaybeDrivePassiveSelection`, a root-cause fix that directly drives DoPassiveSelection when the Endar Spire opening cutscene holds the fade panel obscuring (freezing Q/E halo + passive narration) while the player retains world control. Talks to engine_player, engine_reads, map_note_renames, strings, engine_rebase, log.

## Declarations (in source order)

- L19-25 — `PFN_CSWSAreaGetRoom`, `PFN_GetObjectArray`, `PFN_GetGameObject` typedefs
- L31 — `void* GetServerObjectArray()` — AppManager→CServerExoApp→GetObjectArray chain
- L49 — `void* GetCurrentArea()` (public) — wraps GetPlayerArea
- L53 — `int GetObjectKind(void* gameObject)` (public) — single-byte read at +0x8
- L63 — `uint32_t GetObjectHandle(void* gameObject)` (public) — CGameObject.id @+0x4
- L82 — `bool IsSentinelHandle(uint32_t handle)` — 0 / 0xFFFFFFFF / 0x7F000000
- L88 — `void* ResolveServerObjectHandle(uint32_t handle)` (public)
  note: GetGameObject returns *false on hit, true on miss* (inverted-bool convention)
- L117/131 — `PFN_CClientGetGameObject`, `GetClientExoApp()` (local)
- L131 — `void* ResolveClientObject(uint32_t handle)` (public)
- L146 — `void* ResolveClientObjectHandle(uint32_t handle)` (public) — chains +0xf8 server_object
- L161 — `bool GetObjectPosition(void* gameObject, Vector& out)` (public) — CSWSObject+0x90
- L173 — `void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex)` (public) — CSWSArea::GetRoom
- L186 — `bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld, int* outFailReason)` (public)
  note: middle-face centroid transformed via CollisionMeshLocalToWorld; fail codes 1-6 documented in header
- L260 — `bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize)` (public) — LocString then tag fallback
- L279/289 — `bool GetObjectTag(...)`, `bool GetAreaTag(...)` (public)
- L299 — `bool GetRoomDisplayName(void* area, int roomIndex, char* outBuf, size_t bufSize)` (public) — non-localized .lyt room ids
- L319 — `AreaObjectIterator::AreaObjectIterator(void* area)` (public ctor) — snapshots handles_/size_ at construction
- L343 — `bool TryReadLocString(...)`, L351 `bool TryReadTag(...)`, L361 `void AppendCommaSeparated(...)`
- L385 — `void BuildDoorSuffix(void* serverDoor, char* outBuf, size_t bufSize)`
  note: static doors labelled "kosmetisch" and skip state/dest/description entirely; else state → transition dest → description, comma-joined
- L442 — `static bool TryResolveDisplayNameOnce(void* clientApp, uint32_t handle, char* outBuf, size_t bufSize)`
- L467 — `bool GetObjectDisplayNameByHandle(uint32_t handle, char* outBuf, size_t bufSize)` (public)
  note: tries handle as-is, then retries with 0x80000000 high bit set if server-side handle failed (client-only resolver)
- L509 — `bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize)` (public)
  note: per-kind LocString switch (Door/Creature/Placeable/Item/Waypoint/Trigger); Creature prefers GetObjectDisplayNameByHandle over raw stats read (handles empty first_name via template/appearance/racial fallbacks); Waypoint prefers curated map-note label over LocName; falls back to raw tag universally
- L613 — `bool IsUsablePlaceable(void* placeable)` (public)
- L625 — `bool IsEmptyContainer(void* gameObject)` (public) — gates kind==Placeable first, then has_inventory + repo item count
- L655/665 — `bool IsLandmarkWaypoint(...)`, `bool IsTransitionTrigger(...)` (public)
  note: IsTransitionTrigger mirrors ExtractTextOrStrRef's resolution order rather than the old buggy Vector-reinterpret-as-float probe (0xFFFFFFFF sentinel decoded as NaN, misclassifying every destination-less trigger as a transition)
- L697 — `int GetTriggerGeometry(void* trigger, Vector* out, int maxVerts)` (public) — sanity-caps 3..32 verts
- L716 — `bool GetObjectLocalBoolean(void* gameObject, int index)` (public) — fixed CSWVarTable bitfield, NOT the named ScriptVarTable
- L728 — `bool TrapDetectedByAnyOf(void* gameObject, const uint32_t* ids, int idCount)` (public) — kind-dispatched detected-by list scan
- L759/769 — `bool IsDoorOpen(...)`, `bool IsDoorStatic(...)` (public)
- L801 — `bool MaybeDrivePassiveSelection()` (public)
  note: root-cause fix for the held-obscuring-fade freeze; drives DoPassiveSelection@0x005fa5a0 directly only when the ONLY blocker is a finished-but-held fade with normal world input
- L873-943 — `kDoorMaterialTable[65]` — generic_type → DoorMaterial(Metal/Wood/Stone), joined from genericdoors.2da × placeableobjsnds.2da
- L945 — `DoorMaterial GetDoorMaterial(void* serverDoor)` (public)
- L958/969 — `bool IsMapNoteEnabled(...)`, `bool EnableMapNote(...)` (public) — replicates ExecuteCommandSetMapPinEnabled(TRUE)
- L985 — `bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize)` (public) — curated rename table takes priority over raw TLK text
- L1009-1048 — `PFN_CServerExoApp_GetModule`, `PFN_CSWSAreaMap_IsWorldPointExplored`, `PFN_CSWSAreaMap_GetMapRotateCCW`, `GetServerApp()`, `void* GetAreaMap()` (public)
- L1050 — `bool GetCurrentAreaResName(char* outBuf, size_t bufSize)` (public) — via CSWSModule::GetModuleResourceName (deliberately leaks the engine-allocated c_string)
- L1077 — `int ReadGlobalNumber(const char* name)` (public) — CServerExoApp global-variable-table read, returns -1 on any fault
- L1100 — `bool IsLoadingSaveGame()` (public) — CServerExoApp::GetLoadFromSaveGame
- L1115/1126/1150 — `bool IsWorldPointExplored(...)`, `bool GetFogCellSizeM(...)`, `bool GetMapRotateCCWFromWorldOrientation(...)` (public)
  note: GetMapRotateCCW's engine return is x87 float10 via ST(0); double-typed PFN is binary-equivalent
- L1165/1176/1187/1200/1211/1221/1232 — `GetClientArea`, `GetMapPinCount`, `GetMapPinAt`, `GetMapPinPosition`, `GetMapPinFlags`, `IsMapPinEnabled`, `GetMapPinNoteText` (public) — CSWCMapPin field accessors
- L1264 — `bool CreateMapPin(void* clientArea, const Vector& pos, const char* name, uint32_t referenceNumber, void** outPin)` (public)
  note: replicates operator_new(0x110)→ctor→field-writes→AddMapPin; leaks 0x110 bytes on mid-path SEH fault rather than risk mismatched delete
- L1318-1375 — `PFN_CollisionMeshLocalToWorld`, `kMinEdgeXYLengthSq`, `GetRoomSurfaceMesh`, `ReadFaceVertexIndices`, `TransformEdgeEndpoints`
- L1380 — `int ScanRoomWallEdges(void* surfaceMesh, int roomId, WallEdge* outBuf, int maxEdges, int alreadyWritten)` — per-room adjacency==-1 triangle-edge scan
- L1493-1496 — `kMaxGlobalTriEdges=16384`, `s_globalEdgeA/B/Room[]` — global triangle-edge index for the portal filter (static, single-threaded, overwritten per call)
- L1503 — `int ScanRoomAllTriangleEdges(void* surfaceMesh, int roomId, int alreadyWritten)` — emits every triangle edge regardless of adjacency
- L1561 — `int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges)` (public)
  note: per-room scan → cross-room portal-coincidence filter (drops edges matched in ANY other room's triangle list, superset of the old symmetric-pair filter) → same-room duplicate dedup (non-manifold faces / step+slanted-face pairs, distinguished from multi-floor walls via the 3D-shared-endpoint requirement)
- L1795 — `bool SegmentCrossesWalkmesh(const WallEdge* walls, int wallCount, const Vector& a, const Vector& b, Vector& outHitPoint, bool ignoreZ)` (public)
  note: 2D XY segment intersection + optional 3D same-floor z-guard (kWallCrossZToleranceM=2.0m); ignoreZ for callers with untrustworthy endpoint z (waypoint smoother)
- L1870 — `void* AreaObjectIterator::Next()` (public)
