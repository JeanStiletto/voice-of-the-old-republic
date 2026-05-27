# swoop_race.cpp (~400 lines)

Swoop race minigame accessibility implementation. Phase 1 lay-off: entry/exit announce + keybind cheat sheet + gear-shift announce from CSWTrackFollower.speed jumps + per-tick diagnostic snapshot. Latches the CSWMiniGame pointer because the area chain stops resolving mid-race (verified live in patch log). SEH-guards all reads.

## Declarations (in source order)

- L43 — `namespace acc::swoop_race`
- (engine struct offset constants in anonymous namespace: kClientAreaMiniGameOffset, kMiniGamePlayerOffset, kFollowerSpeedOffset, kMiniPlayerOffsetVectorOffset, kMiniPlayerMinSpeedOffset, kMiniPlayerMaxSpeedOffset, kObstacleAurObjectOffset, kAurObjectPositionOffset, etc.)
- `void Tick()` (public)
- `bool IsActive()` (public)
