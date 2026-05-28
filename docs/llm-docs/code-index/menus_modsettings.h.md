# menus_modsettings.h (122 lines)

Public surface for the mod-settings virtual overlay (Options screen extension). Declares `namespace acc::menus::modsettings`.

## Declarations (in source order)

- L1 — `namespace acc::menus::modsettings`
- L10 — `enum class Option` — enumerates each settings option (toggle rows + AudioGlossary submenu row)
- L20 — `void* GetRootAnchor()` — returns the address of the static s_rootSentinel byte; chain builder uses it as a virtual entry pointer
- L22 — `bool IsRootAnchor(void* control)` — true when control == &s_rootSentinel
- L24 — `void ForEachRootAnchor(void* panel, bool (*callback)(...), void* userData)` — invokes callback with the sentinel and synthetic sort coordinates (cx=180, cy=9000) when panel is InGameOptions or MainMenuOptions
- L30 — `bool ExtractRootLabel(char* outBuf, size_t bufSize)` — fills outBuf with the localised "Mod settings" label
- L34 — `void OpenSubMenu(void* parentPanel)` — opens the overlay, snapshots engine foreground, speaks opened cue + first option
- L36 — `bool IsOpen()`
- L38 — `void Close()` — closes overlay, speaks closed cue, rebinds chain on parent
- L40 — `bool HandleInput(int keyCode)` — routes Up/Down/Enter/Esc; auto-closes on foreground divergence; blocks Left/Right/Home/End from reaching parent
- L42 — `bool GetToggle(Option option)` — returns current toggle state; returns false for non-toggle rows
- L44 — `void Tick()` — fires a pending audio-glossary sound cue after the 750 ms delay
