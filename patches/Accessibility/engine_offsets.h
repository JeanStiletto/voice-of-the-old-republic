// Engine struct/vtable offset table.
//
// Layer: engine/ (pure engine constants — no menu-side state, no behavior).
// All values are derived from Lane's Ghidra DB / SARIF (see file header
// comments inline) and apply to the K1 Steam build (GoG bytes match — see
// memory: project_ghidra_gog_steam_bytes_match).
//
// Constants are intentionally at file scope rather than under
// `namespace acc::engine` so menu-side callsites stay readable. Same
// rationale as `engine_input.h`'s `kInput*` codes.

#pragma once

#include <cstddef>
#include <cstdint>

// ---------------------------------------------------------------------------
// GuiControlMethods vtable indices for RTTI-style downcasts.
//
// Each accessor returns the same `this` cast to the concrete subclass, or
// nullptr if the control isn't of that subclass. They are trivial
// implementations (no engine state mutation, no allocation), so calling them
// from inside our hook is safe.
//
// Verified against the SARIF GuiControlMethods struct (offset 80/84/88/92).
// ---------------------------------------------------------------------------
constexpr int kVtableAsLabel        = 20;
constexpr int kVtableAsLabelHilight = 21;
constexpr int kVtableAsButton       = 22;
constexpr int kVtableAsButtonToggle = 23;

// ---------------------------------------------------------------------------
// CSWGuiButton / CSWGuiLabel field offsets (verified against the SARIF
// datatypes):
//   CSWGuiButton:        navigable(0x6c)+border(0x74)+border(0x74)+text(0x70) = 0x1c4
//                        → text at 0x154, text_params at +0x18 → CExoString at 0x16c
//                        → str_ref at +0x08 within text_params → 0x174
//   CSWGuiButtonToggle:  embeds CSWGuiButton at offset 0; offsets unchanged.
//   CSWGuiLabel:         control(0x5c)+border(0x74)+text(0x70) = 0x140
//                        → text at 0xd0, text_params at +0x18 → CExoString at 0xe8
//                        → str_ref at +0x08 within text_params → 0xf0
//   CSWGuiLabelHilight:  embeds CSWGuiLabel at offset 0; offsets unchanged.
// ---------------------------------------------------------------------------
constexpr size_t kButtonTextOffset    = 0x16c;
constexpr size_t kButtonStrRefOffset  = 0x174;
constexpr size_t kLabelTextOffset     = 0xe8;
constexpr size_t kLabelStrRefOffset   = 0xf0;

// ---------------------------------------------------------------------------
// Element-state field offsets (verified via Ghidra decomp of Draw /
// SetSelected / HandleInputEvent for each class):
//
//   CSWGuiButtonToggle.field2_0x1c8 — uint32; bit 0 = on/off. HandleInputEvent
//                                     XOR's bit 0 with 1 on activate; SetSelected
//                                     masks to bit 0; Draw branches on (& 1) to
//                                     pick the rendered border.
//   CSWGuiSlider.max_value (Lane-named) at +0x70 — uint32, slider max.
//   CSWGuiSlider.cur_value (Lane-named) at +0x74 — uint32, current slider value.
//                                                  HandleInputEvent calls
//                                                  SetCurValue on inc/dec keys.
// ---------------------------------------------------------------------------
constexpr size_t kButtonToggleStateOffset = 0x1c8;
constexpr size_t kSliderMaxValueOffset    = 0x70;
constexpr size_t kSliderCurValueOffset    = 0x74;

