# combat.h (44 lines)

Public surface for combat-mode entry/exit narration and the combat-log message-bus glue. Distinguishes `IsCombatActive` (controlled-leader-only global) from `IsPartyInCombat` (encounter-level OR across the party) — callers that must not flip on a mid-fight Tab should prefer the latter. The four `Tick*` functions are cheap/idle no-ops absent activity; each flushes a specific debounced burst (absorb, deflect, merged ability/grenade/force effects).

## Declarations (in source order)

- L16 — `namespace acc::combat`
- L25 — `bool IsCombatActive()`
  note: mirrors only the controlled leader's combat bit; flips to peace on Tab to a not-yet-engaged member
- L26 — `bool IsPartyInCombat()`
  note: OR of every party member's per-creature combat bit; the stable "encounter active" signal
- L28 — `void TickCombatMode()`
  note: debounced begin/end "Kampf beginnt"/"Kampf beendet" cue + menu auto-close
- L29 — `void TickCombatLog()`
  note: diagnostic-only poll of the messages listbox; live narration comes from the OnAppendToMsgBuffer hook instead
- L33 — `void TickCombatAbsorb()`
- L38 — `void TickCombatDeflect()`
- L42 — `void TickCombatEffects()`
