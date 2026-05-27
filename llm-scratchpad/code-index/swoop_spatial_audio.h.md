# swoop_spatial_audio.h (~22 lines)

Public API for the swoop race continuous obstacle + accelerator-pad cues. Split from swoop_race on 2026-05-27.

## Declarations (in source order)

- L13 — `namespace acc::swoop_race`
- L17 — `void TickSpatialAudio(void* miniGame);`
  Drive per-tick obstacle + accelpad loops. No-op if miniGame is null.
- L22 — `void ResetSpatialAudio();`
  Stop all live loops and clear the per-race diagnostic guards. Called on race ENTER (defensive wipe) and EXIT.
