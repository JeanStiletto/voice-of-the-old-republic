# engine_levelup.cpp (151 lines)

Implementation of engine_levelup.h. No leading comment block.

## Declarations (in source order)

- L9 — `namespace acc::engine_levelup`
- L11 — `namespace { ... }` (anonymous, TU-local helpers)
- L51 — `typedef uint32_t (__thiscall* PFN_ShowLevelUpGUI)(void*, int)`
- L52 — `typedef void (__thiscall* PFN_SetLevelUpMode)(void*, int)`
- L57 — `void* GetInGameCharacterPanel(void* gui)`
  note: reads CGuiInGame slot +0x14; returns nullptr if panel was never instantiated this session
- L72 — `bool SetLevelUpMode(void* gui, int mode)`
  note: 0 = block level-ups, 1 = allow; calls engine setter rather than poking field directly
- L87 — `bool TriggerLevelUp()`
  note: sets level_up_mode=1, tries CSWGuiInGameCharacter path first, falls back to CGuiInGame variant
