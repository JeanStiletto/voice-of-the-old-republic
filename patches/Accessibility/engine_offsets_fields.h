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
const size_t kButtonToggleStateOffset = acc::off::Todo(0x1c8);
const size_t kSliderMaxValueOffset    = acc::off::Todo(0x70);
const size_t kSliderCurValueOffset    = acc::off::Todo(0x74);

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
// STILL Todo, and it is the one gap in the text chain: CAurGUIStringInternal is
// a different class, and it is one of the vtables whose slot count did NOT
// match (67 vs 32), so its layout is the least safe thing here to assume.
// ReadGuiString needs it for the *rendered* text; until it is verified, KOTOR 2
// text extraction has to fall back to the inline CExoString above.
const size_t kAurGuiStringCStrOffset   = acc::off::Todo(0x14);   // CAurGUIStringInternal.field5

// CSWGuiKeyMapButton — the keyboard-mapping screen's row control (vtable
// 0x007593c8). Each row embeds TWO CSWGuiButtons: action_button at +0 (the
// event name, "Vorwärts" — read via the normal button offsets) and
// mapped_key_button at +0x1c8 (the bound key, "W"). Layout decompiled from
// swkotor.exe.h CSWGuiKeyMapButton + the field-offset anchors
// (key_mappings ptr at +0x38c ⇒ sizeof(CSWGuiButton)=0x1c4 ⇒ mapped_key_button
// at +0x1c8). `unchangeable` (non-zero = fixed binding) is at +0x3a4. The key
// text reads at mapped_key + button offsets, e.g. gui_string at 0x1c8+0x168.
const size_t    kKeyMapButtonMappedKeyOffset = acc::off::Todo(0x1c8);
const size_t    kKeyMapButtonUnchangeableOff = acc::off::Todo(0x3a4);
// CSWGuiKeyMapButton.key_code @ +0x39c — the engine InputIndices value of the
// freshly captured key (KEYBOARD_*; NOT a DIK scancode — set with `updated`=1 on
// capture, written to swkotor.ini in decimal on Accept). Resolve to a VK via
// engine_keymap::InputIndexToVk to test the new game bind against mod hotkeys.
const size_t    kKeyMapButtonKeyCodeOff      = acc::off::Todo(0x39c);

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
const size_t    kEditboxShortA             = acc::off::Todo(0x150);
const size_t    kEditboxShortB             = acc::off::Todo(0x152);
const size_t    kEditboxStringCStrOffset   = acc::off::Todo(0x158);
const size_t    kEditboxStringLengthOffset = acc::off::Todo(0x15c);

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
const size_t    kNameChargenEditboxOffset  = acc::off::Todo(0x230);
const size_t    kNameChargenEndButtonOffset = acc::off::Todo(0x6c);

// CSWGuiNameChargen carries a `main_title_label` ("CHARAKTERAUSWAHL") and a
// `subtitle_label` ("Name") at distinct fixed offsets. The first one is the
// stale parent-flow header that BioWare reuses across all chargen sub-
// panels; the second is the screen-specific title. Our title-walk picks
// the first announceable label by panel-controls index, which lands on
// main_title_label first — wrong for any user trying to know which step
// they're on. The editbox spec's titleOverride reads subtitle_label
// directly via this offset to substitute the correct title speech.
const size_t    kNameChargenSubtitleLabelOffset = acc::off::Todo(0x4d0);

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
const size_t    kSaveNameEditboxOffset        = acc::off::Todo(0x3f0);
const size_t    kSaveNameOkButtonOffset       = acc::off::Todo(0x68);
const size_t    kSaveNameTitleLabelOffset     = acc::off::Todo(0x550);

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
const size_t    kClassSelectionsArrayOffset      = acc::off::Todo(0x6c);
const size_t    kClassSelCharSize                = acc::off::Todo(0x25c);
const int       kClassSelectionsCount            = acc::off::Todo(6);
const size_t    kClassSelectionClassLabelOffset  = acc::off::Todo(0x1254);

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
const size_t    kPortraitCharGenCreatureOffset   = acc::off::Todo(0x64);
const size_t    kPortraitLabelOffset             = acc::off::Todo(0x2ec);
const size_t    kPortraitRightArrowOffset        = acc::off::Todo(0xe84);
const size_t    kPortraitLeftArrowOffset         = acc::off::Todo(0x1048);
const size_t    kPortraitIdOffset                = acc::off::Todo(0x1238);

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
const size_t    kFeatsCharGenNameLabelOffset        = acc::off::Todo(0xbac);
const size_t    kFeatsCharGenSelectButtonOffset     = acc::off::Todo(0x1238);
const size_t    kFeatsCharGenFeatsListBoxOffset     = acc::off::Todo(0x13fc);
const size_t    kFeatsCharGenDescriptionListBoxOffset = acc::off::Todo(0x16dc);

