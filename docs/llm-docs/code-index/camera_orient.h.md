# camera_orient.h (30 lines)

Header for the N-key camera-orient hotkey. Documents the SendInput-scancode drive mechanism (bound-key aware via engine_keymap, honours rebinds) and that camera_announce stays muted while a rotation is in flight, announcing only the final direction on release.

## Declarations (in source order)

- L23 — `void Tick()` — per-tick poll + state-machine driver, cheap when idle
- L28 — `bool IsActive()` — true while auto-rotation in flight; lets camera_announce distinguish player-held-turn from auto-drive
