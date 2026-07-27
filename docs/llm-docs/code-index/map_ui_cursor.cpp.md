# map_ui_cursor.cpp (1293 lines)

Virtual W/A/S/D cursor over the in-game map UI (Phase 5 lay-off 6). Moves in
map-pixel space [0..440]x[0..256] using the player's bound movement/turn
keys (not hardcoded WASD, via `engine_keymap::MoveAxisVks`), inverting
`CSWSAreaMap`'s pixel transform inline (4 fields: orientation + per-axis
world-units-per-pixel + origin — `WorldToPixel`/`PixelToWorld` mirror the
engine's `GetMapPixelFromWorldCoord`/inverse exactly, including its 4
orientation cases). A 300ms hover-pause debounce narrates whatever the
cursor sits on, in priority: explicit map-note waypoint / user map-pin /
shipped hint (closest of the three wins) > fog-of-war "Unexplored" > Tier-1
landmark > Tier-2 mod room name > terrain shape (from `wall_topology`, same
source the in-world walking adapter uses so cursor and walking never
disagree) > silence. Speech goes out via `prism::SpeakUrgent` (survives
NVDA's typed-character cancel while WASD is held) except the one-time area
title on map-open, which uses the normal `prism::Speak` channel so it
doesn't collide with the engine's own NVDA-channel panel announce. Talks to
`engine_area`, `engine_manager`/`engine_panels` (foreground/panel-kind
detection), `map_shipped_hints`, `map_user_markers`, `wall_topology`,
`transitions`, `audio_bus`/`audio_cues` (edge-collision cue).

## Declarations (in source order)

- L47 — `constexpr float kCursorSpeedPx = 100.0f`
- L52 — `constexpr int kMapPixelMaxX = 440`, `kMapPixelMaxY = 256` — inclusive bounds per the engine's own `< 0x1b9`/`< 0x101` check
- L61 — `constexpr DWORD kHoverPauseMs = 300`
- L71 — `constexpr int kHoverHitRadiusPx = 36`
  note: tuned live — 24px was hard to pinpoint without sighted feedback, 36px catches landmarks reliably
- L75 — `constexpr DWORD kEdgeCueQuietMs = 250`
- L85 — `constexpr uint8_t kEdgeCuePauseExemptGroup = 0x0b`
  note: map sub-screen runs under SetSoundMode pause, which mutes everything except priority groups 1/2/0xb — the earlier "inaudible" symptom was a pause-exemption bug, not a volume one
- L93 — `CSWSAreaMap` field offsets: orientation/world-units-per-px/origin (L93-97), `CSWGuiInGameMap.map_hider` offset chain + `CExoLinkedList`/`CSWSWaypoint` offsets (L99-127)
- L146 — `enum class AmbientKind { None, Unexplored, Landmark, RoomName, TerrainShape }`
  note: vanilla resref-style room ids (m02_03e, stunt_01_main) never reach RoomName — classify as None and stay silent by design
- L158 — `struct CursorState` — position, waypoint hover-pause pending/last-spoken (pin vs waypoint discriminator), ambient hover-pause pending/last-spoken (kind + repurposed room-idx key + text dedup overlay), TerrainShape speak-buffer cache
- L218 — `CursorState g_state`
- L228 — `bool IsForegroundProcess()`
- L240 — `bool IsMapPanelActive(void** outPanel)` — walks panels[] explicitly since the InGameMenu strip sits above InGameMap in the foreground stack
- L274 — `bool ReadAreaMapTransform(...)` — SEH-guarded read of the 4 transform scalars, all required
- L297 — `void ApplyOrientationForward` / L309 `ApplyOrientationInverse` — the engine's 4 orientation-rotation cases and their inverses
- L320 — `bool WorldToPixel(...)` / L331 `bool PixelToWorld(...)`
- L352 — `void* FindNearestExploredMapNote(...)`
  note: walks CExoLinkedList<ulong> (two indirections per node, per decompiled GetNextMapNote), filters has_map_note + IsWorldPointExplored, same gate as engine GetNext/PrevMapNote
- L464 — `void* FindNearestUserMapPin(...)`
  note: identified by IDENTITY via map_user_markers registry, not a flags bit-test — engine note pins also set the high bit, so a bit-test leaked unexplored notes; user markers skip fog (player's own drop)
- L507 — `const ShippedHint* FindNearestShippedHint(...)` — fog-gated like engine notes (unlike user markers)
- L538 — `bool ReadWaypointMapNoteText(...)`
- L542 — `void SeedCursorAtPlayer(void* areaMap)`
- L556 — `void ResetSessionState()`
- L573 — `const char* AmbientKindStr(AmbientKind k)`
- L590 — `std::string ResolveAmbientText(...)` — renders speak-text per ambient kind; TerrainShape source differs by call site (fresh vs stashed)
- L632 — `bool ReadWaypointTag(...)` — diagnostic CExoString read
- L645 — `bool KeyDown(int vk)` / L653 `bool AxisDown(MoveAxis axis)`
- L664 — `bool IsActive()`
- L666 — `bool TryGetCursorWorldPosition(Vector& out)`
- L672 — `void PanToWorld(const Vector& world, void* suppressWaypoint)`
  note: cancels in-flight hover-pause so the cursor's own debounce doesn't re-announce/contradict what the cycle just spoke
- L724 — `void Tick()`
  note: activation seeds at player position + speaks area title once (normal channel, not urgent); per-frame: axis-driven pixel move + clamp + edge-collision cue, three parallel hover-pause scans (waypoint/pin/hint, closest wins), then either the waypoint/pin/hint speak branch or the ambient (fog/landmark/room/terrain-shape) classify+dedup+speak branch, each gated by its own hover-pause timer
