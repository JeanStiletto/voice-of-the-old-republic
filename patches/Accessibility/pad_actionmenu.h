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
// But its widgets are custom textured quads in fixed arrays, not
// `CSWGuiControl`s, so the navigation chain cannot see them; narrating it
// would mean reverse-engineering a second custom widget tree across two
// levels for no functional gain. Our menu covers the same nine categories,
// speaks, and is already tested. So: theirs closes, ours opens.
//
// What is still open
// ------------------
// WHICH pad input opens theirs at the category level is not established. The
// variant level is nailed (D-Pad up/down + A); level 1 runs off a per-category
// active test writing the category global this module watches. The most likely
// answer is the A button on a target — in which case the pad's A binding in
// pad_input.cpp already claims it and theirs never opens at all. The edge log
// here settles that in one test round either way: if `Pad.ActionMenu` lines
// never appear, A was the opener and the input layer had already suppressed
// it.
//
// Engine reference: docs/llm-docs/k2-controller-support.md.

#pragma once

namespace acc::pad::actionmenu {

// True while the engine believes one of its nine categories is open.
bool EngineMenuOpen();

// Per-tick watcher. Logs every open/close edge, and on an open edge closes
// the engine's menu back down and opens the mod's unified action menu in its
// place. Cheap (one global read) and self-gates to KOTOR 2.
void Tick();

}  // namespace acc::pad::actionmenu
