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

// Third ingestion path: the KOTOR 2 gamepad (pad_input.cpp). The D-Pad is
// inert in the world — all four codes reach the GUI manager, nothing consumes
// them — so it carries the cycle bindings, with the triggers as a modifier
// layer over the `-` family. Same per-action handlers as the two keyboard
// paths, so pad and keyboard cycling behave identically.
//
// Self-gates on in-world exactly like TryHandleEvent (GetPlayerPosition) and
// picks the World / Map context the same way; returns false when the caller
// should let the event through instead.
enum class PadAction {
    ItemPrev, ItemNext,
    CategoryPrev, CategoryNext,
    ItemFirst, ItemLast,
    AnnounceFocus,   // repeat the last narrated target
    WalkToFocus,     // autowalk (second press cancels)
    BeaconFocus,     // audio beacon (second press cancels)
};
bool DispatchPadAction(PadAction action);

}  // namespace acc::cycle_input
