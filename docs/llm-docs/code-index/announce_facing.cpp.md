# announce_facing.cpp (215 lines)

On-demand facing readout (AltGr / Right Alt, polled via Win32 since it's unbound in stock kotor.ini; the pad's RT routes here as a synthetic press). Two halves on two voices through `prism::SpeakCompassThen`: the facing on the compass voice, then the place on the screen reader once that voice has finished. World frame normally ("Facing north", then the perceptual-cluster label matching transitions.cpp); map frame when `InGameMap` is foreground (map-frame degrees + direction word, then the room). Talks to camera_announce (cached camera yaw), engine_compass, room_topology, and transitions (three-tier room-name lookup: landmark → friendly room name → synthetic "Room N"). Every map-frame failure path falls back to `AnnounceWorldFacing` so the hotkey never feels eaten.

## Declarations (in source order)

- L26 — `int CompassDegreesFromEngineYaw(float engineYaw)` — floor(compass+0.5) mod 360, wrapped positive
- L36 — `bool ReadCameraEngineYawDegrees(float& out)` — prefers camera_announce's cached yaw; falls back to atan2(player-camera)
- L51 — `const char* SectorWord(int compassDegrees)` — CompassToSector + strings::Get
- L60 — `bool ResolveClusterLabelForPlayer(std::string& outBuf)` — room_topology::LookupAt; false on open-area/no-cluster
- L86 — `void AnnounceWorldFacing()` — FmtFacingWorld on the compass voice, cluster label after it
- L112 — `bool ResolveRoomNameForPlayer(char* outBuf, size_t bufSize)` — landmark(15m) → room display name (skip resref-style) → "Room N"
- L151 — `void AnnounceMapFacing()` — map-space yaw via GetMapRotateCCWFromWorldOrientation; FmtFacingMap, room after it; falls back to world on any missing piece
- L202 — `void PollWin32()` — hotkey gate + HasActiveMapPanel branch to map vs world readout
