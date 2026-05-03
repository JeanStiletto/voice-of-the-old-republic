// Pillar 4 cycle input — keypress dispatch into cycle_state.
//
// Layer: input/ (consumes engine_input + cycle_state, mutates the cycle
// singleton, returns a "consumed" verdict to the menu-side hook). Owns the
// shift-modifier tracking for `Shift+,` / `Shift+.` / `Shift+-`.
//
// Phase 2 lay-off 3: wires `,` / `.` / `Shift+,` / `Shift+.` to the cycle
// helpers, and `-` / `Shift+-` to log-only stubs (announce + pathfind land
// in lay-offs 4 / 6 respectively). Speech is intentionally absent here —
// the user-perceptible Pillar 4 milestone is lay-off 4.
//
// Gating: cycle keys only fire when the player is in-game (GetPlayerPosition
// returns true). In menus, chargen, dialog, etc. the keys pass through
// unchanged. This is the cheapest "are we actually playing?" signal we have
// in the engine layer; the alternative (panel-kind sniffing) is more
// invasive and not worth it for a key dispatch decision.
//
// Working assumption on key codes: unmapped keys (comma, period, minus,
// shift) pass through ManagerTranslateCode unchanged and arrive at our hook
// as their raw InputIndices values (kInputKbComma=103, kInputKbPeriod=104,
// kInputKbMinus=94, kInputKbLeftShift=24, kInputKbRightShift=25). Confirm
// at first in-game test by watching the existing per-event log; if the
// codes differ, patch engine_input.h.

#pragma once

namespace acc::cycle_input {

// Try to handle a manager-level input event as a Pillar 4 cycle keystroke.
// Returns true if the event was consumed (caller should swallow it from the
// engine's downstream dispatch via OnHandleInputEvent's return path).
//
// Tracks shift state internally across calls — the caller does not need to
// pass modifier flags. Press / release of any shift key updates the held
// flag regardless of whether the event is otherwise consumed.
bool TryHandleEvent(int param_1, int param_2);

}  // namespace acc::cycle_input
