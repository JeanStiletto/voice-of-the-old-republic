# menus_modsettings.h (128 lines)

Header for the virtual "Mod Einstellungen" submenu. Documents the sentinel-pointer chain-entry design (never touches an engine-allocated control), that the submenu itself has no engine panel (menus.cpp's input hook routes keys through HandleInput while IsOpen()), and the three scaffolded toggle options.

## Declarations (in source order)

- L44 — `enum class acc::menus::modsettings::Option { ExtendedCycling, RoomShapes, WallSounds, HumanSubtitles, TurretAutoAim, SkipIntros, CueVolume, UrgentVolume, Keybindings, AudioGlossary, Count }`
- L62 — `void* GetRootAnchor()`
- L68 — `bool IsRootAnchor(void* control)`
- L76 — `void ForEachRootAnchor(void* panel, bool(*callback)(void*,int,int,void*), void* userData)` — fires once for InGameOptions/MainMenuOptions
- L84 — `bool ExtractRootLabel(char* outBuf, size_t bufSize)`
- L88 — `void OpenSubMenu(void* parentPanel)`
- L93 — `bool IsOpen()`
- L98 — `void Close()`
- L112 — `bool HandleInput(int keyCode)` — press-edges only; keyCode is the engine's kInput* value
- L119 — `bool GetToggle(Option option)`
- L126 — `void Tick()` — drives the Audio glossary's delayed-playback timer; safe to call unconditionally
