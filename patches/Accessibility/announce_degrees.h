// On-demand exact-heading hotkey (AltGr / Right Alt).
//
// Speaks the camera's current facing in compass degrees, e.g. "47 degrees".
// In-world frame by default; map-frame when InGameMap is foreground.
//
// Camera (not character) because A/D rotates the camera and W only snaps
// the character on commit — the camera direction is what every other
// orient cue references, so a manual readout on a different frame would
// disagree with the last sector word the user heard.
//
// Win32 polling because AltGr is unbound in stock kotor.ini and the
// engine keymap drops unbound scancodes before our manager hook sees them.

#pragma once

namespace acc::announce_degrees {

void PollWin32();

}  // namespace acc::announce_degrees
