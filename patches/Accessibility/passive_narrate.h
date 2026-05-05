// Phase 2 lay-off 9a — passive-selection narration loop.
//
// Layer: narration/ (consumes engine + filter + audio + strings + tolk;
// no engine re-entry of its own beyond reading LastTarget once per tick).
//
// What this does: per OnUpdate tick, reads CClientExoApp::GetLastTarget
// @0x005edd80. The engine populates this organically as the player walks
// near interactables (verified by lay-off 9-probe — see investigation Q6
// §"RE — does MoveMouseToPosition trigger world-hover?"). On change, the
// watcher resolves the handle to a CSWSObject*, classifies it through
// the existing six-category Pillar 4 filter, and speaks the localised
// name + 3D positional cue at the object's world position.
//
// What this does NOT do:
// - Cycle (Pillar 4 lay-off 4 owns that). Cycle keys produce their own
//   narration via cycle_input.cpp; this watcher adds an independent
//   ambient channel on top.
// - Push to LastTarget when cycle moves. Channels stay independent per
//   the 2026-05-04 decision in `docs/navsystem-longterm-plan.md`. If
//   double-narration proves disruptive, add a recency-suppress here
//   (don't speak if cycle spoke within ~500 ms).
// - Announce focus-loss. Transitions *to* the no-target sentinel
//   (0x7F000000) are silent — losing focus on something is not
//   user-actionable.
// - Filter to "interesting" objects. The engine's LastTarget already
//   represents what the engine considers worth highlighting. We pass it
//   through the same six-category Pillar 4 filter (Door / Creature /
//   Container / Item / Landmark / Transition with their sub-state
//   predicates) to drop combat targets and other non-nav signals.
//
// Self-gates on player-loaded — silent in menus / chargen / area-load.

#pragma once

namespace acc::passive_narrate {

// Per-tick poll. Reads LastTarget; on change, resolves + classifies +
// speaks. Cheap when idle (one engine call + one comparison). Call from
// OnUpdate before any path-relevant per-tick work; ordering with
// cycle_input::PollWin32 doesn't matter (channels are independent).
void Tick();

// GetTickCount() snapshot of the last time engine LastTarget transitioned
// to a non-sentinel value (i.e. the user's mouse / Q/E / passive selection
// landed on a real object). interact_hotkey compares this against
// cycle_state.mutationTick to resolve cycle-vs-engine focus conflicts —
// most-recent-event wins. 0 = never transitioned to a real target since
// load. Comparisons must use signed-diff to survive GetTickCount wrap.
unsigned int LastTargetChangeTick();

}  // namespace acc::passive_narrate
