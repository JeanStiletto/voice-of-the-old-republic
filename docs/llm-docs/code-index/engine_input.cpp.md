# engine_input.cpp (104 lines)

Implementation of engine_input.h. No leading comment block.

## Declarations (in source order)

- L6 — `namespace acc::engine`
- L11 — `const char* InputIndexName(int code)`
  note: contains static 132-entry k_names table covering MOUSE_*, KEYBOARD_*, JOYSTICK_*; also handles logical code 0xCE ("LOGICAL_TAB")
- L92 — `int ManagerTranslateCode(int code)`
