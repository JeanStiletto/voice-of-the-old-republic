# announce_degrees.cpp (226 lines)

On-demand exact-heading hotkey handler (AltGr / Right Alt, polled via Win32 since it's unbound in stock kotor.ini). Speaks camera facing in compass degrees plus an orientation label: world-frame ("N degrees, near <cluster>") normally, map-frame (room + degrees) when `InGameMap` is foreground. Talks to camera_announce (cached camera yaw), engine_compass, wall_topology (perceptual-cluster label matching transitions.cpp), and transitions (three-tier room-name lookup: landmark → friendly room name → synthetic "Room N"). Every map-frame failure path falls back to `OnAnnounceWorldDegrees` so the hotkey never feels eaten.

## Declarations (in source order)

- L26 — `int CompassDegreesFromEngineYaw(float engineYaw)` — floor(compass+0.5) mod 360, wrapped positive
- L36 — `bool ReadCameraEngineYawDegrees(float& out)` — prefers camera_announce's cached yaw; falls back to atan2(player-camera)
- L51 — `const char* SectorWord(int compassDegrees)` — CompassToSector + strings::Get
- L60 — `bool ResolveClusterLabelForPlayer(std::string& outBuf)` — wall_topology::LookupAt; false on open-area/no-cluster
- L83 — `void OnAnnounceWorldDegrees()` — builds FmtWorldStateOriented/UnknownCluster message, prism::Speak(interrupt=true)
- L117 — `bool ResolveRoomNameForPlayer(char* outBuf, size_t bufSize)` — landmark(15m) → room display name (skip resref-style) → "Room N"
- L156 — `void OnAnnounceMapDegrees()` — derives map-space yaw via GetMapRotateCCWFromWorldOrientation; falls back to world on any missing piece
- L213 — `void PollWin32()` — hotkey gate + HasActiveMapPanel branch to map vs world announce
