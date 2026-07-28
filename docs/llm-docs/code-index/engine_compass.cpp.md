# engine_compass.cpp (56 lines)

Pure math: engine-yaw ↔ compass-yaw conversion, 8-point sector bucketing, and clock-position (12/3/6/9-style) computation for a world-space offset relative to a facing. No engine reads, no state.

## Declarations (in source order)

- L9-11 — `kSectorCount=8`, `kSectorSize=45.0f`, `kHalfSector=22.5f`
- L15 — `float EngineYawToCompass(float engineYawDeg)` (public) — involutive; camera_announce relies on apply-twice=identity
- L21 — `int CompassToSector(float compassDeg)` (public) — nearest-sector with half-sector offset, no hysteresis
- L29 — `int ClockPosition(float playerYawDeg, float dx, float dy)` (public)
  note: 30° round-to-nearest buckets; ahead bucket returns 12, never 0
- L40 — `acc::strings::Id SectorString(int sector)` (public) — sector 0..7 → DirNorth..DirNorthwest
