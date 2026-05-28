# menus_charsheet.cpp (356 lines)

Character-sheet virtual-row implementation. Maps anchor labels to their value (and modifier/threshold) siblings using a spec table keyed by struct offset.

## Declarations (in source order)

- L56 — `constexpr size_t kCharSheetLblClass`, `kCharSheetLblLevel`, `kCharSheetLblFort`, `kCharSheetLblRef`, `kCharSheetLblWill`, `kCharSheetLblXpCur`, `kCharSheetLblXpThresh`, `kCharSheetLblDefStat`, `kCharSheetLblFp`, `kCharSheetLblHp`, and ability score label offsets (Str, StrMod, Wis, WisMod, Cha, ChaMod, Int, IntMod, Con, ConMod, Dex, DexMod) + `kCharSheetSldAlign` slider offset
- L87 — `void ReadCharSheetLabel(void* panel, size_t offset, char* outBuf, size_t bufSize)` — SEH-guarded panel-offset label read (anonymous ns)
- L124 — `enum class StatRowKind` — LabelValue, LabelValueMod, LabelValueThresh, Slider (anonymous ns)
- L131 — `struct StatRowSpec` — fields: anchorOffset, valueOffset, modOffset, kind, stringId (anonymous ns)
- L139 — `constexpr StatRowSpec k_statRowSpecs[13]` — one entry per character-sheet stat row (anonymous ns)
- L169 — `const StatRowSpec* FindSpecForControl(void* panel, void* labelControl)` — resolves label address to spec by computing panel-relative offset (anonymous ns)
- L185 — `bool acc::menus::charsheet::IsStatRowAnchor(void* panel, void* labelControl)`
- L189 — `void acc::menus::charsheet::ForEachStatRowAnchor(void* panel, bool (*callback)(void*, void*, void*), void* userData)`
- L201 — `bool acc::menus::charsheet::ExtractStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)` — formats "LabelText: Value [Mod]" or "LabelText: Value / Threshold" depending on StatRowKind
- L259 — `void acc::menus::charsheet::MaybeAnnounce(void* panel)` — guards on InGameCharacter PanelKind; walks all stat-row anchors and speaks the summary
