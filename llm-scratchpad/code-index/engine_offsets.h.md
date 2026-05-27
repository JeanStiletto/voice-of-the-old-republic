# engine_offsets.h (1305 lines)

Pure constants — no behaviour. Engine struct/vtable offset table. Values from Lane's Ghidra DB; GoG bytes match Steam. File-scope (not namespaced) for callsite brevity.

## Declarations (in source order)

### GuiControlMethods vtable downcast indices (L9)
- `kVtableAsLabel`, `kVtableAsLabelHilight`, `kVtableAsButton`, `kVtableAsButtonToggle`
  note: verified against SARIF GuiControlMethods struct (offsets 80/84/88/92); trivial implementations (no engine state mutation); safe to call from hooks

### CSWGuiButton / CSWGuiLabel field offsets (L22)
- `kButtonTextOffset` = 0x16c (CExoString in CSWGuiButton), `kButtonStrRefOffset` = 0x174
- `kLabelTextOffset` = 0xe8 (CExoString in CSWGuiLabel), `kLabelStrRefOffset` = 0xf0
  note: CSWGuiButtonToggle embeds CSWGuiButton at offset 0; offsets unchanged; CSWGuiLabelHilight same

### Element-state field offsets (L37)
- `kButtonToggleStateOffset` = 0x1c8 (uint32; bit 0 = on/off)
- `kSliderMaxValueOffset` = 0x70, `kSliderCurValueOffset` = 0x74

### CSWGuiText layout + gui_string pointer offsets (L52)
- `kLabelGuiStringPtrOffset` = 0xE4, `kLabelTextObjectOffset` = 0x138
- `kButtonGuiStringPtrOffset` = 0x168, `kButtonTextObjectOffset` = 0x1BC
- `kTextObjectTextOffset` = 0x18, `kTextObjectStrRefOffset` = 0x20
- `kAurGuiStringCStrOffset` = 0x14 (CAurGUIStringInternal.field5)
  note: gui_string is ground-truth; CSWGuiText::Draw reads ONLY through gui_string; overridden subclasses (InGameMenu icon labels) leave CExoString/strref empty — gui_string still holds rendered c_string

### Control vtable identity constants (L93)
- `kVtableCAurGUIStringInternal` = 0x00741878 — vtable guard before deref at +0x14
- `kVtableSlider` = 0x0073E9D0 — no AsSlider downcast exists; vtable equality only
- `kVtableListBox` = 0x0073E840
- `kVtableCSWGuiButton` = 0x0073E658 — used to disambiguate Button from Label children (SaveLoad-vs-Workbench-upgrade ID 11 collision)
- `kVtableEditbox` = 0x0073EAC8 — single-instance in vanilla (chargen Name panel)
  note: editbox layout documented (+0x150/+0x152 caret shorts, +0x158 typed-text CExoString); CSWGuiEditbox is append-only, no caret-state tracking

### CSWGuiNameChargen struct offsets (L157)
- `kVtableCSWGuiNameChargen` = 0x00759F38
- `kNameChargenEditboxOffset` = 0x230, `kNameChargenEndButtonOffset` = 0x6c
- `kNameChargenSubtitleLabelOffset` = 0x4d0 — used as titleOverride; panel.controls[] visits main_title_label first (stale parent-flow header "CHARAKTERAUSWAHL") so subtitle must be read directly

### CSWGuiClassSelection struct offsets (L191)
- `kVtableCSWGuiClassSelection` = 0x00758020
- `kClassSelectionsArrayOffset` = 0x6c, `kClassSelCharSize` = 0x25c (one icon entry), `kClassSelectionsCount` = 6
- `kClassSelectionClassLabelOffset` = 0x1254 — engine source-of-truth; updated by OnEnterButton on hover/focus; read its gui_string

