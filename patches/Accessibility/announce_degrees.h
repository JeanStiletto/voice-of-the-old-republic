// On-demand exact-heading hotkey — Pillar 2 sub-feature D ("announce/degrees").
//
// Layer: announce/ (depends on engine_player for yaw, strings for the
// localised format, tolk for speech). One-shot speech; no per-tick state
// other than rising-edge detection on the trigger key.
//
// Trigger: AltGr (right Alt — the key directly right of space on a German
// QWERTZ keyboard). Spoken payload is the player's current facing in
// compass-frame degrees (0° = North, 90° = East, CW positive — same frame
// as the octagonal compass announcer in `turn_announce.cpp`), e.g.
// "47 Grad" / "47 degrees". No quantisation, no hysteresis — the user
// asked, the user gets the exact bearing.
//
// Why Win32 polling and not the engine HandleInputEvent path: AltGr (right
// Alt) isn't bound to anything in stock kotor.ini, and the engine's keymap
// drops unbound scancodes before our manager hook sees them — same
// situation as the cycle keys (`,`/`.`/`-`). See memory entry
// `project_inworld_input_pipeline.md`.
//
// Phase 4 sub-feature D (the "Shift+press for zone hierarchy" half is
// deferred to a later session — not requested in this lay-off).

#pragma once

namespace acc::announce_degrees {

// Per-tick poll. Edge-detects AltGr rising, gates on foreground-window +
// in-world player presence, speaks the heading. Idempotent on missing
// player creature (silent no-op — same convention as `cycle_input::PollWin32`).
void PollWin32();

}  // namespace acc::announce_degrees
