// Engine struct field offsets - the upstream AddressDatabase `offsets` table
// (keyed class + member; our flat k*Offset names carry the class in the comment
// above each block, which is the piece the name itself loses).
//
// Part of the engine_offsets.h family - see engine_offsets_types.h for the
// split rationale and the full file list.
//
// Scope note: two kinds of constant live here that are not literally offsets,
// because separating them from the field they describe makes both halves
// unreadable:
//   * struct geometry - element counts, strides and sizeof values
//     (kClassSelCharSize, kClassSelectionsCount, kSkillFlowColumnStride, ...);
//   * field interpretation - the bit masks and sentinels documented with the
//     field they decode (kControlVisibleBit, kSwsItemInfiniteStockBit,
//     kFlowSkillStructEmptyFeatId).
// Constants that stand alone - vtable slot indices, TLK strrefs, enum bytes,
// panel input codes - are in engine_offsets_values.h instead.
//
// Per-game values (KOTOR 2 port)
// ------------------------------
// Every declaration here goes through one of the acc::off markers, so the file
// carries both games' layouts rather than only KOTOR 1's. See
// engine_offsets_select.h for how to choose between them:
//
//   Same(x)      - verified identical in both games
//   Pick(k1, k2) - verified different
//   Todo(k1)     - KOTOR 2 value not yet established; poisons on KOTOR 2
//
// They are `const` rather than `constexpr` because Pick/Todo resolve at load
// time against the detected game. Nothing here needs to be a compile-time
// constant - that was checked across the whole codebase before the conversion
// (no array bounds, no static_assert, no case labels, no template arguments).
// If you add one that does, it cannot use these markers.
//
// Everything is currently Todo(): the conversion was made deliberately as a
// pure structural change with the KOTOR 1 values untouched, so that KOTOR 1
// behaviour is provably identical and the reverse-engineering can then land
// one constant at a time. `grep -c "Todo("` is the remaining-work counter.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets_select.h"

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
// KOTOR 2 values are OBSERVED bases plus a VERIFIED-unchanged interior, not
// extrapolation. The text sub-object's position was read out of each class's
// own Load/Draw in KOTOR 2 (label 0xd0 -> 0xd8, button 0x154 -> 0x160), and
// CSWGuiText's internals were then confirmed identical (see kTextObject* below),
// so text = base+0x18 and str_ref = base+0x20 still hold.
//
// The button is the clearest illustration of why a flat "+4 per class" would
// have been wrong: its Load touches +0x70, +0xe8, +0x160 where KOTOR 1 has
// 0x6c, 0xe0, 0x154 — deltas of +4, +8 and +12, because navigable, border_1 and
// border_2 each grew 4 bytes and the displacement accumulates.
const size_t kButtonTextOffset    = acc::off::Pick(0x16c, 0x178);
const size_t kButtonStrRefOffset  = acc::off::Pick(0x174, 0x180);
const size_t kLabelTextOffset     = acc::off::Pick(0xe8, 0xf0);
const size_t kLabelStrRefOffset   = acc::off::Pick(0xf0, 0xf8);

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
//
// KOTOR 2 values from the same three functions in its own binary:
//   CSWGuiButtonToggle::Load masks the "ISSELECTED" .gui byte into bit 0 of
//   this+0x1d4 (+0xc — the toggle sits behind a CSWGuiButton, which grew).
//   CSWGuiSlider::HandleInputEvent increments/decrements this+0x78 and compares
//   it against this+0x74 (+4), and reads its extent width/height at 0xc/0x10 —
//   unshifted, which is the same insertion-point evidence as everywhere else.
const size_t kButtonToggleStateOffset = acc::off::Pick(0x1c8, 0x1d4);
const size_t kSliderMaxValueOffset    = acc::off::Pick(0x70, 0x74);
const size_t kSliderCurValueOffset    = acc::off::Pick(0x74, 0x78);

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
const size_t kLabelGuiStringPtrOffset  = acc::off::Pick(0xE4, 0xEC);
const size_t kLabelTextObjectOffset    = acc::off::Pick(0x138, 0x140);
const size_t kButtonGuiStringPtrOffset = acc::off::Pick(0x168, 0x174);
const size_t kButtonTextObjectOffset   = acc::off::Pick(0x1BC, 0x1C8);
// CSWGuiText's OWN layout is identical in both games — verified, and it is what
// makes the composed offsets above trustworthy. KOTOR 2's CSWGuiText::Load
// reads gui_string at +0x14, text_params.color at +0x34 and
// text_params.bit_flags at +0x50; KOTOR 1's header gives exactly those three.
// So only the text object's POSITION inside its owner moved, not its insides.
const size_t kTextObjectTextOffset     = acc::off::Same(0x18);   // CSWGuiText.text_params.text
const size_t kTextObjectStrRefOffset   = acc::off::Same(0x20);   // CSWGuiText.text_params.str_ref
// Was the one gap in the text chain. CLOSED 2026-07-31, and the answer is that
// it does not move — verified, not assumed, which matters because this class
// was the least safe thing here to guess at (its vtable is one of the few whose
// slot count did NOT match, 67 vs 32).
//
// Both constructors do the same four things in the same order: strlen the
// incoming C string, allocate len+1, store the buffer pointer, copy into it.
// KOTOR 2 stores it at this+0x14, exactly as KOTOR 1 does. The head of the
// class agrees too — both zero 0x4/0x8/0xc/0x10 (bounds), both write a -1
// cursor index immediately followed by alignment=9.
//
// KOTOR 2's class HAS grown, just not before this field: its colour vector sits
// at 0x30 where KOTOR 1 keeps it at 0x1c, because KOTOR 2 embeds a 3-element
// array at 0x24 that KOTOR 1 has no counterpart for. So this is `Same` on
// evidence, not on a "shallow class" hunch.
//
// Consequence: KOTOR 2 text extraction can read the RENDERED string like
// KOTOR 1 does, instead of falling back to the inline CExoString — which is the
// difference between reading and not reading every control whose text_params
// are empty.
const size_t kAurGuiStringCStrOffset   = acc::off::Same(0x14);   // CAurGUIStringInternal.field5

// CSWGuiKeyMapButton — the keyboard-mapping screen's row control (vtable
// 0x007593c8). Each row embeds TWO CSWGuiButtons: action_button at +0 (the
// event name, "Vorwärts" — read via the normal button offsets) and
// mapped_key_button at +0x1c8 (the bound key, "W"). Layout decompiled from
// swkotor.exe.h CSWGuiKeyMapButton + the field-offset anchors
// (key_mappings ptr at +0x38c ⇒ sizeof(CSWGuiButton)=0x1c4 ⇒ mapped_key_button
// at +0x1c8). `unchangeable` (non-zero = fixed binding) is at +0x3a4. The key
// text reads at mapped_key + button offsets, e.g. gui_string at 0x1c8+0x168.
//
// KOTOR 2 from its own constructor, which writes the same nine fields KOTOR 1's
// does and no others — including both distinctive -1 initialisers (input_index
// = INPUTDEVICE_NONE, and the trailing field13). Anchors: it stores the
// key_mappings argument at +0x3a4 and the embedded mapped_key_button's
// gui_object at +0x20c and parent_control at +0x1ec, which put that button at
// +0x1d4 by two independent subtractions. Every field after key_mappings then
// lands one slot on from KOTOR 1's, and each written value matches.
const size_t    kKeyMapButtonMappedKeyOffset = acc::off::Pick(0x1c8, 0x1d4);
const size_t    kKeyMapButtonUnchangeableOff = acc::off::Pick(0x3a4, 0x3bc);
// CSWGuiKeyMapButton.key_code @ +0x39c — the engine InputIndices value of the
// freshly captured key (KEYBOARD_*; NOT a DIK scancode — set with `updated`=1 on
// capture, written to swkotor.ini in decimal on Accept). Resolve to a VK via
// engine_keymap::InputIndexToVk to test the new game bind against mod hotkeys.
const size_t    kKeyMapButtonKeyCodeOff      = acc::off::Pick(0x39c, 0x3b4);

// CSWGuiEditbox layout (verified against k1_win_gog_swkotor.exe.xml SYMBOL
// CSWGuiEditbox_vtable @ 0x0073EAC8 + STRUCTURE size 0x160 + swkotor.exe.h
// CSWGuiEditbox/CSWGuiEditText). Two vanilla editboxes share this layout: the
// chargen Name panel's `name_editbox` (this exact vtable) and the save-name
// popup's `edit_box` (a CSWGuiSaveGameEditBox subclass, kVtableSaveGameEditbox
// below — same struct, only HandleKeyPress overridden).
//
//   +0x00..+0x6c   CSWGuiNavigable navigable
//   +0x6c..+0xe0   CSWGuiBorder    border  (single border, not the dual-
//                                           border CSWGuiButton has)
//   +0xe0..+0x160  CSWGuiEditText  edit_text:
//      +0xe0..+0x150  CSWGuiText text  (gui_string ptr at +0xf4 absolute,
//                                       same shape as label/button)
//      +0x150  short  caret-or-selection short A
//      +0x152  short  caret-or-selection short B
//      +0x154  undefined4
//      +0x158  CExoString string (the *typed* text — c_string + length)
//        +0x158  char* c_string
//        +0x15c  uint32 length
//
// The two shorts at +0x150 / +0x152 are caret index and selection length
// (in some order). swkotor.exe.h labels them `field1_0x70` / `field2_0x72`
// without further specifying which is which. Initial assumption: +0x150 =
// caret, +0x152 = selection length. The polling monitor logs both values
// on every diff so we can verify on first run; once confirmed, we strip
// the diagnostic.
//
// KOTOR 2 shifts the whole editbox tail by +8, from two OBSERVED bases in its
// own CSWGuiEditbox::Load: it reaches the border vtable at this+0x70 (KOTOR 1:
// 0x6c) and the edit_text's text vtable at this+0xe8 (KOTOR 1: 0xe0) — +4 then
// +8, because navigable and border each grew 4.
//
// The interior is then unchanged, and that is established rather than assumed:
// CSWGuiText is 0x70 bytes in BOTH games (KOTOR 1 button 0x1c4 - text at 0x154;
// KOTOR 2 button 0x1d0 - text at 0x160), and kTextObject* above records that
// its internals match. So CSWGuiEditText's own members keep their positions and
// only the +8 of the edit_text base carries through.
const size_t    kEditboxShortA             = acc::off::Pick(0x150, 0x158);
const size_t    kEditboxShortB             = acc::off::Pick(0x152, 0x15a);
const size_t    kEditboxStringCStrOffset   = acc::off::Pick(0x158, 0x160);
const size_t    kEditboxStringLengthOffset = acc::off::Pick(0x15c, 0x164);

// CSWGuiNameChargen (chargen "Name eingeben" panel — step 5 of Eigener
// Charakter, also reused in the Standard-Charakter quick flow). Verified
// against k1_win_gog_swkotor.exe.xml SYMBOL CSWGuiNameChargen_vtable @
// 0x00759F38 + STRUCTURE size 0x9C4 + swkotor.exe.h CSWGuiNameChargen.
//
//   +0x00..+0x64   CSWGuiPanel    panel
//   +0x64          undefined4 field1
//   +0x68          undefined4 field2
//   +0x6c          CSWGuiButton   end_button   (BTN_OK — "Annehmen")
//   +0x230         CSWGuiEditbox  name_editbox
//   +0x390         CSWGuiLabel    main_title_label
//   +0x4d0         CSWGuiLabel    subtitle_label
//   +0x610         CSWGuiButton   back_button  (BTN_CANCEL — "Abbrechen")
//   +0x7d4         CSWGuiButton   random_button ("Zufallsname")
//   ...
//
// `name_editbox` is at a fixed offset within the panel struct (not just in
// panel.controls[]), so the spec's findEditbox callback can index directly
// rather than walking children for the unique vtable.
// K2 column from the CSWGuiNameChargen ctor 0x00918B10 by tag: END_BTN
// @0x70, NAME_BOX_EDIT @0x240, MAIN_TITLE_LBL @0x3a8, SUB_TITLE_LBL @0x4f0,
// BTN_BACK @0x638, BTN_RANDOM @0x808.
const size_t    kNameChargenEditboxOffset  = acc::off::Pick(0x230, 0x240);
const size_t    kNameChargenEndButtonOffset = acc::off::Pick(0x6c, 0x70);

// CSWGuiNameChargen carries a `main_title_label` ("CHARAKTERAUSWAHL") and a
// `subtitle_label` ("Name") at distinct fixed offsets. The first one is the
// stale parent-flow header that BioWare reuses across all chargen sub-
// panels; the second is the screen-specific title. Our title-walk picks
// the first announceable label by panel-controls index, which lands on
// main_title_label first — wrong for any user trying to know which step
// they're on. The editbox spec's titleOverride reads subtitle_label
// directly via this offset to substitute the correct title speech.
const size_t    kNameChargenSubtitleLabelOffset = acc::off::Pick(0x4d0, 0x4f0);

