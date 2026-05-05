// Pillar 1 cue playback — single callsite for "play NavCue at world pos P".
//
// Layer: audio/ (depends on audio_bus.h, audio_cues.h, core_settings.h,
// engine_offsets.h's Vector). Wraps acc::audio::PlayCue3D with two gates
// so the per-tick consumers (spatial_change_detector — Trigger 1,
// spatial_front_cone — Trigger 2) don't each reimplement them:
//
//   1. **Per-kind toggle.** Cues in the Pillar 1 vocabulary (Wall,
//      HazardLedge, Door, NpcCreature, ContainerPlaceable, Item, Landmark,
//      TransitionExit) are gated on the matching `core_settings::Get()
//      .pillar1.cueX` flag. Cues outside that vocabulary (Collision,
//      Beacon*) always pass — they're guidance/view-mode signals, not
//      change-driven Pillar 1 cues.
//   2. **Awareness-range cap.** 3D Euclidean distance from the listener
//      position; cues at distance > rangeMax are silently dropped. Range
//      is per-call so Trigger 1 (360° awareness, default 5m from settings)
//      and Trigger 2 (narrow front cone, may want a shorter cap) can pick
//      independently.
//
// (A previous "per-NavCue debounce" backstop was removed 2026-05-05 after
// the first in-game test showed it silencing 84% of wall cues in dense
// corridors — when several walls cross threshold within the same tick or
// the next, the global debounce kept only the first audible. The change-
// detector's per-feature `last_cued_distance` is the proper cadence
// control; debouncing on top of it destroyed real signal.)
//
// All gates are fast-fail; if any one rejects, audio_bus is never called.
// Diagnostic logging fires for every accept and every drop with the reason.
//
// Phase 3 lay-off 2.

#pragma once

#include "audio_cues.h"        // NavCue
#include "engine_offsets.h"    // Vector

namespace acc::audio {

// Play `cue` at world position `worldPos`, gating on awareness range
// against `listenerPos`. Returns true if the cue reached the engine via
// audio_bus, false if dropped at a gate or if audio_bus itself rejected
// (singleton not ready, etc.).
//
// Distance gate is 3D Euclidean (Z included). Pillar 1's design states
// vertical separation doesn't affect cues, but the audio engine's pan +
// falloff already operate on the 3D listener pose, so 3D distance is the
// gate that matches engine semantics.
bool PlayCueAtPosition(NavCue cue,
                       const Vector& worldPos,
                       const Vector& listenerPos,
                       float rangeMax);

}  // namespace acc::audio
