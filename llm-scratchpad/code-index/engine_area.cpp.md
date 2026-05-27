# engine_area.cpp (>400 lines, file not fully read past L400)

Implementation of engine_area.h. No separate leading comment block (header file carries the documentation).

## Declarations (in source order)

- L13 — `namespace acc::engine`
- L15 — `namespace { ... }` (anonymous, TU-local helpers)
- L17 — `typedef void* (__thiscall* PFN_CSWSAreaGetRoom)(void*, Vector*, int*)`
- L20 — `typedef void* (__thiscall* PFN_GetObjectArray)(void*)`
- L22 — `typedef bool (__thiscall* PFN_GetGameObject)(void*, uint32_t, void**)`
- L29 — `void* GetServerObjectArray()`
  note: walks AppManager +0x8 → CServerExoApp → GetObjectArray; SEH-guarded
- L47 — `void* GetCurrentArea()`
- L51 — `int GetObjectKind(void* gameObject)`
- L61 — `uint32_t GetObjectHandle(void* gameObject)`
- L75 — `namespace { ... }` (IsSentinelHandle helper)
- L80 — `bool IsSentinelHandle(uint32_t handle)`
  note: covers 0, 0xFFFFFFFF, and 0x7F000000 (kInvalidObjectId)
- L86 — `void* ResolveServerObjectHandle(uint32_t handle)`
  note: GetGameObject returns false on hit, true on miss — inverted bool convention
- L107 — `namespace { ... }` (client resolver helpers)
- L113 — `typedef void* (__thiscall* PFN_CClientGetGameObject)(void*, uint32_t)`
- L115 — `void* GetClientExoApp()`
- L129 — `void* ResolveClientObjectHandle(uint32_t handle)`
- L156 — `bool GetObjectPosition(void* gameObject, Vector& out)`
- L168 — `void* GetRoomAt(void* area, const Vector& pos)`
- L179 — `void* GetRoomAtIndexed(void* area, const Vector& pos, int& outIndex)`
- L192 — `bool GetRoomRepresentativeWorld(void* area, int roomIdx, Vector& outWorld, int* outFailReason)`
- L266 — `bool GetAreaDisplayName(void* area, char* outBuf, size_t bufSize)`
- L285 — `bool GetRoomDisplayName(void* area, int roomIndex, char* outBuf, size_t bufSize)`
- L305 — `AreaObjectIterator::AreaObjectIterator(void* area)`
- L324 — `namespace { ... }` (TryReadLocString, TryReadTag, AppendCommaSeparated, BuildDoorSuffix helpers)
- L329 — `bool TryReadLocString(void* base, size_t locStringOffset, char* outBuf, size_t bufSize)`
- L335 — `bool TryReadTag(void* obj, char* outBuf, size_t bufSize)`
- L344 — `void AppendCommaSeparated(char* outBuf, size_t bufSize, const char* text)`
- L371 — `void BuildDoorSuffix(void* serverDoor, char* outBuf, size_t bufSize)`
  note: emits ", verriegelt" / ", offen" / ", <transition>" / ", <description>" suffix for door narration
