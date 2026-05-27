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

bool IsActive();

// Read-and-clear flag set when PollEnter handled this tick's Enter rising
// edge. interact_hotkey::PollHotkey checks it to avoid double-dispatch
// (PollEnter exits view mode synchronously, so a plain IsActive() check
// would be false by the time PollHotkey runs).
bool ConsumedEnterThisTick();

// Cursor world position. False when view mode isn't active.
bool TryGetCursorPosition(Vector& out);

// Camera yaw while view mode is active, player yaw otherwise. Used by
// cue systems whose semantics should follow the camera in view mode and
// the character outside it (e.g. Pillar 1's foremost-in-front cone).
bool GetEffectiveOrientationYawDegrees(float& out);

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
