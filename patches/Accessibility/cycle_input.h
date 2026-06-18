// Pillar 4 cycle input — keypress dispatch into cycle_state.
//
// Two ingestion paths share per-action handlers, so cycle behaviour is
// identical regardless of how the keystroke arrived:
//
//   TryHandleEvent — from OnHandleInputEvent. Sees keys the engine routes
//                    through CSWGuiManager (kotor.ini-bound + logical
//                    actions). Tracks engine-side shift latch internally.
//
//   PollWin32 — from OnUpdate. Reads OS-level state via GetAsyncKeyState.
//               Stock kotor.ini doesn't bind `,/./-`, so the engine
//               keymap drops them before TryHandleEvent sees them;
//               PollWin32 is the primary path. If someone binds them
//               manually, both paths see the press but the action
//               handlers debounce via the cycle singleton.
//
// In-game gate: GetPlayerPosition. In menus/chargen/dialog the keys
// pass through unchanged.

#pragma once

namespace acc::cycle_input {

// True if the event was consumed.
bool TryHandleEvent(int param_1, int param_2);

void PollWin32();

}  // namespace acc::cycle_input