// Four parallel feat lists tracked on the panel — each a
// CExoArrayList<ushort> { ushort* data, int size, int capacity }
// inline-stored 12 bytes apart. Together they partition every feat the
// panel cares about so DetermineFeat can return a status byte:
//
//   field19 @ +0x19bc  data; @ +0x19c0 size  — existing  (creature already has)
//   field20 @ +0x19c8  data; @ +0x19cc size  — granted   (auto-given this level)
//   field23 @ +0x19d4  data; @ +0x19d8 size  — available (BuildAvailableList output)
//   field26 @ +0x19e0  data; @ +0x19e4 size  — chosen    (picked this session)
const size_t    kFeatsCharGenExistingListDataOffset    = acc::off::Todo(0x19bc);
const size_t    kFeatsCharGenExistingListSizeOffset    = acc::off::Todo(0x19c0);
const size_t    kFeatsCharGenGrantedListDataOffset     = acc::off::Todo(0x19c8);
const size_t    kFeatsCharGenGrantedListSizeOffset     = acc::off::Todo(0x19cc);
const size_t    kFeatsCharGenAvailableListDataOffset   = acc::off::Todo(0x19d4);
const size_t    kFeatsCharGenAvailableListSizeOffset   = acc::off::Todo(0x19d8);
const size_t    kFeatsCharGenChosenListDataOffset      = acc::off::Todo(0x19e0);
const size_t    kFeatsCharGenChosenListSizeOffset      = acc::off::Todo(0x19e4);

// CSWGuiSkillFlowChart embedded at +0x1a08 (struct size 0x10). It's the
// 2D scrollable feat-tree grid: a CExoArrayList-shaped header + a
// (selected_col, selected_row) pair packed into the trailing bytes.
//
//   chart +0x00  CSWGuiSkillFlow** rows_data
//   chart +0x04  int               rows_size
//   chart +0x08  int               rows_capacity
//   chart +0x0c  byte              selected_col   (0..2 in BuildButtons)
//   chart +0x0d  byte              selected_row
const size_t    kFeatsCharGenChartOffset               = acc::off::Todo(0x1a08);
const size_t    kSkillFlowChartRowsDataOffset          = acc::off::Todo(0x0);
const size_t    kSkillFlowChartRowsSizeOffset          = acc::off::Todo(0x4);
const size_t    kSkillFlowChartSelectedColOffset       = acc::off::Todo(0xc);
const size_t    kSkillFlowChartSelectedRowOffset       = acc::off::Todo(0xd);

