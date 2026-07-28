# transitions.h (77 lines)

Pillar 2 — area + room transition announcements. Poll-based: compares
area pointer + wall_topology cluster id against the last observation each
tick and speaks on delta. Pre-load destination announce ("Loading: …")
goes through the OnSetMoveToModuleString entry hook. Exports the flat
landmark cache (proximity-based, fog-of-war respecting, NOT keyed by
.lyt-room) shared by the map cursor, view-mode, and marker auto-name
modules, plus the module-load-pending latch consumed by other per-tick
probes to avoid use-after-free during a module swap.

## Declarations (in source order)

- L17 — `namespace acc::transitions`
- L19 — `void Tick()`
- L24 — `void AnnouncePreLoadDestination(void* exoStringPtr)`
  note: SEH-guarded CExoString* read; deduplicates re-fires within 2s
- L32 — `bool FindLandmarkNear(const Vector& pos, float rangeM, char* nameOut, size_t nameBufSize, Vector& posOut, int* outLandmarkIdx = nullptr)`
  note: proximity scan over flat landmark cache; NOT keyed by lyt-room (K1 rooms over-segment)
- L39 — `bool IterateLandmarks(int& cursor, char* nameOut, size_t nameBufSize, Vector& posOut, int& outLandmarkIdx)`
  note: cursor=0 on first call; landmarkIdx is opaque key for MarkLandmarkClaimedByDoor
- L46 — `void MarkLandmarkClaimedByDoor(int landmarkIdx)`
  note: suppresses per-tick proximity announce when wall_topology already embedded landmark in a cluster label
- L51 — `bool IsResrefStyleRoomName(const char* name)`
  note: returns true for KOTOR resref-style names (m\d prefix, "stunt" prefix, or underscore); callers fall through to other tiers
- L57 — `bool IsWorldSpeechGated()`
  note: true when combat-active or UI-blocking panel; area-name transitions bypass this gate
- L66 — `bool IsModuleLoadPending()`
  note: true between SetMoveToModuleString firing and next fresh area pointer; guards CLYT::LoadLayout use-after-free
- L75 — `void NotifyExternalLoadStarting(const char* reason)`
  note: arms the same latch for a save-game LOAD from the menu, which bypasses SetMoveToModuleString; callers must gate on "no live player"
