# transitions.cpp (977 lines)

Transitions implementation. Area-change detection, cluster-based room-change detection (wall_topology cluster ids, kClusterStabilityTicks=5), Platz deferred-announce (kPlatzDelayMs=1000ms), proximity landmark scan (kLandmarkEnterRangeM=8m), fog-of-war filtering. Two-tier room-speech resolution: friendly room name (IsResrefStyleRoomName filter) → wall_topology shape.

## Declarations (in source order)

- L23 — `namespace acc::transitions`
- L121 — `static void SpeakArea(void* area)` (anonymous namespace)
- L139 — `static void RebuildLandmarkCache(void* area)` (anonymous namespace)
- L281 — `static bool ResolveRoomSpeech(void* area, const Vector& worldPos, char* outBuf, size_t bufSize, const char*& outSource, int* outClusterIdOpt = nullptr)` (anonymous namespace)
- L349 — `static void LogWallTopoComparison(void* area, int roomIndex, const Vector& worldPos, const char* spoken, const char* source)` (anonymous namespace)
- L385 — `static void SpeakRoomChange(void* area, int clusterId, const Vector& worldPos)` (anonymous namespace)
  note: applies Platz delay path when kind==KindPlatz; uses SpeakUrgent for non-Platz path
- L474 — `static void TickPendingPlatz(void* area, const Vector& playerPos)` (anonymous namespace)
- L531 — `static bool IsWorldSpeechGatedImpl()` (anonymous namespace)
- L540 — `static void TickProximityLandmarks(const Vector& playerPos)` (anonymous namespace)
- L636 — `bool IsResrefStyleRoomName(const char* name)`
- L652 — `bool FindLandmarkNear(const Vector& pos, float rangeM, char* nameOut, size_t nameBufSize, Vector& posOut, int* outLandmarkIdx)`
- L682 — `bool IterateLandmarks(int& cursor, char* nameOut, size_t nameBufSize, Vector& posOut, int& outLandmarkIdx)`
- L699 — `void MarkLandmarkClaimedByDoor(int landmarkIdx)`
- L704 — `void Tick()`
- L924 — `bool IsWorldSpeechGated()`
- L928 — `bool IsModuleLoadPending()`
- L932 — `void AnnouncePreLoadDestination(void* exoStringPtr)`
