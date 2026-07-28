# camera_spin_diag.h (31 lines)

Header for the camera-spin guard + diagnostic. Documents the confirmed driver (cursor-in-edge-band → `CSWCModule::AcclTurnCamera` @0x640090 fires every frame with no input) and the fix (nudge cursor away from the band via a direct field write when live rotation is detected, gated so it never disturbs a paused menu).

## Declarations (in source order)

- L29 — `void Tick()` — kept in to confirm the guard fires and track camera side-effects (episode-based logging under tag CameraSpinDiag)
