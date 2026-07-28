# engine_levelup.h (49 lines)

Level-up screen trigger. Calls `CGuiInGame::ShowLevelUpGUI` @0x0062dc00
(same surface the Charakterblatt btn_levelup click reaches via
`CSWGuiInGameCharacter::ShowLevelUpGUI` @0x006b0bb0). Works without the
InGameCharacter panel constructed. Talks to engine_panels (foreground/panel
queries) and engine_subscreen (overlay pause while the wizard is open).

## Declarations (in source order)

- L15 — `namespace acc::engine_levelup`
- L24 — `bool PlayerCanLevelUp()`
  note: mirrors CSWSCreatureStats::CanLevelUp; false is the conservative default when the leader can't be resolved.
- L31 — `bool TriggerLevelUp()`
  note: refuses (false, level_up_mode untouched) when PlayerCanLevelUp() is false — the engine's ShowLevelUpGUI only gates on level_up_mode, which is forced to 1, so without this guard XP is never actually checked and the wizard opens endlessly.
- L41 — `void TickLevelUpPause()`
  note: per-frame maintenance; releases the BeginOverlayPause(LevelUp) freeze once the wizard panel is observed gone. Inert when no pause is held.
- L47 — `bool IsOpeningLevelUp()`
  note: true only during the synchronous ShowLevelUpGUI dispatch — lets SkillInfoBoxTitleOverride speak the level-up hint before HasActiveLevelUpPanel() would report true.