// CSWGuiSkillFlow row (1148 bytes). Three CSWGuiFlowSkillStruct columns
// at +0x5c, +0x184, +0x2ac (stride 0x128) plus 2 connector-line images
// at +0x3d4 / +0x428 the renderer uses to draw progression arrows.
const size_t    kSkillFlowFirstColumnOffset            = acc::off::Todo(0x5c);
const size_t    kSkillFlowColumnStride                 = acc::off::Todo(0x128);
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
const size_t    kFlowSkillStructFeatIdOffset           = acc::off::Todo(0x11c);
const size_t    kFlowSkillStructStatusOffset           = acc::off::Todo(0x120);
const unsigned  kFlowSkillStructEmptyFeatId            = acc::off::Todo(0xffffffff);

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
const size_t    kRulesFeatsArrayOffset        = acc::off::Todo(0x90);
const size_t    kRulesFeatCountOffset         = acc::off::Todo(0xa4);
const size_t    kFeatStructSize               = acc::off::Todo(0x48);
const size_t    kFeatNameStrRefOffset         = acc::off::Todo(0x08);

// Offset of the embedded CSWGuiSkillFlowChart inside CSWGuiPowersLevelUp.
// Matches struct field33_0x19fc (swkotor.exe.h:16637). We call
// CSWGuiSkillFlowChart::SetSelectedSkill on this offset to keep the chart's
// render-side highlight in sync with our keyboard focus (same pattern as
// chargen_feats — see kFeatsCharGenChartOffset).
const size_t    kPowersLevelUpChartOffset              = acc::off::Todo(0x19fc);

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
const size_t kListBoxControlsOffset         = acc::off::Todo(0x29c);
const size_t kListBoxBitFlagsOffset         = acc::off::Todo(0x2bc);
const size_t kListBoxItemsPerPageOffset     = acc::off::Todo(0x2c4);
const size_t kListBoxSelectionIndexOffset   = acc::off::Todo(0x2c6);
const size_t kListBoxTopVisibleIndexOffset  = acc::off::Todo(0x2c8);

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
const size_t kControlParentOffset       = acc::off::Todo(0x14);  // CSWGuiControl* parent
const size_t kControlTooltipStrRefOffset = acc::off::Todo(0x24); // uint32 strref (0 = none)
const size_t kControlTooltipStringOffset = acc::off::Todo(0x28); // CExoString literal

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
const size_t    kControlIsActiveOffset       = acc::off::Todo(0x4c);

// CSWGuiUpgrade.field9 - the description label CSWGuiUpgrade::OnControlEntered
// writes its built string into (see engine_offsets_addresses.h); we read the
// result back from here.
const size_t    kUpgradeDescLabelOffset            = acc::off::Todo(0x1f60);  // panel.field9 (CSWGuiLabel)

// CSWGuiUpgrade.field24_0x2f48 — bit 0 is the "picker open" state (set by
// OnSlotSelected, cleared by OnUpgradeSelected's close tail). Clear it on cancel.
const size_t    kUpgradePickerOpenFlagOff = acc::off::Todo(0x2f48);  // panel.field24

// CSWGuiUpgrade slot-type table geometry, plus the two panel/button fields that
// index it. The table base is kAddrUpgradeSlotTypeTable in
// engine_offsets_addresses.h, where the per-entry layout is documented.
const size_t    kUpgradeSlotTypeStride    = acc::off::Todo(12);
const size_t    kUpgradeSlotTypeStrRefOff = acc::off::Todo(8);
const size_t    kUpgradePanelCategoryOff  = acc::off::Todo(0x2f4c);  // panel.field25
const size_t    kUpgradeSlotCustomValueOff = acc::off::Todo(0x58);   // slot_btn.custom_value

// CSWGuiUpgrade.field35_0x2f74 — array of installed-mod CSWSItem* indexed by
// the slot button's custom_value. Non-null = slot occupied (the engine
// constructs a CSWSItem and LoadFromTemplate's the mod into this slot when the
// base item already carries that upgrade — bitmask at field27+0x294 — see
// OnPanelAdded @0x006c4d70); null = empty. Both OnEnterSlot @0x006c3c30 (saber
// branch) and OnSlotSelected @0x006c6500 (install/remove branch) index this
// array by custom_value, so it is the authoritative per-slot occupancy field.
const size_t    kUpgradeSlotInstalledItemsOff = acc::off::Todo(0x2f74);  // panel.field35

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