### CSWGuiPortraitCharGen struct offsets + accessors (L220)
- `kVtableCSWGuiPortraitCharGen` = 0x00759ea8
- `kPortraitCharGenCreatureOffset` = 0x64, `kPortraitRightArrowOffset` = 0xe84, `kPortraitLeftArrowOffset` = 0x1048
- `kCreaturePortraitResRefOffset` = 0xa8, `kResRefSize` = 16
- `kAddrCSWCCreatureGetPortraitId` = 0x00617070 (live cycle index, only reliable accessor)
- `kAddrCSWCCreatureGetPortrait` = 0x00617030 (fills caller CResRef; side=0 for light-side variant)

### CSWGuiAbilitiesCharGen struct offsets + GetAbilityPointCost (L280)
- `kVtableCSWGuiAbilitiesCharGen` = 0x00759c68
- `kAbilitiesCharGenLabelsArrayOffset` = 0x110c, `kAbilitiesCharGenButtonsArrayOffset` = 0x188c
- `kAbilitiesCharGenSelectedAbilityOffset` = 0x3dec — must be mirrored on every chain rebind/step (hit-test shift prevents engine's hover-driven write from firing via warp)
- `kAbilitiesCharGenRemainingValueOffset` = 0x70c, `kAbilitiesCharGenCostValueOffset` = 0xc0c, `kAbilitiesCharGenModifierValueOffset` = 0xe8c
- `kCSWGuiLabelSize` = 0x140, `kCSWGuiButtonSize` = 0x1c4
- `kAddrCSWGuiAbilitiesCharGenGetCost` = 0x006f6bb0 — __thiscall(int index); beats hardcoding D&D table; mod-resilient

### CSWGuiSkillsCharGen struct offsets + IsClassSkill + OnEnterPointsButton (L330)
- `kVtableCSWGuiSkillsCharGen` = 0x00759990
- `kSkillsCharGenLabelsArrayOffset` = 0xfcc, `kSkillsCharGenButtonsArrayOffset` = 0x19cc
- `kSkillsCharGenSelectedSkillOffset` = 0x49bc, `kSkillsCharGenSkillCount` = 8
- `kSkillsCharGenRemainingValueOffset` = 0x70c, `kSkillsCharGenCostValueOffset` = 0xc0c
- `kAddrCSWGuiSkillsCharGenIsClassSkill` = 0x006f4b60 — __thiscall(ushort)
- `kAddrCSWGuiSkillsCharGenOnEnterPointsButton` = 0x006f4bf0 — bypasses hover-driven description path; call synchronously after focus move
- `kSkillsCharGenDescriptionListBoxOffset` = 0x6c

### CSWGuiFeatsCharGen struct offsets + feat list offsets + chart offsets + OnEnterFeat/OnFeatPicked (L406)
- `kVtableCSWGuiFeatsCharGen` = 0x007598b0
- `kFeatsCharGenNameLabelOffset` = 0xbac, `kFeatsCharGenSelectButtonOffset` = 0x1238
- `kFeatsCharGenFeatsListBoxOffset` = 0x13fc, `kFeatsCharGenDescriptionListBoxOffset` = 0x16dc
- Four feat lists (existing/granted/available/chosen): data+size offsets at 0x19bc/0x19c0, 0x19c8/0x19cc, 0x19d4/0x19d8, 0x19e0/0x19e4
- Chart (CSWGuiSkillFlowChart) at `kFeatsCharGenChartOffset` = 0x1a08; selected_col at +0x0c, selected_row at +0x0d
- `kSkillFlowFirstColumnOffset` = 0x5c, `kSkillFlowColumnStride` = 0x128, `kSkillFlowColumnsPerRow` = 3
- `kFlowSkillStructFeatIdOffset` = 0x11c, `kFlowSkillStructStatusOffset` = 0x120, `kFlowSkillStructEmptyFeatId` = 0xffffffff
- Rules global: `kAddrRulesGlobal` = 0x007a3a28; `kRulesFeatsArrayOffset` = 0x90; `kFeatStructSize` = 0x48; `kFeatNameStrRefOffset` = 0x08
- `kAddrCSWGuiFeatsCharGenOnEnterFeat` = 0x006f2fb0 — __thiscall(ushort); refreshes name_label + description_listbox + BTN_SELECT state
- `kAddrCSWGuiFeatsCharGenOnFeatPicked` = 0x006f3c20 — __thiscall(ulong); canonical "user picked" entry; dispatches AddChosenFeat/RemoveChosenFeat/can't-change popup
- `kAddrCSWRulesGetFeat` = 0x00550c00, `kAddrCSWFeatGetNameText` = 0x005cd760

### CSWGuiPowersLevelUp OnEnterPower/OnPowerPicked + chart offset (L533)
- `kAddrCSWGuiPowersLevelUpOnEnterPower` = 0x006f1460 — mirror of OnEnterFeat for force powers
- `kAddrCSWGuiPowersLevelUpOnPowerPicked` = 0x006f2030 — mirror of OnFeatPicked
- `kPowersLevelUpChartOffset` = 0x19fc
- `kAddrCSWGuiSkillFlowChartSetSelectedSkill` = 0x006cdc00 — syncs render-side highlight after keyboard nav

### CSWGuiPanel / ListBox layout (L582)
- `kPanelActiveControlOffset` = 0x1c, `kPanelControlsOffset` = 0x20
- `kListBoxControlsOffset` = 0x29c, `kListBoxBitFlagsOffset` = 0x2bc
- `kListBoxItemsPerPageOffset` = 0x2c4, `kListBoxSelectionIndexOffset` = 0x2c6, `kListBoxTopVisibleIndexOffset` = 0x2c8

### CSWGuiControl extent / tooltip / bit_flags fields (L603)
- `kControlExtentOffset` = 0x4 (inline CSWGuiExtent, 16 bytes: left/top/width/height)
- `kControlTooltipStrRefOffset` = 0x24, `kControlTooltipStringOffset` = 0x28 (CExoString)
- `kControlParentOffset` = 0x14
- `kControlIsActiveOffset` = 0x4c — must be raised to 1 before calling OnSelectSlot / OnItemSelected
- `kControlBitFlagsOffset` = 0x44, `kStoreListBoxVisibleBit` = 0x2

### CSWGuiSaveLoadEntry field offsets (L622)
- `kSaveLoadEntrySaveNumberOffset` = 0x1c8
- `kSaveLoadEntrySaveGameNameOffset` = 0x1d8, `kSaveLoadEntryAreaNameOffset` = 0x1e8, `kSaveLoadEntryLastModuleOffset` = 0x1f0
  note: inline CExoStrings after the embedded CSWGuiButton (size 0x1c4); read directly for row enrichment (engine's onSelectionChanged callback doesn't fire from selection_index direct write)

### CExoArrayList and Vector structs (L648)
- `struct CExoArrayList { void** data; int size; int capacity; }` — engine pointer-array container
- `struct Vector { float x; float y; float z; }` — right-handed Z-up; 1 unit ≈ 1 metre; 0° = +X = east; CCW positive

### CTlkTable::GetSimpleString + CExoString typedef (L667)
- `struct CExoString { char* c_string; uint32_t length; }`
- `typedef CExoString* (__thiscall* PFN_GetSimpleString)(void*, CExoString*, uint32_t)`
- `kAddrGetSimpleString` = 0x0041e8f0, `kAddrTlkTablePtr` = 0x007a3a08 (pointer slot)

### CSWGuiInGameEquip slot handlers (L690)
- `typedef` + address pair: `kAddrInGameEquipOnEnterSlot` = 0x006b9470, `kAddrInGameEquipOnSelectSlot` = 0x006b8eb0
- `kAddrInGameEquipOnItemSelected` = 0x006b7920, `kAddrInGameEquipOnOKPressed` = 0x006b9160
  note: OnSelectSlot gates on is_active != 0 at +0x4c; OnItemSelected gates on is_active + description_listbox.bit_flags&2 + items_listbox.bit_flags&8

### CSWGuiUpgrade workbench slot-pick chain (L730)
- `kAddrCSWGuiUpgradeOnEnterSlot` = 0x006c3c30 (hover/label-update; gates on is_active)
- `kAddrCSWGuiUpgradeOnSlotSelected` = 0x006c6500 (populates LB_ITEMS; NOT reachable via vtable[15] activate — must call directly)
- `kAddrCSWGuiUpgradeOnUpgradeSelected` = 0x006c5510 (row-stage)
- `kAddrCSWGuiUpgradeOnAssemble` = 0x006c6190 (commit + close panel)
- Slot-type table: `kAddrUpgradeSlotTypeTable` = 0x00756fb0; stride=12; strref at +8; category field at panel+0x2f4c; slot custom_value at +0x58

### Combat system layout (L788)
- CSWSCreature: `kCreatureCombatRoundOffset` = 0x9c8 (CSWSCombatRound* combat_round)
- CSWSObject: `kObjectHitPointsOffset` = 0xe0 (short), `kObjectEffectsOffset` = 0x124 (CExoArrayList<CGameEffect*>)
- CSWSCombatRound: attacks_list at +0x4 (7 entries, stride 0xc0), timer/length/current_attack/actions/engaged/current_action offsets
- CSWSCombatAttackData per-attack offsets: react_object, missed_by, base_damage, attack_result, critical_threat, attack_deflected, attack_type
- `kGameEffectTypeOffset` = 0x8 (ushort; EFFECT_TYPES enum)

### Attack result enum constants (L863)
- `kAttackResultPending`=0, `kAttackResultHit`=1, `kAttackResultMiss`=2, `kAttackResultCrit`=3, `kAttackResultDeflected`=4
  note: inferred; validate via probe session

### CExoLinkedList layout + combat action field offsets (L872)
- `kLinkedListHeadOffset`=0, `kLinkedListNodeNextOff`=0, `kLinkedListNodeDataOff`=0x8
- CSWSCombatRoundAction offsets: action_type+0x10, target+0x14, retarget+0x18, move_to_pos+0x38, result+0x7c, damage+0x80
- Action type enum: Attack=0, SpellCast=1, ItemCast=2, Equip=3, Unequip=4, Move=5, UseTalent=6, Heal=7, Cutscene=8 (inferred; validate)

### Combat mode + creature stat accessors (L900)
- `kAddrGetCombatMode` = 0x005ede70, `kAddrGetPausedByCombat` = 0x005edc10
- GetMaxHitPoints, GetArmorClass, GetMaxForcePoints, GetDead, GetCurrentHitPoints
- `kAddrCSWSObjectGetDamageLevel` = 0x004cb020 — 0..5 wound level by hp ratio (0=healthy ≥95%, 5=dead)
- `kAddrCSWSCreatureStatsGetLevel` = 0x005a5fd0 — __thiscall(int subNegLevels); 0=raw total
- GetInvisible/GetBlind accessors

### CSWSCreatureStats attribute totals + faction_id (L939)
- `kStatsAttrTotalsOffset` = 0x34 — 6 post-mod bytes: STR/DEX/CON/INT/WIS/CHA
- `kStatsFactionIdOffset` = 0x78 (ushort; standardFactions enum; player+party share faction 0)
- GetSTR/DEX/CON/INT/WIS/CHA/FortSave/WillSave/ReflexSave/SimpleAlignmentGoodEvil — some tentative adjacent-symbol guesses

### CSWRules::GetFeat + CSWFeat::GetNameText + CGameEffect layout (L974)
- `kAddrCSWRulesGetFeat` = 0x00550c00, `kAddrCSWFeatGetNameText` = 0x005cd760
- `kStatsFeatsListOffset` = 0x0 (CExoArrayList<ushort> in CSWSCreatureStats)

### CSWSCreature inventory offsets (L1006)
- `kCreatureInventoryOffset` = 0xa2c (CSWInventory*)
- Per-slot handle offsets in CSWInventory: RightWeapon=0x14, LeftWeapon=0x18, Head=0x4, Torso=0x8, Hands=0x10, LeftArm=0x20, RightArm=0x24, Implant=0x28, Belt=0x2c (ulong game-object handles)

### CSWGuiInGameEquip panel item-id + stat-value label offsets (L1028)
- Per-slot cached handle offsets (0x4284..0x429c range)
- Stat labels: defense, HP, left/right weapon damage/tohit
  note: Lane's struct names for attack block are misleading — `*_attack_label` holds DAMAGE, `*_tohit_label` holds TO HIT (verified via UpdateInventory Ghidra decomp)
- Party-cycle button offsets (back/change_party_1/change_party_2/character_left/right); filtered from chain nav as decorative

### CSWSCreatureStats.feats list offset (L1089)
- `kStatsFeatsListOffset` = 0x0 (CExoArrayList<ushort>; count at +0x4)

### CGuiInGame::ShowExamineBox/HideExamineBox (L1095)
- `kAddrCGuiInGameShowExamineBox` = 0x0062d3e0, `kAddrCGuiInGameHideExamineBox` = 0x0062d440
  note: ShowExamineBox is actually a TLK-strref message-box opener, NOT creature-examine (decompile confirmed); DO NOT call for creature examine — passes handle as strref → junk TLK row; kExaminePanelListBoxOffset = 0x67c, kExaminePanelHandleOffset = 0x984

### CClientExoApp::GetObjectName address (L1129)
- `kAddrCClientExoAppGetObjectName` = 0x005ed350 — __thiscall(ulong handle, CExoString* outName); BYTES_PURGED=8; universal display-name chain; prefer over engine_area::GetObjectName when working from a handle

### CSWGuiInGameMessages panel offsets (L1140)
- messages_lb at +0x64, dialog_lb at +0x344, show_button at +0x76c, exit_button at +0x930

### CSWGuiDialog panel offsets (L1151)
- `kDialogRepliesListBoxOffset` = 0x19c4, `kDialogMessageLabelOffset` = 0x1ca4
- CSWGuiDialogComputer additions: `kDialogComputerMessageListBoxOffset` = 0x2cfc

### CSWGuiStore layout (L1165)
- `kVtableCSWGuiStore` = 0x00756e38, `kVtableCSWGuiStoreItemEntry` = 0x00756850
- `kVtableCSWGuiInGameItemEntry` = 0x007568f8
- shopitems_listbox at +0x1480, invitems_listbox at +0x1760, description_listbox at +0x1a40
- `kStoreItemEntryObjIdOffset` = 0x1c4 (client-side handle)
- `kSwsItemStackSizeOffset` = 0x28c (ushort), `kSwsItemBitFlagsOffset` = 0x288, `kSwsItemInfiniteStockBit` = 0x4 (bit 2)
- `kStorePlayerGoldOffset` = 0x2270 — cached credits; used for can-afford gate
- `kAddrCSWGuiStoreGetItemBuyValue` = 0x006c0790, `kAddrCSWGuiStoreGetItemSellValue` = 0x006c07f0
- `kAddrCSWGuiStoreOnControlInvAButton` = 0x006c0f40, `kAddrCSWGuiStoreOnControlStoreAButton` = 0x006c1130

### CServerExoApp ClientToServer + GetItemByGameObjectID + CSWSItem::GetPropertyDescription (L1268)
- `kAddrServerExoAppClientToServerObjectId` = 0x004aea30
- `kAddrServerExoAppGetItemByGameObjectID` = 0x004ae760
- `kAddrCSWSItemGetPropertyDescription` = 0x0055f340
- `kAppManagerServerExoAppOffset` = 0x8

### CSWGuiInGameJournal panel offsets (L1288)
- `kVtableCSWGuiJournalItemEntry` = 0x007518c0
- title_label at +0x064, item_description_label (listbox) at +0x1a4, items_listbox at +0x5c4; journal rows are CSWGuiButton-derived (size 0x1cc)
