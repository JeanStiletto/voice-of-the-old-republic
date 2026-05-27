// Pillar 1 cue playback — single callsite for "play NavCue at position".
//
// Wraps audio::PlayCue3D with two shared gates:
//   1. Per-kind toggle from core_settings (Wall, HazardLedge, Door, ...).
//      Guidance/view-mode cues (Collision, Beacon*) are not in the
//      vocabulary and always pass.
//   2. Awareness-range cap — 3D Euclidean distance from listener.
//      Per-call so Trigger 1 (360°) and Trigger 2 (front cone) can
//      pick independently.
//
// No per-NavCue debounce here — the change detector's per-feature
// last_cued_distance is the cadence control; layering a global debounce
// silenced real signal in dense corridors.

#pragma once

#include "audio_cues.h"
#include "engine_offsets.h"

namespace acc::audio {

// True iff audio_bus accepted; false on gate reject or singleton miss.
bool PlayCueAtPosition(NavCue cue,
                       const Vector& worldPos,
                       const Vector& listenerPos,
                       float rangeMax);

}  // namespace acc::audio
