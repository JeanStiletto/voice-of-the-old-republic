// KOTOR 2 pad action menu (`CSWGuiActionMenuIos`) — suppressed in favour of
// the mod's own unified action menu.
//
// Why suppress rather than narrate
// --------------------------------
// `CSWGuiActionMenuIos` is the pad's equivalent of the radial plus the action
// bar: nine categories (six personal action columns + three target-action
// rows), each expanding to a variant list, driven by D-Pad up/down + A. It
// stamps the engine's own per-column / per-row selected-action fields and
// calls the engine's own dispatchers — i.e. it drives exactly the machinery
// the mod's unified action menu already drives.
//
// Narrating it would mean reverse-engineering a second widget tree across two
// levels for no functional gain. Our menu covers the same nine categories,
// speaks, and is already tested — so theirs closes, and the mod's own runs in
// its place on the same D-Pad, in LIVE mode (unified_action_menu.h).
//
// Which input opens theirs is no longer open: D-Pad left/right picks the
// category (level 1) and up/down the variant (level 2), with A confirming.
// That is precisely the idiom the mod adopted, and it is why pad_input.cpp
// consumes all four D-Pad codes in the world — which in turn is why this
// module should never see an open edge at all.
//
// So this is a BACKSTOP, not a route in. A `Pad.ActionMenu` line in a log
// means a D-Pad press reached the engine that should not have; the close keeps
// the screen consistent, and the line is the thing to investigate.
//
// Engine reference: docs/llm-docs/k2-controller-support.md.

#pragma once

namespace acc::pad::actionmenu {

// True while the engine believes one of its nine categories is open.
bool EngineMenuOpen();

// Per-tick watcher. Logs every open/close edge, and on an open edge closes the
// engine's menu back down. Cheap (one global read) and self-gates to KOTOR 2.
void Tick();

}  // namespace acc::pad::actionmenu
