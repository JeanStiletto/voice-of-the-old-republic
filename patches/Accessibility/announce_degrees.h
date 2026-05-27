// On-demand exact-heading hotkey — Pillar 2 sub-feature D ("announce/degrees").
//
// Layer: announce/ (depends on camera_announce for the camera yaw,
// engine_player for camera/player positions in the map-frame branch,
// strings for the localised format, prism for speech). One-shot speech;
// no per-tick state other than rising-edge detection on the trigger key.
//
// Trigger: AltGr (right Alt — the key directly right of space on a German
// QWERTZ keyboard). Spoken payload is the **camera's** current facing in
// compass-frame degrees (0° = North, 90° = East, CW positive), e.g.
// "47 Grad" / "47 degrees". No quantisation, no hysteresis — the user
// asked, the user gets the exact bearing.
//
// Why camera, not character: in the verified KOTOR control scheme
// (A/D rotates camera; W snaps character to camera), the character's
// facing is either equal to the camera's (during/after walking) or
// stale-from-last-W (while standing still and looking around). The
// camera direction is what `camera_announce`'s passive cues and the
// N-hotkey orient operate on — keeping the manual heading readout on
// the same reference frame avoids surprising the user with a number
// that disagrees with the last sector word they just heard.
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