const size_t kCreatureCombatRoundOffset           = acc::off::Todo(0x9c8);
const size_t kObjectHitPointsOffset               = acc::off::Todo(0xe0);
const size_t kObjectEffectsOffset                 = acc::off::Todo(0x124);

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
const size_t kObjectActionNodesOffset             = acc::off::Todo(0xfc);
const size_t kExoLinkedListInternalCountOffset    = acc::off::Todo(0x8);

const size_t kCombatRoundAttacksListOffset        = acc::off::Todo(0x4);
const size_t kCombatRoundTimerOffset              = acc::off::Todo(0x944);
const size_t kCombatRoundLengthOffset             = acc::off::Todo(0x94c);
const size_t kCombatRoundCurrentAttackOffset      = acc::off::Todo(0x96c);
const size_t kCombatRoundActionsOffset            = acc::off::Todo(0x9b0);
const size_t kCombatRoundEngagedOffset            = acc::off::Todo(0x9b8);
const size_t kCombatRoundCurrentActionOffset      = acc::off::Todo(0x9d0);

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
const size_t kListInternalOffset       = acc::off::Todo(0x0);  // CExoLinkedList<T>     +0 -> internal*
const size_t kListInternalHeadOffset   = acc::off::Todo(0x0);  // CExoLinkedListInternal+0 -> head node*
const size_t kListInternalCountOffset  = acc::off::Todo(0x8);  // CExoLinkedListInternal+8 -> count (engine authoritative)
const size_t kLinkedListNodeNextOff    = acc::off::Todo(0x4);  // CExoLinkedListNode    +4 -> next
const size_t kLinkedListNodeDataOff    = acc::off::Todo(0x8);  // CExoLinkedListNode    +8 -> data


const size_t kCombatRoundActionTypeOffset       = acc::off::Todo(0x10);
const size_t kCombatRoundActionTargetOffset     = acc::off::Todo(0x14);
const size_t kCombatRoundActionRetargetOffset   = acc::off::Todo(0x18);
const size_t kCombatRoundActionMoveToPosOffset  = acc::off::Todo(0x38);
const size_t kCombatRoundActionResultOffset     = acc::off::Todo(0x7c);
const size_t kCombatRoundActionDamageOffset     = acc::off::Todo(0x80);

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
const size_t    kRulesSpellsOffset                = acc::off::Todo(0x8c);

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
const size_t    kCombatRoundActionSpellIdOffset   = acc::off::Todo(0x24);
const size_t    kCombatRoundActionItemHandleOff   = acc::off::Todo(0x64);
const size_t    kCombatRoundActionFeatIdOffset    = acc::off::Todo(0x5c);

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
const size_t    kGameEffectTypeOffset             = acc::off::Todo(0x8);

