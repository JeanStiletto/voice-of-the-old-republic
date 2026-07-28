# combat_special_watch.h (21 lines)

The "you can act now" tactical audio heartbeat. Counts party-wide "specials" (any queued action that isn't a routine hostile auto-attack) each tick; a falling edge to zero fires immediately, and while it stays at zero a 6-second heartbeat repeats. A first-round quiet gate keeps the opening ~6s silent so "Kampf beginnt" gets clean air. Resets across combat enter/exit.

## Declarations (in source order)

- L16 — `namespace acc::combat::special_watch`
- L19 — `void Tick()`
  note: cheap out of combat (one chain walk); call after combat::TickCombatMode
