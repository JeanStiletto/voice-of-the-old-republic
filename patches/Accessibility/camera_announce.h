// Camera-direction announce.
//
// KOTOR's default control scheme: A / D rotate the camera around the
// character (NOT character facing, NOT strafe); W moves the character in
// the camera's forward direction. Without this announcer the user can't
// tell where the camera is pointing until they press W and the character
// snaps to face the camera.
//
// Yaw is derived per tick from the camera and player world positions —
// the KOTOR orbital camera always looks at the character, so look
// direction = normalize(player - camera). 8 × 45° compass sectors with
// 5° hysteresis around the last spoken sector. Urgent SAPI speech routes
// around NVDA's typed-char cancel since the user is holding A/D.

#pragma once

namespace acc::camera_announce {

void Tick();

// Most recent observed camera yaw in engine frame (0° = +X, CCW positive).
// False until Tick has anchored at least once.
bool TryGetCameraEngineYawDegrees(float& out);

// One-shot facing readout for event triggers (e.g. a door beginning to open)
// that want to re-orient the user after the engine autoturns them at their
// target. Speaks the player's current camera-facing sector, EXCEPT when the
// SAME sector was already spoken within `dedupMs` — i.e. the per-tick
// direction-change announce from the autoturn already said it, so we stay
// silent to avoid doubling. Speaking shares state with the per-tick announcer
// (last-spoken sector + timestamp) so it won't immediately repeat the readout.
//
// Returns true if it spoke. No-op (false) when camera facing is unavailable
// (menu / area-load transient / degenerate geometry) or an engine cinematic is
// driving the camera.
bool AnnounceCurrentFacing(unsigned int dedupMs);

}  // namespace acc::camera_announce
