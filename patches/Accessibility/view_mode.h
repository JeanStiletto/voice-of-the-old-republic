// View mode — freeze the character, keep the camera live.
//
// Pressing B locks the player in place; A/D keep panning the camera
// natively (KOTOR's A/D rotates camera only — the character only snaps to
// camera direction when W/S commits forward motion). W/S then drive a
// virtual cursor instead of the character, the 3D listener is rerouted
// to the cursor, and Enter / Shift+Enter act on whatever the cursor is
// hovering over.
//
// Hotkey B chosen because V is taken (Solo Mode in stock kotor.ini) and
// B is unbound by KOTOR's stock keymap.

#pragma once

#include "engine_offsets.h"  // Vector

namespace acc::view_mode {

// Settle time before a hovered thing is spoken. The cursor must rest on
// the same target this long, so sweeping across a crowded scene does not
// produce a burst of half-started announcements.
//
// Published because the map cursor deliberately follows the same cadence —
// map_ui_cursor.cpp used to declare its own 300 with a comment saying it
// matched this one, which is a coupling better expressed than commented.
// The two surfaces speaking at different rhythms would read as a bug.
constexpr unsigned long kHoverPauseMs = 300;

bool IsActive();

// Read-and-clear flag set when PollEnter handled this tick's Enter rising
// edge. interact_hotkey::PollHotkey checks it to avoid double-dispatch
// (PollEnter exits view mode synchronously, so a plain IsActive() check
// would be false by the time PollHotkey runs).
bool ConsumedEnterThisTick();

// Cursor world position. False when view mode isn't active.
bool TryGetCursorPosition(Vector& out);

// Camera yaw (engine frame), in BOTH normal play and view mode, with
// character yaw as the first-ticks fallback. Used by every cue system
// whose semantics should follow where the user is LOOKING rather than
// which way the body happens to point — which is all of them, because
// KOTOR's A/D orbits the camera without turning the character, so
// character yaw does not move until W/S commits a step.
bool GetEffectiveOrientationYawDegrees(float& out);

// The position spatial announcements must be measured FROM: the view-mode
// cursor while view mode is active, the player creature otherwise. In view
// mode the creature is frozen by SetPlayerInputEnabled(false) and W/S drives
// the cursor instead, so a player-anchored distance stops changing as the
// user explores. Mirrors spatial_change_detector's referencePos pick.
// False when no player position is readable.
bool GetEffectiveReferencePosition(Vector& out);

// The full viewpoint an "at X o'clock, Y metres" line is relative to:
// GetEffectiveReferencePosition + GetEffectiveOrientationYawDegrees in one
// call, so the clock and the distance in a single spoken line can never be
// measured from different frames. False when either read fails; callers
// drop the clock segment rather than speak a stale or garbage one.
bool GetEffectiveViewpoint(Vector& outPos, float& outYawDegrees);

// B / Shift+B edge detection. Bare B toggles view mode; Shift+B dumps
// CClientOptions to the log (diagnostic, no behaviour).
void PollWin32();

// Per-tick cursor step, hover narration, region announce, Enter dispatch.
// Self-gates on IsActive(). Must run after camera_announce::Tick (yaw
// must be current), after PollWin32 (rising-edge toggle takes effect this
// tick), and before interact_hotkey::PollHotkey (so our Enter dispatch
// wins via ConsumedEnterThisTick).
void Tick();

}  // namespace acc::view_mode
