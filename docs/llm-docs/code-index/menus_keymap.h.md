# menus_keymap.h (38 lines)

Header for the engine Key-Mapping screen accessibility layer. Documents the two-level tab/list shape and that the engine's own SetCaptureEvent/OnAcceptClick own persistence — this TU never touches swkotor.ini directly.

## Declarations (in source order)

- L27 — `bool acc::menus::keymap::IsKeyMapPanel(void* panel)`
- L32 — `bool acc::menus::keymap::HandleInput(void* activePanel, int param_1, int param_2, int& outRv)` — rv: 0 = pass to engine, 1 = consumed
- L36 — `void acc::menus::keymap::Tick()` — detects capture completion, re-announces the bound row; no-op off screen