// CSWGuiSaveNamePanel (the "Enter name for saved game" modal popup that opens
// on top of the SaveLoad screen when committing a save). Verified against
// k1_win_gog_swkotor.exe.xml SYMBOL CSWGuiSaveNamePanel_vtable @ 0x007576D0 +
// the SARIF DATATYPE field layout.
//
//   +0x00..+0x64   CSWGuiPanel           panel
//   +0x64          undefined4            field1
//   +0x68          CSWGuiButton          ok_button     (submit — "Annehmen")
//   +0x22c         CSWGuiButton          cancel_button ("Abbrechen")
//   +0x3f0         CSWGuiSaveGameEditBox edit_box
//   +0x550         CSWGuiLabel           title_label
//
// `edit_box` is a CSWGuiSaveGameEditBox: struct size 0x160, body is entirely a
// CSWGuiEditbox (only HandleKeyPress is overridden, engine-side, to filter
// filename-illegal chars). So the string fields sit at the same offsets as a
// plain editbox (c_string +0x158, length +0x15c) and ReadEditbox works
// unchanged. Unlike CSWGuiNameChargen, title_label is the screen-specific
// title (no stale parent header), so the spec's titleOverride reads it
// directly.
// K2 column from the shared SaveNamePanel/SaveGameEditBox ctor 0x008586D0
// by tag: BTN_OK @0x6c, BTN_CANCEL @0x23c, EDITBOX @0x40c, LBL_TITLE @0x574.
const size_t    kSaveNameEditboxOffset        = acc::off::Pick(0x3f0, 0x40c);
const size_t    kSaveNameOkButtonOffset       = acc::off::Pick(0x68, 0x6c);
const size_t    kSaveNameTitleLabelOffset     = acc::off::Pick(0x550, 0x574);

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
//
// KOTOR 2 reads its array geometry straight off its own constructor's
// eh_vector_constructor_iterator_ call, which states base, element size and
// count as literals: (this+0x70, 0x26c, 6). The panel then constructs four
// labels and one button, exactly as KOTOR 1's does, so class_label is still the
// fourth label after the array: 0x70 + 6*0x26c = 0xef8, plus three KOTOR 2
// labels of 0x148 = 0x12d0.
const size_t    kClassSelectionsArrayOffset      = acc::off::Pick(0x6c, 0x70);
const size_t    kClassSelCharSize                = acc::off::Pick(0x25c, 0x26c);
const int       kClassSelectionsCount            = acc::off::Same(6);
const size_t    kClassSelectionClassLabelOffset  = acc::off::Pick(0x1254, 0x12d0);

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
//
// KOTOR 2 values read out of its own constructor, matched to KOTOR 1's by the
// .gui TAG each control is bound to — `InitControl(panel, &member, "BTN_ARRR")`
// in KOTOR 1 against `InitControl(this+0xee4, "BTN_ARRR")` in KOTOR 2. The tag
// is the identity; the offset is whatever it is. That is what makes this safe
// on a panel whose members drift by 0xAB8 overall.
//
// Internal consistency across the whole panel, which is why these are trusted:
// the label stride is 0x140 in KOTOR 1 and 0x148 in KOTOR 2, the button stride
// 0x1c4 and 0x1d0, and every consecutive pair of KOTOR 2 controls is exactly
// one stride apart. The deltas here are NOT uniform (+0x14, +0x60, +0x6c)
// precisely because they accumulate over the grown members in between.
const size_t    kPortraitCharGenCreatureOffset   = acc::off::Pick(0x64, 0x68);
const size_t    kPortraitLabelOffset             = acc::off::Pick(0x2ec, 0x300);
const size_t    kPortraitRightArrowOffset        = acc::off::Pick(0xe84, 0xee4);
const size_t    kPortraitLeftArrowOffset         = acc::off::Pick(0x1048, 0x10b4);
// KOTOR 2 +0x1cf0. Four fields of this class pair up across the two games with
// a delta of exactly 0xAB8, three of them in UpdatePortraitButton alone — the
// two lookup arrays it indexes (0x120c -> 0x1cc4, 0x1218 -> 0x1cd0) and the
// index it uses (0x1234 -> 0x1cec) — plus the float that OnPanelAdded and the
// constructor both write (0x1230 -> 0x1ce8). portrait_id sits 4 bytes above a
// confirmed anchor, so this is bracketed rather than extrapolated.
//
// Do not shortcut this to "UpdatePortraitButton's index field": that function
// indexes with field23_0x1234, which is NOT portrait_id and lands one slot low.
const size_t    kPortraitIdOffset                = acc::off::Pick(0x1238, 0x1cf0);

// CSWCObject.portrait at +0xa8 (CSWPortrait, inline 16-byte CResRef) —
// reserved kept for the resref direct-read path even though the chargen
// flow (verified 2026-05-09 in patch-20260509-053256.log) leaves this
// field zero throughout cycling. The live cycle index is only reachable
// via the engine accessor below.
const size_t    kCreaturePortraitResRefOffset    = acc::off::Todo(0xa8);
// MUST stay constexpr: sizes real arrays (`char liveResref[kResRefSize + 1]`
// in menus_extract), so it has to be a compile-time constant and cannot go
// through the acc::off markers. If KOTOR 2 ever needs a different value, the
// array must be sized to the larger of the two and the *used* length carried
// separately at run time — do not simply widen this to a runtime value.
// A ResRef is a fixed 16-byte engine-wide primitive, so divergence is unlikely.
constexpr size_t kResRefSize                     = 16;

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
const size_t    kAbilitiesCharGenLabelsArrayOffset     = acc::off::Todo(0x110c);
const size_t    kAbilitiesCharGenButtonsArrayOffset    = acc::off::Todo(0x188c);
const size_t    kAbilitiesCharGenSelectedAbilityOffset = acc::off::Todo(0x3dec);
const int       kAbilitiesCharGenAbilityCount          = acc::off::Todo(6);
const size_t    kCSWGuiLabelSize                       = acc::off::Todo(0x140);
const size_t    kCSWGuiButtonSize                      = acc::off::Todo(0x1c4);

// CSWGuiSkillsCharGen (chargen "Fähigkeiten" panel — step 3 of Eigener
// Charakter). Same shape as CSWGuiAbilitiesCharGen — three info-pair
// labels, value buttons, +/- buttons, an int "currently focused" index
// — just 8 skills instead of 6 abilities and no modifier concept.
// Verified against k1_win_gog_swkotor.exe.xml SYMBOL @ 0x00759990 +
// STRUCTURE size 0x49d0.
//
//   +0x70C  remaining-points VALUE (label, mirrors skill_points int)
//   +0xC0C  cost-points VALUE (label, 1 or 2 in vanilla)
//   +0xFCC..+0x19CC   skill_labels[8]   (CSWGuiLabel[8], 0x140 each)
//   +0x19CC..+0x27EC  skill_buttons[8]  (CSWGuiButton[8], 0x1c4 each)
//   +0x2D38..+0x3B58  plus_buttons[8]
//   +0x3B58..+0x4978  minus_buttons[8]
//   +0x49B8           int skill_points (remaining budget)
//   +0x49BC           ulong selected_skill_index (analog of
//                                                 selected_ability)
//
// Skill order matches struct order matches visual top-to-bottom (no
// swap as on the Attribute panel): Computer, Demolitions, Stealth,
// Awareness, Persuade, Repair, Security, Treat Injury.
const size_t    kSkillsCharGenLabelsArrayOffset      = acc::off::Todo(0xfcc);
const size_t    kSkillsCharGenButtonsArrayOffset     = acc::off::Todo(0x19cc);
const size_t    kSkillsCharGenSelectedSkillOffset    = acc::off::Todo(0x49bc);
const int       kSkillsCharGenSkillCount             = acc::off::Todo(8);
const size_t    kSkillsCharGenRemainingValueOffset   = acc::off::Todo(0x70c);
const size_t    kSkillsCharGenCostValueOffset        = acc::off::Todo(0xc0c);

// description_list_box offset within CSWGuiSkillsCharGen (per SARIF).
const size_t    kSkillsCharGenDescriptionListBoxOffset      = acc::off::Todo(0x6c);

// Three info-pair labels on this panel that aren't in the chain (they're
// CSWGuiLabels, not buttons) but carry per-row state the user needs:
//
//   +0x70C  remaining-points VALUE  ("30" → "14" as the user spends).
//           Mirrors the int at +0x3DB8 (ability_points_remaining); we
//           read the rendered label so format quirks (commas, locale)
//           pass through unchanged.
//   +0xC0C  cost-points VALUE       ("0", "1", "3"). Refreshed per
//           focused ability AND per +/- press. Reflects the cost in
//           ability_points_remaining to push the FOCUSED ability up by
//           one (D&D point-buy curve: 8→14 costs 1 each, 14→16 costs 2,
//           16→18 costs 3).
//   +0xE8C  modifier VALUE          ("0", "-1", "+4"). D&D modifier of
//           the focused ability at its current value. Engine pre-formats
//           the sign so we pass through unmodified.
const size_t    kAbilitiesCharGenRemainingValueOffset = acc::off::Todo(0x70c);
const size_t    kAbilitiesCharGenCostValueOffset      = acc::off::Todo(0xc0c);
const size_t    kAbilitiesCharGenModifierValueOffset  = acc::off::Todo(0xe8c);

// description_listbox offset within CSWGuiAbilitiesCharGen (per SARIF) —
// same +0x6c as the Skills panel.
const size_t    kAbilitiesCharGenDescriptionListBoxOffset      = acc::off::Todo(0x6c);

// CSWGuiFeatsCharGen (chargen "Talente" panel — step 5 of Eigener Charakter,
// also reused at level-up). Verified against k1_win_gog_swkotor.exe.xml
// SYMBOL CSWGuiFeatsCharGen_vtable @ 0x007598b0 + STRUCTURE size 0x1a1c.
//
// Unlike the Skills/Abilities panels (fixed 8/6 row arrays of buttons), the
// Feats panel renders feats through a single CSWGuiListBox (feats_listbox)
// whose contents are built at runtime by BuildAvailableList based on the
// chargen creature's class. A second listbox (description_listbox) holds
// the multi-line description of the currently focused feat, and name_label
// mirrors that feat's name.
//
//   +0xbac   CSWGuiLabel  name_label              (focused feat's name)
//   +0xcec   CSWGuiButton accept_button           (BTN_ACCEPT, "OK")
//   +0xeb0   CSWGuiButton back_button             (BTN_BACK, "Abbrechen")
//   +0x1074  CSWGuiButton reccomended_button      (BTN_RECOMMENDED)
//   +0x1238  CSWGuiButton select_button           (BTN_SELECT, "Hinzuf./Entf.")
//   +0x13fc  CSWGuiListBox feats_listbox          (LB_FEATS — picker rows)
//   +0x16dc  CSWGuiListBox description_listbox    (LB_DESC — wrapped text)
//
// The "second popup" the user sees when entering Talente isn't the main
// panel — it's the SkillInfoBox-slot ShowGranted overlay (skillinfo.gui)
// rendered on top, with its own listbox of granted feats. The main panel
// stays underneath; its description_listbox.controls[0] mirrors the picker
// selection so reading from there gives the focused-feat description.
// KOTOR 2 values from its own ctor 0x00909E00, tag-wired (LBL_NAME,
// BTN_SELECT, LB_FEATS, LB_DESC): the panel re-orders members (buttons
// before the listboxes, unlike K1) so nothing here follows a delta rule —
// each is its own witness. OnEnterFeat's tail independently reconfirms
// the name label (+0xab0, via its text_params at +0xf0) and BTN_SELECT.
const size_t    kFeatsCharGenNameLabelOffset        = acc::off::Pick(0xbac, 0xab0);
const size_t    kFeatsCharGenSelectButtonOffset     = acc::off::Pick(0x1238, 0x1168);
const size_t    kFeatsCharGenFeatsListBoxOffset     = acc::off::Pick(0x13fc, 0x15c8);
const size_t    kFeatsCharGenDescriptionListBoxOffset = acc::off::Pick(0x16dc, 0x18b8);

// Four parallel feat lists tracked on the panel — each a
// CExoArrayList<ushort> { ushort* data, int size, int capacity }
// inline-stored 12 bytes apart. Together they partition every feat the
// panel cares about so DetermineFeat can return a status byte:
//
//   field19 @ +0x19bc  data; @ +0x19c0 size  — existing  (creature already has)
//   field20 @ +0x19c8  data; @ +0x19cc size  — granted   (auto-given this level)
//   field23 @ +0x19d4  data; @ +0x19d8 size  — available (BuildAvailableList output)
//   field26 @ +0x19e0  data; @ +0x19e4 size  — chosen    (picked this session)
// KOTOR 2: the four lists sit at +0x1ba8/+0x1bb4/+0x1bc0/+0x1bcc (same
// 12-byte stride, ctor-witnessed), and the K2 BuildButtons twin 0x0090BD50
// pins each list's IDENTITY by painting K1's exact status codes from it
// (status 1=existing from +0x1ba8, 2=granted from +0x1bb4, 0=available
// from +0x1bc0, 4=chosen from +0x1bcc).
const size_t    kFeatsCharGenExistingListDataOffset    = acc::off::Pick(0x19bc, 0x1ba8);
const size_t    kFeatsCharGenExistingListSizeOffset    = acc::off::Pick(0x19c0, 0x1bac);
const size_t    kFeatsCharGenGrantedListDataOffset     = acc::off::Pick(0x19c8, 0x1bb4);
const size_t    kFeatsCharGenGrantedListSizeOffset     = acc::off::Pick(0x19cc, 0x1bb8);
const size_t    kFeatsCharGenAvailableListDataOffset   = acc::off::Pick(0x19d4, 0x1bc0);
const size_t    kFeatsCharGenAvailableListSizeOffset   = acc::off::Pick(0x19d8, 0x1bc4);
const size_t    kFeatsCharGenChosenListDataOffset      = acc::off::Pick(0x19e0, 0x1bcc);
const size_t    kFeatsCharGenChosenListSizeOffset      = acc::off::Pick(0x19e4, 0x1bd0);

