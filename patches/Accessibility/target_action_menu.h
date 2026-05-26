// Target-action menu submenu (Shift+1..Shift+3) — keyboard-driven explore
// for the engine's CSWGuiTargetActionMenu rows 0..2.
//
// Layer: input/menu (consumes engine_radial primitives + speech). Sibling
// to actionbar_menu (Shift+4..Shift+7 for personal columns); same UX
// contract translated to target rows so the user sees a uniform
// "Shift+N = open the action stack at hotkey N" gesture across 1..7.
//
// User contract while active:
//   Up         — cycle to next action within the focused row. Drives the
//                engine's CSWGuiTargetActionMenu::SelectNextAction which
//                updates field1[target_type*3+row] in-place. Speaks the
//                new action label.
//   Down       — cycle to previous action. Drives SelectPrevAction.
//   Shift+arrow — speak the focused action's engine tooltip; do NOT cycle.
//   Enter      — fire the current action via DoTargetAction(row). Speaks
//                "{label} eingesetzt" and disarms.
//   Esc        — disarm without firing. Speaks "abgebrochen".
//
// Open path: calls engine_actionbar::PrepareBareDispatch with the current
// narrated target (or kInvalidObjectId when no target is narrated) before
// reading rows. PrepareBareDispatch wraps SetMainInterfaceTarget +
// RePopulateMainInterface, which is what fills action_lists[row] against
// the target. Mirrors the path bare 1..3 takes via diag_input_pipeline.
//
// Self-dismiss: HandleInputEvent re-resolves the TAM each press; force-
// disarms on chain failure. No tick-driven staleness check yet — the
// engine field1[] is the source of truth for "what would bare-N fire"
// and survives across populates within the same target_type.

#pragma once

namespace acc::target_action_menu {

// Enter explore mode for target row `row` (0..2). Refreshes the engine's
// target rows against the current narrated target, reads the row's
// currently-selected action label + count, speaks the opener
// ("Aktionsmenü Spalte 1: Sicherheit, 2 Optionen") and arms the gate.
//
// No-op when the row stays empty after the refresh (no target narrated,
// target offers no actions in this row): speaks a localised "Spalte ist
// leer" and leaves the gate disarmed. Returns true when armed.
bool Open(int row);

// True when our gate is armed. Callers (interact_hotkey) check this to
// route Up/Down/Enter/Esc here and skip the bare-1..3 announce path.
bool IsActive();

// Manager-level input gate. Called from interact_hotkey's Win32 poll.
// `code` is a pre-translation logical InputIndices value (kInputNavUp,
// kInputEnter1, ...). `value` mirrors the engine's val= field — non-zero
// press, zero release.
bool HandleInputEvent(int code, int value);

// Forced disarm. Called when the chain resolution fails mid-call or when
// other higher-priority modes (dialog, area transition) take over.
void ForceDisarm(const char* reason);

// Read the persistent per-row selection index (last action the user
// landed on via the submenu's Up/Down cycle, or 0 if the row has never
// been explored). Used by diag_input_pipeline's bare-key path so the
// engine's PopulateMenus rebuild (which reassigns action_ids and would
// otherwise invalidate any previously-stamped field1[]) is followed by
// a re-stamp at the same index — preserving the user's cycle choice
// across bare 1..3 presses. Mirrors actionbar_menu::CurrentSelection.
int CurrentSelection(int row);

}  // namespace acc::target_action_menu
