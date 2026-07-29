# engine_offsets_addresses.h (727 lines)

Part of the `engine_offsets.h` family (see that entry for the family map).
Every executable address the patch calls or compares against: 103 `.text`
function/vtable addresses, 2 `.data` global pointers, and the 15 `PFN_*`
calling-convention typedefs that go with them.

## The R() rule — the one thing to get right here

`.text` addresses are **always** wrapped in `acc::addr::R()`; `.data` globals
are **never** wrapped. This is not style. `R()` maps a reference-build address
onto the running build (`engine_rebase.h`); it covers `.text` only, because
`.data` is byte-stable across the builds it targets. Of the 214 `.text`
addresses `kdev sigscan` resolved against the Allard Russian build, **zero**
kept their reference value — displacements run -320..+640 bytes, per function
and not monotonically. So an unwrapped `.text` constant does not degrade
gracefully on that build: it calls into the middle of an unrelated function
with the wrong calling convention.

The declaration form encodes the rule and is load-bearing: `R()` is a runtime
call, so `.text` constants must be `const uintptr_t` (dynamically initialised),
while `.data` globals can be `constexpr`. A `constexpr uintptr_t` in the
`.text` range is therefore a bug signature, not a preference.

The two `.data` globals (`kAddrRulesGlobal`, `kAddrTlkTablePtr`) sit in their
own banner-marked section at the bottom, alongside the note that
`kAddrGuiManagerPtr`, `kAddrAppManagerPtr` and `kAddrCExoSoundPtr` get the same
treatment in their own headers.

## What lives here, by subsystem

- **GUI class identity** — vtable addresses for classes with no RTTI downcast
  accessor: `CAurGUIStringInternal`, Slider, ListBox, `CSWGuiButton`,
  KeyMapButton, Editbox + SaveGameEditBox, Store/StoreItemEntry,
  InGameItemEntry, JournalItemEntry.
- **Chargen panels** — one vtable per screen (NameChargen, SaveNamePanel,
  ClassSelection, PortraitCharGen, AbilitiesCharGen, SkillsCharGen,
  FeatsCharGen, PowersLevelUp) plus their engine handlers:
  `GetPortraitId`/`GetPortrait`, `GetAbilityPointCost`, `IsClassSkill`,
  both `OnEnterPointsButton` twins, `OnEnterFeat`/`OnFeatPicked`,
  `OnEnterPower`/`OnPowerPicked`, `SetSelectedSkill`. Member layouts for all of
  these are in `engine_offsets_fields.h`.
- **Listbox driving** — `CSWGuiListBox::SetSelectedControl`, the engine's own
  select-row call (real selection + native multipage scrolling, unlike a raw
  `selection_index` write).
- **Equip and workbench** — the slot-pick/commit chains
  (`OnEnterSlot`/`OnSelectSlot`/`OnItemSelected`/`OnOKPressed`, and the
  `CSWGuiUpgrade` `OnEnterSlot`/`OnSlotSelected`/`OnUpgradeSelected`/
  `OnAssemble`/`ShowItems`/`OnControlEntered` set), plus the upgrade slot-type
  table base.
- **Combat and creature accessors** — combat-mode globals, HP/AC/FP/dead/
  damage-level/invisible/blind, the stats getter block (attributes + saves +
  alignment), `GetFaction`.
- **Rules tables** — `CSWRules::GetFeat`, feat name/description text,
  `CSWSpellArray::GetSpell`, spell name text.
- **In-game abilities screen** — the coordinate-free repaint handlers
  (`OnEnterSkill`/`OnEnterFeat`/`OnEnterPower`), tab buttons, `UpdateView`,
  `DisplayPowers`, `HandleInputEvent`. `OnAbilitySelectionChanged` is present
  but flagged DO NOT CALL for keyboard nav (mouse-hit-test driven).
- **Examine box** — `ShowExamineBox`/`HideExamineBox`, both flagged DO NOT CALL
  for creature examine: decompile shows a generic TLK-strref message-box
  opener, not a creature-examine API.
- **Store** — buy/sell value, the accept-button handlers, and the
  client-to-server handle bridge (`ClientToServerObjectId`,
  `GetItemByGameObjectID`).
- **Item descriptions** — `GetPropertyDescription`, `GetKeyedPropertyString`,
  the nine per-category property-block builders they chain internally,
  `CExoString` default ctor, `CSWItem::GetBaseItem`.
- **Journal** — `PopulateItemListBox` (lazy-repopulate gotcha documented in
  place).

## Known gap outside this file

Twelve `.text` addresses elsewhere in the patch are declared raw and are
therefore wrong on the Allard build — see the C8 findings in
`docs/refactoring/reports/phase-2-cleanup.md`. They are invisible to
`kdev sigscan`'s harvester because of their declaration form
(`static constexpr`, `constexpr std::uintptr_t`, or an inline
`reinterpret_cast`), which is why they never got a rebase-table entry. New
engine addresses belong in this file, in this file's declaration form.