// CSWGuiSkillFlowChart embedded at +0x1a08 (struct size 0x10). It's the
// 2D scrollable feat-tree grid: a CExoArrayList-shaped header + a
// (selected_col, selected_row) pair packed into the trailing bytes.
//
//   chart +0x00  CSWGuiSkillFlow** rows_data
//   chart +0x04  int               rows_size
//   chart +0x08  int               rows_capacity
//   chart +0x0c  byte              selected_col   (0..2 in BuildButtons)
//   chart +0x0d  byte              selected_row
// KOTOR 2 +0x1bf4 — ctor-witnessed (`add ecx,0x1bf4` before the chart ctor
// 0x0089A650) and the address the SetSelectedSkill caller census flagged.
const size_t    kFeatsCharGenChartOffset               = acc::off::Pick(0x1a08, 0x1bf4);
// K2 witnessed in CSWGuiSkillFlowChart::SetSelectedSkill's twin 0x0089C070
// (rows data [chart+0], size [chart+4], selected col/row bytes +0xc/+0xd)
// and ClearChart's twin 0x0089A6E0 (same rows fields) — all identical.
const size_t    kSkillFlowChartRowsDataOffset          = acc::off::Same(0x0);
const size_t    kSkillFlowChartRowsSizeOffset          = acc::off::Same(0x4);
const size_t    kSkillFlowChartSelectedColOffset       = acc::off::Same(0xc);
const size_t    kSkillFlowChartSelectedRowOffset       = acc::off::Same(0xd);

// CSWGuiSkillFlow row (1148 bytes). Three CSWGuiFlowSkillStruct columns
// at +0x5c, +0x184, +0x2ac (stride 0x128) plus 2 connector-line images
// at +0x3d4 / +0x428 the renderer uses to draw progression arrows.
// K2: the CSWGuiSkillFlow ctor 0x00899930 builds the 3-column vector at
// row+0x60 with element size 0xb4 (the columns SHRANK on K2), and
// SetSelectedSkill's twin walks cells as row + col*0xb4 + 0x108 for the
// feat id — 0x108 = 0x60 (first column) + 0xa8 (id within the cell).
const size_t    kSkillFlowFirstColumnOffset            = acc::off::Pick(0x5c, 0x60);
const size_t    kSkillFlowColumnStride                 = acc::off::Pick(0x128, 0xb4);
// MUST stay constexpr: sizes real arrays (`unsigned short featId[...]` in
// menus_chargen_feats and menus_powers_levelup), so it has to be a compile-time
// constant and cannot go through the acc::off markers. If KOTOR 2's skill-flow
// chart has a different column count, size the arrays to the larger value and
// carry the real count at run time rather than making this dynamic.
constexpr int   kSkillFlowColumnsPerRow          = 3;

// Within a CSWGuiFlowSkillStruct (a single chart cell, 0x128 bytes):
//   +0x11c  ulong  feat ID  (or 0xffffffff for an empty cell)
//   +0x120  ulong  status   (0 avail, 1 chosen-this-level, 2 granted,
//                            3 existing, 4 locked — same enum DetermineFeat
//                            returns)
//   +0x124  ulong  selection bits (bit 0 = currently selected)
// K2 cell fields witnessed in SetSelectedSkill (id compare at cell+0xa8)
// and the per-row SetSkillStatus twin 0x0089A160 (status byte store at
// row + col*0xb4 + 0x10c = cell+0xac). Empty sentinel stays 0xffffffff
// (CreateFeatChart's twin initialises cells with -1, as on KOTOR 1).
const size_t    kFlowSkillStructFeatIdOffset           = acc::off::Pick(0x11c, 0xa8);
const size_t    kFlowSkillStructStatusOffset           = acc::off::Pick(0x120, 0xac);
const unsigned  kFlowSkillStructEmptyFeatId            = acc::off::Same(0xffffffff);

// CSWRules / CSWSRules — the global rules object holds the feats array.
// Global slot at 0x007a3a28 holds a CSWSRules* (which is a thin wrapper
// containing CSWRules at offset 0, so the pointer doubles as a CSWRules*).
// Used to reverse-lookup a feat ID from a row's name strref:
//
//   feats   @ +0x90  CSWFeat[]   (each entry 0x48 bytes)
//   field   @ +0xa4  ushort      feat_count (live count of valid entries)
//
// Within CSWFeat:
//   +0x08   ulong   name_strref (the TLK strref the engine writes onto a
//                                SkillEntry row's text_params)
// (kAddrRulesGlobal itself is in engine_offsets_addresses.h, data-globals.)
// KOTOR 2 values disasm-witnessed in its own GetFeat 0x006A20F0 (the
// Batch-4-banked twin): array POINTER at [rules+0x108], count word at
// [rules+0x11c], stride 0x50 (grew from 0x48; K2 adds a flags dword at
// feat+0x28 whose bit 4 gates validity). Same base as our reads — the
// callers hand it *kAddrRulesGlobal (0x00A1B4D0), the global every K2
// rules decompile this port has made uses.
const size_t    kRulesFeatsArrayOffset        = acc::off::Pick(0x90, 0x108);
const size_t    kRulesFeatCountOffset         = acc::off::Pick(0xa4, 0x11c);
const size_t    kFeatStructSize               = acc::off::Pick(0x48, 0x50);
// Same on K2: its OnEnterFeat twin 0x0090B9B0 SetStrRefs [feat+0x8] onto
// the name label (description strref at [feat+0xc]).
const size_t    kFeatNameStrRefOffset         = acc::off::Same(0x08);

// Offset of the embedded CSWGuiSkillFlowChart inside CSWGuiPowersLevelUp.
// Matches struct field33_0x19fc (swkotor.exe.h:16637). We call
// CSWGuiSkillFlowChart::SetSelectedSkill on this offset to keep the chart's
// render-side highlight in sync with our keyboard focus (same pattern as
// chargen_feats — see kFeatsCharGenChartOffset).
// KOTOR 2: 0x1bf8, witnessed in the K2 ctor 0x009074E0 (`add ecx,0x1bf8`
// before the chart ctor 0x0089A650 and again before the power-set feeder
// 0x0089AAA0), and it is the tail member exactly as on K1: chart + 0x14
// == the panel's own new-size 0x1c0c (allocated by the LevelUpPanel
// powers-button callback 0x00904420).
const size_t    kPowersLevelUpChartOffset              = acc::off::Pick(0x19fc, 0x1bf8);

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
// active_control: KOTOR 2 value read DIRECTLY out of its own
// CSWGuiPanel::SetActiveControl decompile, which compares and assigns
// this+0x20 where KOTOR 1 uses this+0x1c. Not inferred — observed.
//
// The same decompile shows CSWGuiPanel.manager at +0x1c (KOTOR 1: +0x18) and
// the control's gui-sound byte at +0x59 (KOTOR 1: +0x55), so this region of
// both CSWGuiPanel and CSWGuiControl is uniformly +4.
const size_t kPanelActiveControlOffset      = acc::off::Pick(0x1c, 0x20);
// controls: +0x24, now OBSERVED rather than guessed. KOTOR 2's own
// CSWGuiPanel hit-test (vtable slot 16, FUN_0040eae0) indexes the child array
// at this+0x24 with its count at this+0x28, against KOTOR 1's 0x20/0x24.
const size_t kPanelControlsOffset           = acc::off::Pick(0x20, 0x24);
//
// KOTOR 2: the whole listbox tail is +0x10, observed in its own
// CSWGuiListBox::HandleInputEvent (vtable slot 15). Every landmark of KOTOR 1's
// version reappears there at exactly +0x10: the -1 test on selection_index, the
// bit_flags tests for 0x40 / 0x200 / clearing bit 12, `size - items_per_page`,
// and the final `controls.data[selection_index]->HandleInputEvent`. The +0x10
// is the accumulated growth of the bases and embedded members ahead of it
// (control +4, border +4, scrollbar +8), not a flat class delta.
const size_t kListBoxControlsOffset         = acc::off::Pick(0x29c, 0x2ac);
const size_t kListBoxBitFlagsOffset         = acc::off::Pick(0x2bc, 0x2cc);
const size_t kListBoxItemsPerPageOffset     = acc::off::Pick(0x2c4, 0x2d4);
const size_t kListBoxSelectionIndexOffset   = acc::off::Pick(0x2c6, 0x2d6);
const size_t kListBoxTopVisibleIndexOffset  = acc::off::Pick(0x2c8, 0x2d8);

// CSWGuiControl.extent is an inline CSWGuiExtent (16 bytes) at +0x4:
//   +0x0  left    int
//   +0x4  top     int
//   +0x8  width   int
//   +0xC  height  int
// Identical in both games — observed. KOTOR 2's CSWGuiPanel hit-test reads its
// own extent as left/top/width/height at +0x4/+0x8/+0xc/+0x10, exactly as
// KOTOR 1 does (a panel IS a control, so this is the base-class field).
//
// This pins down where KOTOR 2's insertion sits: the extent at 0x4..0x10 is
// unshifted, while active_control (0x1c->0x20), controls (0x20->0x24) and
// bit_flags (0x44->0x48) all move +4. So the added field lands between +0x10
// and +0x1c, and anything below +0x14 can be expected to carry over.
const size_t kControlExtentOffset = acc::off::Same(0x4);

// CSWGuiControl tooltip fields (verified against the
// CSWGuiControl::DisplayToolTip @ 0x418a90 decompile + struct definition in
// swkotor.exe.h:5238). Resolution order the engine uses:
//   * If field4_0x24 (tooltip_strref) is non-zero → CTlkTable::GetSimpleString
//   * Else if tooltip_string at +0x28 is non-empty → use literal CExoString
//   * Else if parent_control at +0x14 is non-null → recurse into parent
//   * Else no tooltip
// (An optional " : KeyName" suffix gated on field6_0x30 / keybind action id —
// we skip this in keyboard nav; the user already knows which key they pressed.)
//
// KOTOR 2 values read out of its own DisplayToolTip (slot 36) and confirmed by
// its CSWGuiControl::Load: all three are +4, and the parent read is corroborated
// twice — Load stores the Obj_ParentID lookup at this+0x18, and DisplayToolTip
// recurses through this+0x18's vtable slot 36. That also settles where the
// insertion sits: the extent at 0x4..0x10 is unshifted, +0x14 already is not.
const size_t kControlParentOffset       = acc::off::Pick(0x14, 0x18);  // CSWGuiControl* parent
const size_t kControlTooltipStrRefOffset = acc::off::Pick(0x24, 0x28); // uint32 strref (0 = none)
const size_t kControlTooltipStringOffset = acc::off::Pick(0x28, 0x2c); // CExoString literal

// CSWGuiControl.id is the .gui-time numeric ID assigned by the layout file.
// Stable across localizations and panel.controls reordering, so this is the
// canonical way to address a known child of a known panel kind.
// +4 in KOTOR 2, observed: its CSWGuiControl::Load reads the .gui property
// named "ID" (default -1) and stores it at this+0x54, where KOTOR 1 uses +0x50.
const size_t kControlIdOffset = acc::off::Pick(0x50, 0x54);  // int id

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
const size_t kSaveLoadEntrySaveNumberOffset    = acc::off::Todo(0x1c8);
const size_t kSaveLoadEntrySaveGameNameOffset  = acc::off::Todo(0x1d8);
const size_t kSaveLoadEntryAreaNameOffset      = acc::off::Todo(0x1e8);
const size_t kSaveLoadEntryLastModuleOffset    = acc::off::Todo(0x1f0);

// CSWGuiControl.is_active @ +0x4c - the gate every engine On*Slot /
// OnControlEntered handler tests before doing anything. Keyboard-driven callers
// must raise it first and restore it after; the handlers themselves are
// documented in engine_offsets_addresses.h.
// KOTOR 2 +0x50, observed: its upgrade OnEnterSlot opens with the identical
// `if (control->is_active != 0)` gate, reading param_1+0x50 where KOTOR 1 reads
// +0x4c. Consistent with the rest of this region of CSWGuiControl being +4.
const size_t    kControlIsActiveOffset       = acc::off::Pick(0x4c, 0x50);

// CSWGuiUpgrade.field9 - the description label CSWGuiUpgrade::OnControlEntered
// writes its built string into (see engine_offsets_addresses.h); we read the
// result back from here.
// K2 0x2ee0, witnessed in the K2 SetDescription twin 0x008CCD70: SetText on
// [this+0x2fd0] (label text_params at label+0xf0), extent block from
// [this+0x2ee4], GetFontHeight on the text object [this+0x2fb8], and the
// SetExtent vtable call through [this+0x2ee0] — the label base.
const size_t    kUpgradeDescLabelOffset            = acc::off::Pick(0x1f60, 0x2ee0);  // panel.field9 (CSWGuiLabel)

// CSWGuiUpgrade.field24_0x2f48 — bit 0 is the "picker open" state (set by
// OnSlotSelected, cleared by OnUpgradeSelected's close tail). Clear it on cancel.
const size_t    kUpgradePickerOpenFlagOff = acc::off::Todo(0x2f48);  // panel.field24

