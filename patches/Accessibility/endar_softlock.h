#pragma once

// Endar Spire Command Module (END_M01AA) softlock guard + diagnostic net.
//
// The tutorial's room-3 -> room-5 -> bridge sequence is scripted: the west
// corridor door to the bridge (tag end_door16) only unlocks once the room-5
// fight is cleared. If that sequence stalls (e.g. a save/reload mid-cutscene),
// the door stays locked with no reachable enemies and no in-world explanation,
// and blind players — guided toward the bridge by the beacon — poke the door
// repeatedly and give up. This module:
//   1. Speaks graduated guidance when the player keeps trying end_door16 while
//      room 5 is uncleared (helpful first cue; reload advice after repeats).
//   2. Edge-logs the plot-state timeline while in the module, so the next
//      stuck report carries the exact sequence that derailed.
// It never edits engine state — narration + logging only.

namespace acc::endar {

// From transitions on area change. Resets per-visit state; logs an entry
// snapshot when the new area is the Command Module.
void OnAreaChanged(void* area);

// Per-frame, from core_tick. Flushes any pending spoken hint and (throttled)
// edge-logs plot-state changes. No-op outside END_M01AA once flushed.
void Tick();

// From the interact door-dispatch with the door's blueprint tag. Drives the
// room-5 door guidance. Cheap no-op for every tag but end_door16.
void NoteDoorInteract(const char* tag);

}  // namespace acc::endar
