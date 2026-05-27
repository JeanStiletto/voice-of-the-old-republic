# announce_degrees.cpp (226 lines)

Implementation of the AltGr on-demand heading readout. Two code paths:
OnAnnounceWorldDegrees (compass sector + cluster label) and
OnAnnounceMapDegrees (map-frame yaw, room/landmark tier lookup).

## Declarations (in source order)

- L18 — `namespace acc::announce_degrees`
- L20 — `namespace` (anonymous)
- L22 — `constexpr float kDegToRad`
- L24 — `int CompassDegreesFromEngineYaw(float engineYaw)`
- L34 — `bool ReadCameraEngineYawDegrees(float& out)`
  note: prefers camera_announce cache; falls back to atan2(player - camera)
- L49 — `const char* SectorWord(int compassDegrees)`
- L58 — `bool ResolveClusterLabelForPlayer(char* outBuf, size_t bufSize)`
  note: returns false for open-area or un-labelled clusters — caller should stay silent
- L83 — `void OnAnnounceWorldDegrees()`
- L117 — `bool ResolveRoomNameForPlayer(char* outBuf, size_t bufSize)`
  note: three-tier: landmark → friendly room name → synthetic "Room N"; skips resref-style names
- L156 — `void OnAnnounceMapDegrees()`
  note: converts camera yaw through GetMapRotateCCWFromWorldOrientation; falls back to world variant on any failure
- L213 — `void PollWin32()`