// CSWGuiUpgrade slot-type table geometry, plus the two panel/button fields that
// index it. The table base is kAddrUpgradeSlotTypeTable in
// engine_offsets_addresses.h, where the per-entry layout is documented.
// Entry stride is 0xc in BOTH games — observed, not assumed: KOTOR 2's
// OnPanelAdded indexes the table with `slot * 0xc + (category - 1) * 0x48`
// against KOTOR 1's `((custom_value - 4) + category * 4) * 0xc`.
//
// **But the INDEX FORMULA differs and that is not a constant.** KOTOR 1 packs
// FOUR slot types per category (its category term is `category * 4 * 0xc`);
// KOTOR 2 packs SIX (`(category - 1) * 0x48`), and biases the category by one
// instead of the slot by four. A caller that keeps KOTOR 1's arithmetic and
// only swaps the base address will read the wrong entry on KOTOR 2 — so the
// index needs a per-game branch in the code, not a per-game constant here.
// See kAddrUpgradeSlotTypeTable in engine_offsets_addresses.h.
const size_t    kUpgradeSlotTypeStride    = acc::off::Same(12);
// Same +8 in KOTOR 2: its OnEnterSlot reads the UpgradeType at
// `&DAT_009a84e8 + idx` and the strref at `&DAT_009a84f0 + idx` — the same two
// fields of the same entry, 8 bytes apart, exactly as KOTOR 1 does.
const size_t    kUpgradeSlotTypeStrRefOff = acc::off::Same(8);
// KOTOR 2 +0x3d2c, from its own OnPanelAdded — same `(char)category == 1`
// saber test, same use as the table's category term. Shifts by 0xDE0, matching
// kUpgradeSlotInstalledItemsOff above.
const size_t    kUpgradePanelCategoryOff  = acc::off::Pick(0x2f4c, 0x3d2c);  // panel.field25
// KOTOR 2 +0x5c, observed rather than inferred from the neighbouring +4s: its
// OnEnterSlot loads the slot index from param_1+0x5c and uses it as both the
// slot-type table index and the installed-items index, exactly as KOTOR 1 uses
// custom_value at +0x58.
const size_t    kUpgradeSlotCustomValueOff = acc::off::Pick(0x58, 0x5c);   // slot_btn.custom_value

// CSWGuiUpgrade.field35_0x2f74 — array of installed-mod CSWSItem* indexed by
// the slot button's custom_value. Non-null = slot occupied (the engine
// constructs a CSWSItem and LoadFromTemplate's the mod into this slot when the
// base item already carries that upgrade — bitmask at field27+0x294 — see
// OnPanelAdded @0x006c4d70); null = empty. Both OnEnterSlot @0x006c3c30 (saber
// branch) and OnSlotSelected @0x006c6500 (install/remove branch) index this
// array by custom_value, so it is the authoritative per-slot occupancy field.
// KOTOR 2 +0x3d54, from its own OnPanelAdded, which walks the same array in the
// same place: `installed[slot] != 0` guards the free-slot search, and the slot
// that wins gets a freshly constructed CSWSItem stored into it. Its category
// byte (below) shifts by exactly the same 0xDE0, which is the cross-check.
const size_t    kUpgradeSlotInstalledItemsOff = acc::off::Pick(0x2f74, 0x3d54);  // panel.field35

// CSWGuiUpgrade.field27_0x2f54 — the CSWSItem* currently being upgraded (the
// weapon/armor/saber). OnEnterSlot / OnControlEntered pass it to
// GetKeyedPropertyString to render a slot's keyed bonus line.
const size_t    kUpgradeBaseItemOff   = acc::off::Todo(0x2f54);  // panel.field27 (CSWSItem*)
// CSWGuiUpgrade.field71_0x2fa4 — per-slot property-key byte array, indexed by
// the slot button's custom_value. OnUpgradeSelected writes the installed mod's
// key here; GetKeyedPropertyString(base, field71[cv]) yields that slot's bonus.
const size_t    kUpgradeSlotKeyArrayOff = acc::off::Todo(0x2fa4);  // panel.field71 (byte[])
// CSWGuiUpgrade.field74_0x2fb0 — the slot button whose mod-picker is currently
// open (set by OnSlotSelected, read as `field74+0x58` for custom_value by
// OnUpgradeSelected). Lets us recover the active slot while the picker is up.
const size_t    kUpgradeActiveSlotOff = acc::off::Todo(0x2fb0);  // panel.field74 (slot btn*)

// Combat system — engine surfaces (per docs/combat-system.md, all
// "suspected" / "known (DB)" until live-validated).
//
// The four combat pillars share a small set of engine layout knowledge:
//
//   CSWSCreature
//     +0x9c8  CSWSCombatRound* combat_round
//     +0xa74  CSWSCreatureStats* creature_stats
//
//   CSWSObject (base of CSWSCreature)
//     +0xe0   short hit_points  (current; SetCurrentHitPoints writes here)
//     +0x124  CExoArrayList<CSWSEffect*> effects
//
//   CSWSCombatRound  (size driven by largest field offset @0x9d0+1)
//     +0x4    CSWSCombatAttackData attacks_list[7]   (each ~0xb0 bytes)
//     +0x944  int   timer
//     +0x94c  int   round_length
//     +0x96c  byte  current_attack
//     +0x9b0  CExoLinkedList<CSWSCombatRoundAction>* actions
//     +0x9b4  CSWSCreature* player_creature  (back-pointer)
//     +0x9b8  int   engaged
//     +0x9d0  byte  current_action
//
//   CSWSCombatAttackData (per attack, ~0xb0 bytes)
//     +0xc    ulong react_object   (target id)
//     +0x18   short missed_by
//     +0x38   short base_damage
//     +0x3a   byte  weapon_attack_type
//     +0x3b   byte  attack_mode
//     +0x50   int   critical_threat
//     +0x54   int   attack_deflected
//     +0x5c   int   attack_result   (0=pending / 1=hit / 2=miss / 3=crit
//                                    / 4=deflected — INFERRED, see plan)
//     +0x64   int   attack_type
//
//   CSWSCombatRoundAction (linked-list node, ~0x84 bytes)
//     +0x10   byte  action_type     (enum — see QueueVerb mapping below)
//     +0x14   ulong target          (handle)
//     +0x18   int   retargettable
//     +0x38   Vector move_to_position
//     +0x7c   int   attack_result
//     +0x80   int   damage
//
// Action-type byte enum (ATTACK / SPELL / EQUIP / etc.) is suspected — the
// values aren't pinned without a probe session. The mapping table in
// combat_queue.cpp uses a best-effort guess matching the order the engine's
// AddX adders are declared in (CSWSCombatRound::Add* @0x4d3660+).

// K2 0x10dc witnessed by caller census: 35 call sites load
// [creature+0x10dc] into ECX immediately before calling a K2 combat-round
// method (AddAction 0x00590270 et al). NOT the derived guess (+0x724 would
// have given 0x10ec — piecewise strikes again).
const size_t kCreatureCombatRoundOffset           = acc::off::Pick(0x9c8, 0x10dc);
// Same on KOTOR 2 — its CSWSObject::GetCurrentHitPoints twin 0x005413C0
// reads the word at [obj+0xe0] (plus a variant adding [obj+0xe8]).
const size_t kObjectHitPointsOffset               = acc::off::Same(0xe0);
// K2: the object's EffectList saver 0x00540860 iterates data [obj+0x148]
// with count [obj+0x14c] — the CExoArrayList moved 0x124→0x148.
const size_t kObjectEffectsOffset                 = acc::off::Pick(0x124, 0x148);

// AI action queue — CSWSObject.action_nodes @+0xfc, a
// CExoLinkedList<CSWSObjectActionNode>. The list holds the player's
// pending engine-driven actions: move-to-point, use-object, and the
// composite walk-to-then-act the world-picker enqueues. When the count
// reaches 0 the queued action has drained (arrived / used / aborted) —
// the authoritative "engine action finished" signal that replaces the
// blind input-restore timer. RE'd from CSWSObject::ClearAllActions
// @0x004CCD80 (iterates `action_nodes.list`). NOTE: combat DOES populate
// this queue (move-into-range + attack actions — observed swinging 0..5
// during a fight, 2026-06-06), separately from CSWSCombatRound @+0x9c8.
// That's harmless for the input-restore use: ordinary combat never arms
// our freeze, so the restore tick never reads this queue during combat —
// the two never overlap.
// CExoLinkedList = { CExoLinkedListInternal* internal } (+0x0); the
// internal is { head(+0x0), tail(+0x4), int count(+0x8) }.
// K2 witnessed in CSWSObject::AddAction (0x0053F7F0): its enqueue call loads
// `ecx = this + 0x100` (the uniform +4 CSWSObject shift), and the enqueue body
// (0x00739E60) shows the list as {head+0, tail+4, count+8} with node layout
// {prev+0, next+4, data+8} — count offset unchanged.
const size_t kObjectActionNodesOffset             = acc::off::Pick(0xfc, 0x100);
const size_t kExoLinkedListInternalCountOffset    = acc::off::Same(0x8);

// K2 column witnessed in the K2 CombatRound GFF saver 0x00592310 (Timer
// 0xa94, RoundLength 0xa9c, CurrentAttack byte 0xabc, Engaged 0xb08 — a
// uniform +0x150 on this block), the loader 0x00592840 (SchedActionList
// appends through the list pointer at [round+0xb00]) and SetCurrentAction's
// twin 0x005908A0 (byte store at +0xb24; K2 inserted a handle at +0xb20).
// The attacks array stays at +0x4 (same head layout as K1; AttackID save
// reads a parallel short array).
const size_t kCombatRoundAttacksListOffset        = acc::off::Same(0x4);
const size_t kCombatRoundTimerOffset              = acc::off::Pick(0x944, 0xa94);
const size_t kCombatRoundLengthOffset             = acc::off::Pick(0x94c, 0xa9c);
const size_t kCombatRoundCurrentAttackOffset      = acc::off::Pick(0x96c, 0xabc);
const size_t kCombatRoundActionsOffset            = acc::off::Pick(0x9b0, 0xb00);
const size_t kCombatRoundEngagedOffset            = acc::off::Pick(0x9b8, 0xb08);
const size_t kCombatRoundCurrentActionOffset      = acc::off::Pick(0x9d0, 0xb24);

// CExoLinkedList layout — verified against SARIF DATATYPE export
// 2026-05-28. THREE distinct structs need correct offsets:
//
//   CExoLinkedList<T>          { internal: CExoLinkedListInternal*  @+0 }
//   CExoLinkedListInternal     { head: Node*  @+0,
//                                tail: Node*  @+4,
//                                count: int   @+8 }
//   CExoLinkedListNode         { prev: Node*  @+0,
//                                next: Node*  @+4,
//                                data: void*  @+8 }
//
// The original walker in combat_queue (and combat_diag) treated the
// internal pointer as a node and walked via +0 — which on a real
// node is `prev`. On the head node `prev` is NULL, so the walk
// terminated after one iteration regardless of how many entries the
// list actually held. That's why queue-depth reads always returned 1
// even when the engine had 4 entries queued (AddAction hard-caps at
// 4 via `if (3 < count) { free; return; }`).
//
// Correct walk: combat_round.actions → +0 = internal* → +0 = head
// node* → walk via Node.next at +4 until null.
// All Same on KOTOR 2 — witnessed twice: Batch 3c's AddAction enqueue body
// (0x00739E60), and this batch's K2 list helpers (GetCount 0x005210F0 reads
// [list]→[internal+8], GetHead 0x007A1720 reads [list]→[internal+0]).
const size_t kListInternalOffset       = acc::off::Same(0x0);  // CExoLinkedList<T>     +0 -> internal*
const size_t kListInternalHeadOffset   = acc::off::Same(0x0);  // CExoLinkedListInternal+0 -> head node*
const size_t kListInternalCountOffset  = acc::off::Same(0x8);  // CExoLinkedListInternal+8 -> count (engine authoritative)
const size_t kLinkedListNodeNextOff    = acc::off::Same(0x4);  // CExoLinkedListNode    +4 -> next
const size_t kLinkedListNodeDataOff    = acc::off::Same(0x8);  // CExoLinkedListNode    +8 -> data


// The WHOLE CSWSCombatRoundAction struct is unchanged on KOTOR 2: its K2
// ClearData twin 0x0058C810 initialises field-for-field what K1's 0x004D1D50
// does — same 0x7f000000 handle sentinels at +0x14/+0x20/+0x44/+0x64, same
// retargettable=1 at +0x18, same result=4 at +0x7c, same +0x38..0x40 skip —
// and the K2 loader allocates the same 0x88 bytes per action.
const size_t kCombatRoundActionTypeOffset       = acc::off::Same(0x10);
const size_t kCombatRoundActionTargetOffset     = acc::off::Same(0x14);
const size_t kCombatRoundActionRetargetOffset   = acc::off::Same(0x18);
const size_t kCombatRoundActionMoveToPosOffset  = acc::off::Same(0x38);
const size_t kCombatRoundActionResultOffset     = acc::off::Same(0x7c);
const size_t kCombatRoundActionDamageOffset     = acc::off::Same(0x80);

