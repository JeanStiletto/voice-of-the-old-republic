// Pillar 1 Trigger 1 — per-feature distance-delta change detector.
//
// Layer: spatial/ (cross-cutting; consumes engine_area's walkmesh-edge
// cache + AreaObjectIterator + filter_objects' six-category predicate;
// dispatches via audio_cue_player). One per-tick scan that maintains a
// per-feature `last_cued_distance` and fires a cue only when distance
// changes by more than `core_settings.pillar1.distanceDeltaThresholdMeters`.
//
// Awareness model is 360° around the character — no sight cone, no front
// bias. Range cap from `core_settings.pillar1.awarenessRangeMeters` (5m
// default per the locked plan).
//
// Two feature streams are scanned each tick:
//
//   1. **Walls.** Reads the cached perimeter edges built on area-change
//      via `engine_area::BuildAreaWallCache`. For each in-range wall,
//      computes the closest-point-on-segment to the player and uses that
//      as the cue position so the engine's spatial pan reads the wall's
//      direction directly.
//
//   2. **Objects.** Iterates `AreaObjectIterator` and classifies each
//      object via `filter::ObjectMatches` against the six locked Pillar 4
//      categories — same predicate used for the discrete cycle, so the
//      ambient and discrete channels stay consistent. Object position is
//      the cue position; engine pan handles direction.
//
// The "first time in range" case fires once on entry; subsequent in-range
// observations fire only on threshold crossing. Leaving range drops the
// tracked state so re-entering range fires fresh.
//
// Phase 3 lay-off 3.

#pragma once

namespace acc::spatial::change_detector {

// Per-tick entry. Self-gates on player + area resolved; self-detects area
// change and rebuilds the wall cache + resets per-feature state on the
// fly. Idempotent on null player / null area (silent return).
void Tick();

}  // namespace acc::spatial::change_detector
