# menus_equipstats.cpp (152 lines)

Equip-stats virtual-row implementation. Maps anchor labels on the equip screen to paired value labels using a spec table.

## Declarations (in source order)

- L38 — `struct EquipStatRowSpec` — fields: panel kind, anchor label offset, value label offset (anonymous ns)
- L47 — `constexpr EquipStatRowSpec k_specs[]` — entries for all equip-stat label pairs (anonymous ns)
- L63 — `const EquipStatRowSpec* FindSpecForControl(void* panel, void* labelControl)` — matches panel kind and label address against k_specs (anonymous ns)
- L80 — `bool ReadEquipLabel(void* panel, size_t offset, char* outBuf, size_t bufSize)` — SEH-guarded read of a label's gui_string at the given panel-relative offset (anonymous ns)
- L101 — `bool acc::menus::equipstats::IsEquipStatRowAnchor(void* panel, void* labelControl)`
- L105 — `void acc::menus::equipstats::ForEachEquipStatRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)`
- L119 — `bool acc::menus::equipstats::ExtractEquipStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)` — concatenates anchor label + value label into outBuf
