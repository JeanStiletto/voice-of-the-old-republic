# peek_description.h (37 lines)

Public surface for the Shift+Up/Down description-peek feature. Adding a new
panel is a one-line entry in the .cpp's panel registry.

## Declarations (in source order)

- L23 — `bool HandleShiftArrow(int param_1, int param_2, void* activePanel, void* focusedControl)`
  note: returns false to let the event pass through to plain nav.
- L27 — `void OnShiftReleased()`
- L34 — `bool SpeakItemRowDescription(void* row)`
  note: used for Enter on quest-item rows, mirroring Enter on a journal quest row.
