# probe_camera_distance.h (68 lines)

Header for the camera-distance clamp probe. Documents the full AppManager ->
Camera chain and behavior field offsets, and the engine primitives
(`ZoomCamera`, `Camera::GetDist`) preferred over raw writes for validation.
Diagnostic-only TU intended to be removed once Option B (audio-listener
collapse via camera-distance clamp) is confirmed viable or shown unworkable.

## Declarations (in source order)

- L65 — `void Tick()`
  note: self-gates on AppManager chain non-null; Ctrl+F12 dump / Ctrl+F11 clamp-mode cycle.
