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

}  // namespace acc::camera_announce
