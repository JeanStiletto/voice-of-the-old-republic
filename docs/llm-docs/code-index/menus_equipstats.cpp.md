# menus_equipstats.cpp (152 lines)

Virtual chain-entry implementation for the four computed stat labels on the Equip panel (Vitality/HP, Defense, Attack to-hit, Damage) that aren't natively chain-navigable. Anchors on the RIGHT-hand value label per stat (single-weapon mode populates right and blanks left; dual-wield populates both), so `ExtractEquipStatRow` picks the dual-format string when the left peer is non-empty. Talks to `engine_panels` (IdentifyPanel gate to InGameEquip only) and `strings`.

## Declarations (in source order)

- L38 — `struct EquipStatRowSpec { valueOffset, formatSingle, formatDual, leftValueOffset, sortCy }` (anonymous ns)
- L47 — `constexpr EquipStatRowSpec k_specs[4]` — Vitality→Defense→Attack→Damage, sortCy 10000+ so the virtual rows sort after every real button
  note: offsets verified via Ghidra decomp of UpdateInventory @0x006b9970 (damage → `*_attack_label`, to-hit → `*_tohit_label`; caption-only labels at 0x2a98/0x2bd8 unused)
- L63 — `const EquipStatRowSpec* FindSpecForControl(panel, labelControl)` — offset-match gated on PanelKind::InGameEquip
- L80 — `bool ReadEquipLabel(panel, offset, outBuf, bufSize)` — gui_string first, ExtractTextOrStrRefIndirect fallback, SEH-wrapped
- L101 — `bool acc::menus::equipstats::IsEquipStatRowAnchor(panel, labelControl)`
- L105 — `void acc::menus::equipstats::ForEachEquipStatRowAnchor(panel, callback, userData)` — iterates the 4 anchors, gated to InGameEquip
- L119 — `bool acc::menus::equipstats::ExtractEquipStatRow(panel, labelControl, outBuf, bufSize)` — reads right (+ left if dual) value and formats via the localised template
