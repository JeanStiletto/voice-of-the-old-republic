# interact_hotkey.h (31 lines)

Enter / Shift+Enter — interact with the currently focused object. Routes through the engine's native click pipeline rather than building action nodes from scratch. Self-gates on player loaded; view_mode owns Enter routing while active.

## Declarations (in source order)

- L17 — `namespace acc::interact`
- L23 — `void PollHotkey()`
  note: self-gates on !view_mode::IsActive; resolves target via narrated_target unified slot
- L29 — `void DispatchInteract(void* target, uint32_t handle, bool forceRadial)`
  note: public seam for view_mode to reuse exact dispatch path; target=server-side CSWSObject*
