# turret_game.h (37 lines)

Public surface for the turret/space-combat gunner minigame accessibility
module. Shares the `CSWMiniGame` struct/vtable with swoop racing
(`CSWCArea.mini_game` at +0x264); distinguished by `type` (+0x80):
swoop=1, turret=2, engine-confirmed via `CSWMiniGame::Load`. Aim lives at
`CSWMiniPlayer.offset` (+0x1c4): offset.x=elevation, offset.z=azimuth,
degrees — engine re-integrates the field each tick so a write steers the
gun. Crosshair reads the gun's `bullethook0` node world direction (the
literal bolt fire line). Same latch-and-debounce detection pattern as
swoop_race.cpp.

## Declarations (in source order)

- L30 — `namespace acc::turret_game`
- L33 — `void Tick()`
  note: no-op when not in the turret minigame; cheap when idle
- L35 — `bool IsActive()`