// CSWSCreatureStats inline attribute-total bytes (post-mod totals). Read
// these directly to avoid relying on the GetXStat dispatch table (some of
// the addresses above are tentative — adjacent-symbol guesses pending
// SARIF confirmation). Field offsets per swkotor.exe.h (CSWCCreatureStats
// has the same layout as CSWSCreatureStats at the byte level for these
// fields per `accessibility-investigation.md`).
const size_t kStatsAttrTotalsOffset               = acc::off::Todo(0x34);  // 6 bytes: STR/DEX/CON/INT/WIS/CHA

// CSWSCreatureStats.faction_id @+0x78 (ushort) — the creature's standard
// faction. Per swkotor.exe.h `standardFactions` enum: HOSTILE_1=1,
// FRIENDLY_1=2, HOSTILE_2=3, FRIENDLY_2=4, NEUTRAL=5, INSANE=6,
// PTAT_TUSKAN=7, GLB_XOR=8, SURRENDER_1=9, SURRENDER_2=10, PREDATOR=11,
// PREY=12, TRAP=13, ENDAR_SPIRE=14, RANCOR=15, GIZKA_1=16, GIZKA_2=17,
// INVALID_FACTION=0xFFFF. The player + party share PLAYER (commonly
// faction id 0, not in the enum). Direct field read — no engine call,
// safe for auto-firing paths.
const size_t kStatsFactionIdOffset                = acc::off::Todo(0x78);

// CSWRules.spells — the spells array. CSWSpellArray* at offset 0x8c
// (140 bytes) per SARIF layout dump. The array exposes GetSpell(id) ->
// CSWSpell*. Used by combat::queue to decode action_type=9 (Cast Force
// Power) queue entries to their specific spell name.
// K2 0x104 — witnessed in the K2 OnEnterPower twin 0x008A49D0, which loads
// the rules global [0x00A1B4D0] and calls GetSpell on the CSWSpellArray*
// at [rules+0x104].
const size_t    kRulesSpellsOffset                = acc::off::Pick(0x8c, 0x104);

// CSWSpell.spell_description — int (TLK strref) at +0x0c per SARIF
// DATATYPE dump. CSWSpell has no GetSpellDescriptionText accessor, so
// callers read the strref and route through LookupTlk themselves.
const size_t    kSpellDescriptionStrRefOffset     = acc::off::Todo(0x0c);

// CSWSCombatRoundAction additional offsets (decoded from GetActionIcon
// @0x686fb0 — case 0xb/0xc switch). The action_type byte at +0x10
// selects which of these is meaningful:
//   action_type=9  → spell_id at +0x24    (CSWSpellArray::GetSpell)
//   action_type=10 → item_handle at +0x64 (CServerExoApp::GetItemByGameObjectID)
//   action_type=11 → feat_id at +0x5c     (CSWRules::GetFeat)
// Same on KOTOR 2 — covered by the ClearData struct witness above.
const size_t    kCombatRoundActionSpellIdOffset   = acc::off::Same(0x24);
const size_t    kCombatRoundActionItemHandleOff   = acc::off::Same(0x64);
const size_t    kCombatRoundActionFeatIdOffset    = acc::off::Same(0x5c);

// CGameEffect layout — what's stored in CSWSObject.effects.
// `effects` is CExoArrayList<CGameEffect*> at +0x124 (already known).
// Each element points to a CGameEffect:
//   +0x0 ulonglong id
//   +0x8 ushort    type            (EFFECT_TYPES enum: HASTE=1, SLOW=3,
//                                   POISON=35, BLINDNESS=73, FORCESHIELD=107,
//                                   ... full table in swkotor.exe.h:3181)
//   +0xa ushort    subtype
//   +0xc float     duration
//   ...
// CSWSObject.effects → walk to get CGameEffect*, then read +0x8 for type.
// K2: the effect loader twin 0x005E5510 stores the GFF "Type" ushort at
// [effect+8] (the 64-bit "Id" occupies +0/+4, as on KOTOR 1).
const size_t    kGameEffectTypeOffset             = acc::off::Same(0x8);

// CSWSCreature.effect_icons — CExoArrayList<CEffectIconObject*> with data
// ptr at +0x8f4 and size at +0x8f8. This is the sighted buff/debuff icon
// row on the portrait: CSWSEffectListHandler::OnApplyEffectIcon inserts one
// entry per applied EFFECTICON effect (priority-sorted, deduped by icon id)
// and OnRemoveEffectIcon walks the same raw offsets — both decompile-
// verified 2026-07-17. Each CEffectIconObject (0x20 bytes): +0x0 ushort
// effecticon.2da row id, +0x2 CResRef icon resref, +0x18 ushort priority.
// DELIBERATELY still Todo on KOTOR 2 (Batch 4): the K2 icon-apply chain
// routes the array through a passed-in list rather than fixed creature
// offsets (K2 CEffectIconObject ctor 0x006ECC90, apply walker 0x006E7610
// — no creature-relative displacement in either), so the K1-style offsets
// have no confirmed K2 twin yet. Consumers are SEH-guarded and degrade:
// the examine view simply lists no buff icons on KOTOR 2 until this is
// resolved from a live probe or a deeper caller decompile.
const size_t    kCreatureEffectIconsDataOffset    = acc::off::Todo(0x8f4);
const size_t    kCreatureEffectIconsSizeOffset    = acc::off::Todo(0x8f8);
const size_t    kEffectIconObjectIdOffset         = acc::off::Todo(0x0);

// CSWSCreature.inventory @+0xa2c → CSWInventory*. Server-side equipment
// container. Combined with CSWInventory::GetItemInSlot below this gives
// us "what is the creature wielding right now".
// KOTOR 2 value from the seeded kotor2_steam_aspyr.db. CSWSCreature's fields
// shift by a large constant there (+0x724 for both this and creature_stats) —
// Obsidian added a substantial block above them, so unlike CSWSObject's +4 this
// is not a small insertion. Do not extrapolate it to other classes.
const size_t    kCreatureInventoryOffset          = acc::off::Pick(0xa2c, 0x1150);

// CSWInventory equipped-slot field layout (validated via Lane's symbol
// table 12715: STRUCTURE CSWInventory SIZE=0x4c). Each slot is a ulong
// game-object handle (NOT a pointer) — resolved via the universal
// CClientExoApp::GetObjectName accessor. Initial attempt routed through
// CSWInventory::GetItemInSlot which returns a small CSWItem* wrapper
// (size 0x10) — the wrong shape for the localized_name @+0x280 chain;
// reading the handle directly bypasses that confusion.
// Same on KOTOR 2: its GetItemInSlot slot-mapper twin 0x006D0670 returns
// inventory+0x14 for slot bit 0x10 (right weapon) and +0x18 for 0x20
// (left weapon) — the identical bit→field table.
const size_t    kInventoryRightWeaponHandleOffset = acc::off::Same(0x14);  // main hand
const size_t    kInventoryLeftWeaponHandleOffset  = acc::off::Same(0x18);  // off hand
// Same on KOTOR 2 — the K2 slot mapper 0x006D0670 maps slot bits to the
// identical field table (bit0→+4, bit1→+8, bit3→+0x10, ...).
const size_t    kInventoryHeadHandleOffset        = acc::off::Same(0x4);
const size_t    kInventoryTorsoHandleOffset       = acc::off::Same(0x8);
const size_t    kInventoryHandsHandleOffset       = acc::off::Same(0x10);
const size_t    kInventoryLeftArmHandleOffset     = acc::off::Todo(0x20);
const size_t    kInventoryRightArmHandleOffset    = acc::off::Todo(0x24);
const size_t    kInventoryImplantHandleOffset     = acc::off::Todo(0x28);
const size_t    kInventoryBeltHandleOffset        = acc::off::Todo(0x2c);

// CSWGuiInGameEquip — cached per-slot item handles and stat-value labels.
// The panel mirrors the displayed character's CSWInventory into local
// fields, and OnSwitchLeft/Right repopulates them on party-cycle, so these
// always match what's on screen regardless of which companion is shown.
// IDs are the same handle space as CClientExoApp::GetObjectName (the
// universal accessor routes both client and server handles), so
// GetObjectDisplayNameByHandle resolves them without translation.
// Offsets verified against Lane's CSWGuiInGameEquip struct (SIZE=0x42bc).
//
// KOTOR 2 base is 0x509c, read DIRECTLY out of its own UpdateInventory rather
// than derived. These nine are not really nine fields: both games treat them as
// one array indexed by the equipment-slot enum, KOTOR 1 as
// `(&this->left_weapon_id)[slot]` and KOTOR 2 as `*(in_ECX + 0x509c + slot*4)`,
// and both guard the same `!= 0x7f000000` sentinel around it. So the slot ORDER
// is the engine's, identical by construction, and only the base had to be found.
//
// Worth recording that the base is NOT where walking back from the surrounding
// anchors predicted. The constructor's 0x7f000000 write fixes `last_selected_id`
// at KOTOR 2 0x50d0 against KOTOR 1 0x42a8, and reading the ids off that would
// have put them 4 bytes high: KOTOR 2 carries one FEWER field ahead of
// selected_slot and three MORE between belt_id and field46 than KOTOR 1 does.
// The two errors do not cancel. This is the case the port keeps re-learning —
// find the instruction that touches the field, do not walk to it.
const size_t    kEquipPanelPlayerCreatureOffset    = acc::off::Todo(0x0064);
const size_t    kEquipPanelHeadIdOffset            = acc::off::Pick(0x4284, 0x50a4);
const size_t    kEquipPanelImplantIdOffset         = acc::off::Pick(0x4298, 0x50b8);
const size_t    kEquipPanelArmorIdOffset           = acc::off::Pick(0x4290, 0x50b0);  // body
const size_t    kEquipPanelLeftArmbandIdOffset     = acc::off::Pick(0x4288, 0x50a8);
const size_t    kEquipPanelRightArmbandIdOffset    = acc::off::Pick(0x428c, 0x50ac);
const size_t    kEquipPanelLeftWeaponIdOffset      = acc::off::Pick(0x427c, 0x509c);
const size_t    kEquipPanelRightWeaponIdOffset     = acc::off::Pick(0x4280, 0x50a0);
const size_t    kEquipPanelGlovesIdOffset          = acc::off::Pick(0x4294, 0x50b4);  // hands
const size_t    kEquipPanelBeltIdOffset            = acc::off::Pick(0x429c, 0x50bc);
// KOTOR 2's second weapon set ("Konfig 2" on the panel; BTN_INV_WEAP_L2 /
// BTN_INV_WEAP_R2, .gui ids 20/21). KOTOR 1 has no such slots.
//
// Read out of the engine's own slot-bit -> array-index mapper 0x008a91c0,
// which HandleInputEvent 0x008aed10 calls as
// `panel[0x1427 + Map(selected_slot)]` (dword index; 0x1427*4 = 0x509c, the
// array base above). The mapper is a complete switch and it re-derives all
// nine KOTOR 1 slots at exactly the offsets already recorded here — head
// 0x01->2, body 0x02->5, hands 0x08->6, right weapon 0x10->1, left weapon
// 0x20->0, left arm 0x80->3, right arm 0x100->4, implant 0x200->7, belt
// 0x400->8 — so the two new arms of the same switch are as trustworthy as
// the rest of the table:
//   slot bit 0x80000 (left weapon 2)  -> index 9  -> 0x509c + 9*4  = 0x50c0
//   slot bit 0x40000 (right weapon 2) -> index 10 -> 0x509c + 10*4 = 0x50c4
// HandleInputEvent's own set-2 handling corroborates the bit values: it
// pairs 0x10 with 0x20 and 0x40000 with 0x80000 when clearing the off-hand.
const size_t    kEquipPanelLeftWeapon2IdOffset     = acc::off::Kotor2Only(0x50c0);
const size_t    kEquipPanelRightWeapon2IdOffset    = acc::off::Kotor2Only(0x50c4);

// Stat-value labels inline in the panel struct. Each is a CSWGuiLabel
// (SIZE=0x140). UpdateInventory @0x006b9970 writes the rendered value
// into gui_string at populate-time; the .gui-time placeholder text is
// overwritten.
//
// Lane's struct-DB names for the attack block are MISLEADING — verified
// 2026-05-23 via Ghidra decomp of UpdateInventory:
//   * `*_attack_label`  members hold the DAMAGE range value ("1-9").
//   * `*_tohit_label`   members hold the TO HIT bonus value ("+5").
//   * `tohit_label`     (0x2a98) and `damage_label` (0x2bd8) are
//                       CAPTION-only labels with static TLK strrefs
//                       (31385/31386). The engine never overwrites
//                       them — they remain "Trefferchance" /
//                       "Schaden".
// Single-weapon mode: only the RIGHT-hand pair carries values; LEFT pair
// is blanked to "". Dual-wield: both pairs carry per-hand values.
// K2 column witnessed in the K2 stat-writer 0x008AD930 (the UpdateInventory
// twin): "%d-%d" damage goes into LBL_ATKL/ATKR (0x22ac/0x253c), the +N
// to-hit into LBL_TOHITL/TOHITR (0x23f4/0x2684), and the armor-class value
// into LBL_DEF (SetText on its text sub-object at 0x4c34+0xf0). KOTOR 2's
// equip panel has NO vitality label at all (none in ctor 0x008A92D0, no HP
// write in the stat-writer) — the HP row is Kotor1Only and the spec walk
// skips it there. K2 also carries a second weapon-set label quartet
// (LBL_ATKL2 0x320c / TOHITL2 0x3354 / ATKR2 0x349c / TOHITR2 0x35e4) the
// engine writes for the alternate weapon config — we anchor on set 1.
const size_t    kEquipPanelDefenseLabelOffset            = acc::off::Pick(0x2098, 0x4c34);
const size_t    kEquipPanelHpLabelOffset                 = acc::off::Kotor1Only(0x21d8);
const size_t    kEquipPanelLeftWeaponDamageLabelOffset   = acc::off::Pick(0x1b98, 0x22ac);  // Lane: left_weapon_attack_label
const size_t    kEquipPanelLeftWeaponTohitLabelOffset    = acc::off::Pick(0x1cd8, 0x23f4);
const size_t    kEquipPanelRightWeaponDamageLabelOffset  = acc::off::Pick(0x1e18, 0x253c);  // Lane: right_weapon_attack_label
const size_t    kEquipPanelRightWeaponTohitLabelOffset   = acc::off::Pick(0x1f58, 0x2684);

