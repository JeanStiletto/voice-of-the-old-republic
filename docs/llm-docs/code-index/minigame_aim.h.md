# minigame_aim.h (68 lines)

Shared engine-grounded facility letting minigame code steer a CSWMiniPlayer
(swoop bike or turret gun, both extend CSWTrackFollower) by writing its
per-tick-integrated `offset` Vector at +0x1c4 — since the engine
re-integrates from the current value each tick, a post-Control write sticks
and only the player's own small delta is added on top next tick. Turret
interprets offset.x/z as elevation/azimuth degrees; swoop interprets offset.x
as a 1:1 world-X lane coordinate. Per-game error computation, target
selection, and sign calibration stay in `turret_game.cpp` /
`swoop_spatial_audio.cpp` — only the read/write primitives and the magnetism
curve are shared here.

## Declarations (in source order)

- L34 — `constexpr size_t kMiniPlayerOffsetVectorOffset = 0x1c4`
- L40 — `bool ReadOffsetVector(void* player, Vector& out)`
- L41 — `void WriteOffsetVector(void* player, const Vector& v)`
- L49-L53 — `struct MagnetParams { gainFar, gainNear, maxStep }`
- L57 — `float MagnetGain(float t, const MagnetParams& p)` — proximity-ramped gain, t=0 at engage edge, t=1 dead on target
- L64 — `float MagnetStep(float offsetVal, float mappedErr, float gain, const MagnetParams& p)`
  note: `mappedErr` must already be offset-unit + offset→world sign mapped by the caller (turret passes sign·worldErr, swoop passes bikeX−padX)
