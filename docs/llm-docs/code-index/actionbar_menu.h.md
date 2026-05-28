# actionbar_menu.h (33 lines)

Player action bar (Aktionsmenü) submenu for Shift+4..Shift+7. While active:
Up/Down cycles variants, Enter fires, Esc disarms. Bare 4..7 are untouched.
Self-disarms only on chain break or column going inactive.

## Declarations (in source order)

- L16 — `namespace acc::actionbar_menu`
- L19 — `bool Open(int slot)`
  note: slot 0..5; speaks "Spalte ist leer" and returns false on empty column.
- L21 — `bool IsActive()`
- L24 — `bool HandleInputEvent(int code, int value)`
  note: press-edge only; radial gate runs before this (modal precedence).
- L26 — `void ForceDisarm(const char* reason)`
- L30 — `int CurrentSelection(int slot)`
  note: used by the bare-key announce path so 4..7 reads the same variant the engine fires.
