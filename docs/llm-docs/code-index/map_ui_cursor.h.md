# map_ui_cursor.h (48 lines)

Map UI virtual cursor. While CSWGuiInGameMap is foreground, W/A/S/D pan a
virtual cursor in map pixel space. 300ms hover-pause speaks whatever is
underneath in priority: explored map-note waypoint, fog-of-war, landmark,
friendly room name. Vanilla resref-style room ids are silenced.

## Declarations (in source order)

- L29 — `namespace acc::map_ui_cursor`
- L36 — `void Tick()`
  note: self-gates on foreground + InGameMap panel foreground + CSWSAreaMap resolvable; run after menu monitors
- L40 — `bool IsActive()`
  note: true iff cursor is consuming W/A/S/D; cycle_input and other consumers yield while active
- L42 — `bool TryGetCursorWorldPosition(Vector& out)`
- L47 — `void PanToWorld(const Vector& world, void* suppressWaypoint)`
  note: called by cycle to warp cursor to a just-announced waypoint; suppresses double-speak on the next hover-pause