// ---------------------------------------------------------------------------
// CSWGuiText layout (from swkotor.exe.h + decompiled CSWGuiText::Initialize
// at 0x00417310 confirmed via headless Ghidra against Lane's gzf):
//   +0x00  vtable
//   +0x04  extent (16 bytes)
//   +0x14  CAurGUIStringInternal* gui_string
//   +0x18  text_params (CSWGuiTextParams):
//             +0x00 (=0x18 in text)  CExoString text  (c_string + length)
//             +0x08 (=0x20 in text)  int str_ref
//             ...
//             +0x50 (=0x68 in text)  CSWGuiText* text_object
//
// For CSWGuiLabel: control(0x5C)+border(0x74)+text@(0xD0).
//   gui_string ptr     @ 0xD0 + 0x14 = 0xE4
//   text_params.text   @ 0xE8 (c_string + length)
//   text_params.str_ref @ 0xF0
//   text_params.text_object @ 0xE8 + 0x50 = 0x138
//
// For CSWGuiButton: navigable(0x6C)+border(0x74)+border(0x74)+text@(0x154).
//   gui_string ptr     @ 0x154 + 0x14 = 0x168
//   text_params.text   @ 0x154 + 0x18 = 0x16C
//   text_params.str_ref @ 0x174
//   text_params.text_object @ 0x16C + 0x50 = 0x1BC
//
// **gui_string is the ground-truth source.** CSWGuiText::Initialize calls
// NewCAurGUIString(text_params.text.c_string, ...) which constructs a
// CAurGUIStringInternal whose constructor copies the c_string into a
// heap-allocated buffer at offset +0x14 within CAurGUIStringInternal
// (Ghidra-named field5_0x14). CSWGuiText::Draw reads ONLY from gui_string
// (it ignores text_params at draw time). For overridden subclasses where
// the inline text_params CExoString and strref are empty (CSWGuiInGameMenu's
// 8 icon labels at vtable=0x0073E8E8 are the canonical case — verified via
// 584 speculative-read miss events in patch-20260502-190936.log on the
// previous build), gui_string still holds the rendered c_string.
// ---------------------------------------------------------------------------
constexpr size_t kLabelGuiStringPtrOffset  = 0xE4;
constexpr size_t kLabelTextObjectOffset    = 0x138;
constexpr size_t kButtonGuiStringPtrOffset = 0x168;
constexpr size_t kButtonTextObjectOffset   = 0x1BC;
constexpr size_t kTextObjectTextOffset     = 0x18;   // CSWGuiText.text_params.text
constexpr size_t kTextObjectStrRefOffset   = 0x20;   // CSWGuiText.text_params.str_ref
constexpr size_t kAurGuiStringCStrOffset   = 0x14;   // CAurGUIStringInternal.field5

// CAurGUIStringInternal vtable address (from Lane's Ghidra DB:
// CAurGUIStringInternal_vtable @ 0x00741878). Used to validate that a
// gui_string pointer actually refers to a CAurGUIStringInternal object
// before dereferencing it — see ReadGuiString for why this matters.
constexpr uintptr_t kVtableCAurGUIStringInternal = 0x00741878;

// Slider class identity by vtable address. Resolved via SARIF xrefs:
// 0x0073E9D0 is referenced by CSWGuiSlider's constructor (0x41bb0d) and
// destructor (0x41bb9d) — i.e. it's the slider's vftable. Sliders have no
// AsSlider downcast accessor in GuiControlMethods, so vtable equality is
// the only safe identity check.
constexpr uintptr_t kVtableSlider = 0x0073E9D0;

// CSWGuiListBox vtable. Same identity-by-vtable pattern as the slider:
// no AsListBox accessor exists in GuiControlMethods, so we identify by
// vtable equality. Used by chain navigation (RebindChain recurses one
// level into multi-row listboxes), the tabbed-panel detector, and the
// listbox-content extraction path in ExtractAnnounceableText (which walks
// a listbox's rows when the panel walk encounters one as a child — the
// recurring `vtable=0073E840 src=none` cases in our log are listbox
// containers wrapping the actual message text).
constexpr uintptr_t kVtableListBox = 0x0073E840;

