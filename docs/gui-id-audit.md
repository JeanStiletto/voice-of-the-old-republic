# .gui control-id audit — getting off file-authored ids

Status: IN PROGRESS (started 2026-08-18). Tracks the conversion of every
lookup that trusts a `.gui`-authored control id to an engine-truth
mechanism. Update the per-surface status lines as work lands.

## Why (the 077noequipment failure)

A K2 beta tester's install carries a variant `equip_p.gui` whose control
ids are shifted by one in the tail band (their file has one extra control
in the stat-label region). On that install our hardcoded K2 ids resolved
to the wrong controls:

- LB_ITEMS: ours 41, theirs 42 → we read a button as a listbox
  (`rows=400065456`, a heap pointer), arrow nav dead, Enter-commit dead.
- BTN_EQUIP: ours 40, theirs 41 → commit would fire the Close handler.
- BTN_BACK: ours 39, theirs 40 → cursor park landed on the "Damage" label.
- The real LB_ITEMS rows leaked into the button chain (the exclusion
  filters the wrong id).

The slot buttons (15..25, 48) happened to match, so slot navigation
worked and only the picker half died — quietly. Every control id we
hardcode has this exposure: `.gui` files are data, and any content mod
may replace them per install. Which mod ships the tester's variant is
still unknown (their `override\equip_p.gui` was requested).

## Why tags are NOT the fix (decompile evidence)

`CSWGuiControl::Load` (K1 @0x00418840, the load-by-tag overload) iterates
the GFF CONTROLS list, reads each element's "TAG" field from the FILE,
compares against the requested tag, and loads the matching element. The
tag is consumed at load time; the in-memory control keeps only the int
`id` (K1 +0x50 / K2 +0x54, from the sibling overload @0x0041b8e0 reading
"ID"). There is no tag on the runtime object to compare against.

## The fix: panel-member offsets (engine's own bindings)

The panel constructors load each control they care about BY TAG into a
member — mostly EMBEDDED sub-objects (buttons 0x1c4 apart in
CSWGuiInGameEquip), sometimes pointers. Those members are how the engine
itself operates the screen (OnSelectSlot uses `&this->items_listbox`,
never an id lookup), so they are immune to `.gui` renumbering by
construction. Reading them is our existing offset paradigm — per-exe
constants behind the installer's SHA-256 gate — no new mechanism.

Conversion rule per site:
- PRIMARY: `panel + memberOffset` (address-of for embedded objects,
  deref for pointer members — check each ctor).
- FALLBACK + TRIPWIRE: keep the id lookup; when both resolve and
  disagree, trust the member and log `GuiIdMismatch` loudly (one line
  per panel instance) so tester logs reveal variant .gui files.
- Where no member exists (purely decorative controls the panel never
  touches), keep the id and accept announce-degradation as the failure
  mode; never let such a control drive input or state.

## Offsets mined so far (2026-08-18)

CSWGuiInGameEquip:
- items_listbox EMBEDDED: K1 +0x30d8 (OnSelectSlot @0x006b8eb0:
  `LEA EDI,[ESI+0x30d8]` → SetActiveControl/SetSelectedControl),
  K2 +0x372c (OnSelectSlot @0x008abe70: `in_ECX+0xdcb*4` passed to
  SetActiveControl, its first dword read as vtable).
- selected slot button storage: K1 +0x42a0, K2 +0x50c8.
- selected_slot type mask: K1 +0x4278 (K2 +0x5098, from earlier RE).
- picker-open flag: K1 +0x4270 / K2 +0x5094 (already shipped as
  kEquipPickerOpenFlagOff).
- BTN_BACK etc.: kEquipPanelBackButtonOffset K1 +0x385c / K2 +0x3edc and
  the four character-cycle buttons were already mined (embedded, 0x1c4
  stride).
- equip_button EMBEDDED: K1 +0x3698 (contiguous button run: back 0x385c −
  0x1c4; the run's other four members match the recorded party-cycle
  constants), K2 +0x3d0c (ctor "BTN_EQUIP" InitControl, dword 0xf43).
- slot buttons EMBEDDED ARRAY: K1 +0x68 stride 0x1c4 × 9, K2 +0x6c stride
  0x1d0 × 11 (ctor _eh_vector_constructor_iterator_ + per-slot InitControl
  loop). Slot labels array: K1 +0x104c stride 0x140, K2 +0x145c stride
  0x148. Slot ORDER identical in both games (weapL, weapR, head, armL,
  armR, body, hands, implant, belt [, weapL2, weapR2]); the ctor loop
  writes the index into each button's custom_value (+0x58 K1 / +0x5c K2)
  and seeds the parallel item-id array (+0x427c / +0x509c) in the same
  order. All shipped as kEquipPanel* constants 2026-08-18.
- Three independent cross-checks passed: K2 ctor's BTN_BACK dword matches
  the previously mined 0x3edc, and BTN_PREVNPC/BTN_NEXTNPC match the
  recorded character-cycle constants 0x50f0/0x52c0.

