# menus_charsheet.h (70 lines)

Public surface for the character sheet virtual-row (stat-row) module. Declares `namespace acc::menus::charsheet`.

## Declarations (in source order)

- L1 — `namespace acc::menus::charsheet`
- L10 — `void MaybeAnnounce(void* panel)` — speaks the character sheet summary when panel is InGameCharacter kind; no-op otherwise
- L14 — `bool ExtractStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)` — reads the stat-row value (and modifier/threshold if applicable) into outBuf
- L18 — `bool IsStatRowAnchor(void* panel, void* labelControl)` — true when the label is the anchor for a stat row on this panel
- L22 — `void ForEachStatRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)` — iterates all stat-row anchors, invoking callback for each
