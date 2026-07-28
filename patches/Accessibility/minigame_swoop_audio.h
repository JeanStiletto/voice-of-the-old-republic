// Swoop race spatial-audio: continuous obstacle + accelerator-pad
// proximity cues. Walks the global CSWMiniGameObjectArray each tick,
// classifies each slot via the AsObstacle / AsEnemy vtable downcasts,
// drives one LoopSource per in-range obstacle (multi-source warn pulse)
// and one nearest-only LoopSource for accelerator pads (clean booster
// channel). See swoop_race.cpp for the race lifecycle, gear watch, and
// wall-collision detection that drives this module via Tick().

#pragma once

namespace acc::swoop_race {

// Drive obstacle + accelpad proximity loops for the active race. Safe
// to call with miniGame == nullptr (no-ops). Reads listener via
// GetCameraPosition with GetPlayerPosition fallback.
void TickSpatialAudio(void* miniGame);

// Stop every active loop and clear the per-race diagnostic guards.
// Called on race ENTER (defensive — wipe any survivors from a previous
// race) and on race EXIT.
void ResetSpatialAudio();

}  // namespace acc::swoop_race