// CSWSCreature.effect_icons — CExoArrayList<CEffectIconObject*> with data
// ptr at +0x8f4 and size at +0x8f8. This is the sighted buff/debuff icon
// row on the portrait: CSWSEffectListHandler::OnApplyEffectIcon inserts one
// entry per applied EFFECTICON effect (priority-sorted, deduped by icon id)
// and OnRemoveEffectIcon walks the same raw offsets — both decompile-
// verified 2026-07-17. Each CEffectIconObject (0x20 bytes): +0x0 ushort
// effecticon.2da row id, +0x2 CResRef icon resref, +0x18 ushort priority.
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
const size_t    kInventoryRightWeaponHandleOffset = acc::off::Todo(0x14);  // main hand
const size_t    kInventoryLeftWeaponHandleOffset  = acc::off::Todo(0x18);  // off hand
const size_t    kInventoryHeadHandleOffset        = acc::off::Todo(0x4);
const size_t    kInventoryTorsoHandleOffset       = acc::off::Todo(0x8);
const size_t    kInventoryHandsHandleOffset       = acc::off::Todo(0x10);
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
const size_t    kEquipPanelPlayerCreatureOffset    = acc::off::Todo(0x0064);
const size_t    kEquipPanelHeadIdOffset            = acc::off::Todo(0x4284);
const size_t    kEquipPanelImplantIdOffset         = acc::off::Todo(0x4298);
const size_t    kEquipPanelArmorIdOffset           = acc::off::Todo(0x4290);  // body
const size_t    kEquipPanelLeftArmbandIdOffset     = acc::off::Todo(0x4288);
const size_t    kEquipPanelRightArmbandIdOffset    = acc::off::Todo(0x428c);
const size_t    kEquipPanelLeftWeaponIdOffset      = acc::off::Todo(0x427c);
const size_t    kEquipPanelRightWeaponIdOffset     = acc::off::Todo(0x4280);
const size_t    kEquipPanelGlovesIdOffset          = acc::off::Todo(0x4294);  // hands
const size_t    kEquipPanelBeltIdOffset            = acc::off::Todo(0x429c);

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
const size_t    kEquipPanelDefenseLabelOffset            = acc::off::Todo(0x2098);
const size_t    kEquipPanelHpLabelOffset                 = acc::off::Todo(0x21d8);
const size_t    kEquipPanelLeftWeaponDamageLabelOffset   = acc::off::Todo(0x1b98);  // Lane: left_weapon_attack_label
const size_t    kEquipPanelLeftWeaponTohitLabelOffset    = acc::off::Todo(0x1cd8);
const size_t    kEquipPanelRightWeaponDamageLabelOffset  = acc::off::Todo(0x1e18);  // Lane: right_weapon_attack_label
const size_t    kEquipPanelRightWeaponTohitLabelOffset   = acc::off::Todo(0x1f58);

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
const size_t    kEquipPanelBackButtonOffset           = acc::off::Todo(0x385C);
const size_t    kEquipPanelChangeParty1ButtonOffset   = acc::off::Todo(0x3A20);
const size_t    kEquipPanelChangeParty2ButtonOffset   = acc::off::Todo(0x3BE4);
const size_t    kEquipPanelCharacterLeftButtonOffset  = acc::off::Todo(0x3DA8);
const size_t    kEquipPanelCharacterRightButtonOffset = acc::off::Todo(0x3F6C);

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
const size_t    kLevelUpButtonBackOffset              = acc::off::Todo(0x1944);
const size_t    kLevelUpButtonCancelOffset            = acc::off::Todo(0x1B08);

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
const size_t    kInGameMapUpButtonOffset              = acc::off::Todo(0xAB0);
const size_t    kInGameMapDownButtonOffset            = acc::off::Todo(0xC74);

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
const size_t    kAbilitiesSkillRankLabelOffset   = acc::off::Todo(0x2190);  // (8592)  "Fähigkeitenrang"
const size_t    kAbilitiesRankValueLabelOffset   = acc::off::Todo(0x22D0);  // (8912)  e.g. "8"
const size_t    kAbilitiesBonusLabelOffset       = acc::off::Todo(0x2410);  // (9232)  "Bonus"
const size_t    kAbilitiesBonusValueLabelOffset  = acc::off::Todo(0x2550);  // (9552)  e.g. "+3"
const size_t    kAbilitiesTotalLabelOffset       = acc::off::Todo(0x2690);  // (9872)  "Gesamtrang"
const size_t    kAbilitiesTotalValueLabelOffset  = acc::off::Todo(0x27D0);  // (10192) e.g. "11"
const size_t    kAbilitiesNameLabelOffset        = acc::off::Todo(0x2910);  // (10512) selected entry name
const size_t    kAbilitiesFeatsButtonOffset      = acc::off::Todo(0x2B90);  // (11152) BTN_FEATS  (Talente)
const size_t    kAbilitiesPowersButtonOffset     = acc::off::Todo(0x2D54);  // (11604) BTN_POWERS (Kräfte)
const size_t    kAbilitiesSkillsButtonOffset     = acc::off::Todo(0x2F18);  // (12056) BTN_SKILLS (Fähigkeiten)
const size_t    kAbilitiesListBoxOffset          = acc::off::Todo(0x30DC);  // (12508) LB_ABILITY (main list)
const size_t    kAbilitiesDescListBoxOffset      = acc::off::Todo(0x33BC);  // (13244) LB_DESC (description)

