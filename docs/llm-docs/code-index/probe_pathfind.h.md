# probe_pathfind.h (63 lines)

Phase 5 lay-off 1 — path-data RE probe.

Diagnostic-only. F9 fires a "synthetic WalkTo without speech": dispatches
acc::guidance::WalkTo to a point 10m ahead of the player, then dumps the
relevant engine state across a tick cascade to locate where the engine
stores its computed path waypoint list.

What gets dumped:
1. Pre-dispatch: CSWSCreature+0x340 region (via pointer deref to
   CPathfindInformation), CSWSArea path_points/path_connections triples
   + sample data.
2. Post-dispatch cascade at t+100ms, t+500ms, t+1500ms, t+3500ms:
   CPathfindInformation struct (0x280 bytes); diffing pinpoints which
   bytes the pathfinder flipped.

Design fork context: if the engine's path list is readable, the Pillar 3
nav feature reads it directly; otherwise we re-solve A* over the per-area
path graph ourselves.

Once the probe has informed the design, this TU + F9 hotkey go away.

## Declarations (in source order)

- L45 — `namespace acc::probe_pathfind`
- L55 — `void PollWin32()`
  note: F9 rising edge; dumps pre-dispatch state, dispatches WalkTo 10m ahead, arms cascade
- L61 — `void Tick()`
  note: fires up to four scheduled dumps at t+100/500/1500/3500ms; disarms after final dump or on creature loss
