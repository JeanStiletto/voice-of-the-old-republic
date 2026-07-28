# engine_compass.h (33 lines)

Engine yaw ↔ compass yaw ↔ 8-point sector helpers, pure math. Engine frame: 0°=+X=East, CCW positive. Compass frame: 0°=North, 90°=East, CW positive (what screen readers think in).

## Declarations (in source order)

- L21 — `float EngineYawToCompass(float engineYawDeg)`
- L22 — `int CompassToSector(float compassDeg)`
- L23 — `acc::strings::Id SectorString(int sector)`
- L30 — `int ClockPosition(float playerYawDeg, float dx, float dy)`
  note: moved here from cycle_input.cpp so trap/object announcements share one definition; 12=ahead, 3=right, 6=behind, 9=left
