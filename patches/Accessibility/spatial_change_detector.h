// Pillar 1 Triggers 1 + 2 — per-feature change detection.
//
// Layer: spatial/ (cross-cutting; consumes engine_area's walkmesh-edge
// cache + AreaObjectIterator + filter_objects' six-category predicate;
// dispatches via audio_cue_player). One per-tick scan that maintains a
// per-feature `last_cued_distance` (T1) + per-feature `last_cued_at`
// timestamp (shared T1/T2) + a single foremost-in-front debounce state
// (T2).
//
// **Trigger 1 — 360° distance-delta.** Fires a cue when |Δdistance| to
// any in-range feature exceeds `core_settings.pillar1.distanceDelta-
// ThresholdMeters`. Range cap from `awarenessRangeMeters` (5m default).
// Wall candidates are binned into 4 character-relative sectors (Front /
// Left / Back / Right) and only the closest in each sector contributes a
// cue, capped at `trigger1MaxWallCuesPerTick` per tick. Object cues fire
// on per-handle threshold crossing without sector binning (population is
// sparse enough that K-closest is not an issue).
//
// **Trigger 2 — foremost-in-front (±45° = T1 Front sector).** Picks the
// closest wall-or-object in the Front sector each tick. When the foremost
// identity changes and stays stable for `kT2QuietMs` (~250ms), fires a
// single cue. Three-variable debounce pattern from `turn_announce.cpp`
// collapses snap-rotation chains (W↔S 180° spins, fast Q/E sweeps) to
// one cue for the resolved foremost. Cone-clear = silence (rotation
// confirmation comes from `turn_announce`).
//
// **T1/T2 coordination via shared `last_cued_at` stamp.** T1 fires on
// distance-delta and stamps `last_cued_at` for the cued feature. T2 only
// fires if `now - last_cued_at > kT2QuietMs` for the foremost feature
// (then also stamps it). Result: approach reads as Trigger-1-only
// (motion-driven cadence wins); T2 only adds audio when T1 is silent —
// stationary-rotation, or rotation across already-stamped features.
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
// First-tick suppression on area-load (post-CalibrateInRange) seeds T2
// state without firing — mirrors `turn_announce`'s "first observation
// since DLL load" handling.
//
// Phase 3 lay-offs 3 (T1) + 4 (T2).

#pragma once

namespace acc::spatial::change_detector {

// Per-tick entry. Self-gates on player + area resolved; self-detects area
// change and rebuilds the wall cache + resets per-feature state on the
// fly. Idempotent on null player / null area (silent return).
void Tick();

}  // namespace acc::spatial::change_detector
