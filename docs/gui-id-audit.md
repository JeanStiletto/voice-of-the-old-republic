# .gui control-id audit — getting off file-authored ids

Status: IN PROGRESS (started 2026-08-18). Tracks the conversion of every
lookup that trusts a `.gui`-authored control id to an engine-truth
mechanism. Update the per-surface status lines as work lands.

DONE + tested: InGameEquip (committed 54533ee), WorkbenchUpgrade
(committed after the 2026-08-18 evening round). NEXT: SaveLoad (its ids
double as the panel identifier — highest silent-failure risk left), then
crafting, powers level-up, container, pazaak, keymap, chargen feats.

## Conversion workflow (follow this per surface — written to run cold)

1. **Find the panel constructors.** K1 project is named: `ListFunctionsByName.java
   "<ClassName>"` via analyzeHeadless on `C:/Tools/ghidra-projects` kotor1
   (program `k1_win_gog_swkotor.exe`), or FindCallers.java on any handler
   the ctor registers (registration sites are DATA refs; FindCallers
   includes them). K2 project (kotor2 / `swkotor2.exe`) is unnamed — reach
   its ctor from recorded K2 twin addresses in
   engine_offsets_addresses.h, or by FindCallers on a known K2 handler.
2. **Decompile both ctors** with `tools/ghidra-scripts/decomp.sh 0xADDR`
   (K2: prefix `KDEV_GHIDRA_PROJ=kotor2 KDEV_GHIDRA_PROGRAM=swkotor2.exe`).
   K1 decompiles show tag strings + named fields; get raw offsets from
   `PrintListing.java` (the `LEA reg,[ESI+0xNNNN]` + `PUSH <tag-string>`
   pair before each InitControl call). K2 decompiles often strip call
   args — cross-read the K2 member from a HANDLER that uses it (the
   OnSlotSelected/OnSelectSlot pattern: `SetActiveControl(this + dword)`
   names the listbox) and from the ctor's structural mirror of K1
   (vector-ctor banks, the `byte+0x54 = 0x10` / `flags &= ~4` triples).
3. **HARD RULE — every offset needs a direct witness.** A tagged
   InitControl, a handler decompile that uses the member, or a runtime
   log line. NEVER infer by adjacency/stride ("one button after X"):
   the K2 upgrade BTN_BACK was inferred that way, was wrong, and Esc's
   activate on the resulting non-button CRASHED the game
   (patch-20260818-113825.log; corrected from the tripwire line itself
   to +0x3b58). Prefer two independent witnesses; record them in the
   constant's comment.
4. **Add constants** to engine_offsets_fields.h (`acc::off::Pick`,
   embedded members = address-of, never deref the offset itself).
5. **Add resolvers** in menus_internal.{h,cpp} using
   `PanelMemberWithTripwire` (member is the answer; the old .gui id is
   only the GuiIdMismatch tripwire — pass guiId -1 where the historical
   id is ambiguous on one game). Slot/button-array membership =
   pointer-arithmetic index fns (see EquipSlotIndexFromButton /
   UpgradeSlotIndexFromButton; no SEH needed, no control deref).
6. **Convert every consumer** (grep the id constant + any id-compare
   predicate), delete the id-based matcher, keep announce-only labels on
   ids (Tier 2 — degradation there is acceptable and never drives input).
7. **Build (`kdev build`), apply BOTH games, test round, then grep the
   fresh logs**: `GuiIdMismatch` must be absent on vanilla installs, and
   the converted flows must show their normal lines. Only then commit
   (one tested batch per surface; changelog bullet if user-facing).

Lesson that rode along (not id-related but found by these rounds): a
picker's cursor park must use `ParkCursorToCorner`, not a button's
extent center — K1 hit-testing resolves warped coordinates offset from
extents on some panels, so a "safe" button center can leave the engine
hovering a list row, and hover-select then reverts every keyboard
selection (the K1 crystal-picker "only 2 of 8 crystals reachable" bug).

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
- WorkbenchUpgrade. STATUS: DONE — tested in-game both games 2026-08-18
  (K1 crystal picker full browse/install/remove; K2 install + Esc clean,
  no GuiIdMismatch). K2 BTN_BACK corrected to +0x3b58 after the first
  round's tripwire+crash (see workflow HARD RULE above).
  Mined from ctors (K1 @0x006c6b60 named decompile + listing, K2
  @0x008c9e10 listing) + K2 OnSlotSelected @0x008ceb00:
  items_listbox K1 +0x1580 / K2 +0x2380 (K2 witnessed twice: ctor setup
  and OnSlotSelected's SetActiveControl); BTN_ASSEMBLE K1 +0x2aa0 / K2
  +0x3694 (identified by the OnAssemble registration + the byte+0x54 =
  0x10 / flags&=~4 triple both ctors share); BTN_BACK K1 +0x2d84 / K2
  +0x3864 (K1 tagged InitControl, K2 one stride after assemble; K1's
  picker-open flag at +0x2f48 sits right after it — layout cross-check);
  slot buttons ONE contiguous run K1 7×0x1c4 @+0x64, K2 9×0x1d0 @+0x7a8
  (banks abut exactly). All consumers converted: chain exclusion +
  greyed-slot filter, Enter detection, spec find/commit, picker monitor,
  cursor park, Esc back-button route, per-kind announce (fallback names
  now keyed by array index: crystal bank first, then weapons — ctor
  construction order), Shift+arrow peek, peek listbox resolver.
  IsWorkbenchUpgradeSlotButtonId deleted. NOTE this also fixes a latent
  K2 bug: the old assemble id (24) collided with a K2 slot-button id, so
  the K1-style commit lookup could grab a slot button; the K2 commit path
  never used it, but the resolver now skips the id tripwire on K2 for
  that reason. Slot semantics still come from custom_value (per-bank),
  NOT the array index — only membership/identity moved to the array.
  Title label (id 25) stays id-based: announce-only, degrades gracefully.
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
