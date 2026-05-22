// KOTOR Accessibility — journal panel (CSWGuiInGameJournal) helpers.
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

}  // namespace acc::menus::journal
