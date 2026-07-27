# menus_equipstats.h (58 lines)

Header for the Equip-panel virtual stat-row anchors (Vitality/Defense/Attack/Damage). Explains why these need synthesis: the inline CSWGuiLabels aren't IsChainNavigable so the chain walker would skip them; surfaced the same way menus_credits/menus_charsheet do for their virtual rows. Wired by menus_chain.cpp (RebindChain registration) and menus_extract.cpp (FromControl step 0 override).

## Declarations (in source order)

- L37 — `bool acc::menus::equipstats::IsEquipStatRowAnchor(void* panel, void* labelControl)`
- L45 — `void acc::menus::equipstats::ForEachEquipStatRowAnchor(void* panel, bool(*callback)(void*,int,void*), void* userData)`
- L55 — `bool acc::menus::equipstats::ExtractEquipStatRow(void* panel, void* labelControl, char* outBuf, size_t bufSize)`
