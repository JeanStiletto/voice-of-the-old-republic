// Passive-selection narration loop.
//
// Layer: narration/ (consumes engine + filter + audio + strings + prism).
//
// What this does: hooks CClientExoAppInternal::ShowObject @ 0x005f9c60
// (via hooks.toml at 0x005f9c8e). ShowObject is the engine's single
// point of user-facing target change — called from DoPassiveSelection
// (mouse-hover auto-target, runs every frame as a refresh) and
// SelectNearestObject (Q/E hostile cycle). On a real transition (delta
// vs. last announced) the watcher resolves the handle, classifies it
// through the six-category Pillar 4 filter, and speaks the localised
// name + 3D positional cue at the object's world position. Same
// announcement path also fires on Q/E key press regardless of delta —
// see ReannounceCurrentShowObjectTarget below.
//
// Why hook ShowObject rather than poll CClientExoApp::GetLastTarget
// (the earlier design): last_target is a multi-source bus, written by
// SelectNearestObject AND by CSWSCreature::CreateNewAttackActions
// (every combat round, on every creature with a queued action). During
// combat the AI hammers last_target so per-tick polling races against
// it and misses Q/E transitions. ShowObject is only called by
// user-driven paths (DoPassiveSelection's tail + SelectNearestObject),
// so it gives a clean signal immune to AI churn.
//
// What this does NOT do:
// - Cycle (Pillar 4 active-scan owns that). Cycle keys produce their
//   own narration via cycle_input.cpp; this watcher adds an
//   independent ambient channel on top.
// - Announce focus-loss. Transitions *to* the no-target sentinel
//   (0x7F000000) are silent — losing focus on something is not
//   user-actionable.
// - Filter to "interesting" objects. The engine's ShowObject already
//   represents what the engine considers worth highlighting (red
//   hilite ring). We pass it through the same six-category Pillar 4
//   filter to drop combat targets and other non-nav signals.
//
// Self-gates on player-loaded — silent in menus / chargen / area-load.

#pragma once

#include <cstdint>

namespace acc::passive_narrate {

// Entry point for the ShowObject hook (handler in passive_narrate.cpp,
// hook declaration in hooks.toml). Updates the current-focus cache for
// the Q/E re-announce path AND drives delta-based ambient narration
// (one announce per real transition, silent on per-frame refresh).
//
// Should be called from the extern "C" OnShowObject hook handler with
// the engine-provided handle (param_1->id or 0x7f000000 sentinel).
void OnEngineShowObject(uint32_t handle);

// Q/E re-announce request — called from diag_input_pipeline on each Q
// (205) / E (204) press-edge. Sets a pending flag; the actual
// reannounce decision is deferred to the next Tick AFTER the engine has
// had a chance to process the keystroke.
//
// Why deferred: our input-event detour fires at the engine's
// HandleInputEvent prologue (BEFORE the engine's switch-case processes
// Q/E + may call ShowObject internally). Calling reannounce here would
// always speak the OLD target — even when the engine is about to change
// focus to a new one, producing a "wrong sound on cycling" double-fire
// per project_qe_reannounce_double_fire memory. By deferring to the
// next Tick, we let OnEngineShowObject cancel the pending request if
// the engine changed focus; if it didn't (single-hostile combat case),
// we reannounce. (project_qe_reannounce_deferred.)
void RequestQEReannounce();

// Per-tick driver. Drains a pending Q/E reannounce request if the engine
// didn't fire ShowObject in the meantime. Must be called from the
// core_tick chain.
void Tick();

}  // namespace acc::passive_narrate
