# engine_manager.cpp (145 lines)

Implementation of engine_manager.h. No leading comment block.

## Declarations (in source order)

- L10 — `namespace acc::engine`
- L22 — `static bool IsTransparentForegroundKind(PanelKind k)`
  note: true only for PanelKind::Fade — the screen-fade overlay the engine leaves stuck at panels[top] after an area transition; fg routing must see through it or Esc falls through to the quit-confirm dialog.
- L31 — `bool IsPanelInManager(void* panel)`
  note: scans both panels[] (cap 32) and modal_stack (cap 32).
- L55 — `void* FindOwningPanel(void* control)`
  note: caps panelCount at 16, per-panel children at 256 (raised from 32 — was masking CSWGuiInGameCharacter's 60+ children from AnnounceControl's owner resolution).
- L85 — `void* GetForegroundPanel(void* mgr)`
  note: walks panels[] top-down skipping IsTransparentForegroundKind entries; returns the actual top if every entry is null/transparent.
- L114 — `void LogManagerStack(void* mgr, const char* tag)`
  note: uses acclog::BlockLog — pointer identity IS the log content, so no Key() dedup; any pointer change reprints in full.
