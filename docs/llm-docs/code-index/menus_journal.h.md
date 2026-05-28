# menus_journal.h (35 lines)

Public surface for the journal listbox entry module. Declares `namespace acc::menus::journal`.

## Declarations (in source order)

- L1 — `namespace acc::menus::journal`
- L10 — `bool IsJournalEntry(void* control)` — true when the control is a journal-entry listbox row
- L14 — `void SpeakDescription(void* panel, void* focusedRow)` — reads and speaks the full quest description for the focused journal row
