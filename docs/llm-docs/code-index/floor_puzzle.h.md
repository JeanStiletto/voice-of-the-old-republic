# floor_puzzle.h (67 lines)

Header for the Rakatan temple floor-plate puzzle assist (module unk_m44ab).
Documents the puzzle model (decompiled k_punk_floor01..09 / k_punk_reset:
each plate toggles itself + orthogonal neighbours, lit state = NWScript local
boolean 10) and the announcement surface. Pure poll, hook-vs-poll principle
(state observation), driven from core_tick.

## Declarations (in source order)

- L47 — `namespace acc::floor_puzzle`
- L49 — `void Tick()`
- L59 — `bool IsPuzzlePlateTrigger(uint32_t handle)` — used by filter_objects to exclude plates from the Transition cycle category
- L65 — `bool IsActive()` — used by the input pipeline to route bare R to the board readout