// ---------------------------------------------------------------------------
// CSWGuiClassSelection (chargen "Klassenauswahl" panel — also backs the
// second-level "Standard- vs. Eigener Charakter" prompt). Verified against
// k1_win_gog_swkotor.exe.xml SYMBOL @ 0x00758020 + STRUCTURE size 0x1560.
//
//   +0x00..+0x64   CSWGuiPanel panel
//   +0x64          undefined4
//   +0x68          CSWCCreature* char_gen_creature
//   +0x6c          CSWGuiClassSelChar class_selections[6]   (6 * 0x25c = 0xe28)
//   +0xe94         CSWGuiLabel character_gen_label
//   +0xfd4         CSWGuiLabel instruction_label
//   +0x1114        CSWGuiLabel description_label
//   +0x1254        CSWGuiLabel class_label  ← currently-focused class name
//   +0x1394        CSWGuiButton back_button
//
// CSWGuiClassSelChar embeds CSWGuiButton at offset 0; panel.controls[]
// stores pointers to that embedded button (single inheritance), so a
// focused class-icon control pointer lands on a multiple-of-0x25c offset
// inside the class_selections[] range.
//
// `class_label` is the engine's source of truth for the focused class name
// (engine updates it on hover/focus via CSWGuiClassSelection::OnEnterButton
// @ 0x006dba70). Read its gui_string instead of the icon button's empty
// inline text or the misleading sibling-label fallback.
// ---------------------------------------------------------------------------
constexpr uintptr_t kVtableCSWGuiClassSelection      = 0x00758020;
constexpr size_t    kClassSelectionsArrayOffset      = 0x6c;
constexpr size_t    kClassSelCharSize                = 0x25c;
constexpr int       kClassSelectionsCount            = 6;
constexpr size_t    kClassSelectionClassLabelOffset  = 0x1254;

// ---------------------------------------------------------------------------
// CSWGuiPortraitCharGen (chargen "Porträtauswahl" panel). Verified against
// k1_win_gog_swkotor.exe.xml SYMBOL @ 0x00759ea8 + STRUCTURE size 0x1240.
//
//   +0x00..+0x64   CSWGuiPanel panel
//   +0x64          CSWCCreature* creature   ← chargen creature being built
//   +0x6c          CSWGuiLabel main_title
//   +0x1ac         CSWGuiLabel sub_title
//   +0x2ec         CSWGuiLabel portrait_label  (named in SARIF but never
//                                                populated at runtime —
//                                                gui_string stays empty)
//   +0xafc         CSWGuiButton accept_button
//   +0xcc0         CSWGuiButton back_button
//   +0xe84         CSWGuiButton right_arrow_button (image-only, cycles +1)
//   +0x1048        CSWGuiButton left_arrow_button  (image-only, cycles -1)
//   +0x1238        ulong portrait_id              (named portrait_id, but
//                                                NOT the live cycle index —
//                                                observed stuck at first
//                                                value across cycles. Likely
//                                                the committed-on-accept
//                                                slot. Kept here only as a
//                                                last-resort fallback.)
//
// Live cycle state lives on `creature.portrait` (CSWPortrait inline =
// CResRef = char[16]) at CSWCObject offset 0xa8 — UpdatePortraitButton
// (0x006f8ad0) writes the new resref there on every cycle. Reading 16
// bytes at panel.creature + 0xa8 yields a string like "po_pmhc3" which
// we parse into a localised description (gender + race + variant).
// ---------------------------------------------------------------------------
constexpr uintptr_t kVtableCSWGuiPortraitCharGen     = 0x00759ea8;
constexpr size_t    kPortraitCharGenCreatureOffset   = 0x64;
constexpr size_t    kPortraitLabelOffset             = 0x2ec;
constexpr size_t    kPortraitRightArrowOffset        = 0xe84;
constexpr size_t    kPortraitLeftArrowOffset         = 0x1048;
constexpr size_t    kPortraitIdOffset                = 0x1238;

// CSWCObject.portrait at +0xa8 (CSWPortrait, inline 16-byte CResRef) —
// reserved kept for the resref direct-read path even though the chargen
// flow (verified 2026-05-09 in patch-20260509-053256.log) leaves this
// field zero throughout cycling. The live cycle index is only reachable
// via the engine accessor below.
constexpr size_t    kCreaturePortraitResRefOffset    = 0xa8;
constexpr size_t    kResRefSize                      = 16;

// CSWCCreature::GetPortraitId — __thiscall, no args, returns the portrait
// row index into portraits.2da (verified live: returned 24 → 25 across a
// Right+Right+Left cycle in the chargen Porträtauswahl panel). The named
// CSWCCreatureStats.portrait_id at +0x11c, CSWGuiPortraitCharGen.portrait_id
// at +0x1238, and CSWCObject.portrait at +0xa8 are all stale during chargen
// — this accessor is the only reliable read-side primitive we have.
constexpr uintptr_t kAddrCSWCCreatureGetPortraitId   = 0x00617070;

