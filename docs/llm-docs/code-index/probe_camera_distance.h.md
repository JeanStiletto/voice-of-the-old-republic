# probe_camera_distance.h (67 lines)

Camera-distance probe — hunts for the addressable orbit-radius field and
tests whether direct writes survive the engine's per-frame update.

Purpose: feasibility check for "Option B" — collapsing camera distance to
~0 so the engine's camera-anchored audio listener ends up at the player
without detouring SetListenerPosition/Orientation.

Engine surfaces documented in the header:
- CSWCameraOnAStick.field46_0x110 — target orbit radius
- CSWCameraOnAStick.field29_0x84 — auto-fit recompute flag (stomps our writes)
- CSWCModule::ZoomCamera @0x006401d0
- Camera::GetDist @0x0045c1d0

Hotkeys (Ctrl-modified to avoid colliding with plain-F-key probes):
- Ctrl+F12 — one-shot snapshot (ProbeCameraDistDump)
- Ctrl+F11 — cycle clamp mode OFF/0.0m/0.5m/2.0m (ProbeCameraDistClampToggle)

Strips out once Option B is confirmed viable or shown unworkable.

## Declarations (in source order)

- L60 — `namespace acc::probe_camera_distance`
- L65 — `void Tick()`
  note: reads hotkey edges; when clamp mode active, writes + reads back field46_0x110 each tick and tracks stomp rate
