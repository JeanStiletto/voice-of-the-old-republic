// Engine executable addresses: .text functions, vtables, and .data globals.
//
// Part of the engine_offsets.h family - see engine_offsets_types.h for the
// split rationale and the full file list.
//
// Two upstream AddressDatabase tables live here and they behave differently:
//
//   functions and vtables (.text / .rdata) - EVERY one goes through
//     acc::addr::R(). The Allard Russian build is the same BioWare source
//     relinked, so these move by -320..+640 bytes each, per function and not
//     monotonically (engine_rebase.h). Of the 214 addresses `kdev sigscan`
//     resolved against that build, zero kept their reference value. Declaring
//     a .text address here without R() therefore does not degrade gracefully:
//     it calls into the middle of an unrelated function.
//
//   global pointers (.data) - raw by design. R() covers .text only and .data is
//     byte-stable across the builds it exists for, so wrapping one would be a
//     bug rather than a redundancy. They have their own section at the bottom
//     of this file.
//
// Note the declaration forms differ and that is load-bearing: R() is a runtime
// call, so .text constants are `const uintptr_t` (dynamically initialised),
// while the .data globals can be `constexpr`.

#pragma once

#include <cstdint>

#include "engine_offsets_types.h"
#include "engine_rebase.h"

// ---------------------------------------------------------------------------
// .text functions and vtables - every one through acc::addr::R().
// ---------------------------------------------------------------------------

// CAurGUIStringInternal vtable address (from Lane's Ghidra DB:
// CAurGUIStringInternal_vtable @ 0x00741878). Used to validate that a
// gui_string pointer actually refers to a CAurGUIStringInternal object
// before dereferencing it — see ReadGuiString for why this matters.
const uintptr_t kVtableCAurGUIStringInternal = acc::addr::R(0x00741878);

// Slider class identity by vtable address. Resolved via SARIF xrefs:
// 0x0073E9D0 is referenced by CSWGuiSlider's constructor (0x41bb0d) and
// destructor (0x41bb9d) — i.e. it's the slider's vftable. Sliders have no
// AsSlider downcast accessor in GuiControlMethods, so vtable equality is
// the only safe identity check.
const uintptr_t kVtableSlider = acc::addr::R(0x0073E9D0);

// CSWGuiListBox vtable. Same identity-by-vtable pattern as the slider:
// no AsListBox accessor exists in GuiControlMethods, so we identify by
// vtable equality. Used by chain navigation (RebindChain recurses one
// level into multi-row listboxes), the tabbed-panel detector, and the
// listbox-content extraction path in ExtractAnnounceableText (which walks
// a listbox's rows when the panel walk encounters one as a child — the
// recurring `vtable=0073E840 src=none` cases in our log are listbox
// containers wrapping the actual message text).
const uintptr_t kVtableListBox = acc::addr::R(0x0073E840);

// CSWGuiButton vtable. The standard button class — used by SaveLoad's
// BTN_DELETE / BTN_BACK / BTN_SAVELOAD, the equipment screen's slot
// buttons, the chargen class icons, the InGameMenu strip icons, the
// workbench upgrade-slot buttons (BTN_UPGRADE3X/4X), and most other
// CSWGuiButton instances in the engine. Identity-by-vtable matters for
// structural panel detectors that need to distinguish a Button child
// from a Label/LabelHilight child sharing the same .gui-time ID (the
// SaveLoad-vs-Workbench-upgrade collision at ID 11 is the canonical
// case — see IsSaveLoadStructural).
const uintptr_t kVtableCSWGuiButton = acc::addr::R(0x0073E658);

// CSWGuiKeyMapButton vtable - the keyboard-mapping screen's row control. The
// row layout it identifies (mapped_key_button, unchangeable, key_code) is
// documented in engine_offsets_fields.h.
const uintptr_t kVtableKeyMapButton          = acc::addr::R(0x007593c8);

// CSWGuiEditbox vtable. The field layout shared by both editbox classes is
// documented in engine_offsets_fields.h.
const uintptr_t kVtableEditbox             = acc::addr::R(0x0073EAC8);
// CSWGuiSaveGameEditBox — subclass embedded in CSWGuiSaveNamePanel. Struct
// size is identical (0x160) and the whole body is a CSWGuiEditbox; only
// HandleKeyPress is overridden (engine-side filename-char filter). It carries
// its own vtable (k1_win_gog_swkotor.exe.xml Address-table symbol
// CSWGuiSaveGameEditBox @ 0x007575B0), so vtable-identity predicates must
// accept it alongside kVtableEditbox; the field offsets below apply unchanged.
const uintptr_t kVtableSaveGameEditbox     = acc::addr::R(0x007575B0);

// CSWGuiNameChargen vtable (chargen name-entry panel). Member layout is
// documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiNameChargen   = acc::addr::R(0x00759F38);

// CSWGuiSaveNamePanel vtable (the save-name modal on top of SaveLoad). Member
// layout is documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiSaveNamePanel    = acc::addr::R(0x007576D0);

// CSWGuiClassSelection vtable (chargen class picker). Member layout and the
// class_selections[] geometry are documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiClassSelection      = acc::addr::R(0x00758020);

// CSWGuiPortraitCharGen vtable (chargen portrait picker). Member layout is
// documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiPortraitCharGen     = acc::addr::R(0x00759ea8);

// CSWCCreature::GetPortraitId — __thiscall, no args, returns the portrait
// row index into portraits.2da (verified live: returned 24 → 25 across a
// Right+Right+Left cycle in the chargen Porträtauswahl panel). The named
// CSWCCreatureStats.portrait_id at +0x11c, CSWGuiPortraitCharGen.portrait_id
// at +0x1238, and CSWCObject.portrait at +0xa8 are all stale during chargen
// — this accessor is the only reliable read-side primitive we have.
const uintptr_t kAddrCSWCCreatureGetPortraitId = acc::addr::R(0x00617070);

