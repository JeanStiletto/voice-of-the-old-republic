# probe_priority_groups.h (37 lines)

Header for the priority-groups one-shot audio table dump. Documents the
output format (one log block per session) and that it's a one-shot: arms
internally, dumps once, then becomes a permanent no-op.

## Declarations (in source order)

- L34 — `void Tick()`
  note: called from core_tick.cpp's Dispatch() after combat::TickCombatMode so the engine is stable before reading.
