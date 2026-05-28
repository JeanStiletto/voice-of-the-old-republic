# announce_degrees.h (20 lines)

On-demand exact-heading hotkey (AltGr / Right Alt). Speaks camera facing in
compass degrees. In-world frame by default; map-frame when InGameMap is
foreground. Uses Win32 polling because AltGr is unbound in stock kotor.ini.

## Declarations (in source order)

- L16 — `namespace acc::announce_degrees`
- L18 — `void PollWin32()`
  note: reads camera yaw and dispatches to world or map variant based on active panel