// CSWCCreature::GetPortrait — __thiscall, fills caller-supplied CResRef
// (16 bytes) with the current portrait baseresref (e.g. "po_pmhc3").
// Signature per SARIF: `CResRef* __thiscall(CResRef* outBuf, byte side)`.
// `side` selects the alignment variant (0 = light/default, 1..4 = darker
// variants matching the baseresrefe / baseresrefve / etc. columns); we
// only ever pass 0 since chargen doesn't ladder dark side. Caller-pops
// 8 bytes (BYTES_PURGED=8). Resref is 16 bytes, NOT necessarily null-
// terminated when length == 16, so we always reserve a 17-byte buffer.
const uintptr_t kAddrCSWCCreatureGetPortrait = acc::addr::R(0x00617030);

// CSWGuiAbilitiesCharGen vtable (chargen attributes panel). Member layout, and
// why we mirror chain focus into selected_ability, are documented in
// engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiAbilitiesCharGen          = acc::addr::R(0x00759c68);

// CSWGuiAbilitiesCharGen::GetAbilityPointCost — engine accessor for the
// point-buy cost of the next +1 increment on a given ability. Returns
// cost as int; takes ability index (0..5 in struct order, same as
// selected_ability). Calling this beats hardcoding the D&D 3.5 PHB
// table in our code: a mod that rebalances the curve via 2DA edits
// would still get the correct number, and we don't have to extrapolate
// values we never observed in a log (the table is small enough that
// 16-17 was always going to be guesswork without verification).
//
// Signature per SARIF (k1_win_gog_swkotor.exe.xml SYMBOL @ 0x006f6bb0):
//   int __thiscall GetAbilityPointCost(int param_1)
// Callee-pops 4 bytes (BYTES_PURGED=4).
const uintptr_t kAddrCSWGuiAbilitiesCharGenGetCost = acc::addr::R(0x006f6bb0);

// CSWGuiSkillsCharGen vtable (chargen skills panel). Member layout is
// documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiSkillsCharGen           = acc::addr::R(0x00759990);

// CSWGuiSkillsCharGen::IsClassSkill — engine predicate for whether the
// skill at index `param_1` (ushort) is a class skill for the chargen
// creature's class. Class skills cost 1 point per +1; cross-class
// skills cost 2. We use this to compute cost ourselves rather than
// reading the engine's cost_value label, which has the same hit-test
// shift / refresh-timing race the Attribute panel taught us about.
//
// Signature per SARIF (SYMBOL @ 0x006f4b60):
//   int __thiscall IsClassSkill(ushort param_1)
// Callee-pops 4 bytes (param is widened to dword on the stack).
const uintptr_t kAddrCSWGuiSkillsCharGenIsClassSkill = acc::addr::R(0x006f4b60);

// CSWGuiSkillsCharGen::OnEnterPointsButton — engine handler that
// populates description_list_box with the description for the given
// skill button. We call this synchronously with the focused button to
// bypass the engine's hover-driven path, which is off-by-one on this
// panel (the cursor warp's hit-test resolves to skill_labels[i-1]
// regardless of Y compensation — labels overlap the cursor's row in a
// way Attribute labels don't). After the call, the listbox row holds
// the correct description and we read + speak it ourselves.
//
// Signature per SARIF (SYMBOL @ 0x006f4bf0):
//   void __thiscall OnEnterPointsButton(CSWGuiControl* param_1)
// Callee-pops 4 bytes.
const uintptr_t kAddrCSWGuiSkillsCharGenOnEnterPointsButton = acc::addr::R(0x006f4bf0);

// CSWGuiAbilitiesCharGen::OnEnterPointsButton — twin of the Skills panel's
// OnEnterPointsButton (0x006f4bf0). Populates description_listbox with the
// description for the given ability button. We call it synchronously with
// the FOCUSED button so the description is keyed by the row the user is on,
// bypassing the engine's hover-driven path. That hover path is fired by our
// chain-step cursor warp's hit-test, which is resolution-dependent (the same
// "resolves one row above" shift Y compensation only papers over at the
// tested resolution) and silently paints the wrong ability's description at
// other GUI scales. Direct call + read removes the coordinate dependency,
// mirroring CSWGuiSkillsCharGen exactly.
//
// Signature per SARIF (SYMBOL @ 0x006f70e0):
//   void __thiscall OnEnterPointsButton(CSWGuiControl* param_1)
// Callee-pops 4 bytes.
const uintptr_t kAddrCSWGuiAbilitiesCharGenOnEnterPointsButton = acc::addr::R(0x006f70e0);

// CSWGuiFeatsCharGen vtable (chargen feats panel). Member layout and the four
// parallel feat lists are documented in engine_offsets_fields.h.
const uintptr_t kVtableCSWGuiFeatsCharGen           = acc::addr::R(0x007598b0);

// CSWGuiFeatsCharGen::OnEnterFeat — engine handler that, given a feat
// ID, runs DetermineFeat (sets the select-button label/colour for the
// owned/can-add/granted/locked state), writes the feat's name strref
// into name_label, and calls SetDescription(feat->description) to
// repopulate description_listbox with the wrapped text. Calling this
// synchronously after a programmatic picker selection_index write is
// the equivalent of OnEnterPointsButton on the chargen Skills panel —
// it bypasses the engine's hover-driven path that DriveListBoxSelection
// short-circuits (no onSelectionChanged callback fires from a direct
// selection_index store).
//
// Signature per Ghidra decomp (DECOMP @0x006f2fb0):
//   void __thiscall OnEnterFeat(ushort param_1)
// Callee-pops 4 bytes (ushort widened to dword on stack).
const uintptr_t kAddrCSWGuiFeatsCharGenOnEnterFeat = acc::addr::R(0x006f2fb0);

// CSWGuiFeatsCharGen::OnFeatPicked — the canonical "user clicked this
// feat" engine entry point. Calls DetermineFeat to derive the user's
// current intent (status byte: 0=add, 1=remove, 2/3/4=can't-change msg
// box), then dispatches AddChosenFeat / RemoveChosenFeat / shows a
// "you can't change this" popup. Calling it directly with the focused
// cell's feat ID lets us bypass BTN_SELECT entirely — saves a chart
// SetSelectedSkill round-trip (BTN_SELECT reads the chart's selected
// (col,row) to derive the feat ID, but we already have the ID).
//
// Signature per Ghidra decomp (DECOMP @0x006f3c20):
//   void __thiscall OnFeatPicked(ulong param_1)
// Callee-pops 4 bytes.
const uintptr_t kAddrCSWGuiFeatsCharGenOnFeatPicked = acc::addr::R(0x006f3c20);

