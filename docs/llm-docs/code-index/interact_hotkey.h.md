# interact_hotkey.h (32 lines)

Enter/Shift+Enter interact-with-focus. Routes through the engine's native
click pipeline (SetLastClickedOnTarget + HandleMouseClickInWorld) rather than
building a CSWSObjectActionNode by hand, avoiding the need to RE that
PlaceHolder struct. Self-gates on player-loaded so menus/chargen pass Enter
through untouched.

## Declarations (in source order)

- L23 — `void PollHotkey()`
  note: additionally self-gates on !view_mode::IsActive — view mode owns Enter routing when its cursor hover is the truth
- L29 — `void DispatchInteract(void* target, uint32_t handle, bool forceRadial)`
  note: public so view_mode can reuse the exact dispatch path; target is the server-side CSWSObject*, handle its matching server handle
