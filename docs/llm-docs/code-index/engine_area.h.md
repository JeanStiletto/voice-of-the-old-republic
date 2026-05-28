# engine_area.h (427 lines)

Per-area object iteration and room lookup. SEH-guarded; no engine re-entry beyond CSWSArea::GetRoom. Documents handle resolution chain (server vs client paths), CSWSArea layout, walkmesh wall-edge extraction offsets, and map-pin creation.

## Declarations (in source order)

- L36 — `namespace acc::engine`
- L39 — `enum class GameObjectKind : int`
  note: GAME_OBJECT_TYPES at CSWSObject +0x8; do not reorder — engine values
- L55 — `void* GetCurrentArea()`
- L61 — `int GetObjectKind(void* gameObject)`
  note: reads ONE byte; wide reads corrupt results for objects with non-zero adjacent field bytes
- L65 — `uint32_t GetObjectHandle(void* gameObject)`
- L73 — `void* ResolveServerObjectHandle(uint32_t handle)`
- L79 — `void* ResolveClientObjectHandle(uint32_t handle)`
  note: for handles with high bit set (0x800000XX); server-side CGameObjectArray won't find these
- L82 — `bool GetObjectPosition(void* gameObject, Vector& out)`
- L85 — `void* GetRoomAt(void* area, const Vector& pos)`
- L89 — `void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex)`
- L97 — `bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld, int* outFailReason = nullptr)`
- L102 — `bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize)`
- L108 — `bool GetRoomDisplayName(void* area, int roomIndex, char* outBuf, size_t bufSize)`
- L122 — `bool GetObjectName(void* gameObject, char* outBuf, size_t bufSize)`
- L128 — `bool GetObjectDisplayNameByHandle(uint32_t handle, char* outBuf, size_t bufSize)`
  note: uses engine's universal accessor with racialtypes.2da/appearance.2da fallbacks; prefer over GetObjectName for handle-based callers
- L137 — `bool IsUsablePlaceable(void* placeable)`
- L138 — `bool IsLandmarkWaypoint(void* waypoint)`
- L139 — `bool IsTransitionTrigger(void* trigger)`
- L143 — `bool IsDoorOpen(void* serverDoor)`
- L149 — `enum class DoorMaterial { Metal, Wood, Stone }`
- L152 — `DoorMaterial GetDoorMaterial(void* serverDoor)`
- L155 — `bool IsMapNoteEnabled(void* waypoint)`
- L161 — `void* GetAreaMap()`
- L165 — `bool IsWorldPointExplored(void* areaMap, const Vector& pos)`
- L172 — `bool GetMapRotateCCWFromWorldOrientation(void* areaMap, const Vector& orientation, float& outDegCCW)`
- L180 — `void* GetClientArea(void* serverArea)`
- L182 — `int GetMapPinCount(void* clientArea)`
- L187 — `void* GetMapPinAt(void* clientArea, int i)`
- L191 — `bool GetMapPinPosition(void* mapPin, Vector& out)`
- L196 — `uint32_t GetMapPinFlags(void* mapPin)`
- L201 — `bool IsMapPinEnabled(void* mapPin)`
- L204 — `bool GetMapPinNoteText(void* mapPin, char* outBuf, size_t bufSize)`
- L221 — `bool CreateMapPin(void* clientArea, const Vector& pos, const char* name, uint32_t referenceNumber, void** outPin = nullptr)`
  note: in-area only; pins vanish on area transition; replicates engine's HandleServerToPlayerMapPinReferenceNumber pattern
- L228 — `bool GetWaypointMapNote(void* waypoint, char* outBuf, size_t bufSize)`
- L235 — `class AreaObjectIterator`
  note: snapshots game_objects array at construction; safe for single-tick scans; resolves handles via CGameObjectArray
- L241 — `void* AreaObjectIterator::Next()`
  note: skips 0-handles and GetGameObject misses (engine uses some as sentinels during area-unload)
- L243 — `int AreaObjectIterator::SnapshotSize() const`
- L373 — `struct WallEdge`
  note: one perimeter walkmesh edge with world-space endpoints; room_id and material_id included
- L409 — `int BuildAreaWallCache(void* area, WallEdge* outBuf, int maxEdges)`
  note: null outBuf returns pre-filter discovered count for buffer sizing; non-null writes post-seam-filter edges (real walls only)
- L424 — `bool SegmentCrossesWalkmesh(const WallEdge* walls, int wallCount, const Vector& a, const Vector& b, Vector& outHitPoint)`