// CSWGuiPowersLevelUp engine surfaces — Force-power picker (pwrlvlup.gui)
// used by both the chargen Power-selection screen and the InGameLevelUp
// "Kr�fte" sub-screen. The "powers_listbox" in the SARIF struct is misleading:
// each of its rows is a CSWGuiSkillFlow with up to 3 CSWGuiFlowSkillStruct
// cells (base / improved / master power variants) — the same shape
// CSWGuiFeatsCharGen uses for its feat tree. The chart at +0x19fc is a
// CSWGuiSkillFlowChart tracking (row, col) selection state; the engine's
// SkillHitCheckMouse uses cached mouse coords to derive the column on mouse
// input, which is why a flat listbox.selection_index drive can't pick the
// right cell. We iterate listbox.controls (the engine's source-of-truth in
// CSWGuiPowersLevelUp::OnPowerSelectionChanged @0x006f1940) and call the
// engine surfaces below to commit the selection.
//
// Signature per Lane's SARIF (FUNCTIONS entry @0x006f1460):
//   void __thiscall OnEnterPower(ulong powerId)
// Mirror of CSWGuiFeatsCharGen::OnEnterFeat — refreshes power_label,
// description_listbox, BTN_SELECT label/colour for the given power.
const uintptr_t kAddrCSWGuiPowersLevelUpOnEnterPower = acc::addr::R(0x006f1460);

// Signature per Lane's SARIF (FUNCTIONS entry @0x006f2030):
//   void __thiscall OnPowerPicked(ulong powerId)
// Mirror of CSWGuiFeatsCharGen::OnFeatPicked — the canonical "user clicked
// BTN_SELECT" entry. Dispatches DeterminePower → AddChosenPower /
// RemoveChosenPower / can't-change message box. Calling directly with the
// focused cell's powerId lets us bypass the click-sim on Hinzuf. Macht.
const uintptr_t kAddrCSWGuiPowersLevelUpOnPowerPicked = acc::addr::R(0x006f2030);

// CSWGuiPowersLevelUp identity by vtable — single heap-allocated class with
// no CGuiInGame slot, so (like CSWGuiFeatsCharGen / CSWGuiLevelUpPanel) the
// vtable is the clean, collision-proof identifier. SYMBOL
// GuiPowersLevelUp_vtable @ 0x00759780 (absoluteAddress 7706496 in Lane's
// SARIF; GoG bytes match Steam). Sits just below CSWGuiFeatsCharGen_vtable
// (0x007598b0) in the vtable region, as expected for sibling GUI classes.
const uintptr_t kVtableCSWGuiPowersLevelUp             = acc::addr::R(0x00759780);

// CSWGuiSkillFlowChart::SetSelectedSkill — sets the chart's render-side
// selection state by feat ID. Walks rows × cols looking for the matching
// feat, updates the chart's (selected_col, selected_row) pair and the
// cell's selection bit. We call this AFTER OnEnterFeat to keep the chart's
// visual highlight in sync with our keyboard focus — without it the user's
// nav cursor and the rendered highlight diverge (engine treats the chart
// as still pointing at the old cell, which matters if a later mouse-side
// event reads back through the chart).
//
// Signature per Ghidra decomp (DECOMP @0x006cdc00):
//   void __thiscall SetSelectedSkill(ulong param_1)
// Callee-pops 4 bytes.
const uintptr_t kAddrCSWGuiSkillFlowChartSetSelectedSkill = acc::addr::R(0x006cdc00);

// CSWGuiListBox::SetSelectedControl @0x0041c040 — __thiscall(listbox, index,
// playSound). The engine's canonical "select row N": deselects the previously
// selected row, writes selection_index, selects the new row (sets its highlight
// state via the row's HandleInputEvent, plays the GUI select sound when
// playSound != 0), sets the scroll-follow bit (bit_flags & 0x1000), and runs
// OrganizeControls so the visible page scrolls to keep the selection on-screen
// — native multipage, identical to a scrollbar drag. This is the SAME call the
// engine's own hover-select (HandleMouseMove) and keyboard nav (HandleInputEvent
// codes 0x3d/0x3e) make. Driving it directly — instead of writing selection_index
// raw via DriveListBoxSelection — gives real selection + scrolling and, with the
// cursor parked off the list so per-frame hover-select can't fight it, the
// selection sticks (the lightsabercrystal "can't reach some crystals" bug:
// hover re-selected the row under the parked cursor every frame, reverting our
// raw write). index < 0 or >= controls.size clears the selection.
typedef void (__thiscall* PFN_CSWGuiListBoxSetSelectedControl)(void* listbox,
                                                               int index,
                                                               int playSound);
const uintptr_t kAddrCSWGuiListBoxSetSelectedControl = acc::addr::R(0x0041c040);

// CTlkTable::GetSimpleString — resolves a TLK str_ref to a localized string.
// Many KOTOR UI controls (e.g. Options screen "Annehmen"/"Abbrechen", certain
// chargen labels) leave their CExoString empty and store only a str_ref; the
// engine renders by resolving the str_ref through dialog.tlk every frame.
//
// Signature (from SARIF):
//   CExoString * __thiscall GetSimpleString(CExoString * out, ulong strref)
// Address  : 0x0041e8f0
// Global   : the live CTlkTable* lives at 0x007a3a08 (one indirection — the
//            address holds a pointer to the table). Confirmed by decompiling
//            the first GetSimpleString caller at 0x0418b29:
//              MOV ECX, [0x007a3a08]   ; this = *g_pTlkTable
//              CALL GetSimpleString
typedef CExoString* (__thiscall* PFN_GetSimpleString)(void* this_,
                                                      CExoString* out,
                                                      uint32_t strref);
const uintptr_t kAddrGetSimpleString = acc::addr::R(0x0041e8f0);

