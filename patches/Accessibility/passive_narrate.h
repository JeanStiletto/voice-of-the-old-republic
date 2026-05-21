// Passive-selection narration loop.
//
// Layer: narration/ (consumes engine + filter + audio + strings + tolk).
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

// Q/E re-announce hook — called from diag_input_pipeline when the user
// presses Q (205) or E (204). Re-announces the current ShowObject
// target. Use case: combat with a single hostile in range — engine's
// Q/E becomes a no-op visually, sighted players see the red hilite
// stay put, blind player hears nothing without this path. Reads the
// cache populated by OnEngineShowObject; silent when no current target.
void ReannounceCurrentShowObjectTarget();

}  // namespace acc::passive_narrate
