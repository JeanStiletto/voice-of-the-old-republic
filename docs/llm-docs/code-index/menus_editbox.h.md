# menus_editbox.h (77 lines)

Public surface for the editbox accessibility module. Declares `namespace acc::menus::editbox`.

## Declarations (in source order)

- L1 — `namespace acc::menus::editbox`
- L10 — `bool TryHandleInput(int n, void* thisPtr, void* activePanel, int param_1, int param_2, int& outRv)` — arms on editbox-panel entry; handles Up/Down (re-read), intercepts character-announcement; returns true when consumed
- L16 — `void TickEditboxMonitors()` — polls armed editbox for text diff and announces changes (new chars, deletions)
- L20 — `const char* GetTitleOverride(void* panel)` — returns spec-driven panel title for the chargen-name panel or nullptr
