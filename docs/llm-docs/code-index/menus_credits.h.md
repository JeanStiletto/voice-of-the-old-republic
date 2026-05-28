# menus_credits.h (55 lines)

Public surface for the credits-screen virtual-row module. Declares `namespace acc::menus::credits`.

## Declarations (in source order)

- L1 — `namespace acc::menus::credits`
- L10 — `bool IsCreditsRowAnchor(void* panel, void* labelControl)` — true when the label is the anchor for a credits text row on this panel
- L14 — `void ForEachCreditsRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)` — iterates all anchor labels on the panel, invoking callback for each
- L18 — `bool ExtractCreditsRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)` — reads the multi-part credits row text (header + value siblings) into outBuf