// CSWGuiInGameEquip slot handlers — invoked directly to bypass click-sim
// hit-test problems on the equip panel. See docs/equip-flow-investigation.md
// (post-2026-05-04 update).
//
//   OnEnterSlot(panel, slot_btn) — populates panel.items_listbox with items
//     from the player's inventory matching slot_btn's slot type. Sets
//     panel.selected_slot. No is_active gate. Equivalent to mouse-hover.
//   OnSelectSlot(panel, slot_btn) — stages the equip if items_listbox has
//     entries (raises panel.field33_0x4270 |= 1 and pre-selects row 1), or
//     pops the "Für diesen Slot..." modal if empty. Gates on
//     `slot_btn->is_active != 0` — caller must raise that bit first.
//     Equivalent to mouse-click after hover.
//
// is_active lives at +0x4c on every CSWGuiControl (verified by Ghidra
// decompile of OnSelectSlot's prologue test).
typedef void (__thiscall* PFN_InGameEquipOnEnterSlot)(void* panel, void* slot_btn);
typedef void (__thiscall* PFN_InGameEquipOnSelectSlot)(void* panel, void* slot_btn);
const uintptr_t kAddrInGameEquipOnEnterSlot = acc::addr::R(0x006b9470);
const uintptr_t kAddrInGameEquipOnSelectSlot = acc::addr::R(0x006b8eb0);

// Picker-side commit handlers.
//
//   OnItemSelected(panel, item_entry) — commits the equip. Calls
//     EquipItem(this, item_id, selected_slot, 1) on the inventory item the
//     entry wraps. Gates on `item_entry->is_active != 0` AND
//     `description_listbox.bit_flags & 2 != 0` (set by OnSelectSlot's
//     ShowDescription) AND `items_listbox.bit_flags & 8 != 0` (set by
//     OnSelectSlot's SetEnabled(items_listbox, 1)). Also handles
//     ShowCantEquipMessage on prerequisites failure and stages a swap when
//     replacing an already-equipped item.
//   OnOKPressed(panel, btn_equip) — cleanup only. Clears previously_equipped_*,
//     calls CloseDescription. Does NOT commit; the equip must already have
//     happened in OnItemSelected. Gates on `btn_equip->is_active != 0` AND
//     `panel.field33_0x4270 & 1` (latter set by OnSelectSlot).
typedef void (__thiscall* PFN_InGameEquipOnItemSelected)(void* panel, void* item_entry);
typedef void (__thiscall* PFN_InGameEquipOnOKPressed)(void* panel, void* btn_equip);
const uintptr_t kAddrInGameEquipOnItemSelected = acc::addr::R(0x006b7920);
const uintptr_t kAddrInGameEquipOnOKPressed = acc::addr::R(0x006b9160);

// CSWGuiUpgrade (workbench upgrade.gui) slot-pick + commit chain.
// Same structural shape as the equip-screen pair above, RE'd from Lane's
// gzf at 2026-05-25:
//
//   OnEnterSlot(panel, slot_btn) @0x006c3c30 — "hover" path. Updates the
//     LBL_SLOTNAME / upgrade_count_label / property_label labels for the
//     hovered slot. Gates on `slot_btn->is_active != 0` (read at +0x4c —
//     same offset as the equip slot gate; caller must raise the bit).
//     Does NOT populate LB_ITEMS on its own.
//
//   OnSlotSelected(panel, slot_btn) @0x006c6500 — "click" path. Builds the
//     compatible-mods list from CSWPartyTable items + the upcrystals_2da
//     or upgrades_2da table (per slot kind), AddControls-replaces the
//     LB_ITEMS contents, calls ShowItems(panel, 1) to flip the item-pick
//     zone visible, and SetActiveControl(items_listbox). Stores the slot
//     button pointer in panel.field74_0x2fb0 (used later by OnAssemble
//     to know which slot to install into). Same is_active gate as
//     OnEnterSlot. THIS is the function that populates LB_ITEMS — the
//     mouse-driven path reaches it via HandleLMouseUp, but the engine's
//     CGuiButton::HandleInputEvent(0x27) path does NOT, which is why
//     vtable[15] activate on a slot button keeps LB_ITEMS empty (verified
//     in patch-20260525-142247.log).
//
//   OnUpgradeSelected(panel, item_entry) @0x006c5510 — row-stage. Called
//     when the user picks a mod in LB_ITEMS. Stages the selection but
//     doesn't install — the install happens in OnAssemble. Gates on
//     `item_entry->is_active != 0` (same +0x4c offset, on the row).
//
//   OnAssemble(panel, btn_assemble) @0x006c6190 — commit. Plays the
//     assemble sound, calls FinishUpgrading on the parent
//     UpgradeItemSelect panel, then PopModalPanel — so the upgrade.gui
//     panel closes synchronously when this returns. Gates on
//     `btn_assemble->is_active != 0`.
typedef void (__thiscall* PFN_CSWGuiUpgradeOnEnterSlot)     (void* panel, void* slot_btn);
typedef void (__thiscall* PFN_CSWGuiUpgradeOnSlotSelected)  (void* panel, void* slot_btn);
typedef void (__thiscall* PFN_CSWGuiUpgradeOnUpgradeSelected)(void* panel, void* item_entry);
typedef void (__thiscall* PFN_CSWGuiUpgradeOnAssemble)      (void* panel, void* btn_assemble);
const uintptr_t kAddrCSWGuiUpgradeOnEnterSlot = acc::addr::R(0x006c3c30);
const uintptr_t kAddrCSWGuiUpgradeOnSlotSelected = acc::addr::R(0x006c6500);
const uintptr_t kAddrCSWGuiUpgradeOnUpgradeSelected = acc::addr::R(0x006c5510);
const uintptr_t kAddrCSWGuiUpgradeOnAssemble = acc::addr::R(0x006c6190);

