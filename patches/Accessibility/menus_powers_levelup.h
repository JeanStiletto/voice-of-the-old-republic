// KOTOR Accessibility — Force-power picker (CSWGuiPowersLevelUp).
//
// Used by both the chargen Power-selection screen and the InGameLevelUp
// "Kr�fte" sub-screen — the same engine class hosts both flows
// (swkotor.exe.h:16603, decompile of OnPowerSelectionChanged @0x006f1940).
//
// The panel is structurally a 2D skill tree, NOT a flat listbox: its
// powers_listbox at .gui id 6 holds CSWGuiSkillFlow rows (one per power
// family), each with up to 3 CSWGuiFlowSkillStruct cells (base / improved
// / master variants) — identical layout to chargen Talente. The chart at
// CSWGuiPowersLevelUp + 0x19fc is a CSWGuiSkillFlowChart tracking
// (row, col) selection state.
//
// Our navigation model mirrors menus_chargen_feats: a 2D cursor over
// non-empty cells, vertical movement keeps the column (snapping to the
// nearest filled column when the destination row doesn't have it),
// horizontal movement steps through filled columns within a row, then
// virtual rows for the action buttons. Enter on a cell dispatches
// OnPowerPicked(powerId); Enter on a button activates that button; Esc
// queues BTN_BACK.
//
// Engine state changes per focused cell:
//   * CSWGuiSkillFlowChart::SetSelectedSkill(chart, powerId) — chart
//     render highlight + (selected_row, selected_col).
//   * CSWGuiPowersLevelUp::OnEnterPower(panel, powerId) — refreshes
//     power_label (id 8) + description_listbox (id 7) + BTN_SELECT
//     state for the focused power.

#pragma once

namespace acc::menus::powers_levelup {

// True iff `panel` is the CSWGuiPowersLevelUp screen (identified via
// engine_panels::PanelKind::PowersLevelUp).
bool IsPowersLevelUpPanel(void* panel);

// Title-override hook for AnnouncePanelTitle. Returns the panel's
// sub_title_label (.gui id 1, "Kr�fte") instead of the misleading
// main_title_label (id 0, "CHARAKTERAUSWAHL") baked into pwrlvlup.gui.
// nullptr when `panel` isn't a PowersLevelUp screen. Buffer is
// thread-local; caller copies / speaks before re-invoking.
const char* GetTitleOverride(void* panel);

// Input dispatcher. Returns true when the event is consumed (caller
// returns `outRv` from the input hook); false to fall through.
//
// Up/Down: change row across non-empty cells + button virtual entries.
// Left/Right: step through filled columns within the current row.
// Enter: activate the focused entry (chart cell → OnPowerPicked; button
//        → QueueButtonByIdActivate).
// Esc:  queue BTN_BACK activation.
// All other keys: fall through.
bool HandleInput(int n, void* thisPtr, void* panel,
                 int param_1, int param_2, int& outRv);

}  // namespace acc::menus::powers_levelup
