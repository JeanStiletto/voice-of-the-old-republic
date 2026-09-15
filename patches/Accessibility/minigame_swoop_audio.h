#pragma once

#include <cstdint>

// Swoop race spatial-audio: continuous obstacle + accelerator-pad
// proximity cues. Walks the global CSWMiniGameObjectArray each tick,
// classifies each slot via the AsObstacle / AsEnemy vtable downcasts,
// drives one LoopSource per in-range obstacle (multi-source warn pulse)
// and one nearest-only LoopSource for accelerator pads (clean booster
// channel). See swoop_race.cpp for the race lifecycle, gear watch, and
// wall-collision detection that drives this module via Tick().

namespace acc::swoop_race {

// Drive obstacle + accelpad proximity loops for the active race. Safe
// to call with miniGame == nullptr (no-ops). Reads listener via
// GetCameraPosition with GetPlayerPosition fallback.
void TickSpatialAudio(void* miniGame);

// Stop every active loop and clear the per-race diagnostic guards.
// Called on race ENTER (defensive — wipe any survivors from a previous
// race) and on race EXIT.
void ResetSpatialAudio();

// KOTOR 2: what the parked bike is stuck against, as seen by the obstacle
// sweep (gear 0, inside a low bar's lateral span, the bar just ahead). The
// stuck hint in minigame_swoop_race.cpp turns this into the right escape:
//   WallLeft / WallRight — wedged between the bar's end and the posts on
//                          that side: steer the other way first (a jump or a
//                          relaunch from the wall hits the pillar), then jump,
//                          then W.
//   Strip                — the bar has an open strip and the guide tone is
//                          already pointing at it: follow the tone, then W.
//   MustJump             — no strip: jump from the standstill, then W (the
//                          arc is above bar height before W has moved the
//                          bike the two units into it).
enum class ParkedBar : uint8_t { None = 0, WallLeft, WallRight, Strip, MustJump };
ParkedBar GetParkedBarSituation();

}  // namespace acc::swoop_race
