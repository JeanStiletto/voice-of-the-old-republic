# swoop_spatial_audio.h (23 lines)

Public surface for the swoop-race continuous spatial-audio sweep: walks
the global CSWMiniGameObjectArray each tick to drive per-obstacle warning
loops and an accelpad steering-guide tone. Split out of swoop_race.cpp
(2026-05-27) purely for file-size management; swoop_race.cpp owns the race
lifecycle and calls these two entry points.

## Declarations (in source order)

- L11 — `namespace acc::swoop_race`
- L16 — `void TickSpatialAudio(void* miniGame)`
  note: safe to call with miniGame == nullptr; listener via GetCameraPosition with GetPlayerPosition fallback
- L21 — `void ResetSpatialAudio()` — stops all loops; called on race ENTER (defensive) and EXIT