// The two CSWGuiSkillFlowChart members on the panel (field30/field31). Their
// internals are the SAME CSWGuiSkillFlowChart layout the chargen/level-up
// grids use — read the cursor and row array through
// kSkillFlowChartSelectedRow/SelectedCol/RowsData/RowsSize above rather than
// re-declaring panel-local aliases for them. We read row vs row-count to clamp
// the engine's chart nav, which otherwise WRAPS top<->bottom (unlike the skills
// listbox, which clamps).
const size_t    kAbilitiesPowersChartOffset       = acc::off::Todo(0x3f78);  // field30 (Powers)
const size_t    kAbilitiesFeatsChartOffset        = acc::off::Todo(0x3f88);  // field31 (Feats)

// CGuiInGame.field139_0xbc0 — the active abilities tab: 0 = Skills,
// 1 = Powers, 2 = Feats. Read to route per-tab input + announce the tab.
const size_t    kGuiInGameAbilitiesTabOffset      = acc::off::Todo(0xbc0);

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
const size_t kInGameMessagesMessagesListBoxOffset = acc::off::Todo(0x64);
const size_t kInGameMessagesDialogListBoxOffset   = acc::off::Todo(0x344);
const size_t kInGameMessagesShowButtonOffset      = acc::off::Todo(0x76c);
const size_t kInGameMessagesExitButtonOffset      = acc::off::Todo(0x930);

// CSWGuiDialog (and Cinematic / ComputerCamera variants which share base
// layout):
//   panel             @+0x0
//   replies_listbox   @+0x19c4
//   message_label     @+0x1ca4
const size_t kDialogRepliesListBoxOffset          = acc::off::Todo(0x19c4);
const size_t kDialogMessageLabelOffset            = acc::off::Todo(0x1ca4);

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
const size_t kServerObjectDialogOwnerOffset       = acc::off::Todo(0x54);

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
const size_t kCreatureStatsRaceOffset             = acc::off::Todo(0xdc);

// CSWSCreatureStats.appearance_type (ushort, indexes appearance.2da).
// Verified from Lane's exported header @0x186 (line 15707 in swkotor.exe.h).
// THIS is the authoritative species discriminator — the CSWSCreature inline
// cache at +0xa4c is unreliable.
const size_t kCreatureStatsAppearanceTypeOffset   = acc::off::Todo(0x186);

// CSWGuiDialogComputer adds a terminal-output listbox above the embedded
// replies listbox.
//   message_listbox  @+0x2cfc   (terminal output text)
//   obscure_label    @+0x34dc
const size_t kDialogComputerMessageListBoxOffset  = acc::off::Todo(0x2cfc);

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
const size_t kCGuiInGameDialogSpeakerOffset       = acc::off::Todo(0x170);

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
const size_t kCGuiInGameReplyCountOffset          = acc::off::Todo(0x114);
const size_t kCGuiInGameReplyTextArrayOffset      = acc::off::Todo(0x118);

