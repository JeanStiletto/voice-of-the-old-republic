# combat.h (35 lines)

Poll-based combat event narration. Channels: combat mode entry/exit (debounced),
combat log (messages_listbox poll via CGuiInGame), attack resolution (attacks_list[7]
diff), saving throws (skeleton). Each Tick is cheap and idle when nothing is happening.

## Declarations (in source order)

- L19 — `namespace acc::combat`
- L22 — `bool IsCombatActive()`
  note: cross-feature gate; returns false on chain fault, not only on peaceful state
- L24 — `void TickCombatMode()`
  note: Phase 1A — debounced combat-mode enter/exit; speaks CombatBegins / CombatEnds
- L25 — `void TickCombatLog()`
  note: Phase 1B — polls CSWGuiInGameMessages.messages_listbox for new rows (log-only, no speech — live narration is via OnAppendToMsgBuffer hook)
- L29 — `void TickAttackResolutions()`
  note: Phase 4A — player creature only; diffs attacks_list[7] for 0->resolved transitions
- L33 — `void TickSavingThrows()`
  note: Phase 4B skeleton — no-op; real signal needs hook on SavingThrowRoll
