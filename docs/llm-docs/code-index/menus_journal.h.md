# menus_journal.h (56 lines)

Header for the journal panel helpers. Documents that the row class fires no listener on Enter in vanilla play, so this repurposes it; and that Sort's cmd 0x2b only rebuilds the quest list lazily in Draw() so ForceRepopulate must run before a chain rebind while Swap's cmd 0x2a repopulates synchronously.

## Declarations (in source order)

- L24 — `bool acc::menus::journal::IsJournalEntry(void* control)`
- L33 — `void acc::menus::journal::SpeakDescription(void* panel, void* focusedRow)` — caller must gate on IsJournalEntry + PanelKind::InGameJournal
- L39 — `bool acc::menus::journal::IsSortButton(void* panel, void* control)`
- L44 — `bool acc::menus::journal::IsSwapButton(void* panel, void* control)`
- L49 — `void acc::menus::journal::LogEntryCounts(void* panel)`
- L54 — `void acc::menus::journal::ForceRepopulate(void* panel)`
