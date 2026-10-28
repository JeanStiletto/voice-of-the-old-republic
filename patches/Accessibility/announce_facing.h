// On-demand facing readout (AltGr / Right Alt; the pad's right trigger).
//
// In the world: the camera's facing as a compass direction word ("Facing
// north"), then the area the player stands in. On the map screen: the facing
// in map-frame degrees plus the direction word, then the room. The facing
// half speaks on the compass voice and the place half follows on the screen
// reader once it has finished (prism::SpeakCompassThen), so the heading never
// runs into the area description.
//
// Camera (not character) because A/D rotates the camera and W only snaps
// the character on commit — the camera direction is what every other
// orient cue references, so a manual readout on a different frame would
// disagree with the last sector word the user heard.
//
// Win32 polling because AltGr is unbound in stock kotor.ini and the
// engine keymap drops unbound scancodes before our manager hook sees them.

#pragma once

namespace acc::announce_facing {

void PollWin32();

}  // namespace acc::announce_facing
