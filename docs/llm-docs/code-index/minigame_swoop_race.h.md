# swoop_race.h (35 lines)

Public surface for the swoop-race minigame accessibility module: a per-tick
poll of `CSWCArea.mini_game`, entry/exit narration, and hand-off to
`swoop_spatial_audio` for continuous obstacle/accelpad cues. Documents the
CSWMiniGame/CSWMiniPlayer offsets used by the .cpp and the detection
rationale (engine pointer, not area-tag, because CSWMiniGame is shared with
the turret minigame — distinguished by `type` 1 vs 2).

## Declarations (in source order)

- L28 — `namespace acc::swoop_race`
- L31 — `void Tick()`
  note: no-op when not in a minigame; called every engine tick
- L33 — `bool IsActive()`
