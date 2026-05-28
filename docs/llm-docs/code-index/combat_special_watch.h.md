# combat_special_watch.h (21 lines)

Specials-queue heartbeat — "you can act now" audio cue. Edge-fires on party specials
dropping from >=1 to 0; repeats every 6s while specials stay at 0 in continued combat.
First-round 6s gate keeps "Kampf beginnt" clean. Auto-resets on combat exit.

## Declarations (in source order)

- L16 — `namespace acc::combat::special_watch`
- L19 — `void Tick()`
  note: cheap out-of-combat (one IsCombatActive chain walk); call after combat::TickCombatMode
