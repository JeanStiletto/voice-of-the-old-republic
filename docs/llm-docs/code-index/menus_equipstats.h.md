# menus_equipstats.h (58 lines)

Public surface for the equip-stats virtual-row module. Declares `namespace acc::menus::equipstats`.

## Declarations (in source order)

- L1 — `namespace acc::menus::equipstats`
- L10 — `bool IsEquipStatRowAnchor(void* panel, void* labelControl)` — true when the label is the anchor for an equip-stat row on the equip screen
- L14 — `void ForEachEquipStatRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)` — iterates all equip-stat-row anchors
- L18 — `bool ExtractEquipStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)` — reads the stat label and its paired value label into outBuf
