# minigame_aim.cpp (48 lines)

Implementation of the shared minigame aim-assist primitives: SEH-guarded
read/write of the CSWMiniPlayer `offset` Vector at +0x1c4, plus the
proximity-ramped "sticky magnetism" math (turret's default-mode assist,
generalised for reuse by swoop). No panel/game-specific logic lives here —
see minigame_aim.h for the shared-vs-per-game split.

## Declarations (in source order)

- L11-L21 — `bool ReadOffsetVector(void* player, Vector& out)` — SEH-guarded read of +0x1c4
- L23-L31 — `void WriteOffsetVector(void* player, const Vector& v)` — SEH-guarded write; no-op on fault
- L33-L37 — `float MagnetGain(float t, const MagnetParams& p)` — t clamped to [0,1], t² blend of gainFar→gainNear
- L39-L45 — `float MagnetStep(float offsetVal, float mappedErr, float gain, const MagnetParams& p)` — step = -mappedErr*gain, capped to ±maxStep
