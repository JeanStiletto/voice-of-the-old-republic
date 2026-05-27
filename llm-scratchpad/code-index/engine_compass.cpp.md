# engine_compass.cpp (44 lines)

Implementation of engine_compass.h. No leading comment block.

## Declarations (in source order)

- L5 — `namespace acc::engine`
- L7 — `namespace { ... }` (kSectorCount, kSectorSize, kHalfSector constants)
- L15 — `float EngineYawToCompass(float engineYawDeg)`
- L21 — `int CompassToSector(float compassDeg)`
- L29 — `acc::strings::Id SectorString(int sector)`
