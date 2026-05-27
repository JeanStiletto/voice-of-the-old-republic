# transitions.h (68 lines)

Pillar 2 — area + room transition announcements. Poll-based. Pre-load destination announce via OnSetMoveToModuleString detour. Exports the flat landmark cache (proximity-based, fog-of-war respecting) shared by map cursor, view mode, and marker auto-name.

## Declarations (in source order)

- L17 — `namespace acc::transitions`
- L19 — `void Tick()`
- L24 — `void AnnouncePreLoadDestination(void* exoStringPtr)`
  note: SEH-guarded CExoString* read; deduplicates re-fires within 2s
- L33 — `bool FindLandmarkNear(const Vector& pos, float rangeM, char* nameOut, size_t nameBufSize, Vector& posOut, int* outLandmarkIdx = nullptr)`
  note: proximity scan over flat landmark cache; NOT keyed by lyt-room
- L40 — `bool IterateLandmarks(int& cursor, char* nameOut, size_t nameBufSize, Vector& posOut, int& outLandmarkIdx)`
  note: cursor=0 on first call; landmarkIdx is opaque key for MarkLandmarkClaimedByDoor
- L46 — `void MarkLandmarkClaimedByDoor(int landmarkIdx)`
  note: suppresses per-tick proximity announce when wall_topology already embedded landmark in a cluster label
- L51 — `bool IsResrefStyleRoomName(const char* name)`
  note: returns true for KOTOR resref-style names (m\d prefix or underscore); callers fall through to other tiers
- L57 — `bool IsWorldSpeechGated()`
  note: true when combat-active or UI-blocking panel; area-name transitions bypass this gate
- L65 — `bool IsModuleLoadPending()`
  note: true between SetMoveToModuleString and next fresh area pointer; guards against use-after-free in CLYT::LoadLayout