// Bottom-row party-cycle buttons inline in CSWGuiInGameEquip — mirrors the
// 4-button strip on InGameCharacter. All four (change_party_1/2 portraits
// + character_left/right arrows) are dropped from chain navigation by
// menus_chain.cpp's IsDecorativeForChain filter: Tab cycles the active
// leader engine-side and party_leader_announce speaks the new name, so
// these in-panel buttons are redundant. Runtime IDs are unstable (engine
// renumbers when runtime-added char_left/right collide with gui-declared
// BTN_CHANGE2's id=40), so the filter identifies by struct offset.
//
// Derived 2026-05-25 from patch-20260525-204630.log addresses (panel
// 0FD03C68): back@0x385C, change_party_1@0x3A20, change_party_2@0x3BE4,
// character_left@0x3DA8, character_right@0x3F6C. Stride = 0x1c4 =
// sizeof(CSWGuiButton). Struct order matches swkotor.exe.h:9087-9091.
//
// KOTOR 2 restructured this run, and its constructor shows exactly how. BTN_BACK
// is at 0x3edc and LBL_CANTEQUIP at 0x40ac — ADJACENT, one KOTOR 2 button apart.
// So the four members KOTOR 1 has between back_button and cant_equip_label are
// simply not there. The two party-portrait buttons are gone outright: KOTOR 2's
// constructor declares no BTN_CHANGE1 / BTN_CHANGE2 tag anywhere. The two
// arrows survive as BTN_PREVNPC / BTN_NEXTNPC, relocated to the end of the
// struct after a block of labels KOTOR 1 does not have.
//
// Note the arrow mapping is by ROLE, not by tag: KOTOR 1's character_left/right
// are runtime-added and carry no .gui tag to match against (that is the id
// collision the paragraph above describes). KOTOR 2's pair is gui-declared, sits
// 0x1d0 apart as one button should, and is the same prev/next party-member
// affordance. Blast radius if that is wrong is small — these offsets only tell
// IsDecorativeForChain which buttons to drop from navigation.
const size_t    kEquipPanelBackButtonOffset           = acc::off::Pick(0x385C, 0x3edc);
const size_t    kEquipPanelChangeParty1ButtonOffset   = acc::off::Kotor1Only(0x3A20);
const size_t    kEquipPanelChangeParty2ButtonOffset   = acc::off::Kotor1Only(0x3BE4);
const size_t    kEquipPanelCharacterLeftButtonOffset  = acc::off::Pick(0x3DA8, 0x50f0);
const size_t    kEquipPanelCharacterRightButtonOffset = acc::off::Pick(0x3F6C, 0x52c0);

// CSWGuiLevelUpPanel "Zurück" (button_back) and "Abbrechen"
// (button_cancel) — the two trailing CSWGuiButton members before
// field9_0x1ccc in the struct. Both are dead ends for keyboard nav and
// are filtered from the chain (see menus_chain.cpp isDecorative): Zurück
// only steps the engine's visual category highlight (we navigate
// categories with our own arrows), and Abbrechen → OnCancelPressed is
// gated on a can-cancel flag that the engine only ever assigns 0
// (CSWGuiLevelUpCharGen::OnPanelAdded calls SetCanCancel(panel, 0), the
// sole caller in the binary) — so an in-game level-up cannot be
// cancelled; Annehmen is the only exit. Identify by offset, not control
// id: ids are reassigned per session (Zurück seen as id 19 then id 1).
// Derived 2026-05-31 from patch-20260531-182325.log: panel 15E793C8,
// Zurück@0x1944 (15E7AD0C). Stride 0x1c4 = sizeof(CSWGuiButton); struct
// order per swkotor.exe.h CSWGuiLevelUpPanel (…button_back, button_cancel,
// field9_0x1ccc@0x1ccc).
// KOTOR 2 values by .gui tag out of its own constructor (BTN_BACK / BTN_CANCEL),
// 0x1d0 apart as one KOTOR 2 button should be.
const size_t    kLevelUpButtonBackOffset              = acc::off::Pick(0x1944, 0x18a8);
const size_t    kLevelUpButtonCancelOffset            = acc::off::Pick(0x1B08, 0x1a78);

// CSWGuiInGameMap up_button / down_button — the two image-only buttons
// flanking the map render ("Vorheriger Hinweis" / "Nächster Hinweis" once
// menus_extract's per-kind fallback names them). Both dispatch
// CSWGuiPanel::HandleInputEvent(0x31/0x32, 1), which the InGameMap override
// routes to CSWGuiMapHider::GetPrevMapNote / GetNextMapNote — the engine's
// cycle through explored map-note waypoints. Verified 2026-05-12 from the
// GoG xml MEMBER offsets + the decomp of OnUpArrowPressed / OnDownArrowPressed.
// Identify by struct offset: both carry empty text and no strref, so a
// button-spec read misses and the .gui ids are the only other handle.
// Consumed by menus_extract.cpp (TryInGameMapArrow) and menus_chain.cpp
// (IsDecorativeControl, which drops them from chain navigation — the map
// cursor covers note reading).
//
// KOTOR 2 values by .gui tag out of its own constructor: BTN_UP at 0x610,
// BTN_DOWN at 0x7e0 (0x1d0 apart — one KOTOR 2 button). They sit LOWER than
// KOTOR 1's, which is not a mistake: KOTOR 2's map panel drops the compass
// label, BTN_RETURN and BTN_PRTYSLCT, so the two arrows move up by more than
// the struct growth pushes them down. A reminder that these offsets cannot be
// sanity-checked by "K2 should be bigger".
const size_t    kInGameMapUpButtonOffset              = acc::off::Pick(0xAB0, 0x610);
const size_t    kInGameMapDownButtonOffset            = acc::off::Pick(0xC74, 0x7e0);

// ----------------------------------------------------------------------------
// CSWGuiInGameAbilities — the in-game "Fähigkeiten" screen (CGuiInGame slot
// 0x18, abilities.gui). A view-only character screen shaped like the settings
// menu: three tab buttons (Powers / Skills / Talents) switch which list the
// single LB_ABILITY listbox shows, and selecting a row repaints a detail area
// (name + rank/bonus/total labels + the LB_DESC description box). NOT the
// chargen/level-up button-grid shape — it rides the shared ListBoxPanelSpec +
// description-peek machinery (see menus_abilities.cpp).
//
// Member offsets verified from Lane's RE database (docs/llm-docs/re/
// k1_win_gog_swkotor.exe.sarif, struct CSWGuiInGameAbilities). Decimal
// offsets from the SARIF in parentheses.
// KOTOR 2 column mined from the K2 panel ctor 0x008A25C0 by .gui tag (label
// stride 0x148, button stride 0x1d0 both check out on consecutive members).
// K2 inserts LBL_BAR1..6 / LBL_FILTER / LBL_ABILITIES / LB_DESC_FEATS before
// the listboxes, which is why LB_ABILITY jumps by ~0x1000. K2 also has a
// SECOND description listbox, LB_DESC_FEATS @0x3c84, which the engine's own
// description switcher (0x008A53D0) selects when the Feats tab is active.
const size_t    kAbilitiesSkillRankLabelOffset   = acc::off::Pick(0x2190, 0x214c);  // LBL_SKILLRANK
const size_t    kAbilitiesRankValueLabelOffset   = acc::off::Pick(0x22D0, 0x2294);  // LBL_RANKVAL
const size_t    kAbilitiesBonusLabelOffset       = acc::off::Pick(0x2410, 0x23dc);  // LBL_BONUS
const size_t    kAbilitiesBonusValueLabelOffset  = acc::off::Pick(0x2550, 0x2524);  // LBL_BONUSVAL
const size_t    kAbilitiesTotalLabelOffset       = acc::off::Pick(0x2690, 0x266c);  // LBL_TOTAL
const size_t    kAbilitiesTotalValueLabelOffset  = acc::off::Pick(0x27D0, 0x27b4);  // LBL_TOTALVAL
const size_t    kAbilitiesNameLabelOffset        = acc::off::Pick(0x2910, 0x28fc);  // LBL_NAME
const size_t    kAbilitiesFeatsButtonOffset      = acc::off::Pick(0x2B90, 0x2b8c);  // BTN_FEATS  (Talente)
const size_t    kAbilitiesPowersButtonOffset     = acc::off::Pick(0x2D54, 0x2d5c);  // BTN_POWERS (Kräfte)
const size_t    kAbilitiesSkillsButtonOffset     = acc::off::Pick(0x2F18, 0x2f2c);  // BTN_SKILLS (Fähigkeiten)
const size_t    kAbilitiesListBoxOffset          = acc::off::Pick(0x30DC, 0x40bc);  // LB_ABILITY (main list)
const size_t    kAbilitiesDescListBoxOffset      = acc::off::Pick(0x33BC, 0x43ac);  // LB_DESC (description)

// The two CSWGuiSkillFlowChart members on the panel (field30/field31). Their
// internals are the SAME CSWGuiSkillFlowChart layout the chargen/level-up
// grids use — read the cursor and row array through
// kSkillFlowChartSelectedRow/SelectedCol/RowsData/RowsSize above rather than
// re-declaring panel-local aliases for them. We read row vs row-count to clamp
// the engine's chart nav, which otherwise WRAPS top<->bottom (unlike the skills
// listbox, which clamps).
// K2 values witnessed twice each: the panel ctor constructs the two chart
// members at +0x4874/+0x4884 (same 0x10 spacing as KOTOR 1), and the panel's
// CreatePowerChart/CreateFeatChart wrappers (0x008A4830 / 0x008A47C0) run
// `add ecx, 0x4874` / `add ecx, 0x4884` before calling the chart builders —
// the powers wrapper passes the same trailing 0 K1's CreatePowerChart takes.
const size_t    kAbilitiesPowersChartOffset       = acc::off::Pick(0x3f78, 0x4874);  // field30 (Powers)
const size_t    kAbilitiesFeatsChartOffset        = acc::off::Pick(0x3f88, 0x4884);  // field31 (Feats)

// CGuiInGame.field139_0xbc0 — the active abilities tab: 0 = Skills,
// 1 = Powers, 2 = Feats. Read to route per-tab input + announce the tab.
// K2: all three K2 tab-button handlers (0x008A5790/57D0/5810) store their
// tab value (2/1/0) at [GetInGameGui()+0xfc0] — a triple witness.
const size_t    kGuiInGameAbilitiesTabOffset      = acc::off::Pick(0xbc0, 0xfc0);

// CSWSCreatureStats.feats @+0x0 — CExoArrayList<ushort>. Count lives
// at +0x4 (size field of the list). Static feat list (granted at level-
// up + class); doesn't drift mid-combat. Used by the Ö examine view to communicate
// "this creature has N feats" without enumerating them.
const size_t    kStatsFeatsListOffset             = acc::off::Todo(0x0);

// CSWGuiExamine.message_box.listbox_message lives at panel +0x67c. Kept
// for the kExamineSpec ListBoxPanelSpec entry — if the engine itself ever
// pops the generic message box (e.g. via store "can't afford" or other
// confirmation popups), the spec handles row navigation. We just don't
// open it ourselves for creature examine — that would land on an empty
// TLK-lookup result.
const size_t    kExaminePanelListBoxOffset        = acc::off::Todo(0x67c);
const size_t    kExaminePanelHandleOffset         = acc::off::Todo(0x984);

// CSWGuiInGameMessages — combat log + dialog history panel.
//   panel        @+0x0
//   messages_lb  @+0x64    (combat-feedback log)
//   dialog_lb    @+0x344   (dialog history)
//   show_button  @+0x76c   (toggles between feedback / dialog view)
//   exit_button  @+0x930
// KOTOR 2 rebuilt this panel (ctor 0x00757C40): separate LB_MESSAGES /
// LB_DIALOG / LB_COMBAT / LB_EFFECTS_GOOD / LB_EFFECTS_BAD listboxes with a
// four-way view-button row (BTN_DIALOG 0x1c7c / BTN_FEEDBACK 0x1e4c /
// BTN_COMBAT 0x201c / BTN_EFFECTS 0x21ec) replacing K1's single show
// toggle — hence Kotor1Only for the toggle; K2's view buttons ride the
// generic button path.
const size_t kInGameMessagesMessagesListBoxOffset = acc::off::Pick(0x64, 0x68);     // LB_MESSAGES
const size_t kInGameMessagesDialogListBoxOffset   = acc::off::Pick(0x344, 0x358);   // LB_DIALOG
const size_t kInGameMessagesShowButtonOffset      = acc::off::Kotor1Only(0x76c);
const size_t kInGameMessagesExitButtonOffset      = acc::off::Pick(0x930, 0x12fc);  // BTN_EXIT

