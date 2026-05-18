// Map UI virtual cursor — Phase 5 lay-off 6 (Pillar 3).
//
// Layer: feature/. Composes engine_panels (foreground detection),
// engine_player (player position seed + area access), engine_area
// (waypoint iteration), audio_cue_player (collision cue), tolk + strings
// (hover-pause speech).
//
// User model:
//
// While the in-game area-map UI is open (CSWGuiInGameMap is foreground),
// the player's bound movement keys (W/A/S/D-equivalents) no longer move
// the character — they move a virtual cursor across the map's pixel
// space. After a 300 ms hover-pause the cursor speaks whatever sits
// underneath, classified by priority:
//
//   1. An explored map-note waypoint (CSWSWaypoint with map_note_enabled
//      != 0) under the cursor pin — speak its localized map_note text
//      ("Brücke", "Mannschaftsquartier", "Cargo Hold", ...).
//   2. Fog-of-war (CSWSAreaMap::IsWorldPointExplored returns false) —
//      speak the localized "Unexplored" / "Nebel des Krieges" string.
//      Spoiler-correct: never reveal the room/landmark name underneath.
//   3. Explored layout-room with a cached Bioware landmark covering it
//      — speak the landmark text (Tier 1, reuses transitions.cpp's
//      per-area RebuildLandmarkCache).
//   4. Explored layout-room whose CSWSArea.room_names[] entry is a
//      mod-supplied human-readable string (passes IsResrefStyleRoomName
//      false) — speak the room name (Tier 2; rare in vanilla, present
//      in some mods).
//   5. Otherwise silent — vanilla resref-style room ids never reach the
//      voice channel.
//
// Distinct from the engine's built-in prev/next-note cycle (up_button /
// down_button — engine HandleInputEvent 0x31 / 0x32 → CSWGuiMapHider::
// GetPrevMapNote / GetNextMapNote). The cycle steps through curated
// landmarks one at a time; the cursor lets the user pan continuously
// "tell me about *this part* of the map". Both interactions ship — they
// solve different exploration needs.
//
// Engine surfaces (all verified 2026-05-12):
//
//   CSWSAreaMap (per area, hung off CServerExoApp::GetModule->field89_0x218)
//     +0x10  orientation              ulong (0..3 — rotates the world->pixel
//                                            transform; see Q4)
//     +0x18  world_units_per_x_pixel  float
//     +0x1c  world_units_per_y_pixel  float
//     +0x20  world_origin_x           float
//     +0x24  world_origin_y           float
//
//   CSWSAreaMap::GetMapPixelFromWorldCoord(Vector, int* px, int* py)
//     @0x00578e00 — pixel space [0..440] × [0..256]; returns 0 + (-1,-1)
//     on out-of-range. We invert this inline from the fields above; no
//     engine call on the cursor hot path.
//
//   CSWSAreaMap::IsWorldPointExplored(Vector) @0x00579210 — fog-of-war
//     read. Used to gate "unexplored" announcements and filter waypoint
//     hits so we never expose names the player hasn't earned. (Same
//     gate the engine's GetNext/PrevMapNote applies internally.)
//
//   CServerExoApp::GetModule @0x004ae6b0 — __thiscall returning a
//     CSWSModule*. Module +0x218 holds the CSWSAreaMap* for the active
//     area.
//
//   CSWGuiInGameMap layout: panel.controls[] + map_hider field at
//     +0xdb0 — used to walk the field11_0x238 CExoLinkedList of map-note
//     waypoints already filtered by explored-state.
//
// Input gating:
//   - Cursor is active only while PanelKind::InGameMap is the foreground
//     panel.
//   - When not foreground, cursor stays dormant — W/A/S/D pass through
//     to the engine (or to view-mode if it's active).
//   - When the map opens, the cursor seeds at the player's projected
//     map-pixel position.

#pragma once

#include "engine_offsets.h"  // Vector

namespace acc::map_ui_cursor {

// Per-OnUpdate poll. Reads VK W/A/S/D held state, advances the virtual
// cursor in map-pixel space, and emits hover-pause announcements when
// the cursor settles. Self-gates on:
//   - foreground window
//   - PanelKind::InGameMap is foreground panel
//   - server-side CSWSAreaMap resolvable
// Idle (zero engine reads beyond the foreground check) when any gate
// fails.
//
// Runs from `core_tick::Dispatch` AFTER the menu monitors have built
// the panel snapshot, so the foreground check sees the most recent
// state.
void Tick();

// True iff the cursor is currently driving input (i.e. map is fg AND
// we've successfully seeded). Used by consumers that would otherwise
// also consume W/A/S/D — e.g. cycle_input::PollWin32 — so they yield
// when the cursor owns the keys.
bool IsActive();

// Current cursor position in world space, if active. Returns false
// otherwise. Provided so future consumers (e.g. lay-off 6 + Ctrl+- to
// beacon to the cursor) can read the cursor without coupling to the
// pixel-space internals.
bool TryGetCursorWorldPosition(Vector& out);

// Pan the virtual cursor to `world`. No-op if the cursor is not active
// (e.g. the map sub-screen isn't foreground). When the caller has just
// announced a map-note waypoint via the cycle, `suppressWaypoint` should
// point at that CSWSWaypoint* so the cursor's own hover-pause does not
// re-announce the same name on the next tick. For non-waypoint pans
// (Door / Transition / map pin / party member), pass nullptr — the
// ambient hover-pause will fall through to terrain-shape narration if
// the cursor sits still, which is informative rather than duplicate.
//
// Used by cycle_input::AnnounceCurrent in map context so the cursor's
// position stays coherent with the cycle's spoken focus.
void PanToWorld(const Vector& world, void* suppressWaypoint);

}  // namespace acc::map_ui_cursor
