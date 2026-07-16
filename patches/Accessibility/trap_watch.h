// Trap ("mine") detection watcher — sighted-parity trap awareness.
// Engine model: docs/llm-docs/mine-trap-model.md.
//
// The engine tracks per-trap "detected-by" id lists (Awareness checks in
// CSWSCreature::UpdateMineCheck) and shows sighted players a red ground
// overlay for every detected hostile mine. This module mirrors exactly
// that state — a trap counts as visible when its detected-by list holds a
// party member — so blind players learn about a trap at the same moment a
// sighted player sees it appear, never earlier.
//
// Three outputs:
//   1. Ground mines (trigger kind): the engine already emits a feedback
//      line ("<name> entdeckt <Minenname>: ...") through the combat
//      message funnel. We only enrich it — combat.cpp's RuleMineDetect
//      correlates the line with the freshest new detection via
//      PeekFreshMine/ConsumeFreshMine and appends clock direction +
//      distance.
//   2. Trapped doors/placeables: the engine broadcasts NO feedback line
//      for those (UpdateMineCheck only messages in the trigger branch).
//      We announce "Falle entdeckt: <name>, auf X Uhr, Y Meter" on the
//      detected transition ourselves.
//   3. Proximity warning: a detected ground mine within a small radius
//      speaks its name + clock + metres ONCE per approach (re-arms only
//      after walking away) — deliberately not repeating, per user
//      decision: you either disarm it or step over it on purpose.

#pragma once

#include <cstddef>

#include "engine_offsets.h"  // Vector

namespace acc::trap_watch {

// Per-frame tick (internally throttled). Scans the current area's
// trappable objects for detected-state transitions and runs the
// proximity warning.
void Tick();

// Rate-limited forced rescan (min 100ms spacing). The engine appends the
// mine-detect feedback line in the same tick it updates the detected-by
// list, which can beat the throttled scan; combat.cpp's RuleMineDetect
// calls this before concluding no detection is pending.
void ScanNow();

// Freshest not-yet-consumed ground-mine detection (recorded when a
// trigger trap's detected-by list first gains a party member; entries
// expire after a few seconds). Returns false when none pending.
bool PeekFreshMine(char* nameOut, size_t nameSize, Vector& posOut);
void ConsumeFreshMine();

}  // namespace acc::trap_watch