// CSWCCreature::GetPortrait — __thiscall, fills caller-supplied CResRef
// (16 bytes) with the current portrait baseresref (e.g. "po_pmhc3").
// Signature per SARIF: `CResRef* __thiscall(CResRef* outBuf, byte side)`.
// `side` selects the alignment variant (0 = light/default, 1..4 = darker
// variants matching the baseresrefe / baseresrefve / etc. columns); we
// only ever pass 0 since chargen doesn't ladder dark side. Caller-pops
// 8 bytes (BYTES_PURGED=8). Resref is 16 bytes, NOT necessarily null-
// terminated when length == 16, so we always reserve a 17-byte buffer.
constexpr uintptr_t kAddrCSWCCreatureGetPortrait     = 0x00617030;

// ---------------------------------------------------------------------------
// CSWGuiAbilitiesCharGen (chargen "Attribute" panel — step 2 of Eigener
// Charakter). Verified against k1_win_gog_swkotor.exe.xml SYMBOL @
// 0x00759c68 + STRUCTURE size 0x3df4.
//
//   +0x110c..+0x1ba4   ability_labels[6]   (CSWGuiLabel[6], 0x140 each)
//   +0x188c..+0x2324   ability_buttons[6]  (CSWGuiButton[6], 0x1c4 each)
//   +0x2870..+0x3308   ability_plus_buttons[6]
//   +0x3308..+0x3da0   ability_minus_buttons[6]
//   +0x3dec            int selected_ability  ← the field +/- handlers read
//
// Struct order is STR(0), DEX(1), CON(2), WIS(3), INT(4), CHA(5) — matched
// 1:1 against the addresses in patch-20260509-055548.log:1233-1241 by
// computing button - panel_base for each chain entry. Note that this
// differs from the visual top-to-bottom order: row 4 (INT) is struct
// index 4, row 5 (WIS) is struct index 3. selected_ability uses struct
// order, so derive the index from the button's offset (not its chain
// position).
//
// Why we touch this panel: OnPlusButton (0x6f8670) / OnMinusButton
// (0x6f8480) are zero-arg thiscalls that read selected_ability — the
// fired button's identity is NOT used to pick which ability changes. The
// engine only writes selected_ability on a real mouse click (or via the
// engine's own OnEnterPointsButton on hover, which our chain-step cursor
// warp doesn't reliably trigger because the engine's hit-test resolves
// one row above the warp coords here too — same pattern as the Options
// tab cluster). With selected_ability stuck at 0, every Left/Right press
// modifies STR. We mirror chain focus into the field on every chain
// rebind / step so +/- targets the focused row.
// ---------------------------------------------------------------------------
constexpr uintptr_t kVtableCSWGuiAbilitiesCharGen          = 0x00759c68;
constexpr size_t    kAbilitiesCharGenLabelsArrayOffset     = 0x110c;
constexpr size_t    kAbilitiesCharGenButtonsArrayOffset    = 0x188c;
constexpr size_t    kAbilitiesCharGenSelectedAbilityOffset = 0x3dec;
constexpr int       kAbilitiesCharGenAbilityCount          = 6;
constexpr size_t    kCSWGuiLabelSize                       = 0x140;
constexpr size_t    kCSWGuiButtonSize                      = 0x1c4;

// ---------------------------------------------------------------------------
// Container offsets verified against Lane's SARIF (DATATYPE entries for
// CSWGuiPanel and CSWGuiListBox). CExoArrayList layout:
//   +0x00  T**      data         (heap array of element pointers)
//   +0x04  int      size
//   +0x08  int      capacity
//
// CSWGuiPanel.activeControl is at +0x1c — current focused child (read by
// our SetActiveControl mid-function hook before the SET).
// CSWGuiPanel.controls is at +0x20 — list of every direct child control.
// CSWGuiListBox.controls is at +0x29c — list of row controls. Listbox cursor
// state is in three shorts immediately after the controls array:
//   +0x2c4  short    items_per_page
//   +0x2c6  short    selection_index   ← which row is "current"
//   +0x2c8  short    top_visible_index ← scroll offset
// ---------------------------------------------------------------------------
constexpr size_t kPanelActiveControlOffset      = 0x1c;
constexpr size_t kPanelControlsOffset           = 0x20;
constexpr size_t kListBoxControlsOffset         = 0x29c;
constexpr size_t kListBoxBitFlagsOffset         = 0x2bc;
constexpr size_t kListBoxItemsPerPageOffset     = 0x2c4;
constexpr size_t kListBoxSelectionIndexOffset   = 0x2c6;
constexpr size_t kListBoxTopVisibleIndexOffset  = 0x2c8;

