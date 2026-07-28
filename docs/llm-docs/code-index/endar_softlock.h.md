# endar_softlock.h (43 lines)

Endar Spire Command Module softlock guard + diagnostic net: speaks graduated guidance if the room3→room5→bridge sequence stalls (end_door16 stays locked with no reachable enemies), and edge-logs plot-state so future stuck reports carry the exact sequence. Never edits engine state or stands in for an engine call — narration + logging only.

## Declarations (in source order)

- L22 — `void OnAreaChanged(void* area)`
  note: from transitions on area change; resets per-visit state, logs entry snapshot when entering the Command Module
- L26 — `void Tick()`
  note: per-frame from core_tick; flushes pending spoken hint + throttled plot-state edge-log
- L40 — `void RegisterMsgRule()`
  note: hangs guidance off the engine's own "This object is locked" report rather than interact dispatch — a wrong guess at dispatch time could suppress the story trigger attached to the open attempt (how v0.6.0/v0.6.1 stranded players at end_door19/end_door10_cut2)
