// Target-action submenu (Shift+1..Shift+3) — keyboard explore of the
// engine's CSWGuiTargetActionMenu rows 0..2.
//
// Sibling to actionbar_menu (Shift+4..Shift+7 for personal columns).
//
// While active:
//   Up/Down       — engine's SelectNextAction / SelectPrevAction within row.
//   Shift+arrow   — speak focused action's tooltip; do NOT cycle.
//   Enter         — DoTargetAction(row); speaks "{label} eingesetzt".
//   Esc           — disarm.
//
// Open path goes through engine_actionbar::PrepareBareDispatch
// (SetMainInterfaceTarget + RePopulateMainInterface). Mirrors the bare
// 1..3 path in diag_input_pipeline.

#pragma once

namespace acc::target_action_menu {

// row 0..2. False on empty row (speaks "Spalte ist leer", stays disarmed).
bool Open(int row);

bool IsActive();

bool HandleInputEvent(int code, int value);

void ForceDisarm(const char* reason);

// Used by diag_input_pipeline: PopulateMenus reassigns action_ids and
// invalidates field1[]; we re-stamp at the persistent per-row index so
// the user's cycle choice survives bare 1..3 presses.
int CurrentSelection(int row);

}  // namespace acc::target_action_menu
