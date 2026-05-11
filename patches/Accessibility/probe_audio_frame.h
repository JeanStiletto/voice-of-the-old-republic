// Audio-frame diagnostic probe.
//
// F10 advances through the 8 compass sectors (N → NE → E → SE → S → SW
// → W → NW → repeat), emitting a 3D-positional cue at 5m from the
// player in each direction and speaking the spoken-direction name via
// Tolk. The user listens for whether the audio pan matches the spoken
// direction.
//
// Purpose: characterise the relationship between our compass frame
// (used by the beacon description, camera_announce, turn_announce) and
// the engine's 3D audio listener frame. Pillar 3 beacon testing
// uncovered a mismatch where the user perceived the heartbeat as
// coming from a different direction than the description implied. We
// don't know which axis the engine treats as "listener forward" — this
// probe lets the user report the actual perceived direction for each
// compass sector, from which we can build a compensation matrix or
// confirm there's no mismatch at all.
//
// Cue resref: "gui_open" — distinct from BeaconActive's "gui_check" so
// the user can audibly separate probe fires from any in-flight beacon.
//
// Hotkey choice: F10 — unbound in stock kotor.ini. Same Win32-polling
// rationale as the other unbound probes.

#pragma once

namespace acc::probe_audio_frame {

// OnUpdate per-tick poll. Reads VK_F10 + VK_F11 via GetAsyncKeyState,
// edge-detects rising edges, self-gates on foreground window + player
// loaded.
//
// F10: emit the next sector's probe cue (advances through N → NE → … →
//      NW → repeat).
// F11: emit a FIXED probe cue always at compass-North, 5m. Used to test
//      whether the engine's listener orientation is camera-driven vs
//      character-driven: press F11, rotate camera, press F11 again — if
//      the pan moved with camera rotation, listener is camera-anchored;
//      if it stayed the same, listener is character-anchored (or
//      world-fixed) and camera rotation doesn't affect audio pan.
void PollWin32();

}  // namespace acc::probe_audio_frame
