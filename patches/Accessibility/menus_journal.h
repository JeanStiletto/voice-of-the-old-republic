// journal panel (CSWGuiInGameJournal) helpers.
//
// The journal lists active/done quest titles in items_listbox at +0x5c4.
// For sighted players, hovering a row triggers the engine's
// CSWGuiInGameJournal::OnControlEntered which rewrites item_description_label
// (at +0x1a4) with the full entry text (planet name + description). Pressing
// Enter on a row does nothing meaningful — the row's class fires no listener.
//
// For screen-reader users we repurpose Enter on a journal row: it refreshes
// item_description_label for the focused entry and speaks the full text with
// interrupt. The arrow-key chain step still announces just the title, and
// Shift+Down/Up via peek_description still walks description blocks (the
// journal only has one block, so that path effectively replays the same
// text — kept consistent with other panels).

#pragma once

namespace acc::menus::journal {

// True iff `control`'s vtable matches CSWGuiJournalItemEntry — a row of the
// items_listbox in CSWGuiInGameJournal. Used by the chain Enter dispatch to
// route quest-row activate to the description path instead of the no-op
// FireActivate.
bool IsJournalEntry(void* control);

// Refresh item_description_label for `focusedRow` (calls the engine's
// CSWGuiInGameJournal::OnControlEntered with the focused row) and speak the
// resulting description text with interrupt=true. SEH-guarded. Logs on miss
// (no row text, refresh fault, etc.) so a silent Enter is debuggable.
//
// Caller is expected to gate on IsJournalEntry(focusedRow) AND
// IdentifyPanel(panel) == PanelKind::InGameJournal — no defensive re-check.
void SpeakDescription(void* panel, void* focusedRow);

// True iff `control` is the journal's Sort button (panel+0xc2c). Sort dispatches
// cmd 0x2b, which only sets the sort order — the engine rebuilds the quest list
// lazily in Draw() on the next frame. An immediate chain rebuild would capture
// half-constructed rows, so the caller must ForceRepopulate() before re-binding.
bool IsSortButton(void* panel, void* control);

// True iff `control` is the journal's Swap button (panel+0xa68). Swap dispatches
// cmd 0x2a, which repopulates the list synchronously inside its own handler — the
// caller only needs to invalidate the chain so it re-binds to the new list.
bool IsSwapButton(void* panel, void* control);

// Diagnostic: log the raw CSWCJournal active/done entry counts (read directly
// from engine data, independent of the current view) plus the live items_listbox
// row count, so we can confirm the display surfaces every entry the engine has.
void LogEntryCounts(void* panel);

// Force a synchronous rebuild of the quest list by calling the engine's
// CSWGuiInGameJournal::PopulateItemListBox. SEH-guarded. Used after a Sort
// activate so the subsequent chain rebuild sees fully-constructed rows.
void ForceRepopulate(void* panel);

}  // namespace acc::menus::journal
