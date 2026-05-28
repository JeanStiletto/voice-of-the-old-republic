# engine_input.h (44 lines)

Engine input-code translation. Pure read-side; no engine state access.

## Declarations (in source order)

- L4 — `namespace acc::engine`
- L8 — `const char* InputIndexName(int code)`
  note: returns human-readable name for InputIndices enum values; "?" for unknown, "INPUTDEVICE_NONE" for -1
- L12 — `int ManagerTranslateCode(int code)`
  note: translates KOTOR logical-action codes that CSWGuiManager::HandleInputEvent rewrites; unknown codes pass through unchanged
- L17 — `constexpr int kInputNavUp    = 0xb6`
- L18 — `constexpr int kInputNavDown  = 0xb7`
- L19 — `constexpr int kInputNavLeft  = 0xb8`
- L20 — `constexpr int kInputNavRight = 0xb9`
- L21 — `constexpr int kInputEnter1   = 0xb5`
- L22 — `constexpr int kInputEnter2   = 0xbb`
- L23 — `constexpr int kInputEsc1     = 0xb4`
- L24 — `constexpr int kInputEsc2     = 0xdf`
- L25 — `constexpr int kInputActivate = 0x27`
  note: KEYBOARD_F1; the engine's activate code post-translation
- L30 — `constexpr int kInputHome     = 32`
- L31 — `constexpr int kInputEnd      = 33`
- L36 — `constexpr int kInputKbLeftShift  = 24`
- L37 — `constexpr int kInputKbRightShift = 25`
- L38 — `constexpr int kInputKbComma      = 103`
- L39 — `constexpr int kInputKbPeriod     = 104`
- L40 — `constexpr int kInputKbAnnounce   = 105`
  note: physical key right of `.` (KEYBOARD_SLASH) — labelled `-` on QWERTZ, `/` on QWERTY; contiguous with `,` `.`
