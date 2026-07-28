# menus_journal.cpp (224 lines)

Journal panel (CSWGuiInGameJournal) helpers. Repurposes Enter on a quest row to call the engine's `OnControlEntered` (@0x00645100) synchronously to refresh `item_description_label`, then speaks it with interrupt — vanilla Enter on a journal row is a no-op for sighted play. Also identifies the Sort/Swap buttons by fixed struct offset (locale-independent) and exposes a diagnostic entry-count cross-check plus a forced repopulate helper for post-Sort chain rebuild. Talks to `engine_reads` (ReadGuiString/ExtractTextOrStrRefIndirect), `log`, `prism`.

## Declarations (in source order)

- L22 — `constexpr uintptr_t kAddrJournalOnControlEntered = 0x00645100`
- L26 — `constexpr size_t kJournalDescriptionListBoxOffset = 0x1a4`
- L28 — `typedef PFN_PanelOnControl(panel, control)`, `typedef PFN_PanelThiscall(panel)`
- L35 — `bool IsButtonAtOffset(panel, control, offset)` (anonymous ns) — struct-offset identity check
- L41 — `bool ReadRowText(row, outBuf, bufSize)` — gui_string first, ExtractTextOrStrRefIndirect fallback
- L63 — `bool acc::menus::journal::IsJournalEntry(void* control)` — vtable check against kVtableCSWGuiJournalItemEntry
- L74 — `void acc::menus::journal::SpeakDescription(void* panel, void* focusedRow)` — calls OnControlEntered, reads description_listbox row 0, speaks with interrupt=true, SEH-guarded at every stage with acclog diagnostics on miss
- L148 — `bool acc::menus::journal::IsSortButton(panel, control)` — panel+0xc2c
- L152 — `bool acc::menus::journal::IsSwapButton(panel, control)` — panel+0xa68
- L156 — `void acc::menus::journal::LogEntryCounts(void* panel)` — reads CSWCJournal active/done counts directly (via AppManager→client→GetQuestJournal) + listbox row count, diagnostic only
- L210 — `void acc::menus::journal::ForceRepopulate(void* panel)` — calls PopulateItemListBox after a Sort activate so the subsequent chain rebuild sees fully-constructed rows