// CSWGuiUpgrade::OnControlEntered @0x006c5370 — __thiscall(panel, item_entry).
// The workbench picker's own hover handler. Unlike the generic item tooltip
// (CSWSItem::GetPropertyDescription, which omits the property block entirely
// for lightsaber crystals), this builds the description shown on-screen:
//   description_indentified  (or GUI string 0x7dac when empty), and
//   for saber upgrades (panel.field25 == 1) on a non-color slot it PREPENDS
//   GetKeyedPropertyString(base_item, key) — the keyed bonus line sighted
//   players see and the generic path drops. Gates on item_entry->is_active
//   (same gate as Inventory/Store; drive via CallOnControlEnteredWithActive).
// SetDescription writes the resulting string into the description label at
// panel + kUpgradeDescLabelOffset, so we read it back from there.
typedef void (__thiscall* PFN_CSWGuiUpgradeOnControlEntered)(void* panel, void* item_entry);
const uintptr_t kAddrCSWGuiUpgradeOnControlEntered = acc::addr::R(0x006c5370);

// CSWGuiUpgrade::ShowItems @0x006c2f80 — __thiscall(int visible). visible!=0
// opens the mod-picker zone (disables the slot buttons + slot labels, shows
// items_listbox); visible==0 closes it: re-enables the slot buttons (re-sets
// their interactive bit by category), re-shows the slot labels, SetActiveControl
// back to the slot zone, and clears items_listbox visibility. We call the
// close form to undo OnSlotSelected's open when the user backs out of the
// picker, so sibling slots stop reading "unavailable". OnUpgradeSelected runs
// this on commit; we mirror it on cancel.
typedef void (__thiscall* PFN_CSWGuiUpgradeShowItems)(void* panel, int visible);
const uintptr_t kAddrCSWGuiUpgradeShowItems = acc::addr::R(0x006c2f80);

// CSWGuiUpgrade slot-type table — 16 entries × 12 bytes, indexed by
// `(slot_btn.custom_value - 4) + panel.field25_0x2f4c * 4`. Each entry:
//   +0 (int)     UpgradeType — matches upgrades_2da's UpgradeType column
//   +4 (char*)   resref tag prefix (e.g. "i_vcell")
//   +8 (uint32)  strref into dialog.tlk for the slot's display name
//                ("Energiezelle", "Vibrationszelle", "Sch\xE4rfe", …)
// Sentinel entries carry UpgradeType = -1 / strref = 0 for slot positions
// the category doesn't use. RE'd from OnEnterSlot @0x006c3c30 (the
// `DAT_00756fb8` reference, +8 from the table base) and verified against
// a 240-byte dump at 0x00756fb0.
const uintptr_t kAddrUpgradeSlotTypeTable = acc::addr::R(0x00756fb0);

// Server-side combat-mode global. Read via accessor for safety; the
// CClientExoApp facade is 8 bytes (vtable + internal), and the actual
// flag lives on the internal struct.
const uintptr_t kAddrGetCombatMode = acc::addr::R(0x005ede70);
const uintptr_t kAddrGetPausedByCombat = acc::addr::R(0x005edc10);

// CSWSCreature engine getters — Phase 2A snapshot path.
const uintptr_t kAddrCSWSCreatureGetMaxHitPoints = acc::addr::R(0x004ed310);
const uintptr_t kAddrCSWSCreatureGetArmorClass = acc::addr::R(0x004ed1d0);
const uintptr_t kAddrCSWSCreatureGetMaxForcePoints = acc::addr::R(0x004fd490);
const uintptr_t kAddrCSWSCreatureGetDead = acc::addr::R(0x004ef820);
const uintptr_t kAddrCSWSObjectGetCurrentHitPoints = acc::addr::R(0x004caec0);

// CSWSObject::GetDamageLevel @0x004cb020 — `ulong __thiscall(this)`.
// Returns a 0..5 byte (verified via decompile 2026-05-22) representing
// the creature's visible wound state by hp_cur / hp_max ratio:
//   0 = healthy   (>= 95%)
//   1 = light     (>= 75%)
//   2 = wounded   (>= 50%)
//   3 = badly     (>= 25%)
//   4 = dying     (> 0%, < 25%)
//   5 = dead      (<= 0%)
// No accessor-validation concern — this is a pure ratio computation
// over fields we already trust.
const uintptr_t kAddrCSWSObjectGetDamageLevel = acc::addr::R(0x004cb020);

// CSWSCreatureStats::GetLevel @0x005a5fd0 — `int __thiscall(this, int subNegLevels)`.
// Sums level over each entry in CSWSCreatureStats.classes[2]. param_1=0
// → raw total (don't subtract negative levels from drain effects);
// param_1=1 → effective level. Use 0 for the displayed level.
const uintptr_t kAddrCSWSCreatureStatsGetLevel = acc::addr::R(0x005a5fd0);

// CSWSCreature::GetInvisible @0x00501950 / GetBlind @0x004ee210 — bool
// __thiscall(this). Direct flag accessors; safe to call from manual
// paths. We only emit a row when the flag is set (no need to announce
// "not invisible").
const uintptr_t kAddrCSWSCreatureGetInvisible = acc::addr::R(0x00501950);
const uintptr_t kAddrCSWSCreatureGetBlind = acc::addr::R(0x004ee210);

// CSWSCreatureStats getters — saves + attribute scores. CSWSCreatureStats
// lives at CSWSCreature +0xa74 (kCreatureStatsPtrOffset).
const uintptr_t kAddrStatsGetSTR = acc::addr::R(0x005a6190);
const uintptr_t kAddrStatsGetDEX = acc::addr::R(0x005a61a0);  // tentative — adjacent slots
const uintptr_t kAddrStatsGetCON = acc::addr::R(0x005a61b0);
const uintptr_t kAddrStatsGetINT = acc::addr::R(0x005a61c0);
const uintptr_t kAddrStatsGetWIS = acc::addr::R(0x005a61d0);
const uintptr_t kAddrStatsGetCHA = acc::addr::R(0x005a61e0);
const uintptr_t kAddrStatsGetFortSave = acc::addr::R(0x005ab810);
const uintptr_t kAddrStatsGetWillSave = acc::addr::R(0x005ab880);
const uintptr_t kAddrStatsGetReflexSave = acc::addr::R(0x005ab8f0);
const uintptr_t kAddrStatsGetSimpleAlignmentGoodEvil = acc::addr::R(0x005a5110);

