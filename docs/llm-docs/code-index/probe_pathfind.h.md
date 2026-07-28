# probe_pathfind.h (64 lines)

Header for the F9 path-data RE probe ("Phase 5 lay-off 1"). Diagnostic-only;
documents that the wired call sites in `core_tick` are the only consumers and
the TU is expected to be removed after a single in-game investigation
session.

## Declarations (in source order)

- L55 — `void PollWin32()`
  note: F9, unbound in stock kotor.ini; dumps pre-dispatch state then dispatches a synthetic WalkTo 10m ahead.
- L61 — `void Tick()`
  note: fires the scheduled post-dispatch dump cascade at t+100/500/1500/3500ms; disarms after the final dump or on creature loss.
