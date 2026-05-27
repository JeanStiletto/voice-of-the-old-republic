// chargen "Talente" main panel (CSWGuiFeatsCharGen).
//
// The screen is a 2D feat-tree chart: a CSWGuiSkillFlowChart of N rows ×
// up to 3 columns, embedded as the single child of feats_listbox. Each
// row is a feat-progression chain (col0 root → col1 successor → col2
// master); each cell carries (feat ID, status byte). The user picks
// feats by clicking cells with the right status — Add for available,
// Remove for chosen-this-level — and the same Hinzuf./Entf. button
// reflects the cell's add/remove affordance.
//
// This panel doesn't fit the listbox-driven dispatcher in
// menus_listbox.cpp (the chart isn't row-shaped — DriveListBoxSelection
// would manipulate the listbox's selection_index but the listbox has a
// single child). It gets its own input handler that builds a flat
// row-major cursor over non-empty chart cells + 3 trailing button
// virtual-entries (Empfohlen / OK / Abbrechen), navigated with K/L.
//
// Engine state changes we drive from the keyboard:
//   * On per-cell focus: SetSelectedSkill(featId) + OnEnterFeat(featId)
//     — refreshes name_label, description_listbox, and the chart's
//     visual highlight in one round-trip.
//   * On Enter against a chart cell: OnFeatPicked(featId) — engine
//     dispatches DetermineFeat + AddChosenFeat / RemoveChosenFeat /
//     "you can't change this" message box, then BuildButtons rewrites
//     statuses on every cell.
//   * On Enter against a button virtual-entry: QueueButtonByIdActivate
//     for the matching .gui-time button id.
//   * On Esc: QueueButtonByIdActivate(BTN_BACK).
//
// Cell statuses (chart enum, lowest byte at FlowSkillStruct +0x120):
//   0 available, 1 existing, 2 granted-this-level, 3 locked
//   (default — prerequisite chain not yet satisfied), 4 chosen-this-level.

#pragma once

namespace acc::menus::chargen_feats {

// True iff `panel`'s vtable matches CSWGuiFeatsCharGen.
bool IsChargenFeatsPanel(void* panel);

// Input dispatcher. Returns true when the event is consumed (caller
// returns `outRv` from the input hook); false to fall through to chain
// nav and the rest of OnHandleInputEvent.
//
// Up/Down: advance the cursor across non-empty chart cells + the 3
// button entries, announcing each focus.
// Enter: activate the focused entry (chart cell → OnFeatPicked; button
// → QueueButtonByIdActivate).
// Esc: queue BTN_BACK activation.
// All other keys: fall through.
bool HandleInput(int n, void* thisPtr, void* panel,
                 int param_1, int param_2, int& outRv);

}  // namespace acc::menus::chargen_feats