// CSWSCreature::GetFaction → CSWSFaction*. Reserved for the future if we
// need to query the dynamic reputation table (custom mod factions
// outside the standard enum). The direct faction_id field-read above
// covers the typical hostile/friendly/neutral classification.
const uintptr_t kAddrCSWSCreatureGetFaction = acc::addr::R(0x00513fc0);

// Rules global pointer used for feat lookup — see kAddrRulesGlobal in the
// data-globals section at the end of this file. Dereferences to a
// CSWSRules*; CSWRules is at offset 0 (the `internal` member), so the
// same pointer is usable for both as the `this` for CSWRules::GetFeat.

// CSWRules::GetFeat — __thiscall(ushort feat_index) -> CSWFeat*.
// Returns nullptr if index out-of-range or feat not loaded (bit_flags
// & 0x10 unset). BYTES_PURGED=4.
const uintptr_t kAddrCSWRulesGetFeat = acc::addr::R(0x00550c00);

// CSWFeat::GetNameText — __thiscall(CExoString* out) -> CExoString*.
// Fetches localized feat name via CTlkTable::Fetch using the feat's
// `field2_0x8` strref. Constructs the out CExoString in place; caller
// must read .c_string before destruct (we deliberately leak the heap
// string, same pattern as CSWSItem::GetPropertyDescription).
const uintptr_t kAddrCSWFeatGetNameText = acc::addr::R(0x005cd760);

// CSWFeat::GetDescriptionText — __thiscall(CExoString* out) -> CExoString*.
// Sibling of GetNameText. Resolves the feat's `description` strref at
// +0x0c through CTlkTable; same heap-leak rule applies.
const uintptr_t kAddrCSWFeatGetDescriptionText = acc::addr::R(0x005cd800);

// CSWSpellArray::GetSpell — __thiscall(int spell_id) -> CSWSpell* (cast
// as int in the Ghidra signature). Returns nullptr / 0 if spell_id is
// out of range. BYTES_PURGED=4.
const uintptr_t kAddrCSWSpellArrayGetSpell = acc::addr::R(0x0059b6d0);

// CSWSpell::GetSpellNameText — __thiscall(CExoString* out) -> CExoString*.
// Same shape as CSWFeat::GetNameText: constructs the localized name into
// the out string in place; caller must read .c_string before any
// destructor runs (we leak — CRT mismatch otherwise). BYTES_PURGED=4.
const uintptr_t kAddrCSWSpellGetSpellNameText = acc::addr::R(0x0059b940);

// CSWGuiInGameAbilities methods (__thiscall). OnAbilitySelectionChanged is the
// repaint entry point: after we drive the LB_ABILITY cursor (DriveListBoxSelection
// bypasses the engine's onSelectionChanged), calling it repaints the detail
// labels + description for the new row. The On*ButtonPressed trio switches tab.
// OnEnterPower null-derefs when the Powers tab has no powers (the tutorial-save
// "Kräfte, nicht verfügbar" case) — guard the Powers tab before driving it.
// Per-entry repaint handlers — the coordinate-free path. OnEnterSkill reads
// row->id as the skill index; OnEnterFeat takes a feat id; both rewrite the
// detail labels + description. These (NOT OnAbilitySelectionChanged, which is
// mouse-hit-test driven) are what keyboard nav must call. All are
// __thiscall(this, <one 4-byte arg>) — purgeSize 4; the typedef must carry the
// arg or the callee's `ret 4` corrupts the caller frame.
const uintptr_t kAddrAbilitiesOnEnterSkill = acc::addr::R(0x006ad180);  // (this, CSWGuiControl* row)
const uintptr_t kAddrAbilitiesOnEnterFeat = acc::addr::R(0x006ad410);  // (this, ushort featId)
const uintptr_t kAddrAbilitiesOnEnterPower = acc::addr::R(0x006acce0);  // (this, int) — crashes when powers empty
// OnAbilitySelectionChanged is the engine's mouse-driven selection handler
// (hit-tests cursor vs the CSWGuiSkillFlow chart). Kept for reference; do NOT
// call it for keyboard nav.
const uintptr_t kAddrAbilitiesOnAbilitySelChanged = acc::addr::R(0x006ad4b0);  // (this, int) mouse-driven
const uintptr_t kAddrAbilitiesUpdateView = acc::addr::R(0x006ad560);  // void(void)
const uintptr_t kAddrAbilitiesOnSkillsButton = acc::addr::R(0x006adad0);  // void(void) — field139=0 + UpdateView
const uintptr_t kAddrAbilitiesOnFeatsButton = acc::addr::R(0x006ada70);  // void(void) — field139=2 + UpdateView
const uintptr_t kAddrAbilitiesOnPowersButton = acc::addr::R(0x006adaa0);  // void(void) — field139=1 + UpdateView
// DisplayPowers() — predicate: returns 1 iff the character is a Jedi AND the
// powers chart has rows. Used to decide whether the Powers tab exists (the
// engine's own tab cycle uses it to skip an empty Powers tab). Pure check.
const uintptr_t kAddrAbilitiesDisplayPowers = acc::addr::R(0x006abe70);  // int(void)

// CSWGuiInGameAbilities::HandleInputEvent(this, int code, int val). The panel's
// own input handler; code 0x29 runs the engine's smart tab cycle
// (Skills -> Powers-if-any -> Feats -> Skills, auto-skipping an empty Powers
// tab). These are panel-internal codes, distinct from the manager kInput* codes.
const uintptr_t kAddrAbilitiesHandleInputEvent = acc::addr::R(0x006ae5f0);

