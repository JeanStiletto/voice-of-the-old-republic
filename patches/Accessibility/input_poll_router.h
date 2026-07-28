// per-tick Win32 hotkey router.
//
// Split out of interact_hotkey.cpp by the Phase-1 structure pass
// (refactoring candidate 19). The file it came from was named for one
// feature but had become the home of general cross-subsystem routing:
// this reads every Action rising edge once per tick and offers it, in a
// fixed order, to the action-bar / target-row openers, level-up, the bare
// number-key announces, examine view, the combat queue, the unified
// action menu, bare-R narration, and only then Enter/Shift+Enter interact
// dispatch.
//
// This is the poll-driven half of the mod's input handling.
// input_pipeline.* remains the engine-detour half - the two were
// deliberately NOT merged (user decision during the Phase-1 walk): the
// detour path is load-bearing for the focus/keyboard-death fixes and the
// unified-menu population rule, and mixing Win32 polling into it would
// put two different mechanisms in one file.
//
// The routing ORDER is behaviour, not style. Each subsystem gets first
// refusal in a specific sequence; reordering changes which one wins a
// contested key.

#pragma once

namespace acc::input_poll {

// Runs once per tick from core_tick. Self-gates on player-loaded and on
// view mode owning Enter routing.
void PollHotkey();

}  // namespace acc::input_poll
