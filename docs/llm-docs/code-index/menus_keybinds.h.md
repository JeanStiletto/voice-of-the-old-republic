# menus_keybinds.h (57 lines)

Header for the mod keybind configurator. Documents it as an entirely virtual submenu mirroring the engine Key Mapping screen's two-level shape, and that capture is our own Win32 poll (the engine's SetCaptureEvent only serves engine panels).

## Declarations (in source order)

- L25 — forward decl `namespace acc { namespace hotkeys { enum class Action : int; } }`
- L33 — `const char* acc::menus::keybinds::DisplayName(acc::hotkeys::Action action)` — "" if not in the catalogue
- L36 — `bool acc::menus::keybinds::IsOpen()`
- L40 — `void acc::menus::keybinds::Open()`
- L46 — `bool acc::menus::keybinds::HandleInput(int keyCode)` — flips IsOpen() false on Esc-at-category-level
- L50 — `void acc::menus::keybinds::Tick()`
- L55 — `void acc::menus::keybinds::Reset()` — force-close without speech, called when the whole Mod-settings submenu tears down
