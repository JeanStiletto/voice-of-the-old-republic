# view_mode.h (46 lines)

View mode (B hotkey — chosen because V is stock-bound to Solo Mode and B
is unbound). Freezes the character in place while keeping the camera
live: A/D still pan the camera natively (KOTOR's A/D only rotates the
camera; the character snaps to face it only on a W/S commit), while W/S/
A/D instead drive a virtual cursor. The 3D listener is rerouted to the
cursor position via a detour on CExoSound::SetListenerPosition. Enter /
Shift+Enter act on whatever the cursor is hovering over.

## Declarations (in source order)

- L17 — `namespace acc::view_mode`
- L19 — `bool IsActive()`
- L24 — `bool ConsumedEnterThisTick()`
  note: read-and-clear flag; interact_hotkey checks this to avoid double-dispatch after PollEnter exits view mode synchronously
- L28 — `bool TryGetCursorPosition(Vector& out)`
- L33 — `bool GetEffectiveOrientationYawDegrees(float& out)`
  note: returns camera yaw while active, player yaw otherwise; used by Pillar 1 cone and similar cue systems
- L38 — `void PollWin32()`
  note: bare B toggles view mode; Shift+B dumps CClientOptions to log (diagnostic only)
- L44 — `void Tick()`
  note: must run after camera_announce::Tick and PollWin32, and before interact_hotkey::PollHotkey