// CSWGuiBarkBubble.object_id @+0x1c0 — the bark speaker's CLIENT object id,
// written by CSWGuiBarkBubble::SetBark @0x006a9920 (this->object_id = param_1)
// and consumed by ::Draw @0x006a9ce0 via CClientExoApp::GetGameObject(client,
// object_id) → AsSWCObject for the 6m proximity/cull test. Sentinel
// 0x7f000000 means "no owning creature" — system/loudspeaker barks (camera
// zone messages, area feedback). Resolve a real id through
// ClientToServerObjectId → ResolveServerObjectHandle to classify the speaker,
// exactly as the dialog-speaker path does for CGuiInGame +0x170.
const size_t kBarkBubbleObjectIdOffset            = acc::off::Todo(0x1c0);

const size_t    kStoreShopItemsListBoxOffset           = acc::off::Todo(0x1480);
const size_t    kStoreInvItemsListBoxOffset            = acc::off::Todo(0x1760);
const size_t    kStoreDescriptionListBoxOffset         = acc::off::Todo(0x1a40);
const size_t    kStoreCancelButtonOffset               = acc::off::Todo(0x1d20);
const size_t    kStoreToggleButtonOffset               = acc::off::Todo(0x1ee4);  // examine_button in struct DB
const size_t    kStoreAcceptButtonOffset               = acc::off::Todo(0x20a8);
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
const uint32_t  kControlVisibleBit                     = acc::off::Todo(0x2);

// CSWGuiStoreItemEntry.obj_id @ +0x1c4 — the client-side game-object
// handle for the row's CSWSItem. Resolve via ClientToServerObjectId then
// GetItemByGameObjectID.
const size_t    kStoreItemEntryObjIdOffset             = acc::off::Todo(0x1c4);

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
const size_t    kSwsItemStackSizeOffset                = acc::off::Todo(0x28c);
const size_t    kSwsItemBitFlagsOffset                 = acc::off::Todo(0x288);
const uint32_t  kSwsItemInfiniteStockBit               = acc::off::Todo(0x4);

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
const size_t    kSwsItemChargesOffset                  = acc::off::Todo(0x258);
const size_t    kSwsItemMaxChargesOffset               = acc::off::Todo(0x25c);

// CSWSItem.description_indentified is a CExoLocString. GetPropertyDescription
// appends its text via CExoLocString::GetString, which returns the INLINE
// substring first (the per-item embedded copy) and only falls back to the TLK
// strref when there's no inline. Some German items carry a corrupt inline copy
// (all umlauts collapsed to 0xFD) while the TLK string is clean — so for the
// description block we resolve the strref directly through the TLK
// (LookupTlk), bypassing the bad inline copy. CExoLocString = { internal @0,
// strref @0x4 } (decompile-verified at CExoLocString::GetString 005ea130).
const size_t    kItemDescriptionLocStringOffset = acc::off::Todo(0x270);
const size_t    kExoLocStringStrRefOffset       = acc::off::Todo(0x4);

// CSWBaseItem fields, read off the pointer CSWItem::GetBaseItem returns (see
// engine_offsets_addresses.h, where the CMP-verified derivation is recorded).
const size_t    kBaseItemWeaponTypeOffset        = acc::off::Todo(0x09);
const size_t    kBaseItemItemTypeOffset          = acc::off::Todo(0xac);

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
const size_t    kJournalItemsListBoxOffset             = acc::off::Todo(0x5c4);
const size_t    kJournalQuestItemsButtonOffset         = acc::off::Todo(0x8a4);
const size_t    kJournalSwapTextButtonOffset           = acc::off::Todo(0xa68);
const size_t    kJournalSortButtonOffset               = acc::off::Todo(0xc2c);
const size_t    kJournalExitButtonOffset               = acc::off::Todo(0xdf0);

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
const size_t    kInventoryItemListBoxOffset            = acc::off::Todo(0x564);
const size_t    kInventoryExitButtonOffset             = acc::off::Todo(0x1164);
const size_t    kInventoryUseItemButtonOffset          = acc::off::Todo(0x1328);
const size_t    kInventoryFilterButtonOffset           = acc::off::Todo(0x14ec);
