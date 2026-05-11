// Route description — turn-by-turn TTS readout of a computed path.
//
// Layer: guidance/ (depends on engine_compass for sector lookup,
// strings/tolk for speech, log for diagnostics). Phase 5 lay-off 5
// folded into the Ctrl+- hotkey wiring so the user gets a sanity-check
// readout of the calculated route at the same time as the beacon starts.
//
// Output shape (German example):
//   "Route zu Tür: 5 Meter Norden, 4 Meter Nord-Ost, 6 Meter Osten,
//    kein Übergang."
//
// Each segment is the displacement vector between two consecutive
// nodes in the walk sequence (player → wp[0], wp[i] → wp[i+1]),
// described as `{rounded metres} Meter {8-point compass direction}`.
// Consecutive segments in the same compass sector are merged so a
// long corridor doesn't read as five separate "Norden" entries.
// Sub-threshold segments (<1m) are folded into the next segment so
// noise from "nearest path_point happens to be 30cm from player"
// stays out of the readout.
//
// Transition note: when the destination object is a Transition kind
// (CSWSTrigger with transition_destination set), the suffix flips
// from "kein Übergang" / "no transition" to "1 Übergang" / "1
// transition" — the user knows their walk will cross-load a module.
//
// Empty path: speaks the localised no-path failure phrase.
// Single-point path (player already at destination): speaks the
//   localised "already at destination" phrase.

#pragma once

#include <vector>

#include "engine_offsets.h"  // Vector

namespace acc::guidance::description {

// Speak a localised turn-by-turn description of `waypoints` starting
// from `playerPos`. `targetName` is the resolved destination object
// name (caller's responsibility — typically GetObjectName or fall
// back to category label). `isTransition` flips the suffix between
// the "1 transition" / "no transition" strings.
//
// `interrupt` is forwarded to Tolk — Ctrl+- speech preempts any
// in-flight cycle/passive narration so the user hears the route
// description in full.
//
// Returns false only when the caller supplied an empty waypoint
// list (caller's contract violation; should have caught from
// ComputePath returning false). The localised "no path" speech
// fires in that case too, so the user always hears something.
bool Speak(const Vector& playerPos,
           const std::vector<Vector>& waypoints,
           const char* targetName,
           bool isTransition,
           bool interrupt);

}  // namespace acc::guidance::description
