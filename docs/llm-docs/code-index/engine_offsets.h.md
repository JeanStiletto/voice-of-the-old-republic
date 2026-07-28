# engine_offsets.h (1820 lines)

Central RE-derived offset/address table. Pure constants — no behaviour.
Values from Lane's Ghidra DB; GoG bytes match Steam. File-scope (not
namespaced) for callsite brevity, matching engine_input.h's `kInput*`
convention. Every `.text` address is wrapped in `acc::addr::R(...)` for
build-rebasing (see engine_rebase.h); `.data`/struct-offset constants are
bare. Organized as one running list of per-subsystem blocks (GUI control
primitives → chargen panels → generic panel/listbox container layout →
equip/workbench → combat → creature/inventory accessors → in-game
sub-screens → store → item property-description builders → journal),
roughly the order features were reverse-engineered rather than a fixed
taxonomy.

## Section index (by first line of the block)

- L11 — GuiControlMethods vtable downcast indices (AsLabel/AsButton/etc.) + CSWGuiButton/Label field offsets (text/strref) + element-state fields (toggle bit, slider min/cur).
- L69 — CSWGuiText/gui_string layout — the ground-truth rendered-text chain (`kLabelGuiStringPtrOffset` etc.) and the CAurGUIStringInternal/Slider/ListBox/CSWGuiButton vtable-identity constants (L110-142) used since none of these classes has an RTTI downcast accessor.
- L144 — CSWGuiKeyMapButton (keybind row: action + mapped-key embedded buttons) through CSWGuiEditbox/CSWGuiSaveGameEditBox (caret/selection shorts, typed-text CExoString) — L144-198.
- L200 — Chargen panel offsets, one block per screen: CSWGuiNameChargen (L200), CSWGuiSaveNamePanel (L233), CSWGuiClassSelection (L257), CSWGuiPortraitCharGen + GetPortraitId/GetPortrait accessors (L286), CSWGuiAbilitiesCharGen + GetAbilityPointCost (L346), CSWGuiSkillsCharGen + IsClassSkill + OnEnterPointsButton (L396), CSWGuiFeatsCharGen + four feat-status lists + CSWGuiSkillFlowChart (L492), CSWRules feat reverse-lookup + OnEnterFeat/OnFeatPicked (L572-617), CSWGuiPowersLevelUp mirror of the feats surfaces (L619-673).
- L675 — Generic CSWGuiPanel/CSWGuiListBox container layout: CExoArrayList shape, `kPanelControlsOffset`, listbox cursor state (items_per_page/selection_index/top_visible_index), `SetSelectedControl` engine call (L697), control extent/tooltip/id fields (L716-739).
- L745 — CSWGuiSaveLoadEntry row layout (savegame metadata) + CExoArrayList/Vector primitive structs (L770) + CTlkTable::GetSimpleString + CExoString (L789).
- L812 — Equip-screen (CSWGuiInGameEquip) slot-pick + item-select engine handlers, bypassing click-sim hit-test problems.
- L852 — Workbench (CSWGuiUpgrade) slot-pick + commit chain (OnEnterSlot/OnSlotSelected/OnUpgradeSelected/OnAssemble), ShowItems open/close, slot-type table, installed-mod/base-item/key-array panel fields (L852-959).
- L961 — Combat system layout: CSWSCreature.combat_round (+0x9c8), CSWSObject hit_points/effects, CSWSCombatRound/CSWSCombatAttackData/CSWSCombatRoundAction field maps, inferred action_type enum (L1078-1089), AI action queue (`action_nodes`, L1013), CExoLinkedList 3-struct layout correction (L1039 — the walker used to treat `internal*` as a node, always reading count=1).
- L1091 — Combat-mode globals + CSWSCreature stat/attribute getters (HP/AC/FP/dead/invisible/blind, L1097-1141) + inline attribute-total bytes + faction_id (L1149-1165).
- L1172 — CSWRules::GetFeat/GetNameText/GetDescriptionText, CSWRules.spells + GetSpell + spell name/description (L1189-1209), CombatRoundAction spell/item/feat-id offsets keyed by action_type (L1211).
- L1221 — CGameEffect layout (effects array) + CSWSCreature.effect_icons (sighted buff/debuff icon row, L1234) + CSWSCreature.inventory + CSWInventory per-slot handle offsets (L1245-1265).
- L1267 — CSWGuiInGameEquip cached slot-item ids + stat-value labels (damage/tohit — Lane's struct names are swapped, verified by decompile, L1291) + party-cycle button offsets filtered from chain nav (L1309) + CSWGuiLevelUpPanel back/cancel buttons, also filtered (dead ends — level-up can never be cancelled, L1328).
- L1346 — CSWGuiInGameAbilities ("Fähigkeiten" screen): label offsets, OnEnterSkill/OnEnterFeat/OnEnterPower (coordinate-free repaint path, NOT the mouse-hit-test OnAbilitySelectionChanged), tab buttons, HandleInputEvent chart-nav codes, per-chart row/row-count clamp fields (engine WRAPS, this code clamps) — L1346-1424.
- L1426 — CSWSCreatureStats.feats list + CGuiInGame::ShowExamineBox (L1432 — DO NOT CALL for creature examine: decompile-verified generic TLK-strref message-box opener, not a creature-examine API) + CClientExoApp::GetObjectName universal accessor (L1466).
- L1477 — CSWGuiInGameMessages (combat log/dialog history) + CSWGuiDialog replies-listbox/message-label + dialog_owner conversation-partner field (L1496) + CGuiInGame.current_dialog_speaker (overheard-NPC speaker id, L1539) + CGuiInGame reply-text model (authoritative off-page-safe reply array, L1551) + CSWGuiBarkBubble.object_id (L1570).
- L1580 — CSWGuiStore (merchant panel): buy/sell mode-detection via bit_flags, row/listbox/button offsets, cached player-gold, CSWSItem bit_flags/stack_size/charges (L1647-1679), GetItemBuyValue/SellValue + accept-button handlers (L1681-1700).
- L1702 — ClientToServerObjectId/GetItemByGameObjectID + CSWSItem::GetPropertyDescription/GetKeyedPropertyString (L1707-1729) + the nine per-category property-block builder functions GetPropertyDescription chains internally (tags/values/properties, L1731) + description CExoLocString inline-vs-TLK quirk (L1752) + CSWItem::GetBaseItem weapon_type/item_type (L1769).
- L1783 — CSWGuiInGameJournal (quest journal): layout, button→command wiring via generic FireActivate(0x27), PopulateItemListBox (lazy repopulate gotcha, L1811), CSWGuiJournalItemEntry row shape (L1817).
