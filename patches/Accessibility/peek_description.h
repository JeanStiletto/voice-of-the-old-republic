// Shift+Arrow description peek.
//
// Layer: menu/ — consumes engine-rendered description listboxes and speaks
// them block-by-block when the user holds Shift and presses Up/Down. No
// engine re-entry; pure read-side.
//
// Behaviour contract (from user direction):
//   * Shift+Down advances one block forward, Shift+Up advances one block
//     back. Each press speaks exactly one block (one row of the panel's
//     description listbox).
//   * Releasing Shift resets the block cursor; the next Shift+arrow press
//     speaks block 0 again. The cursor never accumulates across releases.
//   * Normal arrow nav (without Shift) is unchanged — the existing
//     panel-specific handlers continue to announce row titles. Peek does
//     NOT auto-announce on selection changes; only on explicit Shift+arrow.
//
// Generalisation: the panel-kind → description-listbox-offset table
// (peek_description.cpp) is the only thing that needs an entry per
// supported panel. Adding force powers / settings tooltips / journal
// entries / etc. = one row in that table. Each panel struct already
// embeds a `description_listbox` (or equivalently named) member at a
// known offset (read off Lane's Ghidra DB).

#pragma once

namespace acc::peek {

// Called from the manager-level input dispatch in OnHandleInputEvent.
// Returns true iff Shift+Up/Down was consumed for description peeking.
// On false, the event passes through to existing handlers unchanged
// (so plain Up/Down still navigate row selection, etc.).
//
// `focusedControl` is the panel's currently-focused chain target (in
// the chain-navigation model, that's `g_chain[g_chainIndex].control`).
// Some panels need it to refresh the description listbox before peek
// reads — e.g. CSWGuiInGameInventory's items are direct panel
// children rather than listbox rows, and the engine's normal hover
// path (OnControlEntered) takes the item entry directly. Pass nullptr
// when no chain target is meaningful (e.g. listbox-only panels like
// the equip picker, where the items live as listbox rows and the
// refresh function reads selection_index instead).
bool HandleShiftArrow(int param_1, int param_2, void* activePanel,
                      void* focusedControl);

// Called from cycle_input.cpp on the shift-release edge (the
// transition from held to not-held). Resets the block cursor so the
// next Shift+arrow press speaks block 0 again.
void OnShiftReleased();

}  // namespace acc::peek