// CSWGuiControl.extent is an inline CSWGuiExtent (16 bytes) at +0x4:
//   +0x0  left    int
//   +0x4  top     int
//   +0x8  width   int
//   +0xC  height  int
constexpr size_t kControlExtentOffset = 0x4;

// ---------------------------------------------------------------------------
// CSWGuiSaveLoadEntry layout (from swkotor.exe.h:16673). Each row in the
// CSWGuiSaveLoad.games_listbox is a CSWGuiSaveLoadEntry that embeds a
// CSWGuiButton at offset 0 and carries the slot's metadata as inline
// CExoStrings. We read these directly (no engine call) to enrich the row
// announcement with planet + area names — these aren't in the rendered
// button text, only in the right-hand preview pane labels which are stale
// until the engine fires its onSelectionChanged callback (and our direct
// selection_index write doesn't trigger that callback).
//
// Field offsets after the embedded CSWGuiButton (size 0x1c4):
//   +0x1c4  uint32   bit_field
//   +0x1c8  uint32   save_number
//   +0x1cc  uint32   field3
//   +0x1d0  uint32   field4
//   +0x1d4  byte     gameplayhint
//   +0x1d5  byte     storyhint
//   +0x1d8  CExoString  savegamename     (user-given save name)
//   +0x1e0  CExoString  save_file_name
//   +0x1e8  CExoString  areaname         (e.g. "Kommandomodul")
//   +0x1f0  CExoString  lastmodule       (e.g. "Endar Spire")
// ---------------------------------------------------------------------------
constexpr size_t kSaveLoadEntrySaveNumberOffset    = 0x1c8;
constexpr size_t kSaveLoadEntrySaveGameNameOffset  = 0x1d8;
constexpr size_t kSaveLoadEntryAreaNameOffset      = 0x1e8;
constexpr size_t kSaveLoadEntryLastModuleOffset    = 0x1f0;

// ---------------------------------------------------------------------------
// CExoArrayList<T*> — engine container for arrays of pointers.
// ---------------------------------------------------------------------------
struct CExoArrayList {
    void** data;
    int    size;
    int    capacity;
};

// ---------------------------------------------------------------------------
// Vector — Aurora-engine 3D vector. Used for world position, object
// orientation (heading vector with z=0), audio listener pose, etc.
//
// Coordinate frame (investigation Q1 — right-handed, Z-up):
//   +X = east, +Y = north, +Z = up. 1.0 unit ≈ 1 metre.
//   Bearing convention for object orientation: 0° = +X = east; CCW positive.
// ---------------------------------------------------------------------------
struct Vector {
    float x;
    float y;
    float z;
};

// ---------------------------------------------------------------------------
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
// ---------------------------------------------------------------------------
struct CExoString {
    char*    c_string;
    uint32_t length;
};
typedef CExoString* (__thiscall* PFN_GetSimpleString)(void* this_,
                                                      CExoString* out,
                                                      uint32_t strref);
constexpr uintptr_t kAddrGetSimpleString = 0x0041e8f0;
constexpr uintptr_t kAddrTlkTablePtr     = 0x007a3a08;

// ---------------------------------------------------------------------------
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
// ---------------------------------------------------------------------------
typedef void (__thiscall* PFN_InGameEquipOnEnterSlot)(void* panel, void* slot_btn);
typedef void (__thiscall* PFN_InGameEquipOnSelectSlot)(void* panel, void* slot_btn);
constexpr uintptr_t kAddrInGameEquipOnEnterSlot  = 0x006b9470;
constexpr uintptr_t kAddrInGameEquipOnSelectSlot = 0x006b8eb0;
constexpr size_t    kControlIsActiveOffset       = 0x4c;

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
constexpr uintptr_t kAddrInGameEquipOnItemSelected = 0x006b7920;
constexpr uintptr_t kAddrInGameEquipOnOKPressed    = 0x006b9160;
