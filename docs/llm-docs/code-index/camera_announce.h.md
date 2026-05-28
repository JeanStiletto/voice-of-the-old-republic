# camera_announce.h (25 lines)

Camera-direction announce. Derives yaw per tick from orbital camera position
(player - camera vector). 8 x 45 degree compass sectors with 5 degree hysteresis.
Urgent SAPI speech to survive NVDA typed-char cancel while A/D is held.

## Declarations (in source order)

- L17 — `namespace acc::camera_announce`
- L19 — `void Tick()`
- L23 — `bool TryGetCameraEngineYawDegrees(float& out)`
  note: returns engine-frame yaw (0=+X, CCW+), not compass degrees; false until Tick anchors
