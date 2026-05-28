# camera_orient.h (26 lines)

Camera-orient hotkey (N). Rotates camera to beacon target or next CW cardinal.
Drive mechanism: synthesised A/D keypresses via DirectInput scancodes (SendInput
with KEYEVENTF_SCANCODE). camera_announce stays muted while IsActive() is true.

## Declarations (in source order)

- L16 — `namespace acc::camera_orient`
- L19 — `void Tick()`
- L24 — `bool IsActive()`
  note: true during auto-rotation AND while N hotkey is physically held; covers camera_announce mute window
