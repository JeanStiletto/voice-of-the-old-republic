// Stealth distance readout.
//
// While the active leader has Stealth mode engaged AND a hostile creature is
// the current narrated-target focus, speak the 2-D distance (bare number, in
// metres) each time it changes by at least kStepMeters as the gap closes or
// widens. Purpose: a blind player sneaking toward an enemy hears the closing
// distance, so they know when they are inside Sneak Attack range (< 10 m,
// where a strike from stealth lands the passive bonus damage).
//
// Silent in all normal play — the module is inert unless the leader is
// actually stealthed AND a hostile creature is focused. Reads only (leader
// stealth_mode byte, focus distance); no hooks, driven from the per-tick
// dispatcher.
#pragma once

namespace acc::stealth_watch {

void Tick();

}  // namespace acc::stealth_watch