// CGuiInGame::ShowExamineBox — DO NOT CALL DIRECTLY (skeleton).
// Verified 2026-05-10 from Lane's symbol table: this is a 2-parameter
// __thiscall — `void(ulong handle, int param_2)` with BYTES_PURGED=8.
// param_2's purpose is unknown; the engine populates the panel via a
// server roundtrip (`SendServerToPlayerExamineGui_CreatureData @0x56ebe0`
// and 4 sister functions per object kind), so calling ShowExamineBox
// without the prior server request leaves the panel showing stale text
// from the last examine. The Phase 2C hotkey now reads stats directly
// instead of trying to drive the panel.
// CGuiInGame::ShowExamineBox @0x62d3e0 — DO NOT CALL FOR CREATURE EXAMINE.
// Despite the name, this is a **generic TLK-message-box** opener, NOT a
// creature-examine API. Decompile of vtable[27] (CSWGuiMessageBox::SetMessage)
// shows param_1 is treated as a TLK strref:
//   CTlkTable::GetSimpleString(TlkTable, &outStr, param_1);
//   SetMessage(outStr);
// The only retail caller is CSWGuiStore::OnControlStoreAButton which passes
// 0xa3de (a TLK strref = 41950) for the "you can't afford this" popup.
// Passing a game-object handle would look up a junk TLK row and produce
// an empty message box. KOTOR 1 has no rich creature-examine panel — the
// sighted-player "Examine" action renders its content from the local
// in-world UI overlay, not a separate panel. Keep the address constant
// in case we want to drive a TLK-strref popup later (e.g. for help text).
const uintptr_t kAddrCGuiInGameShowExamineBox = acc::addr::R(0x0062d3e0);
const uintptr_t kAddrCGuiInGameHideExamineBox = acc::addr::R(0x0062d440);

// CClientExoApp::GetObjectName — universal display-name accessor.
// __thiscall(ulong handle, CExoString* outName) -> int. BYTES_PURGED=8
// (verified 2026-05-10 from Lane's symbol table). Returns a localized
// display name for any object kind, falling through the engine's own
// name-resolution chain (template FirstName / appearance.2da
// displayname / racialtypes.2da name / tag). Use this in preference to
// engine_area::GetObjectName when working from a handle (queue targets,
// LastTarget) — the latter falls back to the modder-assigned tag for
// generic enemies whose `first_name` strref is empty.
const uintptr_t kAddrCClientExoAppGetObjectName = acc::addr::R(0x005ed350);

// CSWGuiStore — merchant/trading panel (PanelKind::Store, slot 0x84 in
// CGuiInGame). Two listboxes plus a description listbox; the mode-toggle
// flips which one is visible.
//
// Mode detection is language-agnostic: ShowBuyGUI sets bit 1 of
// shopitems_listbox.navigable.control.bit_flags (and clears it on
// invitems_listbox); ShowSellGUI does the inverse. We read either
// listbox's CSWGuiControl-level bit_flags (offset +0x44 within the
// listbox, identical to every other CSWGuiControl) and decide.
//
// Row entries (rows of shopitems / invitems listboxes) are
// CSWGuiStoreItemEntry (size 0x394) whose first 0x1c4 bytes are the
// row's CSWGuiButton — same as the chain sees. The item handle lives
// at +0x1c4 within the entry.
//
// GetItemBuyValue / GetItemSellValue take a CSWSItem*, not a handle. The
// handle is *client*-side, so we run it through CServerExoApp::
// ClientToServerObjectId before CServerExoApp::GetItemByGameObjectID.
// CSWSItem.stack_size + bit_flags expose the stock count / infinite-stock
// flag (bit 2 = infinite, per CSWGuiStore::OnControlEntered).
const uintptr_t kVtableCSWGuiStore                     = acc::addr::R(0x00756e38);
const uintptr_t kVtableCSWGuiStoreItemEntry            = acc::addr::R(0x00756850);

// CSWGuiInGameItemEntry — rows of CSWGuiInGameInventory.item_listbox AND
// Container's loot listbox. Same shape as CSWGuiStoreItemEntry: button at
// offset 0, item_game_object_id at +0x1c4. Resolves through
// ResolveItemFromClientHandle and reads stack_size via the same offsets
// above.
const uintptr_t kVtableCSWGuiInGameItemEntry           = acc::addr::R(0x007568f8);

// CSWGuiStore::GetItemBuyValue / GetItemSellValue — __thiscall returning
// ulong, single CSWSItem* argument. Both pop 4 bytes (callee).
const uintptr_t kAddrCSWGuiStoreGetItemBuyValue = acc::addr::R(0x006c0790);
const uintptr_t kAddrCSWGuiStoreGetItemSellValue = acc::addr::R(0x006c07f0);

// CSWGuiStore::OnControlInvAButton / OnControlStoreAButton — the engine
// click handlers attached to the accept_button in Sell / Buy mode
// respectively (see ShowSellGUI / ShowBuyGUI, which switch the binding
// via CSWGuiControl::AddEvent). Both are __thiscall(this, CSWGuiControl*
// param_1) and read the item handle from param_1+0x1c4 — which works
// for an accept_button (lands on store->item_id) OR a row pointer
// (lands on row.obj_id). Calling either with a store-item row as
// param_1 is the keyboard-Enter shortcut that bypasses the accept-button
// step entirely.
//
// Both open the engine's confirmation MessageBox if the price exceeds
// the player's level threshold; otherwise they commit the trade
// immediately via SellItem / BuyItem.
const uintptr_t kAddrCSWGuiStoreOnControlInvAButton = acc::addr::R(0x006c0f40);
const uintptr_t kAddrCSWGuiStoreOnControlStoreAButton = acc::addr::R(0x006c1130);

// CServerExoApp::ClientToServerObjectId — __thiscall(ulong) -> ulong.
// CServerExoApp::GetItemByGameObjectID — __thiscall(ulong) -> CSWSItem*.
const uintptr_t kAddrServerExoAppClientToServerObjectId = acc::addr::R(0x004aea30);
const uintptr_t kAddrServerExoAppGetItemByGameObjectID = acc::addr::R(0x004ae760);

// CSWSItem::GetPropertyDescription — __thiscall(CExoString* out) -> CExoString*.
// The text Inventory/Store/Equip render into their description listbox on
// hover: the formatted property block (damage, feats, defence, on-hit, attack
// mod, misc) followed by the base description. IMPORTANT (decompile-verified):
// for base item_type 0x2e (lightsaber crystals) and 6 (grenades) it SKIPS the
// whole property block and returns ONLY the description — for a crystal that's
// usually just the bare "Spezial:" header, since crystal templates carry no
// item properties (the bonus lives on the assembled saber / in the workbench's
// keyed-property line, see kAddrCSWSItemGetKeyedPropertyString). The caller
// passes uninitialised stack memory for `out`; the function constructs a
// CExoString in place by allocating a heap c_string. We then read out the
// c_string and deliberately leak the allocation rather than calling
// ~CExoString (heap ownership across the DLL/EXE boundary risks CRT mismatch;
// see the same pattern in LookupTlk above).
const uintptr_t kAddrCSWSItemGetPropertyDescription = acc::addr::R(0x0055f340);

