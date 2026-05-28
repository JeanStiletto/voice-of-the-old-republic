# menus_journal.cpp (137 lines)

Journal listbox-entry accessibility implementation. Reads and speaks the full quest description when the user focuses a journal row.

## Declarations (in source order)

- L28 — `typedef void (__thiscall* PFN_PanelOnControl)(void* panel, void* control)` — used to call the panel's OnControl callback to read the description (anonymous ns)
- L30 — `bool ReadRowText(void* row, char* outBuf, std::size_t bufSize)` — extracts quest-title text from a journal listbox row (anonymous ns)
- L52 — `bool acc::menus::journal::IsJournalEntry(void* control)`
- L63 — `void acc::menus::journal::SpeakDescription(void* panel, void* focusedRow)` — calls OnControl to populate the description label, then reads and speaks it
