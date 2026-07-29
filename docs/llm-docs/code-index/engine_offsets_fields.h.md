# engine_offsets_fields.h (1128 lines)

Part of the `engine_offsets.h` family (see that entry for the family map).
The bulk of the old table: 244 constants describing where fields live inside
engine structs. Maps onto the upstream AddressDatabase `offsets` table, which
is keyed class + member — our flat `k*Offset` names lose the class, so the
class name lives in the comment block above each group. Keep it that way.

## Scope note — two kinds of non-offset that live here anyway

Separating these from the field they describe makes both halves unreadable, so
they stay:

- **Struct geometry** — element counts, strides and sizeof values
  (`kClassSelCharSize`, `kClassSelectionsCount`, `kCSWGuiButtonSize`,
  `kSkillFlowColumnStride`, `kResRefSize`, ...).
- **Field interpretation** — bit masks and sentinels documented with the field
  they decode (`kControlVisibleBit`, `kStoreListBoxVisibleBit`,
  `kSwsItemInfiniteStockBit`, `kFlowSkillStructEmptyFeatId`).

Constants that stand alone — vtable slot indices, TLK strrefs, enum bytes,
panel input codes — belong in `engine_offsets_values.h` instead.

## What lives here, by subsystem

- **GUI control primitives** — button/label text + strref offsets,
  toggle-state bit, slider max/cur, and the `CSWGuiText`/`gui_string` chain.
  That chain is the ground truth for rendered text: `text_params` can be empty
  on overridden subclasses while `gui_string` still holds the drawn string.
- **Generic container layout** — `CSWGuiPanel.activeControl`/`controls`,
  `CSWGuiListBox.controls` plus the three cursor shorts (items_per_page,
  selection_index, top_visible_index), control extent/parent/tooltip/id/
  is_active/bit_flags.
- **Keybind row and editboxes** — `CSWGuiKeyMapButton` (embedded action +
  mapped-key buttons, `unchangeable`, `key_code`) and the `CSWGuiEditbox` /
  `CSWGuiSaveGameEditBox` shared layout (caret/selection shorts, typed-text
  `CExoString`).
- **Chargen panel member maps** — one block per screen: NameChargen,
  SaveNamePanel, ClassSelection, PortraitCharGen, AbilitiesCharGen,
  SkillsCharGen, FeatsCharGen (plus its four parallel feat-status lists),
  `CSWGuiSkillFlowChart` and its cells, PowersLevelUp chart. The vtables that
  identify these panels are in `engine_offsets_addresses.h`.
- **Rules tables** — `CSWRules.feats`/`feat_count`/`CSWFeat` entry shape,
  `CSWRules.spells`, spell description strref.
- **SaveLoad rows** — `CSWGuiSaveLoadEntry` metadata strings (save name, area
  name, last module) read directly, because the preview-pane labels are stale
  until the engine's own `onSelectionChanged` fires.
- **Equip and workbench panel fields** — cached per-slot item handles,
  stat-value labels (Lane's `*_attack_label` / `*_tohit_label` names are
  swapped — decompile-verified, documented in place), party-cycle buttons
  filtered from chain nav, and the `CSWGuiUpgrade` slot-type/installed-item/
  base-item/key-array fields.
- **Combat model** — the big narrative block mapping `CSWSCreature`,
  `CSWSObject`, `CSWSCombatRound`, `CSWSCombatAttackData` and
  `CSWSCombatRoundAction`, plus the AI action queue and the corrected
  three-struct `CExoLinkedList` layout. That correction is worth knowing: the
  original walker treated the `internal*` as a node and walked via `+0`, which
  on a real node is `prev` — so queue depth always read 1.
- **Creature and inventory** — effects array, effect-icon row (sighted
  buff/debuff parity), `CSWInventory` per-slot handles, creature stats
  pointer/race/appearance_type (the `CSWSCreature` inline appearance cache at
  `+0xa4c` is documented as unreliable — use stats `+0x186`).
- **In-game screens** — Abilities label/button/listbox offsets and chart
  row/row-count clamp fields (the engine's chart nav WRAPS; we clamp),
  Messages, Dialog + DialogComputer, `CGuiInGame` dialog-speaker and the
  reply-text model (authoritative for off-page replies, which read empty from
  listbox rows), BarkBubble object id, Store, `CSWSItem`
  stack/charges/bit_flags, item description `CExoLocString`, `CSWBaseItem`
  weapon/item type, Journal.
