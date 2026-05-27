// Map UI virtual cursor.
//
// While CSWGuiInGameMap is foreground, W/A/S/D pan a virtual cursor in
// map pixel space instead of moving the character. 300ms hover-pause
// speaks whatever sits underneath, in priority:
//
//   1. Explored map-note waypoint (CSWSWaypoint.map_note_enabled != 0).
//   2. Fog-of-war (CSWSAreaMap::IsWorldPointExplored false) — speaks
//      "Unexplored", never the room name underneath.
//   3. Explored room with a cached landmark — landmark text.
//   4. Explored room with a mod-supplied friendly room_names entry.
//   5. Silent — vanilla resref-style ids never reach speech.
//
// Distinct from the engine's prev/next-note cycle (up/down buttons).
// Cycle steps through curated landmarks; cursor pans continuously.
//
// Engine surfaces:
//   CSWSAreaMap (Module+0x218):
//     +0x10 orientation, +0x18/+0x1c units-per-pixel, +0x20/+0x24 origin.
//   CSWSAreaMap::GetMapPixelFromWorldCoord @0x00578e00 — we invert this
//     inline (no engine call on the cursor hot path).
//   CSWSAreaMap::IsWorldPointExplored @0x00579210 — fog gate.
//   CServerExoApp::GetModule @0x004ae6b0.

#pragma once

#include "engine_offsets.h"

namespace acc::map_ui_cursor {

// Self-gates on foreground window + PanelKind::InGameMap foreground +
// CSWSAreaMap resolvable. Idle (zero engine reads beyond foreground
// check) when any gate fails. Run after menu monitors so the panel
// snapshot is current.
void Tick();

// True iff the cursor is consuming W/A/S/D. cycle_input::PollWin32 and
// other consumers yield while active.
bool IsActive();

bool TryGetCursorWorldPosition(Vector& out);

// suppressWaypoint = the waypoint just announced via cycle, to avoid
// double-speak on the next hover-pause. Pass nullptr for non-waypoint
// pans (cursor falls through to terrain-shape narration).
void PanToWorld(const Vector& world, void* suppressWaypoint);

}  // namespace acc::map_ui_cursor
