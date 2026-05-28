# engine_levelup.h (21 lines)

Level-up screen trigger. Calls CGuiInGame::ShowLevelUpGUI @0x0062dc00 (same surface the Charakterblatt btn_levelup click reaches via CSWGuiInGameCharacter::ShowLevelUpGUI @0x006b0bb0). Works without the InGameCharacter panel constructed.

## Declarations (in source order)

- L15 — `namespace acc::engine_levelup`
- L19 — `bool TriggerLevelUp()`
  note: true iff chain resolved and dispatched without SEH; engine-side can-level-up gates run inside ShowLevelUpGUI itself
