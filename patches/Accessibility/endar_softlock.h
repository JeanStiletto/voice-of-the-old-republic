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
// It never edits engine state, and never stands in for an engine call —
// narration + logging only.

namespace acc::endar {

// From transitions on area change. Resets per-visit state; logs an entry
// snapshot when the new area is the Command Module.
void OnAreaChanged(void* area);

// Per-frame, from core_tick. Flushes any pending spoken hint and (throttled)
// edge-logs plot-state changes. No-op outside END_M01AA once flushed.
void Tick();

// Register the door-guidance rule with the feedback router. Call once,
// alongside the other Register*MsgRule() calls from the router bootstrap.
//
// The guidance hangs off the engine's own "This object is locked" report, NOT
// off our interact dispatch. That distinction is the whole point: a door hint
// keyed on a tag at dispatch time has to guess whether the door is dead, and a
// wrong guess that returns before the engine call suppresses the story trigger
// attached to the open attempt (see the WARNING in interact_hotkey.cpp — that
// is exactly how v0.6.0/v0.6.1 stranded players at end_door19 and
// end_door10_cut2). Keyed on the engine's report instead, a hint can only ever
// speak when the door genuinely refused to open, and it never displaces the
// attempt itself.
void RegisterMsgRule();

}  // namespace acc::endar
