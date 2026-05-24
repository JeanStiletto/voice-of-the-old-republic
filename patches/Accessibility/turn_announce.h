// Phase 2 ad-hoc — octagonal direction-on-turn announcement (Pillar 2
// sub-feature C, pulled forward from Phase 4 because the user reported
// "no clue if I'm doing anything" — they need feedback that A/D / Q/E
// are turning the character vs. just rotating the camera).
//
// Layer: narration/ (consumes engine_player + strings + prism; no engine
// re-entry). Reads server-side player yaw per tick, buckets into 8
// cardinal sectors (N / NE / E / SE / S / SW / W / NW), speaks the
// localised direction name via Prism on sector change. 5° hysteresis
// per the long-term plan to avoid border thrashing.
//
// Yaw frame conversion: engine `GetPlayerYawDegrees` returns
// [0, 360) with 0° = +X = east, CCW positive. Compass uses 0° = North,
// 90° = East, CW positive. Conversion: `compass = (90 - engine + 360) % 360`.
//
// Speech is queued without interrupt — direction shouldn't talk over an
// in-flight passive_narrate / cycle announcement. If the user wants
// silence the toggle goes via core_settings later (Phase 7 — not yet
// implemented; default ON per plan §"Locked defaults — Pillar 2").
//
// First-tick suppression: the very first sector observation after DLL
// load doesn't speak — the user hasn't turned yet, that's just where
// they happen to be facing.

#pragma once

namespace acc::turn_announce {

// Per-tick poll. Self-gates on GetPlayerYawDegrees succeeding (false
// in menus / chargen / pre-spawn / degenerate facing during spawn).
void Tick();

}  // namespace acc::turn_announce
