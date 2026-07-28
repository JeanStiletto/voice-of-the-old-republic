# menus_editbox.h (77 lines)

Public surface for the editbox dispatcher/monitor. Documents the "auto-enter edit mode on focus" model: focus-enter speech comes from `FromControl` (extract.cpp step 6b), the per-tick monitor here only initialises the diff snapshot and doesn't speak that entry. Details the announce rules for insert/delete/bulk-change and notes Left/Right/Backspace flow to the engine untouched.

## Declarations (in source order)

- L55 — `bool acc::menus::editbox::TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)` — called from menus.cpp's OnHandleInputEvent after listbox dispatch, before chain nav
- L63 — `void acc::menus::editbox::TickEditboxMonitors()` — called from menus.cpp's TickMonitors
- L75 — `const char* acc::menus::editbox::GetTitleOverride(void* panel)` — called from AnnouncePanelTitle alongside the listbox equivalent
