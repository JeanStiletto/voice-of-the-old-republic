# view_mode.h (46 lines)

View mode (B hotkey). Freezes the character in place while keeping the camera
live. W/S drive a virtual cursor instead of the character. 3D listener is
rerouted to the cursor position via a detour on CExoSound::SetListenerPosition.
Enter / Shift+Enter act on whatever the cursor is hovering over.

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
