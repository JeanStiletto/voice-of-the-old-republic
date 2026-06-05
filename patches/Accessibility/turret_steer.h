// Turret (gunner) minigame — barrel steering by synthesized keyboard input.
//
// STATUS 2026-06-05: DISABLED / MOOT for the turret. We later proved the gun is
// FIXED and the world rotates around it (CSWMiniPlayer +0x240 orientation is a
// constant identity; bolts fire a near-fixed world line, unrelated to +0x1c4 in
// both manual and auto play). You cannot "turn" a fixed gun, so this WASD-
// synthesis controller does not aim it — only ReleaseAll() is still called, to
// guarantee no key is left held. The cue now references the live-measured fire
// line instead (turret_game.cpp). Re-doing aim-assist / Autoaiming means driving
// whatever input rotates the WORLD to bring the locked fighter onto that fixed
// fire line — a different control target than this module assumes. Kept (not
// deleted) as the scaffolding for that rework. See
// project_turret_aim_write_does_not_steer_bolts.md.
//
// (Original rationale, now superseded:) writing +0x1c4 doesn't steer bolts, so
// the idea was to drive the native WASD input channel via SendInput scancodes
// (as camera_orient.cpp rotates the in-world camera). That moved +0x1c4 but not
// the bolts — because the gun is fixed, not because the input was wrong.
//
// Two ownership modes (decided by the caller from the TurretAutoAim toggle):
//   - FULL AUTOAIM: unconditional. The controller owns A/D/W/S and tracks the
//     locked fighter by itself.
//   - ASSIST (magnetism default): yields to a SUSTAINED manual swing. Quick
//     stutter-taps still get corrected (the controller nudges in the gaps and
//     against brief wrong-way taps); but once the player HOLDS an axis key past
//     a short threshold ("I really want to swing around"), the controller backs
//     off that axis until a beat after they let go.
//
// The key->aim-direction mapping is SELF-CALIBRATED at runtime (hold a key,
// watch which way the aim moves) so we don't hard-code the minigame's azimuth/
// elevation sign convention.

#pragma once

namespace acc::turret_steer {

// Drive the gun toward the target for this tick.
//   curAz/curEl   current aim azimuth/elevation in degrees (read from +0x1c4).
//   tgtAz/tgtEl   desired (lead-corrected) intercept azimuth/elevation degrees.
//   deadbandDeg   on-target tolerance; an axis is released inside it (the
//                 hitbox subtend — same value the cue uses).
//   engage        whether steering is permitted this tick (false => release
//                 all keys). Caller sets it from the assist-zone / lock state.
//   fullAuto      true = full-autoaim (own the keys); false = assist (yield to
//                 a sustained manual hold).
//   gameForeground  only inject when the game window is foreground (SendInput
//                   is OS-global; never spray keys into another app).
void Tick(float curAz, float curEl, float tgtAz, float tgtEl,
          float deadbandDeg, bool engage, bool fullAuto, bool gameForeground);

// Release any synthetic keys we are holding. Call on lock loss, out-of-view,
// no valid aim, and turret entry/exit so a key is never stranded down.
void ReleaseAll();

}  // namespace acc::turret_steer
