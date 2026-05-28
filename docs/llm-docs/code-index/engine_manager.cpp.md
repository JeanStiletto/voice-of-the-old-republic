# engine_manager.cpp (143 lines)

Implementation of engine_manager.h. No leading comment block.

## Declarations (in source order)

- L10 — `namespace acc::engine`
- L22 — `static bool IsTransparentForegroundKind(PanelKind k)`
  note: returns true for PanelKind::Fade; prevents stale Fade overlays from being returned as the interactive foreground panel
- L31 — `bool IsPanelInManager(void* panel)`
- L55 — `void* FindOwningPanel(void* control)`
  note: cap is 256 children per panel — CSWGuiInGameCharacter has 60+ children so the prior 32-cap masked it
- L85 — `void* GetForegroundPanel(void* mgr)`
- L114 — `void LogManagerStack(void* mgr, const char* tag)`
