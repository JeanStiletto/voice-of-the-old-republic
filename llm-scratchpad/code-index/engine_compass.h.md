# engine_compass.h (25 lines)

Engine yaw ↔ compass yaw ↔ 8-point sector helpers. Pure math, no state.

Documents two coordinate frames: Engine (0°=East, CCW positive) and Compass (0°=North, CW positive). EngineYawToCompass is involutive. Sectors 0..7 = N/NE/E/SE/S/SW/W/NW with half-sector snap, no hysteresis.

## Declarations (in source order)

- L19 — `namespace acc::engine`
- L21 — `float EngineYawToCompass(float engineYawDeg)`
- L22 — `int CompassToSector(float compassDeg)`
- L23 — `acc::strings::Id SectorString(int sector)`
  note: returns a strings::Id enum value (not a raw string); caller passes to acc::strings::Get
