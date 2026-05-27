# map_ui_cursor.cpp (1215 lines)

Implementation of the map virtual cursor. Cursor lives in map-pixel space
[0..440]x[0..256]. Two parallel hover scans: CSWSWaypoint map-notes and
user-placed CSWCMapPin entries. Ambient fallback classifies fog, landmark,
friendly room name, or terrain shape (via wall_topology) at hover-pause.

## Declarations (in source order)

- L34 — `namespace acc::map_ui_cursor`
- L36 — `namespace` (anonymous)
- L42 — `constexpr float kCursorSpeedPx`
- L47 — `constexpr int kMapPixelMaxX`
- L48 — `constexpr int kMapPixelMaxY`
- L51 — `constexpr float kMaxDtSec`
- L56 — `constexpr DWORD kHoverPauseMs`
- L67 — `constexpr int kHoverHitRadiusPx`
  note: 36-pixel radius for waypoint hit-test; tested live — tighter was hard to pinpoint without sighted feedback
- L71 — `constexpr DWORD kEdgeCueQuietMs`
- L78 — `constexpr float kEdgeCueGain`
  note: 8.0x — same as guidance_beacon; needed to overcome pause-mode audio dampening
- L86 — `constexpr size_t kAreaMapOrientationOffset`
- L87 — `constexpr size_t kAreaMapWorldUnitsPerXPxOffset`
- L88 — `constexpr size_t kAreaMapWorldUnitsPerYPxOffset`
- L89 — `constexpr size_t kAreaMapWorldOriginXOffset`
- L90 — `constexpr size_t kAreaMapWorldOriginYOffset`
- L101 — `constexpr size_t kInGameMapHiderOffset`
- L102 — `constexpr size_t kMapHiderWaypointListOffset`
- L111 — `constexpr size_t kCExoLinkedListInternalOffset`
- L112 — `constexpr size_t kCExoLLInternalHeadOffset`
- L113 — `constexpr size_t kCExoLLNodeNextOffset`
- L114 — `constexpr size_t kCExoLLNodePayloadOffset`
- L118 — `constexpr size_t kWaypointPositionOffset`
- L119 — `constexpr size_t kWaypointHasMapNoteOff`
- L120 — `constexpr size_t kWaypointMapNoteLocOff`
- L139 — `enum class AmbientKind`
  note: None / Unexplored / Landmark / RoomName / TerrainShape; controls hover-pause speak path and dedup key
- L151 — `struct CursorState`
  note: holds pixel pos, world pos, waypoint and ambient hover-pause state machines, text-based dedup buffers
- L211 — `CursorState g_state`
- L222 — `bool IsForegroundProcess()`
- L233 — `bool IsMapPanelActive(void** outPanel)`
  note: walks panels[] explicitly — InGameMap sits under the strip, not at GetForegroundPanel
- L270 — `bool ReadAreaMapTransform(void* areaMap, uint32_t& outOrientation, float& outWuPerPx, float& outWuPerPy, float& outOriginX, float& outOriginY)`
- L290 — `void ApplyOrientationForward(uint32_t orient, float& xw, float& yw)`
  note: matches CSWSAreaMap::GetMapPixelFromWorldCoord exactly; case 0-3 rotation grid
- L302 — `void ApplyOrientationInverse(uint32_t orient, float& xw, float& yw)`
- L313 — `bool WorldToPixel(void* areaMap, const Vector& world, float& outPx, float& outPy)`
- L324 — `bool PixelToWorld(void* areaMap, float px, float py, float zSeed, Vector& outWorld)`
- L345 — `void* FindNearestExploredMapNote(void* mapPanel, void* areaMap, float cursorPx, float cursorPy, int* outScannedCount, float* outBestDist2)`
  note: spatial scan of the map-hider waypoint linked list; only waypoints with has_map_note != 0 and IsWorldPointExplored pass
- L454 — `void* FindNearestUserMapPin(void* clientArea, void* areaMap, float cursorPx, float cursorPy, int* outScannedCount, float* outBestDist2)`
  note: hit-tests user-placed pins (flags high bit set); engine quest pins deliberately skipped
- L493 — `bool ReadWaypointMapNoteText(void* waypoint, char* outBuf, size_t bufSize)`
- L497 — `void SeedCursorAtPlayer(void* areaMap)`
- L511 — `void ResetSessionState()`
- L528 — `const char* AmbientKindStr(AmbientKind k)`
- L543 — `bool ReadWaypointTag(void* waypoint, char* outBuf, size_t bufSize)`
- L556 — `bool KeyDown(int vk)`
- L562 — `bool IsActive()`
- L564 — `bool TryGetCursorWorldPosition(Vector& out)`
- L570 — `void PanToWorld(const Vector& world, void* suppressWaypoint)`
- L622 — `void Tick()`