K2 OnSelectSlot behavioural note (affects UX, not ids): K2 refuses to
open the picker via a PER-SLOT no-candidates flag (+0x2274..+0x229c band,
helper @0x008ab640) that counts only UNEQUIPPED fitting items; K1 instead
checks `items_listbox.controls.size != 1`, which includes the equipped
row. So on K2 an occupied slot with no spare pops the "no items" modal —
vanilla behaviour, mouse included. The mod now follows that modal with
"Item still equipped: <name>" (FmtEquipStillEquipped).

## Inventory (audit of all id-trusting sites)

Tier 1 — drives input/state; convert to member offsets:
- InGameEquip picker trio (LB_ITEMS / BTN_EQUIP / BTN_BACK). STATUS:
  DONE 2026-08-18 (in-game test pending). All consumers now resolve via
  EquipPanelItemsListBox / EquipPanelEquipButton / EquipPanelBackButton
  (menus_internal.cpp); the .gui id survives only as the GuiIdMismatch
  tripwire log. Chain exclusion, picker monitor, spec find, Enter-commit,
  cursor park, and the BTN_EQUIP/BTN_BACK chain filter all converted.
- InGameEquip slot buttons + labels. STATUS: DONE 2026-08-18 (in-game
  test pending). IsEquipSlotButtonId (id compare) deleted; every consumer
  uses EquipSlotIndexFromButton/FromControl (embedded-array pointer
  arithmetic) — chain input, click-pitch capture, per-kind announce
  (k_equipSlotsByIndex, keyed by engine slot index), Shift-arrow peek,
  and the refusal follow-up's item read. Side win: KOTOR 2's two
  second-weapon-set slots now announce/peek like every other slot.
- WorkbenchUpgrade: kWorkbenchUpgradeLbItemsId (0), BtnBack (13/28),
  BtnAssemble, title (menus_listbox.cpp, menus_listbox_picker.cpp,
  menus_chain_input.cpp:703 duplicate const). upgrade.gui is a prime
  mod-replacement target. STATUS: to mine (ctors K1 @?, K2 @0x008c9e10
  recorded in addresses header notes).
- Crafting (K2 only): kSelUpgradeListId/kSelUpgradeItemsBtn/
  kCraftShopListId/kCraftInvListId/kCraftAcceptBtnId/kCraftExamineBtn*
  (menus_crafting.cpp). STATUS: to mine from the three panel ctors.
- Powers level-up: IdPowersListbox/IdDescriptionLb/BtnRecommended/
  BtnAccept/BtnBack (menus_powers_levelup.cpp). STATUS: to mine.
- SaveLoad: SaveLoadLbGamesId/BtnSaveLoad/BtnBack/BtnDelete
  (menus_internal.cpp, menus_listbox.cpp; also used as the panel
  IDENTIFIER via IsSaveLoadShape — double exposure). STATUS: to mine.
- Container: kContainerBtnOkId/GiveId/CancelId (menus_listbox.cpp).
  STATUS: to mine.
- Pazaak deck builder: kControlPlayId/kControlClearId + side arithmetic
  (menus_pazaakdeck.cpp private FindControlById). STATUS: to mine.
- Pazaak wager: WagerLess/More/MaxLabel gui ids (minigame_pazaak.cpp,
  menus_extract.cpp). STATUS: to mine.
- Keymap screen: kIdListBox/Default/Accept/Cancel/Filter* —
  K1-only screen (menus_keymap.cpp). STATUS: to mine.
- Chargen feats + powers: kBtnBackId, buttonId table
  (menus_chargen_feats.cpp). STATUS: to mine.
- SkillInfoBox: kSkillInfoBoxLbSkillsId/TitleId (menus_listbox.cpp).
  STATUS: to mine.
- Script select (AI state): kScriptSelectLbAiStateId. STATUS: to mine.

Tier 2 — announce-only; keep id, degradation acceptable (revisit only if
tester logs show breakage):
- InGameMenu strip icons (strref table keyed by id).
- K2 saveload detail labels (kK2SaveLoadLbl planet/area/time).
- Journal listbox detection (already pointer/offset-based per
  menus_chain.cpp isJournalItemsLb — verify), credits value labels
  (kCraftPoolValueGuiId etc. — anchored by id but text-only).
- Class-select description label (kClassSelDescLabelId).
- Equip stat rows: already member offsets (kEquipPanel*LabelOffset). DONE
  by construction.
- Charsheet: already member offsets. DONE by construction.
- InGameMessages listbox: already member offset
  (kInGameMessagesMessagesListBoxOffset). DONE by construction.
- InGameMap arrows: already member offsets. DONE by construction.

## Execution order

1. InGameEquip picker trio (proven broken) + slot buttons. Includes
   removing the id-based chain exclusion at menus_chain.cpp:481.
2. WorkbenchUpgrade picker (same class of risk, same code shape).
3. Crafting screens, SaveLoad (its shape-check doubles as panel
   identification — highest silent-failure cost after the pickers).
4. Powers level-up, container, chargen feats, skill info, script select,
   keymap, pazaak surfaces.
5. Tier-2 review pass: add GuiIdMismatch tripwires where cheap.

Every converted surface keeps the id path as fallback + tripwire until a
test round on both games confirms the member path, then the id constant
stays only as the fallback.
