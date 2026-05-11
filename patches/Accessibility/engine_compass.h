// Compass-frame helpers — engine-yaw → compass-yaw → 8-point sector → string.
//
// Layer: engine/ (pure math; no engine state access). Centralises the
// EngineYawToCompass / CompassToSector / SectorString triad that
// `turn_announce`, `camera_announce`, `announce_degrees` and (now)
// `guidance_description` all need. Per project rule
// (CLAUDE.md "Before adding a helper, search for an existing one"),
// adding a fourth user warranted promoting to a shared TU rather than
// dragging the duplicate forward.
//
// Frame conventions:
//   Engine yaw   — 0° = +X = East, CCW positive, range [0, 360).
//                  Source: ExecuteCommandGetFacing / GetPlayerYawDegrees.
//   Compass yaw  — 0° = North, 90° = East, CW positive, range [0, 360).
//                  Frame consumers (TTS, screen readers) think in.
//
// Conversion is involutive: applying EngineYawToCompass twice returns the
// original engine yaw. camera_announce relies on that property for its
// inverse projection in TryGetCameraEngineYawDegrees.
//
// 8-point sector convention (matches turn_announce):
//   0 = N    1 = NE   2 = E    3 = SE
//   4 = S    5 = SW   6 = W    7 = NW
// Sector centres are at i × 45°; CompassToSector buckets via half-sector
// offset before integer division. SectorString resolves to the
// language-stable acc::strings::Id (DirNorth..DirNorthwest).

#pragma once

#include "strings.h"  // acc::strings::Id

namespace acc::engine {

// Convert engine yaw (CCW from +X) to compass yaw (CW from North). Output
// in [0, 360). Pure; no side effects.
float EngineYawToCompass(float engineYawDeg);

// Bucket a compass-frame yaw into one of 8 sectors, 0..7 (N, NE, E, SE,
// S, SW, W, NW). Nearest-sector with a half-sector offset — no hysteresis.
// Callers needing hysteresis (turn_announce, camera_announce) wrap this
// with their own state.
int CompassToSector(float compassDeg);

// 8-point compass sector → localised direction string Id. Caller resolves
// to bytes via acc::strings::Get.
acc::strings::Id SectorString(int sector);

}  // namespace acc::engine
