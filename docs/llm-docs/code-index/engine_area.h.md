# engine_area.h (628 lines)

Per-area object iteration, handle resolution (server/client namespaces), object naming, per-kind sub-state predicates, global-variable/save-flag reads, map-pin CRUD, fog-of-war geometry, and the walkmesh wall-edge extraction API (BuildAreaWallCache / SegmentCrossesWalkmesh). Header carries most of the offset documentation quoted directly in engine_area.cpp; declares `AreaObjectIterator` and the `GameObjectKind`/`DoorMaterial` enums plus `WallEdge` struct.

## Declarations (in source order)

- L41 — `enum class GameObjectKind : int { Area=4, Creature=5, Item=6, Trigger=7, Projectile=8, Placeable=9, Door=10, AreaOfEffect=11, Waypoint=12, Encounter=13, Store=14, Sound=16 }`
- L56/62/66 — `GetCurrentArea()`, `GetObjectKind(void*)`, `GetObjectHandle(void*)`
- L74/80/86 — `ResolveServerObjectHandle`, `ResolveClientObjectHandle`, `ResolveClientObject` — two independent handle namespaces (server 0x000000XX vs client 0x800000XX high-bit)
- L89 — `bool GetObjectPosition(void* gameObject, Vector& out)`
- L94/103 — `GetRoomAtIndexed`, `GetRoomRepresentativeWorld`
- L107/112 — `GetAreaDisplayName`, `GetRoomDisplayName`
- L126 — `bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize)` — per-kind LocString table documented inline
- L133/139 — `GetObjectTag`, `GetAreaTag`
  note: area Tag is almost always the useless GFF default "untitled" — prefer GetCurrentAreaResName
- L147 — `bool GetCurrentAreaResName(char* outBuf, size_t bufSize)` — stable per-area id (module resref)
- L155 — `int ReadGlobalNumber(const char* name)` — -1 on fault
- L165 — `bool IsLoadingSaveGame()`
- L172 — `bool GetObjectDisplayNameByHandle(uint32_t handle, char* outBuf, size_t bufSize)`
- L184-186 — `IsUsablePlaceable`, `IsLandmarkWaypoint`, `IsTransitionTrigger`
- L194 — `int GetTriggerGeometry(void* trigger, Vector* out, int maxVerts)`
- L203 — `bool GetObjectLocalBoolean(void* gameObject, int index)` — fixed CSWVarTable @+0x110, NOT the named ScriptVarTable @+0x100
- L212 — `bool IsEmptyContainer(void* gameObject)`
- L216 — `bool IsDoorOpen(void* serverDoor)`
- L224 — `bool IsDoorStatic(void* serverDoor)`
- L234 — `bool MaybeDrivePassiveSelection()` — Endar Spire held-fade root-cause fix, called once per frame from OnUpdate
- L240 — `enum class DoorMaterial { Metal, Wood, Stone }`
- L243 — `DoorMaterial GetDoorMaterial(void* serverDoor)`
- L247/256 — `IsMapNoteEnabled`, `EnableMapNote`
- L261 — `void* GetAreaMap()`
- L265/274/284 — `IsWorldPointExplored`, `GetFogCellSizeM`, `GetMapRotateCCWFromWorldOrientation`
- L289/291/296/300/309/313/317 — `GetClientArea`, `GetMapPinCount`, `GetMapPinAt`, `GetMapPinPosition`, `GetMapPinFlags`, `IsMapPinEnabled`, `GetMapPinNoteText`
- L334 — `bool CreateMapPin(void* clientArea, const Vector& pos, const char* name, uint32_t referenceNumber, void** outPin)`
- L342 — `bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize)`
- L348 — `class AreaObjectIterator { explicit AreaObjectIterator(void* area); void* Next(); int SnapshotSize() const; }`
  note: snapshots data pointer + size at construction; safe for single-tick scans since game_objects rebuilds only on area-load
- L369-536 — file-scope (non-namespaced) offset/address constants: handle-resolution chain, map/fog-of-war chain, CSWCMapPin allocation chain, CSWSArea layout, per-subclass localized-name offsets, Pillar-4 sub-state offsets, trap detected-by lists, trigger geometry, walkmesh wall-edge struct layout (CSWRoomSurfaceMesh/CSWCollisionMesh/SurfaceMeshAdjacency)
- L548 — `bool TrapDetectedByAnyOf(void* gameObject, const uint32_t* ids, int idCount)`
- L556 — `struct WallEdge { Vector a; Vector b; int room_id; int material_id; }`
- L585 — `int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges)`
  note: outBuf==nullptr or maxEdges<=0 returns pre-filter discovered count for buffer sizing; cache once per area-load (immutable)
- L597 — `constexpr float kWallCrossZToleranceM = 2.0f`
- L620 — `bool SegmentCrossesWalkmesh(const WallEdge* walls, int wallCount, const Vector& a, const Vector& b, Vector& outHitPoint, bool ignoreZ = false)`