// CSWGuiDialog (and Cinematic / ComputerCamera variants which share base
// layout):
//   panel             @+0x0
//   replies_listbox   @+0x19c4
//   message_label     @+0x1ca4
// K2 values from the CSWGuiDialogCinematic constructor (0x008BBA80,
// Batch 3b): "LB_REPLIES" wired as the EMBEDDED control at panel+0x2760
// (dword slot 0x9d8; the ctor then makes a virtual call through the embedded
// control's own vtable, confirming embed-not-pointer, same shape as K1) and
// "LBL_MESSAGE" embedded at panel+0x2A50.
const size_t kDialogRepliesListBoxOffset          = acc::off::Pick(0x19c4, 0x2760);
const size_t kDialogMessageLabelOffset            = acc::off::Pick(0x1ca4, 0x2a50);

// Conversation partner — on every server-side game object (CSWSObject)
// the engine maintains a `dialog_owner: CSWSObject*` at +0x54 pointing
// at the other party in the current conversation. For the player creature
// this points at the NPC they're talking to. Used by dialog_speech to
// classify the speaker (human / non-human) for the "Read human subtitles"
// toggle.
//
// Caveat: this is the conversation partner, not the per-line speaker. In
// multi-party cutscenes the speaker can be a third creature; the partner
// pointer is still useful as a "human-ish dialog?" heuristic but not
// authoritative. For 1-on-1 dialog and barks it's exactly the speaker.
// Verified IDENTICAL on KOTOR 2 (Batch 3b): the K2 SetDialogOwner twin
// (0x00546DB0, called 3× from the server dialog cluster at 0x006C5xxx) is
// the same one-line `MOV [this+0x54], arg` setter as K1's 0x004CB7A0. So
// the CSWSObject field insertion that moved area_id (0x8c→0x90) sits ABOVE
// +0x54.
const size_t kServerObjectDialogOwnerOffset       = acc::off::Same(0x54);

// CSWSCreature inline appearance cache at +0xa4c per Lane's struct, but
// VERIFIED LIVE 2026-05-30 to read 0 even for fully-initialised speakers
// (Carth, Larrim) — the engine doesn't populate this cache reliably. Use
// CSWSCreatureStats.appearance_type at stats+0x186 instead (real value).
// Kept here as a constant for future re-investigation, NOT used by
// dialog_speech.
const size_t kCreatureAppearanceTypeOffset        = acc::off::Todo(0xa4c);

// CSWSCreature.creature_stats — pointer to CSWSCreatureStats.
// KOTOR 2 value from the seeded kotor2_steam_aspyr.db (+0x724, same shift as
// kCreatureInventoryOffset above).
//
// NOTE: engine_area.h declares kCreatureStatsPtrOffset for this SAME field.
// Two names, two files, one engine fact — they must be changed together until
// one of them is retired.
const size_t kCreatureStatsPointerOffset          = acc::off::Pick(0xa74, 0x1198);

// CSWSCreatureStats.race (ushort; enum RACE values: DROID=5, HUMAN=6).
// Diagnostic-only — the enum collapses every humanoid species (Twi'lek,
// Cathar, Echani, Mandalorian) to HUMAN, so we discriminate by
// appearance_type, not by race. Race is logged so future overrides can be
// designed on observed (race, appearance_type) pairs from the diagnostic.
// K2 0xe0 — witnessed twice (2026-08-01, Batch 3b): CSWSCreatureStats' GFF
// loader (0x006AFED0) reads [stats+0xe0] as the "Race" default and the saver
// (0x006B3D10) writes it back under the same label.
const size_t kCreatureStatsRaceOffset             = acc::off::Pick(0xdc, 0xe0);

// CSWSCreatureStats.appearance_type (ushort, indexes appearance.2da).
// Verified from Lane's exported header @0x186 (line 15707 in swkotor.exe.h).
// THIS is the authoritative species discriminator — the CSWSCreature inline
// cache at +0xa4c is unreliable.
// K2 0x194 (word) — same double witness as Race: "Appearance_Type" loader
// store at 0x006B123B and saver read at 0x006B4AFD, both [stats+0x194].
const size_t kCreatureStatsAppearanceTypeOffset   = acc::off::Pick(0x186, 0x194);

// CSWGuiDialogComputer adds a terminal-output listbox above the embedded
// replies listbox.
//   message_listbox  @+0x2cfc   (terminal output text)
//   obscure_label    @+0x34dc
// K2 0x35FC from the CSWGuiDialogComputer constructor (0x008BC620):
// "LB_MESSAGE" embedded at dword slot 0xd7f. The same ctor wires
// "LB_REPLIES" at 0x2760 — identical to DialogCinematic's, confirming the
// shared CSWGuiDialog base layout holds on KOTOR 2 too.
const size_t kDialogComputerMessageListBoxOffset  = acc::off::Pick(0x2cfc, 0x35fc);

// CGuiInGame.current_dialog_speaker (field93_0x170) — the CLIENT-side object
// id of the creature speaking the current dialog entry. Written by
// CGuiInGame::HandleDialogEntry @0x00631d80 on EVERY shown entry, sourced
// from the server's per-node GetSpeaker() resolution
// (CSWSDialog::SendDialogEntry @0x005a4010 → SendDialogEntryNode @0x005a13d0,
// 3rd object arg → ServerToClientObjectId → this field). Set regardless of
// whether the player participates, so it identifies the speaker in overheard
// NPC-to-NPC scenes where the player's dialog_owner (+0x54) is null. Sentinel
// 0x7f000000 means "no participant". Sibling slots: +0x174 listener,
// +0x178 previous speaker, +0x184 third participant.
// K2 0x190 — the whole speaker block shifted +0x20, witnessed in K2's
// HandleDialogEntry (0x007CBF60, identity confirmed by the fade latch, the
// SetReplyData-loop, the TLK gender dance and the camera dispatch): the K1
// update logic appears verbatim over +0x190/+0x194/+0x198/+0x19C with the
// first-speaker latch at +0x1A4. Sibling slots shift with it (listener
// +0x194, previous speaker +0x198, third participant +0x1A4).
const size_t kCGuiInGameDialogSpeakerOffset       = acc::off::Pick(0x170, 0x190);

// CGuiInGame reply-text model — the engine's authoritative, render-independent
// store of the CURRENT entry's selectable reply strings. Populated by
// CGuiInGame::SetReplyData @0x00628750 (one call per active reply), consumed by
// CGuiInGame::UpdateDialog @0x006339c0 which reads the chosen reply's text from
// `field70_0x118 + selIdx*8`. Each entry is a CExoString (char* + uint32 len, 8
// bytes), indexed by the SAME reply index as the replies listbox selection_index
// (see SelectReply→SetDialogSelection→UpdateDialog).
//
// This is why we read replies from here, NOT from the listbox row CSWGuiLabels:
// the reply listbox is a scrolling list whose row gui_string materialises only
// for rendered (on-page) rows, so off-page replies (option 3+ at smaller GUI
// page sizes) read empty from the controls — the silent-dropped "missing
// entries" bug in the droid/computer (DialogCinematicCopy) interfaces. The
// CExoString array here always carries every active reply's resolved text.
//   field69_0x114  reply array capacity/count (SetReplyData bounds-guards on it)
//   field70_0x118  pointer to the CExoString[] array
//
// K2: the whole reply block shifted +0x20 — count 0x134, text array 0x138 —
// witnessed in K2's SetReplyData (0x007C0C70, found by its unique 19-param
// ret 0x4C signature; the body is K1's sixteen parallel arrays with the same
// types in the same order, +0x13C..+0x17C). This settles the Batch 3b trap:
// K1's +0x114/+0x118 land inside K2's MESSAGE RINGS (+0x110/+0x118), so the
// old Todo values would have silently read ring pointers as reply data.
const size_t kCGuiInGameReplyCountOffset          = acc::off::Pick(0x114, 0x134);
const size_t kCGuiInGameReplyTextArrayOffset      = acc::off::Pick(0x118, 0x138);

// CSWGuiBarkBubble.object_id @+0x1c0 — the bark speaker's CLIENT object id,
// written by CSWGuiBarkBubble::SetBark @0x006a9920 (this->object_id = param_1)
// and consumed by ::Draw @0x006a9ce0 via CClientExoApp::GetGameObject(client,
// object_id) → AsSWCObject for the 6m proximity/cull test. Sentinel
// 0x7f000000 means "no owning creature" — system/loudspeaker barks (camera
// zone messages, area feedback). Resolve a real id through
// ClientToServerObjectId → ResolveServerObjectHandle to classify the speaker,
// exactly as the dialog-speaker path does for CGuiInGame +0x170.
// K2 0x1CC from CSWGuiBarkBubble::Draw (0x008BE740, vtable-slot-paired with
// K1's 0x006A9CE0): the guard `[this+0x1CC] != 0x7f000000` feeds the id into
// the client GetGameObject facade (0x0073F4D0 — the Batch 3 find, mutually
// confirming) and the result into the `< 36.0` six-metre-squared cull test,
// K1's exact Draw shape.
const size_t kBarkBubbleObjectIdOffset            = acc::off::Pick(0x1c0, 0x1cc);

// KOTOR 2 values by .gui tag out of its own constructor (LB_SHOPITEMS,
// LB_INVITEMS, LB_DESCRIPTION, BTN_Cancel, BTN_Examine, BTN_Accept), same
// members in the same order. The strides corroborate the shared-class work
// above without being part of it: the three listboxes sit 0x2e0 apart in
// KOTOR 1 and 0x2f0 in KOTOR 2 — the same +0x10 CSWGuiListBox growth that
// kListBox*Offset was measured at — and the three buttons 0x1c4 vs 0x1d0.
const size_t    kStoreShopItemsListBoxOffset           = acc::off::Pick(0x1480, 0x1504);
const size_t    kStoreInvItemsListBoxOffset            = acc::off::Pick(0x1760, 0x17f4);
const size_t    kStoreDescriptionListBoxOffset         = acc::off::Pick(0x1a40, 0x1ae4);
const size_t    kStoreCancelButtonOffset               = acc::off::Pick(0x1d20, 0x1dd4);
const size_t    kStoreToggleButtonOffset               = acc::off::Pick(0x1ee4, 0x1fa4);  // examine_button in struct DB
const size_t    kStoreAcceptButtonOffset               = acc::off::Pick(0x20a8, 0x2174);
const size_t    kStoreItemIdOffset                     = acc::off::Todo(0x226c);
const size_t    kStoreCostValueLabelOffset             = acc::off::Todo(0xbc0);
const size_t    kStoreStockValueLabelOffset            = acc::off::Todo(0xe40);
const size_t    kStoreCreditsValueLabelOffset          = acc::off::Todo(0x1200);

// CSWGuiStore.field31_0x2270 — int — the player's gold cached on the
// store struct. Written by PopulateStore via CSWSCreature::GetGold,
// updated again after every SellItem / BuyItem. OnControlStoreAButton
// reads this to decide whether the player can afford the trade:
//   if (field31_0x2270 < GetItemBuyValue(item)) ShowExamineBox(strref 0xa3de)
// We use it for the same gate, but speak our own "not enough credits"
// line instead of letting the engine pop its examine box.
const size_t    kStorePlayerGoldOffset                 = acc::off::Todo(0x2270);

// Bit 1 (0x02) of the listbox's CSWGuiControl.bit_flags is set on the
// "visible" listbox by ShowBuyGUI / ShowSellGUI. Same offset (+0x44)
// every other CSWGuiControl uses.
// +4 in KOTOR 2, observed: its CSWGuiPanel hit-test tests bit 0 of this+0x48
// to decide whether to letterbox-adjust the cursor, where KOTOR 1 uses +0x44.
const size_t    kControlBitFlagsOffset                 = acc::off::Pick(0x44, 0x48);
const uint32_t  kStoreListBoxVisibleBit                = acc::off::Todo(0x2);
// The same bit_flags 0x02 is the general CSWGuiControl "shown" bit. The
// StatusSummary popup lays out one label per notification type and sets
// this bit only on the row(s) it actually displays — hidden template rows
// (still reading "<CUSTOM0>" placeholders) leave it clear, with stale float
// data in the adjacent flag fields (verified via PopupGeom dump 2026-06-03).
//
// KOTOR 2: same bit, and observed rather than assumed. Its
// CSWGuiInGameEquip::OnPanelAdded shows/hides three buttons by writing
// `flags & ~2 | 2` (or `& ~2`) at panel+0x4f0c, +0x5138 and +0x5308. Each of
// those is exactly one of BTN_SWAPWEAPONS / BTN_PREVNPC / BTN_NEXTNPC plus
// 0x48 — the control bit_flags offset. So this one decompile independently
// confirms three things: the visible bit is still 0x2, bit_flags is still at
// +0x48 in KOTOR 2, and the three button offsets derived from that panel's
// constructor by .gui tag are right.
const uint32_t  kControlVisibleBit                     = acc::off::Same(0x2);

