# engine_options.cpp (108 lines)

Implementation of engine_options.h. No leading comment block.

## Declarations (in source order)

- L9 — `namespace acc::engine`
- L14 — `void* GetClientOptions()`
  note: walks AppManager → CClientExoApp → CClientExoAppInternal → CClientOptions; distinct chain from GetPlayerServerObject's.
- L37 — `bool GetMouseLook(bool& out)`
- L51 — `bool GetActionMenuAutoPause(bool& out)`
- L65 — `namespace { bool WriteMouseLook(void* options, bool enabled) }` (anonymous)
  note: read-modify-write preserving sibling bits in the +0x8 bitfield.
- L84 — `bool SetMouseLook(bool enabled)`
- L90 — `bool ToggleMouseLook(bool& outNew)`
