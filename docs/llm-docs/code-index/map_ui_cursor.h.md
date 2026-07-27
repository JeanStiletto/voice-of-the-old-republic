# map_ui_cursor.h (49 lines)

Header for the map UI virtual cursor. While CSWGuiInGameMap is foreground,
W/A/S/D pan the cursor in map-pixel space instead of moving the character;
a 300ms hover-pause speaks whatever's underneath in priority: explored
map-note waypoint > fog-of-war "Unexplored" > landmark > mod room name >
silence for resref-style ids. Distinct from the engine's prev/next-note
cycle (steps through curated landmarks) — the cursor pans continuously.
Lists the key engine surfaces: `CSWSAreaMap` (Module+0x218, transform
fields at +0x10/+0x18/+0x1c/+0x20/+0x24), `GetMapPixelFromWorldCoord`
(inverted inline, no engine call on the hot path), `IsWorldPointExplored`
(fog gate), `CServerExoApp::GetModule`.

## Declarations (in source order)

- L35 — `void Tick()` — self-gates on foreground window + InGameMap panel + resolvable CSWSAreaMap; idle otherwise. Run after menu monitors so the panel snapshot is current.
- L39 — `bool IsActive()` — true iff the cursor is consuming W/A/S/D; other consumers (cycle_input::PollWin32) yield while active
- L41 — `bool TryGetCursorWorldPosition(Vector& out)`
- L46 — `void PanToWorld(const Vector& world, void* suppressWaypoint)`
  note: pass the just-announced waypoint to avoid double-speak on the next hover-pause; nullptr for non-waypoint pans (falls through to terrain-shape narration)