// CSWGuiStoreItemEntry.obj_id @ +0x1c4 — the client-side game-object
// handle for the row's CSWSItem. Resolve via ClientToServerObjectId then
// GetItemByGameObjectID.
// K2 +0x1d0: the id sits right after the embedded CSWGuiButton, whose size
// grew 0x1c4 -> 0x1d0. Witnessed, not derived: the K2 item-entry ctor
// 0x008B0A60 initialises `[entry+0x1d0] = 0x7f000000` (the invalid-handle
// sentinel, K1's exact init), and all three K2 OnControlEntered twins
// (inventory 0x008A8100, store 0x008B6E90, upgrade rows 0x008CCB80) read
// the id at [row+0x1d0].
const size_t    kStoreItemEntryObjIdOffset             = acc::off::Pick(0x1c4, 0x1d0);

// CSWSItem.bit_flags @ +0x288 (ulong), CSWSItem.stack_size @ +0x28c (ushort).
// Verified two ways: (1) Ghidra struct DB names them at these offsets;
// (2) raw disassembly of CSWGuiStore::OnControlEntered @ 0x006c0aa0:
//   f6 87 88 02 00 00 04   TEST BYTE PTR [EDI+0x288], 0x04   ; bit_flags
//   8b 87 8c 02 00 00      MOV  EAX,       [EDI+0x28c]       ; stack_size
// The engine reads stack_size as a 4-byte access via `*(int *)&` cast, but
// the underlying field is 2 bytes — the upper 2 bytes are padding that
// reads back as 0 in practice. We read 2 bytes (uint16_t) to match the
// declared type.
//
// bit 2 (0x04) in bit_flags = infinite stock (decomp branch in
// CSWGuiStore::OnControlEntered: `if ((item->bit_flags & 4) == 0)`). The
// flag is only ever set on shop-side merchant items; player-owned items
// always have it clear, so stack_size is meaningful for inventory rows.
//
// Note: the prior values (0xfc / 0x108) read random data inside CSWSObject
// and made every stock count appear as 1. Fixed 2026-05-22.
// KOTOR 2: the whole CSWSItem field band shifted +0x40, each field
// individually witnessed (never derived) — the K2 item GFF saver 0x00602DD0
// writes StackSize from a WORD read at [item+0x2cc] and a bit-5 flag from
// [item+0x2c8], and the K2 store OnControlEntered twin 0x008B6E90 reads
// stack [item+0x2cc] / tests bit 2 of [item+0x2c8] exactly like K1's. The
// infinite-stock bit VALUE is unchanged (both games test `>> 2 & 1`).
const size_t    kSwsItemStackSizeOffset                = acc::off::Pick(0x28c, 0x2cc);
const size_t    kSwsItemBitFlagsOffset                 = acc::off::Pick(0x288, 0x2c8);
const uint32_t  kSwsItemInfiniteStockBit               = acc::off::Same(0x4);

// CSWSItem.charges @ +0x258 (ulong), CSWSItem.max_charges @ +0x25c (ulong).
// Both from the Ghidra struct DB (re/swkotor.exe.h CSWSItem body). Layout
// cross-checked against the verified bit_flags @ +0x288: the three trailing
// CExoLocString members (description_identified / description /
// localized_name, 8 bytes each) span +0x270..+0x288, walking back through
// item_repo / some_obj_id / model+body+texture+pad / add_cost / max_charges
// lands charges on +0x258. A "charged" item (limited-use consumable that
// can't stack — e.g. some droid/usable items) has max_charges > 0; regular
// gear and stackables leave both at 0, so this never collides with the
// stack_size suffix.
// KOTOR 2 +0x298/+0x29c: the K2 item GFF loader 0x00601740 stores the
// "Charges" field to [item+0x298] and its saver 0x00602DD0 writes the
// adjacent field from [item+0x29c] — K1's exact adjacency, +0x40.
const size_t    kSwsItemChargesOffset                  = acc::off::Pick(0x258, 0x298);
const size_t    kSwsItemMaxChargesOffset               = acc::off::Pick(0x25c, 0x29c);

// CSWSItem.description_indentified is a CExoLocString. GetPropertyDescription
// appends its text via CExoLocString::GetString, which returns the INLINE
// substring first (the per-item embedded copy) and only falls back to the TLK
// strref when there's no inline. Some German items carry a corrupt inline copy
// (all umlauts collapsed to 0xFD) while the TLK string is clean — so for the
// description block we resolve the strref directly through the TLK
// (LookupTlk), bypassing the bad inline copy. CExoLocString = { internal @0,
// strref @0x4 } (decompile-verified at CExoLocString::GetString 005ea130).
// KOTOR 2 +0x2b0: the K2 saver 0x00602DD0 writes DescIdentified from
// [item+0x2b0] (Description from +0x2b8, LocalizedName from +0x2c0 — the
// full K1 LocString trio at +0x40 each, in K1's order).
const size_t    kItemDescriptionLocStringOffset = acc::off::Pick(0x270, 0x2b0);
// Same on K2: its CExoLocString::GetString twin 0x007356B0 reads the strref
// at [this+4] before the TLK global, exactly like K1's 0x005ea130.
const size_t    kExoLocStringStrRefOffset       = acc::off::Same(0x4);

// CSWBaseItem fields, read off the pointer CSWItem::GetBaseItem returns (see
// engine_offsets_addresses.h, where the CMP-verified derivation is recorded).
// Same on K2, witnessed in its GetPropertyDescription twin 0x00607790: the
// item-type guards compare [base+0xac] and the weapon gate tests [base+9].
const size_t    kBaseItemWeaponTypeOffset        = acc::off::Same(0x09);
const size_t    kBaseItemItemTypeOffset          = acc::off::Same(0xac);

// CSWSItem/CSWItem base-item row INDEX at +0xc (both games; K2 witnesses:
// the GetPropertyDescription twin's `[this+0xc] == 0x2d` lightsaber gate and
// the store stock helper 0x00605FF0 comparing [itemA+0xc] == [itemB+0xc]).
// Used by the K2 section replica only — K1's guard never reads it.
const size_t    kSwsItemBaseItemIdOffset         = acc::off::Same(0xc);

// CSWGuiInGameJournal — quest journal panel (the "Aufträge" sub-screen).
//
// Layout (from SARIF):
//   0x000 panel base
//   0x064 title_label                  ("Aufträge — Laufende Aufträge - …")
//   0x1a4 item_description_label       (CSWGuiListBox, holds 1 row with
//                                        "<Planet>:\n<entry text>")
//   0x484 description label inline     (the single row inside item_description)
//   0x5c4 items_listbox                (one CSWGuiJournalItemEntry per quest)
//   0x8a4 quest_items_button           (cmd 0x29 → opens CSWGuiQuestItem modal)
//   0xa68 swap_text_button             (cmd 0x2a → toggle active/done quests)
//   0xc2c sort_button                  (cmd 0x2b → cycle sort order)
//   0xdf0 exit_button                  (cmd 0x28 → close)
//
// Button→panel command wiring (decompiled CSWGuiInGameJournal::CSWGuiInGameJournal
// @0x00644a40 + ::HandleInputEvent @0x006456e0): each button AddEvent's a 0x27
// (activate) callback that calls panel->HandleInputEvent(cmd). Our generic
// FireActivate(0x27) reaches them. Sort (0x2b) only sets the sort order and
// repopulates the quest list LAZILY in Draw() next frame, so an immediate chain
// rebuild captures half-built rows (base CSWGuiObject vtable, unreadable text);
// force PopulateItemListBox first. Swap (0x2a) repopulates synchronously inside
// the handler — just invalidate the chain so it re-binds to the new list.
// KOTOR 2: LB_ITEMS at 0x5e8 by tag, out of its own constructor.
//
// The quest-items button has NO KOTOR 2 counterpart, and the constructor shows
// why directly: in KOTOR 1 the member immediately after items_listbox is
// quest_items_button, with swap_text_button one button further on; in KOTOR 2
// the member immediately after LB_ITEMS *is* BTN_SWAPTEXT. Exactly one button
// was dropped from that run. That agrees with the independent finding that
// KOTOR 2 has no CSWGuiQuestItem class at all — the sub-screen this button
// opened does not exist there. KOTOR 2's journal offers BTN_MESSAGES instead.
const size_t    kJournalItemsListBoxOffset             = acc::off::Pick(0x5c4, 0x5e8);
const size_t    kJournalQuestItemsButtonOffset         = acc::off::Kotor1Only(0x8a4);
// K2 (ctor 0x007FAE60 by tag): BTN_SWAPTEXT @0x8d8 — engine-witnessed a
// second time by the K2 journal HandleInputEvent (case 0x27 swaps mode and
// restamps the caption strref at panel+0x8d8). K1's single BTN_SORT has no
// K2 counterpart: KOTOR 2 replaced it with three direct sort-mode buttons,
// declared below; journal::IsSortButton matches any of them there.
const size_t    kJournalSwapTextButtonOffset           = acc::off::Pick(0xa68, 0x8d8);
const size_t    kJournalSortButtonOffset               = acc::off::Kotor1Only(0xc2c);
const size_t    kJournalExitButtonOffset               = acc::off::Pick(0xdf0, 0xaa8);
// KOTOR 2 only — the three sort-mode buttons that replaced K1's BTN_SORT
// (their handlers, cases 0x2f/0x30 in the K2 journal HandleInputEvent,
// repopulate the list synchronously): BTN_FILTER_TIME / _NAME / _PLANET.
// K1 column is a dead value (the buttons do not exist there); consumers
// gate on IsKotor2 before touching these.
const size_t    kJournalFilterTimeButtonOffset         = acc::off::Pick(0, 0xc78);
const size_t    kJournalFilterNameButtonOffset         = acc::off::Pick(0, 0xe48);
const size_t    kJournalFilterPlanetButtonOffset       = acc::off::Pick(0, 0x1018);

// CSWGuiInGameInventory — the "Inventar" sub-screen. Same embedded-button
// shape as the journal above: every button is constructed in place at
// panel+offset, so identifying one by offset is locale-independent.
//
// Layout (SARIF DATATYPE dump):
//   0x1a4 item_description_label
//   0x420 inventory_label       ("Gruppengepäck - <CURRENT filter>")
//   0x424 credits_value_label   (see menus_credits.cpp)
//   0x564 item_listbox          (one CSWGuiInGameItemEntry per filtered item)
//   0x1164 exit_button          (caption strref 1582 — dropped by the
//                                universal close-button filter)
//   0x1328 useitem_button       ("Verwenden")
//   0x14ec questitems_button    — Ghidra's name is wrong for this panel:
//                                SetNextFilter @0x006b2a80 writes
//                                "Zeigen: <NEXT filter>" into it, so it is
//                                the item-filter cycle button.
//   0x1a38 / 0x1bfc switch_left / switch_right (the filter's cycle arrows,
//                                dropped by SquashCycleFlankers)
//
// Two behaviours matter for chain nav (both decompiled 2026-07-31):
//
//  * useitem_button AddEvent's AcceptButtonCallback @0x00624ba0, which is a
//    thunk to panel->OnAButtonPressed → CSWGuiPanel::HandleInputEvent(0x27).
//    The inventory's own HandleInputEvent has no case 0x27, so the base panel
//    dispatches activate to panel.active_control — i.e. the button is just
//    the gamepad-A "activate what is focused" shortcut, and it fires on
//    whatever row the engine still considers active rather than on our chain
//    focus. Enter on the row itself reaches the same handler directly, so the
//    button is redundant AND less precise; IsDecorativeControl drops it.
//
//  * questitems_button dispatches cmd 0x29 → SetNextFilter, which only sets
//    the new filter and raises bit_flags bit 0. PopulateItemListBox — which
//    rebuilds item_listbox through CheckFilter — runs LAZILY in Draw() next
//    frame, exactly like the journal Sort button. The row controls are POOLED
//    and reused (verified in patch-20260731-083448.log: the same control
//    pointers spoke different item names after a filter change) so nothing
//    dangles, but the chain keeps the OLD row count and row→item mapping.
//    Force the repopulate, then invalidate.
// K2 (ctor 0x008A6170 by tag): LB_ITEMS @0x588, BTN_EXIT @0xb68,
// BTN_USEITEM @0xd38. K1's single filter-CYCLE button has no K2
// counterpart: KOTOR 2 lays out SEVEN direct filter buttons instead
// (BTN_ALL @0x1800 .. BTN_QUESTS @0x22e0, stride 0x1d0), matched as a
// range by inventory::IsFilterButton on that game.
const size_t    kInventoryItemListBoxOffset            = acc::off::Pick(0x564, 0x588);
const size_t    kInventoryExitButtonOffset             = acc::off::Pick(0x1164, 0xb68);
const size_t    kInventoryUseItemButtonOffset          = acc::off::Pick(0x1328, 0xd38);
const size_t    kInventoryFilterButtonOffset           = acc::off::Kotor1Only(0x14ec);
// KOTOR 2 only — bounds of the direct filter-button run (see above).
const size_t    kInventoryFilterFirstButtonOffset      = acc::off::Pick(0, 0x1800);  // BTN_ALL
const size_t    kInventoryFilterLastButtonOffset       = acc::off::Pick(0, 0x22e0);  // BTN_QUESTS
const size_t    kInventoryFilterButtonStride           = acc::off::Pick(1, 0x1d0);
