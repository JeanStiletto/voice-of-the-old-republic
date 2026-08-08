// KOTOR 1: the sticks WALK the character and TURN the camera.
//
// Why this exists on one game only
// --------------------------------
// KOTOR 2's engine moves the player from the left stick and rotates the camera
// from the right one, natively — the mod never even sees those axes, and its
// only interest in the left stick is knowing that the player is walking
// (pad::StickMoving, read by the drive-loop suppression and the autowalk
// cancel). KOTOR 1's engine has no gamepad path at all, so nothing walks the
// character unless this module does.
//
// How
// ---
// By holding the player's own bound movement keys down, as DirectInput
// scancodes (key_inject). That is the same mechanism camera_orient's snap-turn
// has always used, and it is the only one that works: the engine consumes its
// movement and turn axes inside its own per-frame update, so writing to them
// from an out-of-band tick does not move anything.
//
// The stick is quantised to the eight directions a keyboard player already
// gets — forward/back on Action280, strafe on Action281, camera rotate on
// Action284 — with a press/release hysteresis so a stick resting near a
// boundary cannot chatter a key. It is not analog: KOTOR 1 has no analog
// movement to drive, so "how far the stick is pushed" has nothing to control.
//
// Safety
// ------
// Every key this module presses, it is responsible for releasing. It releases
// on the dead-zone edge, on losing foreground, on leaving the world (a menu, a
// dialog, an area load), when the pad stops answering, and when camera_orient
// wants the turn key for its own snap-turn. A stranded held key would type
// into whatever window the player switched to, so that list is the module's
// whole risk surface and every exit runs through one ReleaseAll().

#pragma once

namespace acc::pad::movement {

// Per-tick. Reads the stick sample pad::Tick() left behind and presses or
// releases accordingly. No-op on KOTOR 2 and whenever no pad has been seen.
void Tick();

// Drop every synthesised key immediately. Called by Tick's own guards; exposed
// so a teardown path can guarantee nothing is left held.
void ReleaseAll();

}  // namespace acc::pad::movement
