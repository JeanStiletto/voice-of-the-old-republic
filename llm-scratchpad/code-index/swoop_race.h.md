# swoop_race.h (33 lines)

Swoop race minigame accessibility. Polls CSWCArea.mini_game (+0x264) once per tick. Entry/exit announce + keybind cheat sheet; gear-shift announce; continuous obstacle/accelerator-pad cues. Latches the CSWMiniGame pointer on first detect to avoid churn during race-start transition.

## Declarations (in source order)

- L26 — `namespace acc::swoop_race`
- L29 — `void Tick()`
  note: no-op when not in a minigame; cheap when idle
- L31 — `bool IsActive()`
