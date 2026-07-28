# spectator_scene.h (33 lines)

Scoped special-case narration for the Endar Spire scripted "spectator battle"
(module END_M01AA, tag prefix `end_cut2_soldier*`): a doomed Republic-vs-Sith
firefight beyond a walkmesh gap the player cannot reach. Deliberately
hard-scoped by creature-tag whitelist rather than a general "unreachable
combat" detector, to keep false positives at zero until more such scenes turn
up.

## Declarations (in source order)

- L20 — `bool IsScriptedBattleSoldier(void* obj)`
  note: case-insensitive tag-prefix match; safe on read fault / no tag
- L26 — `void OnObjectNarrated(void* obj)`
  note: first-sight funnel hook from passive_narrate::NarrateHandle; no-op after first narration this area visit
- L31 — `const char* DramaticLine()`
  note: localised via Id::SpectatorBattleDoomed