// CSWSItem::GetKeyedPropertyString — __thiscall(CExoString* out, byte key) ->
// CExoString*. Formats just the properties on this item whose slot-key byte
// matches `key` into the "Spezielle Eigenschaften: …" block (same header +
// sorting as GetSortedPropertyStrings). This is the keyed bonus line the
// workbench shows for an upgrade slot — the gameplay text crystals lack in
// GetPropertyDescription. Same heap-leak rule as GetPropertyDescription.
const uintptr_t kAddrCSWSItemGetKeyedPropertyString = acc::addr::R(0x0055f510);

// Per-category property-block builders that GetPropertyDescription calls in
// fixed order (decompile-verified at 0055f340). Each is
// __thiscall(CSWSItem* this, CExoString* accumulator) and *appends* its
// labelled block to the accumulator (never clears it). Calling them into our
// own separate accumulators lets us reconstruct the four screen-reader blocks
// (tags / values / properties / description) without parsing the localised
// blob — see engine_reads::BuildItemDescriptionBlocks. The engine's own guards
// (replicated by the caller) are: skip ALL of these when item_type is 0x2e
// (crystal) or 6 (grenade); call the five weapon-only builders only when
// weapon_type != 0.
typedef void (__thiscall* PFN_AddItemProperty)(void* item, CExoString* accum);
const uintptr_t kAddrItemAddFeatRequirements = acc::addr::R(0x00556490);  // -> "tags"
const uintptr_t kAddrItemAddDamageProperties = acc::addr::R(0x00556de0);  // weapon-only
const uintptr_t kAddrItemAddRangeProperties = acc::addr::R(0x005543b0);  // weapon-only
const uintptr_t kAddrItemAddCriticalThreatProps = acc::addr::R(0x00558950);  // weapon-only
const uintptr_t kAddrItemAddOnHitProperties = acc::addr::R(0x00558c10);  // weapon-only
const uintptr_t kAddrItemAddWeaponSizeProperties = acc::addr::R(0x005544f0);  // weapon-only
const uintptr_t kAddrItemAddAttackModifierProps = acc::addr::R(0x0055e930);  // -> "values"
const uintptr_t kAddrItemAddDefenceProperties = acc::addr::R(0x005599d0);  // -> "values"
const uintptr_t kAddrItemAddMiscellaneousProps = acc::addr::R(0x0055a510);  // -> "properties"

// CExoString default constructor — __thiscall(CExoString* this). Initialises a
// valid empty string the engine builders can append to. (GetPropertyDescription
// calls this on its accumulator before the Add* sequence.)
typedef CExoString* (__thiscall* PFN_CExoStringCtor)(CExoString* this_);
const uintptr_t kAddrCExoStringDefaultCtor = acc::addr::R(0x005b3190);

// CSWItem::GetBaseItem — __thiscall(CSWItem* this) -> CSWBaseItem*. The CSWItem
// subobject is at offset 0 of CSWSItem, so the CSWSItem* we already hold is a
// valid `this`. Offsets into the returned CSWBaseItem (CMP-verified from the
// GetPropertyDescription disassembly, NOT the placeholder struct header):
//   +0x09 (byte) weapon_type — 0 = not a weapon
//   +0xac (byte) item_type   — 0x2e crystal, 6 grenade
const uintptr_t kAddrCSWItemGetBaseItem = acc::addr::R(0x005b4790);

// CSWGuiInGameJournal::PopulateItemListBox @0x00645330 — clears items_listbox
// and rebuilds one CSWGuiJournalItemEntry per quest in the current
// active/done + sort mode, then clears the journal's HasChanged flag (so the
// next Draw won't repopulate again).
const uintptr_t kAddrJournalPopulateItemListBox = acc::addr::R(0x00645330);

// CSWGuiInGameInventory::PopulateItemListBox @0x006b4810 — the inventory
// sibling of the journal call above. Rebuilds item_listbox from the equipped
// slots + the party item repository, keeping only rows CheckFilter accepts,
// then clears bit_flags bit 0 so the next Draw won't repopulate again. The
// filter button ("Zeigen: …") only raises that bit, so calling this makes the
// new list final before we re-bind the chain to it.
// NOT in the 2004-relink rebase table (no known-good bytes to sigscan for);
// R() returns 0 there and inventory::ForceRepopulate self-skips via
// acc::addr::Ok, leaving that build on the pre-existing lazy behaviour.
const uintptr_t kAddrInventoryPopulateItemListBox = acc::addr::R(0x006b4810);

// CSWGuiJournalItemEntry rows are CSWGuiButton-derived (size 0x1cc) with
// their own vtable. The journal's OnControlEntered fires on mouse hover
// over a row and rewrites item_description_label with the full text.
const uintptr_t kVtableCSWGuiJournalItemEntry          = acc::addr::R(0x007518c0);

// ---------------------------------------------------------------------------
// .data global pointers - deliberately NOT passed through acc::addr::R().
//
// R() covers .text only (engine_rebase.h) and .data is byte-stable across the
// builds it exists for, so wrapping these would be wrong rather than merely
// redundant. Upstream models them as the AddressDatabase `global_pointers`
// table and already carries them by name for both K1 and K2. Same treatment as
// kAddrGuiManagerPtr (engine_manager.h), kAddrAppManagerPtr (engine_player.h)
// and kAddrCExoSoundPtr (audio_bus.h).
// ---------------------------------------------------------------------------

// CSWSRules* - the global rules object. The feat and spell table geometry
// hanging off it is documented in engine_offsets_fields.h.
constexpr uintptr_t kAddrRulesGlobal              = 0x007a3a28;

// CTlkTable* - the live talk table, one indirection. Used as the `this` for
// kAddrGetSimpleString above.
constexpr uintptr_t kAddrTlkTablePtr     = 0x007a3a08;
