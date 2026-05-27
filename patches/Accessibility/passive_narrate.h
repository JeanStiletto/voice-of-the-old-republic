// Passive-selection narration.
//
// Hooks CClientExoAppInternal::ShowObject — the engine's single point of
// user-facing target change (called by DoPassiveSelection's mouse-hover
// auto-target and SelectNearestObject's Q/E cycle). On a real transition
// the watcher resolves the handle, classifies it through the six-category
// nav filter, and speaks the localised name plus a 3D positional cue.
//
// Hook ShowObject rather than poll GetLastTarget because last_target is
// also written by combat AI (CSWSCreature::CreateNewAttackActions, every
// round, every queued creature) and per-tick polling races that. ShowObject
// is only called by user-driven paths, so it's a clean signal.
//
// Does NOT handle focus-loss announce (silent on sentinel transitions),
// the Q/E cycle itself (cycle_input owns that), or filtering to
// "interesting" — the engine already decided by calling ShowObject; we
// just drop non-nav categories.

#pragma once

#include <cstdint>

namespace acc::passive_narrate {

// ShowObject hook entry. Updates the current-focus cache for Q/E
// re-announce AND drives delta-based ambient narration.
void OnEngineShowObject(uint32_t handle);

// Arm a Q/E re-announce. Deferred to the next Tick because our input
// detour fires before the engine's Q/E handler can call ShowObject —
// announcing here would always speak the OLD target. If the engine
// then fires ShowObject this tick, OnEngineShowObject cancels the
// request; otherwise Tick drains it (single-hostile combat case).
void RequestQEReannounce();

// Per-tick drain of pending Q/E re-announce.
void Tick();

}  // namespace acc::passive_narrate
