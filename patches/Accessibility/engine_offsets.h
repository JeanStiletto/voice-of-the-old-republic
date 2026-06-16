// Engine struct/vtable offset table. Pure constants — no behaviour.
// Values from Lane's Ghidra DB; GoG bytes match Steam. File-scope (not
// namespaced) for callsite brevity, same as engine_input.h's kInput*.

#pragma once

#include <cstddef>
#include <cstdint>

// GuiControlMethods vtable indices for RTTI-style downcasts.
//
// Each accessor returns the same `this` cast to the concrete subclass, or
// nullptr if the control isn't of that subclass. They are trivial
// implementations (no engine state mutation, no allocation), so calling them
// from inside our hook is safe.
//
// Verified against the SARIF GuiControlMethods struct (offset 80/84/88/92).
constexpr int kVtableAsLabel        = 20;
constexpr int kVtableAsLabelHilight = 21;
constexpr int kVtableAsButton       = 22;
constexpr int kVtableAsButtonToggle = 23;

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
constexpr size_t kButtonTextOffset    = 0x16c;
constexpr size_t kButtonStrRefOffset  = 0x174;
constexpr size_t kLabelTextOffset     = 0xe8;
constexpr size_t kLabelStrRefOffset   = 0xf0;

// The engine's reusable "close/back" caption. Every standalone dismiss
// button across the sub-screen .gui files — BTN_EXIT (abilities, character,
// inventory, journal, map, messages, optionsingame), BTN_BACK (equip,
// questitem, upgrade*, all options sub-tabs, titlemovie), BTN_CANCEL
// (container) and BTN_Cancel (store) — points its CAPTION at this single
// strref (verified by dumping CONTROLS across all 21 panels with gffdump).
// It renders as "Schliess." in German, "Close" in English, etc., so a
// match on the engine's *resolved* text for this strref is fully
// locale-agnostic. Confirm/Yes-No popups deliberately use 1580 (OK/Yes) and
// 1581 (Cancel/No), NOT 1582, so matching 1582 never strips the only
// actionable button from a choice dialog. Used by RebindChain's
// isDecorative filter to drop the redundant close button (Esc already
// dismisses every such panel via HandleEsc → FindCancelButton/Close).
constexpr uint32_t kCloseButtonStrRef = 1582;

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
constexpr size_t kButtonToggleStateOffset = 0x1c8;
constexpr size_t kSliderMaxValueOffset    = 0x70;
constexpr size_t kSliderCurValueOffset    = 0x74;

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

// CSWGuiButton vtable. The standard button class — used by SaveLoad's
// BTN_DELETE / BTN_BACK / BTN_SAVELOAD, the equipment screen's slot
// buttons, the chargen class icons, the InGameMenu strip icons, the
// workbench upgrade-slot buttons (BTN_UPGRADE3X/4X), and most other
// CSWGuiButton instances in the engine. Identity-by-vtable matters for
// structural panel detectors that need to distinguish a Button child
// from a Label/LabelHilight child sharing the same .gui-time ID (the
// SaveLoad-vs-Workbench-upgrade collision at ID 11 is the canonical
// case — see IsSaveLoadStructural).
constexpr uintptr_t kVtableCSWGuiButton = 0x0073E658;

// CSWGuiKeyMapButton — the keyboard-mapping screen's row control (vtable
// 0x007593c8). Each row embeds TWO CSWGuiButtons: action_button at +0 (the
// event name, "Vorwärts" — read via the normal button offsets) and
// mapped_key_button at +0x1c8 (the bound key, "W"). Layout decompiled from
// swkotor.exe.h CSWGuiKeyMapButton + the field-offset anchors
// (key_mappings ptr at +0x38c ⇒ sizeof(CSWGuiButton)=0x1c4 ⇒ mapped_key_button
// at +0x1c8). `unchangeable` (non-zero = fixed binding) is at +0x3a4. The key
// text reads at mapped_key + button offsets, e.g. gui_string at 0x1c8+0x168.
constexpr uintptr_t kVtableKeyMapButton          = 0x007593c8;
constexpr size_t    kKeyMapButtonMappedKeyOffset = 0x1c8;
constexpr size_t    kKeyMapButtonUnchangeableOff = 0x3a4;
// CSWGuiKeyMapButton.key_code @ +0x39c — the engine InputIndices value of the
// freshly captured key (KEYBOARD_*; NOT a DIK scancode — set with `updated`=1 on
// capture, written to swkotor.ini in decimal on Accept). Resolve to a VK via
// engine_keymap::InputIndexToVk to test the new game bind against mod hotkeys.
constexpr size_t    kKeyMapButtonKeyCodeOff      = 0x39c;

// CSWGuiEditbox layout (verified against k1_win_gog_swkotor.exe.xml SYMBOL
// CSWGuiEditbox_vtable @ 0x0073EAC8 + STRUCTURE size 0x160 + swkotor.exe.h
// CSWGuiEditbox/CSWGuiEditText). The editbox is single-instance in vanilla
// KOTOR — it appears only on the chargen Name panel as `name_editbox`.
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
constexpr uintptr_t kVtableEditbox             = 0x0073EAC8;
constexpr size_t    kEditboxShortA             = 0x150;
constexpr size_t    kEditboxShortB             = 0x152;
constexpr size_t    kEditboxStringCStrOffset   = 0x158;
constexpr size_t    kEditboxStringLengthOffset = 0x15c;

// CSWGuiNameChargen (chargen "Name eingeben" panel — step 5 of Eigener
// Charakter, also reused in the Standard-Charakter quick flow). Verified
// against k1_win_gog_swkotor.exe.xml SYMBOL CSWGuiNameChargen_vtable @
// 0x00759F38 + STRUCTURE size 0x9C4 + swkotor.exe.h CSWGuiNameChargen.
//
//   +0x00..+0x64   CSWGuiPanel    panel
//   +0x64          undefined4 field1
//   +0x68          undefined4 field2
//   +0x6c          CSWGuiButton   end_button   (BTN_OK — "Annehmen")
//   +0x230         CSWGuiEditbox  name_editbox (the only editbox in vanilla)
//   +0x390         CSWGuiLabel    main_title_label
//   +0x4d0         CSWGuiLabel    subtitle_label
//   +0x610         CSWGuiButton   back_button  (BTN_CANCEL — "Abbrechen")
//   +0x7d4         CSWGuiButton   random_button ("Zufallsname")
//   ...
//
// `name_editbox` is at a fixed offset within the panel struct (not just in
// panel.controls[]), so the spec's findEditbox callback can index directly
// rather than walking children for the unique vtable.
constexpr uintptr_t kVtableCSWGuiNameChargen   = 0x00759F38;
constexpr size_t    kNameChargenEditboxOffset  = 0x230;
constexpr size_t    kNameChargenEndButtonOffset = 0x6c;

// CSWGuiNameChargen carries a `main_title_label` ("CHARAKTERAUSWAHL") and a
// `subtitle_label` ("Name") at distinct fixed offsets. The first one is the
// stale parent-flow header that BioWare reuses across all chargen sub-
// panels; the second is the screen-specific title. Our title-walk picks
// the first announceable label by panel-controls index, which lands on
// main_title_label first — wrong for any user trying to know which step
// they're on. The editbox spec's titleOverride reads subtitle_label
// directly via this offset to substitute the correct title speech.
constexpr size_t    kNameChargenSubtitleLabelOffset = 0x4d0;

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
constexpr uintptr_t kVtableCSWGuiClassSelection      = 0x00758020;
constexpr size_t    kClassSelectionsArrayOffset      = 0x6c;
constexpr size_t    kClassSelCharSize                = 0x25c;
constexpr int       kClassSelectionsCount            = 6;
constexpr size_t    kClassSelectionClassLabelOffset  = 0x1254;

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
constexpr uintptr_t kVtableCSWGuiAbilitiesCharGen          = 0x00759c68;
constexpr size_t    kAbilitiesCharGenLabelsArrayOffset     = 0x110c;
constexpr size_t    kAbilitiesCharGenButtonsArrayOffset    = 0x188c;
constexpr size_t    kAbilitiesCharGenSelectedAbilityOffset = 0x3dec;
constexpr int       kAbilitiesCharGenAbilityCount          = 6;
constexpr size_t    kCSWGuiLabelSize                       = 0x140;
constexpr size_t    kCSWGuiButtonSize                      = 0x1c4;

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
constexpr uintptr_t kAddrCSWGuiAbilitiesCharGenGetCost = 0x006f6bb0;

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
constexpr uintptr_t kVtableCSWGuiSkillsCharGen           = 0x00759990;
constexpr size_t    kSkillsCharGenLabelsArrayOffset      = 0xfcc;
constexpr size_t    kSkillsCharGenButtonsArrayOffset     = 0x19cc;
constexpr size_t    kSkillsCharGenSelectedSkillOffset    = 0x49bc;
constexpr int       kSkillsCharGenSkillCount             = 8;
constexpr size_t    kSkillsCharGenRemainingValueOffset   = 0x70c;
constexpr size_t    kSkillsCharGenCostValueOffset        = 0xc0c;

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
constexpr uintptr_t kAddrCSWGuiSkillsCharGenIsClassSkill = 0x006f4b60;

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
constexpr uintptr_t kAddrCSWGuiSkillsCharGenOnEnterPointsButton = 0x006f4bf0;

// description_list_box offset within CSWGuiSkillsCharGen (per SARIF).
constexpr size_t    kSkillsCharGenDescriptionListBoxOffset      = 0x6c;

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
constexpr size_t    kAbilitiesCharGenRemainingValueOffset = 0x70c;
constexpr size_t    kAbilitiesCharGenCostValueOffset      = 0xc0c;
constexpr size_t    kAbilitiesCharGenModifierValueOffset  = 0xe8c;

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
constexpr uintptr_t kVtableCSWGuiFeatsCharGen           = 0x007598b0;
constexpr size_t    kFeatsCharGenNameLabelOffset        = 0xbac;
constexpr size_t    kFeatsCharGenSelectButtonOffset     = 0x1238;
constexpr size_t    kFeatsCharGenFeatsListBoxOffset     = 0x13fc;
constexpr size_t    kFeatsCharGenDescriptionListBoxOffset = 0x16dc;

// Four parallel feat lists tracked on the panel — each a
// CExoArrayList<ushort> { ushort* data, int size, int capacity }
// inline-stored 12 bytes apart. Together they partition every feat the
// panel cares about so DetermineFeat can return a status byte:
//
//   field19 @ +0x19bc  data; @ +0x19c0 size  — existing  (creature already has)
//   field20 @ +0x19c8  data; @ +0x19cc size  — granted   (auto-given this level)
//   field23 @ +0x19d4  data; @ +0x19d8 size  — available (BuildAvailableList output)
//   field26 @ +0x19e0  data; @ +0x19e4 size  — chosen    (picked this session)
constexpr size_t    kFeatsCharGenExistingListDataOffset    = 0x19bc;
constexpr size_t    kFeatsCharGenExistingListSizeOffset    = 0x19c0;
constexpr size_t    kFeatsCharGenGrantedListDataOffset     = 0x19c8;
constexpr size_t    kFeatsCharGenGrantedListSizeOffset     = 0x19cc;
constexpr size_t    kFeatsCharGenAvailableListDataOffset   = 0x19d4;
constexpr size_t    kFeatsCharGenAvailableListSizeOffset   = 0x19d8;
constexpr size_t    kFeatsCharGenChosenListDataOffset      = 0x19e0;
constexpr size_t    kFeatsCharGenChosenListSizeOffset      = 0x19e4;

// CSWGuiSkillFlowChart embedded at +0x1a08 (struct size 0x10). It's the
// 2D scrollable feat-tree grid: a CExoArrayList-shaped header + a
// (selected_col, selected_row) pair packed into the trailing bytes.
//
//   chart +0x00  CSWGuiSkillFlow** rows_data
//   chart +0x04  int               rows_size
//   chart +0x08  int               rows_capacity
//   chart +0x0c  byte              selected_col   (0..2 in BuildButtons)
//   chart +0x0d  byte              selected_row
constexpr size_t    kFeatsCharGenChartOffset               = 0x1a08;
constexpr size_t    kSkillFlowChartRowsDataOffset          = 0x0;
constexpr size_t    kSkillFlowChartRowsSizeOffset          = 0x4;
constexpr size_t    kSkillFlowChartSelectedColOffset       = 0xc;
constexpr size_t    kSkillFlowChartSelectedRowOffset       = 0xd;

// CSWGuiSkillFlow row (1148 bytes). Three CSWGuiFlowSkillStruct columns
// at +0x5c, +0x184, +0x2ac (stride 0x128) plus 2 connector-line images
// at +0x3d4 / +0x428 the renderer uses to draw progression arrows.
constexpr size_t    kSkillFlowFirstColumnOffset            = 0x5c;
constexpr size_t    kSkillFlowColumnStride                 = 0x128;
constexpr int       kSkillFlowColumnsPerRow                = 3;

// Within a CSWGuiFlowSkillStruct (a single chart cell, 0x128 bytes):
//   +0x11c  ulong  feat ID  (or 0xffffffff for an empty cell)
//   +0x120  ulong  status   (0 avail, 1 chosen-this-level, 2 granted,
//                            3 existing, 4 locked — same enum DetermineFeat
//                            returns)
//   +0x124  ulong  selection bits (bit 0 = currently selected)
constexpr size_t    kFlowSkillStructFeatIdOffset           = 0x11c;
constexpr size_t    kFlowSkillStructStatusOffset           = 0x120;
constexpr unsigned  kFlowSkillStructEmptyFeatId            = 0xffffffff;

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
constexpr uintptr_t kAddrRulesGlobal              = 0x007a3a28;
constexpr size_t    kRulesFeatsArrayOffset        = 0x90;
constexpr size_t    kRulesFeatCountOffset         = 0xa4;
constexpr size_t    kFeatStructSize               = 0x48;
constexpr size_t    kFeatNameStrRefOffset         = 0x08;

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
constexpr uintptr_t kAddrCSWGuiFeatsCharGenOnEnterFeat   = 0x006f2fb0;

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
constexpr uintptr_t kAddrCSWGuiFeatsCharGenOnFeatPicked  = 0x006f3c20;

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
constexpr uintptr_t kAddrCSWGuiPowersLevelUpOnEnterPower    = 0x006f1460;

// Signature per Lane's SARIF (FUNCTIONS entry @0x006f2030):
//   void __thiscall OnPowerPicked(ulong powerId)
// Mirror of CSWGuiFeatsCharGen::OnFeatPicked — the canonical "user clicked
// BTN_SELECT" entry. Dispatches DeterminePower → AddChosenPower /
// RemoveChosenPower / can't-change message box. Calling directly with the
// focused cell's powerId lets us bypass the click-sim on Hinzuf. Macht.
constexpr uintptr_t kAddrCSWGuiPowersLevelUpOnPowerPicked   = 0x006f2030;

// Offset of the embedded CSWGuiSkillFlowChart inside CSWGuiPowersLevelUp.
// Matches struct field33_0x19fc (swkotor.exe.h:16637). We call
// CSWGuiSkillFlowChart::SetSelectedSkill on this offset to keep the chart's
// render-side highlight in sync with our keyboard focus (same pattern as
// chargen_feats — see kFeatsCharGenChartOffset).
constexpr size_t    kPowersLevelUpChartOffset              = 0x19fc;

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
constexpr uintptr_t kAddrCSWGuiSkillFlowChartSetSelectedSkill = 0x006cdc00;

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

// CSWGuiControl tooltip fields (verified against the
// CSWGuiControl::DisplayToolTip @ 0x418a90 decompile + struct definition in
// swkotor.exe.h:5238). Resolution order the engine uses:
//   * If field4_0x24 (tooltip_strref) is non-zero → CTlkTable::GetSimpleString
//   * Else if tooltip_string at +0x28 is non-empty → use literal CExoString
//   * Else if parent_control at +0x14 is non-null → recurse into parent
//   * Else no tooltip
// (An optional " : KeyName" suffix gated on field6_0x30 / keybind action id —
// we skip this in keyboard nav; the user already knows which key they pressed.)
constexpr size_t kControlParentOffset       = 0x14;  // CSWGuiControl* parent
constexpr size_t kControlTooltipStrRefOffset = 0x24; // uint32 strref (0 = none)
constexpr size_t kControlTooltipStringOffset = 0x28; // CExoString literal

// CSWGuiControl.id is the .gui-time numeric ID assigned by the layout file.
// Stable across localizations and panel.controls reordering, so this is the
// canonical way to address a known child of a known panel kind.
constexpr size_t kControlIdOffset = 0x50;  // int id

// Engine sentinel for "no object" in AI-queue object-id slots. Distinct
// from CGameObjectArray's removed-handle sentinel (0xFFFFFFFF).
constexpr uint32_t kInvalidObjectId = 0x7F000000u;

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
constexpr size_t kSaveLoadEntrySaveNumberOffset    = 0x1c8;
constexpr size_t kSaveLoadEntrySaveGameNameOffset  = 0x1d8;
constexpr size_t kSaveLoadEntryAreaNameOffset      = 0x1e8;
constexpr size_t kSaveLoadEntryLastModuleOffset    = 0x1f0;

// CExoArrayList<T*> — engine container for arrays of pointers.
struct CExoArrayList {
    void** data;
    int    size;
    int    capacity;
};

// Vector — Aurora-engine 3D vector. Used for world position, object
// orientation (heading vector with z=0), audio listener pose, etc.
//
// Coordinate frame (investigation Q1 — right-handed, Z-up):
//   +X = east, +Y = north, +Z = up. 1.0 unit ≈ 1 metre.
//   Bearing convention for object orientation: 0° = +X = east; CCW positive.
struct Vector {
    float x;
    float y;
    float z;
};

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
struct CExoString {
    char*    c_string;
    uint32_t length;
};
typedef CExoString* (__thiscall* PFN_GetSimpleString)(void* this_,
                                                      CExoString* out,
                                                      uint32_t strref);
constexpr uintptr_t kAddrGetSimpleString = 0x0041e8f0;
constexpr uintptr_t kAddrTlkTablePtr     = 0x007a3a08;

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
constexpr uintptr_t kAddrCSWGuiUpgradeOnEnterSlot      = 0x006c3c30;
constexpr uintptr_t kAddrCSWGuiUpgradeOnSlotSelected   = 0x006c6500;
constexpr uintptr_t kAddrCSWGuiUpgradeOnUpgradeSelected = 0x006c5510;
constexpr uintptr_t kAddrCSWGuiUpgradeOnAssemble       = 0x006c6190;

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
constexpr uintptr_t kAddrUpgradeSlotTypeTable = 0x00756fb0;
constexpr size_t    kUpgradeSlotTypeStride    = 12;
constexpr size_t    kUpgradeSlotTypeStrRefOff = 8;
constexpr size_t    kUpgradePanelCategoryOff  = 0x2f4c;  // panel.field25
constexpr size_t    kUpgradeSlotCustomValueOff = 0x58;   // slot_btn.custom_value

// CSWGuiUpgrade.field35_0x2f74 — array of installed-mod CSWSItem* indexed by
// the slot button's custom_value. Non-null = slot occupied (the engine
// constructs a CSWSItem and LoadFromTemplate's the mod into this slot when the
// base item already carries that upgrade — bitmask at field27+0x294 — see
// OnPanelAdded @0x006c4d70); null = empty. Both OnEnterSlot @0x006c3c30 (saber
// branch) and OnSlotSelected @0x006c6500 (install/remove branch) index this
// array by custom_value, so it is the authoritative per-slot occupancy field.
constexpr size_t    kUpgradeSlotInstalledItemsOff = 0x2f74;  // panel.field35

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

constexpr size_t kCreatureCombatRoundOffset           = 0x9c8;
constexpr size_t kObjectHitPointsOffset               = 0xe0;
constexpr size_t kObjectEffectsOffset                 = 0x124;

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
constexpr size_t kObjectActionNodesOffset             = 0xfc;
constexpr size_t kExoLinkedListInternalCountOffset    = 0x8;

constexpr size_t kCombatRoundAttacksListOffset        = 0x4;
constexpr size_t kCombatRoundTimerOffset              = 0x944;
constexpr size_t kCombatRoundLengthOffset             = 0x94c;
constexpr size_t kCombatRoundCurrentAttackOffset      = 0x96c;
constexpr size_t kCombatRoundActionsOffset            = 0x9b0;
constexpr size_t kCombatRoundEngagedOffset            = 0x9b8;
constexpr size_t kCombatRoundCurrentActionOffset      = 0x9d0;

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
constexpr size_t kListInternalOffset       = 0x0;  // CExoLinkedList<T>     +0 -> internal*
constexpr size_t kListInternalHeadOffset   = 0x0;  // CExoLinkedListInternal+0 -> head node*
constexpr size_t kListInternalCountOffset  = 0x8;  // CExoLinkedListInternal+8 -> count (engine authoritative)
constexpr size_t kLinkedListNodeNextOff    = 0x4;  // CExoLinkedListNode    +4 -> next
constexpr size_t kLinkedListNodeDataOff    = 0x8;  // CExoLinkedListNode    +8 -> data

// Legacy name kept so existing call sites compile. Same value/meaning
// as kListInternalOffset (the offset from CExoLinkedList to its
// internal pointer).
constexpr size_t kLinkedListHeadOffset     = kListInternalOffset;

constexpr size_t kCombatRoundActionTypeOffset       = 0x10;
constexpr size_t kCombatRoundActionTargetOffset     = 0x14;
constexpr size_t kCombatRoundActionRetargetOffset   = 0x18;
constexpr size_t kCombatRoundActionMoveToPosOffset  = 0x38;
constexpr size_t kCombatRoundActionResultOffset     = 0x7c;
constexpr size_t kCombatRoundActionDamageOffset     = 0x80;

// Inferred action_type byte mapping. The engine's AddX adders on
// CSWSCombatRound are declared in this order; matches typical enum-by-
// declaration patterns. Validate via DumpBytes on each AddX path.
constexpr unsigned char kActionTypeAttack          = 0;
constexpr unsigned char kActionTypeSpellCast       = 1;
constexpr unsigned char kActionTypeItemCast        = 2;
constexpr unsigned char kActionTypeEquip           = 3;
constexpr unsigned char kActionTypeUnequip         = 4;
constexpr unsigned char kActionTypeMove            = 5;
constexpr unsigned char kActionTypeUseTalent       = 6;
constexpr unsigned char kActionTypeHeal            = 7;
constexpr unsigned char kActionTypeCutscene        = 8;

// Server-side combat-mode global. Read via accessor for safety; the
// CClientExoApp facade is 8 bytes (vtable + internal), and the actual
// flag lives on the internal struct.
constexpr uintptr_t kAddrGetCombatMode                = 0x005ede70;
constexpr uintptr_t kAddrGetPausedByCombat            = 0x005edc10;

// CSWSCreature engine getters — Phase 2A snapshot path.
constexpr uintptr_t kAddrCSWSCreatureGetMaxHitPoints  = 0x004ed310;
constexpr uintptr_t kAddrCSWSCreatureGetArmorClass    = 0x004ed1d0;
constexpr uintptr_t kAddrCSWSCreatureGetMaxForcePoints = 0x004fd490;
constexpr uintptr_t kAddrCSWSCreatureGetDead          = 0x004ef820;
constexpr uintptr_t kAddrCSWSObjectGetCurrentHitPoints = 0x004caec0;

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
constexpr uintptr_t kAddrCSWSObjectGetDamageLevel     = 0x004cb020;

// CSWSCreatureStats::GetLevel @0x005a5fd0 — `int __thiscall(this, int subNegLevels)`.
// Sums level over each entry in CSWSCreatureStats.classes[2]. param_1=0
// → raw total (don't subtract negative levels from drain effects);
// param_1=1 → effective level. Use 0 for the displayed level.
constexpr uintptr_t kAddrCSWSCreatureStatsGetLevel    = 0x005a5fd0;

// CSWSCreature::GetInvisible @0x00501950 / GetBlind @0x004ee210 — bool
// __thiscall(this). Direct flag accessors; safe to call from manual
// paths. We only emit a row when the flag is set (no need to announce
// "not invisible").
constexpr uintptr_t kAddrCSWSCreatureGetInvisible     = 0x00501950;
constexpr uintptr_t kAddrCSWSCreatureGetBlind         = 0x004ee210;

// CSWSCreatureStats getters — saves + attribute scores. CSWSCreatureStats
// lives at CSWSCreature +0xa74 (kCreatureStatsPtrOffset).
constexpr uintptr_t kAddrStatsGetSTR                  = 0x005a6190;
constexpr uintptr_t kAddrStatsGetDEX                  = 0x005a61a0;  // tentative — adjacent slots
constexpr uintptr_t kAddrStatsGetCON                  = 0x005a61b0;
constexpr uintptr_t kAddrStatsGetINT                  = 0x005a61c0;
constexpr uintptr_t kAddrStatsGetWIS                  = 0x005a61d0;
constexpr uintptr_t kAddrStatsGetCHA                  = 0x005a61e0;
constexpr uintptr_t kAddrStatsGetFortSave             = 0x005ab810;
constexpr uintptr_t kAddrStatsGetWillSave             = 0x005ab880;
constexpr uintptr_t kAddrStatsGetReflexSave           = 0x005ab8f0;
constexpr uintptr_t kAddrStatsGetSimpleAlignmentGoodEvil = 0x005a5110;

// CSWSCreatureStats inline attribute-total bytes (post-mod totals). Read
// these directly to avoid relying on the GetXStat dispatch table (some of
// the addresses above are tentative — adjacent-symbol guesses pending
// SARIF confirmation). Field offsets per swkotor.exe.h (CSWCCreatureStats
// has the same layout as CSWSCreatureStats at the byte level for these
// fields per `accessibility-investigation.md`).
constexpr size_t kStatsAttrTotalsOffset               = 0x34;  // 6 bytes: STR/DEX/CON/INT/WIS/CHA

// CSWSCreatureStats.faction_id @+0x78 (ushort) — the creature's standard
// faction. Per swkotor.exe.h `standardFactions` enum: HOSTILE_1=1,
// FRIENDLY_1=2, HOSTILE_2=3, FRIENDLY_2=4, NEUTRAL=5, INSANE=6,
// PTAT_TUSKAN=7, GLB_XOR=8, SURRENDER_1=9, SURRENDER_2=10, PREDATOR=11,
// PREY=12, TRAP=13, ENDAR_SPIRE=14, RANCOR=15, GIZKA_1=16, GIZKA_2=17,
// INVALID_FACTION=0xFFFF. The player + party share PLAYER (commonly
// faction id 0, not in the enum). Direct field read — no engine call,
// safe for auto-firing paths.
constexpr size_t kStatsFactionIdOffset                = 0x78;

// CSWSCreature::GetFaction → CSWSFaction*. Reserved for the future if we
// need to query the dynamic reputation table (custom mod factions
// outside the standard enum). The direct faction_id field-read above
// covers the typical hostile/friendly/neutral classification.
constexpr uintptr_t kAddrCSWSCreatureGetFaction       = 0x00513fc0;

// Rules global pointer used for feat lookup — see kAddrRulesGlobal
// definition higher up in this file (line ~526). Dereferences to a
// CSWSRules*; CSWRules is at offset 0 (the `internal` member), so the
// same pointer is usable for both as the `this` for CSWRules::GetFeat.

// CSWRules::GetFeat — __thiscall(ushort feat_index) -> CSWFeat*.
// Returns nullptr if index out-of-range or feat not loaded (bit_flags
// & 0x10 unset). BYTES_PURGED=4.
constexpr uintptr_t kAddrCSWRulesGetFeat              = 0x00550c00;

// CSWFeat::GetNameText — __thiscall(CExoString* out) -> CExoString*.
// Fetches localized feat name via CTlkTable::Fetch using the feat's
// `field2_0x8` strref. Constructs the out CExoString in place; caller
// must read .c_string before destruct (we deliberately leak the heap
// string, same pattern as CSWSItem::GetPropertyDescription).
constexpr uintptr_t kAddrCSWFeatGetNameText           = 0x005cd760;

// CSWFeat::GetDescriptionText — __thiscall(CExoString* out) -> CExoString*.
// Sibling of GetNameText. Resolves the feat's `description` strref at
// +0x0c through CTlkTable; same heap-leak rule applies.
constexpr uintptr_t kAddrCSWFeatGetDescriptionText    = 0x005cd800;

// CSWRules.spells — the spells array. CSWSpellArray* at offset 0x8c
// (140 bytes) per SARIF layout dump. The array exposes GetSpell(id) ->
// CSWSpell*. Used by combat::queue to decode action_type=9 (Cast Force
// Power) queue entries to their specific spell name.
constexpr size_t    kRulesSpellsOffset                = 0x8c;

// CSWSpellArray::GetSpell — __thiscall(int spell_id) -> CSWSpell* (cast
// as int in the Ghidra signature). Returns nullptr / 0 if spell_id is
// out of range. BYTES_PURGED=4.
constexpr uintptr_t kAddrCSWSpellArrayGetSpell        = 0x0059b6d0;

// CSWSpell::GetSpellNameText — __thiscall(CExoString* out) -> CExoString*.
// Same shape as CSWFeat::GetNameText: constructs the localized name into
// the out string in place; caller must read .c_string before any
// destructor runs (we leak — CRT mismatch otherwise). BYTES_PURGED=4.
constexpr uintptr_t kAddrCSWSpellGetSpellNameText     = 0x0059b940;

// CSWSpell.spell_description — int (TLK strref) at +0x0c per SARIF
// DATATYPE dump. CSWSpell has no GetSpellDescriptionText accessor, so
// callers read the strref and route through LookupTlk themselves.
constexpr size_t    kSpellDescriptionStrRefOffset     = 0x0c;

// CSWSCombatRoundAction additional offsets (decoded from GetActionIcon
// @0x686fb0 — case 0xb/0xc switch). The action_type byte at +0x10
// selects which of these is meaningful:
//   action_type=9  → spell_id at +0x24    (CSWSpellArray::GetSpell)
//   action_type=10 → item_handle at +0x64 (CServerExoApp::GetItemByGameObjectID)
//   action_type=11 → feat_id at +0x5c     (CSWRules::GetFeat)
constexpr size_t    kCombatRoundActionSpellIdOffset   = 0x24;
constexpr size_t    kCombatRoundActionItemHandleOff   = 0x64;
constexpr size_t    kCombatRoundActionFeatIdOffset    = 0x5c;

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
constexpr size_t    kGameEffectTypeOffset             = 0x8;

// CSWSCreature.inventory @+0xa2c → CSWInventory*. Server-side equipment
// container. Combined with CSWInventory::GetItemInSlot below this gives
// us "what is the creature wielding right now".
constexpr size_t    kCreatureInventoryOffset          = 0xa2c;

// CSWInventory equipped-slot field layout (validated via Lane's symbol
// table 12715: STRUCTURE CSWInventory SIZE=0x4c). Each slot is a ulong
// game-object handle (NOT a pointer) — resolved via the universal
// CClientExoApp::GetObjectName accessor. Initial attempt routed through
// CSWInventory::GetItemInSlot which returns a small CSWItem* wrapper
// (size 0x10) — the wrong shape for the localized_name @+0x280 chain;
// reading the handle directly bypasses that confusion.
constexpr size_t    kInventoryRightWeaponHandleOffset = 0x14;  // main hand
constexpr size_t    kInventoryLeftWeaponHandleOffset  = 0x18;  // off hand
constexpr size_t    kInventoryHeadHandleOffset        = 0x4;
constexpr size_t    kInventoryTorsoHandleOffset       = 0x8;
constexpr size_t    kInventoryHandsHandleOffset       = 0x10;
constexpr size_t    kInventoryLeftArmHandleOffset     = 0x20;
constexpr size_t    kInventoryRightArmHandleOffset    = 0x24;
constexpr size_t    kInventoryImplantHandleOffset     = 0x28;
constexpr size_t    kInventoryBeltHandleOffset        = 0x2c;

// CSWGuiInGameEquip — cached per-slot item handles and stat-value labels.
// The panel mirrors the displayed character's CSWInventory into local
// fields, and OnSwitchLeft/Right repopulates them on party-cycle, so these
// always match what's on screen regardless of which companion is shown.
// IDs are the same handle space as CClientExoApp::GetObjectName (the
// universal accessor routes both client and server handles), so
// GetObjectDisplayNameByHandle resolves them without translation.
// Offsets verified against Lane's CSWGuiInGameEquip struct (SIZE=0x42bc).
constexpr size_t    kEquipPanelPlayerCreatureOffset    = 0x0064;
constexpr size_t    kEquipPanelHeadIdOffset            = 0x4284;
constexpr size_t    kEquipPanelImplantIdOffset         = 0x4298;
constexpr size_t    kEquipPanelArmorIdOffset           = 0x4290;  // body
constexpr size_t    kEquipPanelLeftArmbandIdOffset     = 0x4288;
constexpr size_t    kEquipPanelRightArmbandIdOffset    = 0x428c;
constexpr size_t    kEquipPanelLeftWeaponIdOffset      = 0x427c;
constexpr size_t    kEquipPanelRightWeaponIdOffset     = 0x4280;
constexpr size_t    kEquipPanelGlovesIdOffset          = 0x4294;  // hands
constexpr size_t    kEquipPanelBeltIdOffset            = 0x429c;

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
constexpr size_t    kEquipPanelDefenseLabelOffset            = 0x2098;
constexpr size_t    kEquipPanelHpLabelOffset                 = 0x21d8;
constexpr size_t    kEquipPanelLeftWeaponDamageLabelOffset   = 0x1b98;  // Lane: left_weapon_attack_label
constexpr size_t    kEquipPanelLeftWeaponTohitLabelOffset    = 0x1cd8;
constexpr size_t    kEquipPanelRightWeaponDamageLabelOffset  = 0x1e18;  // Lane: right_weapon_attack_label
constexpr size_t    kEquipPanelRightWeaponTohitLabelOffset   = 0x1f58;

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
constexpr size_t    kEquipPanelBackButtonOffset           = 0x385C;
constexpr size_t    kEquipPanelChangeParty1ButtonOffset   = 0x3A20;
constexpr size_t    kEquipPanelChangeParty2ButtonOffset   = 0x3BE4;
constexpr size_t    kEquipPanelCharacterLeftButtonOffset  = 0x3DA8;
constexpr size_t    kEquipPanelCharacterRightButtonOffset = 0x3F6C;

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
constexpr size_t    kLevelUpButtonBackOffset              = 0x1944;
constexpr size_t    kLevelUpButtonCancelOffset            = 0x1B08;

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
constexpr size_t    kAbilitiesSkillRankLabelOffset   = 0x2190;  // (8592)  "Fähigkeitenrang"
constexpr size_t    kAbilitiesRankValueLabelOffset   = 0x22D0;  // (8912)  e.g. "8"
constexpr size_t    kAbilitiesBonusLabelOffset       = 0x2410;  // (9232)  "Bonus"
constexpr size_t    kAbilitiesBonusValueLabelOffset  = 0x2550;  // (9552)  e.g. "+3"
constexpr size_t    kAbilitiesTotalLabelOffset       = 0x2690;  // (9872)  "Gesamtrang"
constexpr size_t    kAbilitiesTotalValueLabelOffset  = 0x27D0;  // (10192) e.g. "11"
constexpr size_t    kAbilitiesNameLabelOffset        = 0x2910;  // (10512) selected entry name
constexpr size_t    kAbilitiesFeatsButtonOffset      = 0x2B90;  // (11152) BTN_FEATS  (Talente)
constexpr size_t    kAbilitiesPowersButtonOffset     = 0x2D54;  // (11604) BTN_POWERS (Kräfte)
constexpr size_t    kAbilitiesSkillsButtonOffset     = 0x2F18;  // (12056) BTN_SKILLS (Fähigkeiten)
constexpr size_t    kAbilitiesListBoxOffset          = 0x30DC;  // (12508) LB_ABILITY (main list)
constexpr size_t    kAbilitiesDescListBoxOffset      = 0x33BC;  // (13244) LB_DESC (description)

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
constexpr uintptr_t kAddrAbilitiesOnEnterSkill        = 0x006ad180;  // (this, CSWGuiControl* row)
constexpr uintptr_t kAddrAbilitiesOnEnterFeat         = 0x006ad410;  // (this, ushort featId)
constexpr uintptr_t kAddrAbilitiesOnEnterPower        = 0x006acce0;  // (this, int) — crashes when powers empty
// OnAbilitySelectionChanged is the engine's mouse-driven selection handler
// (hit-tests cursor vs the CSWGuiSkillFlow chart). Kept for reference; do NOT
// call it for keyboard nav.
constexpr uintptr_t kAddrAbilitiesOnAbilitySelChanged = 0x006ad4b0;  // (this, int) mouse-driven
constexpr uintptr_t kAddrAbilitiesUpdateView          = 0x006ad560;  // void(void)
constexpr uintptr_t kAddrAbilitiesOnSkillsButton      = 0x006adad0;  // void(void) — field139=0 + UpdateView
constexpr uintptr_t kAddrAbilitiesOnFeatsButton       = 0x006ada70;  // void(void) — field139=2 + UpdateView
constexpr uintptr_t kAddrAbilitiesOnPowersButton      = 0x006adaa0;  // void(void) — field139=1 + UpdateView
// DisplayPowers() — predicate: returns 1 iff the character is a Jedi AND the
// powers chart has rows. Used to decide whether the Powers tab exists (the
// engine's own tab cycle uses it to skip an empty Powers tab). Pure check.
constexpr uintptr_t kAddrAbilitiesDisplayPowers       = 0x006abe70;  // int(void)

// CSWGuiInGameAbilities::HandleInputEvent(this, int code, int val). The panel's
// own input handler; code 0x29 runs the engine's smart tab cycle
// (Skills -> Powers-if-any -> Feats -> Skills, auto-skipping an empty Powers
// tab). These are panel-internal codes, distinct from the manager kInput* codes.
constexpr uintptr_t kAddrAbilitiesHandleInputEvent    = 0x006ae5f0;
constexpr int       kAbilitiesPanelCodeCycleTab       = 0x29;
// Chart-nav codes consumed by HandleInputEvent on the Feats/Powers tabs and
// routed to CSWGuiSkillFlowChart::HandleInput (@0x006cdd80): 0x31/0x32 step the
// feat-chain rows (up/down, skipping empty cells) and trigger OnEnterFeat/
// OnEnterPower to repaint the detail + description. 0x2f/0x30 step columns
// (tiers within a chain) — not wired yet (Left/Right are tab-switch).
constexpr int       kAbilitiesPanelCodeChartUp        = 0x31;
constexpr int       kAbilitiesPanelCodeChartDown      = 0x32;

// The two CSWGuiSkillFlowChart members on the panel (field30/field31), and the
// chart's own cursor fields. We read row vs row-count to clamp the engine's
// chart nav, which otherwise WRAPS top<->bottom (unlike the skills listbox,
// which clamps). field_0xd = current row, field1_0x4 = row count.
constexpr size_t    kAbilitiesPowersChartOffset       = 0x3f78;  // field30 (Powers)
constexpr size_t    kAbilitiesFeatsChartOffset        = 0x3f88;  // field31 (Feats)
constexpr size_t    kSkillFlowChartRowOffset          = 0x0d;    // field_0xd
constexpr size_t    kSkillFlowChartRowCountOffset     = 0x04;    // field1_0x4

// CGuiInGame.field139_0xbc0 — the active abilities tab: 0 = Skills,
// 1 = Powers, 2 = Feats. Read to route per-tab input + announce the tab.
constexpr size_t    kGuiInGameAbilitiesTabOffset      = 0xbc0;

// CSWSCreatureStats.feats @+0x0 — CExoArrayList<ushort>. Count lives
// at +0x4 (size field of the list). Static feat list (granted at level-
// up + class); doesn't drift mid-combat. Used by the Ö examine view to communicate
// "this creature has N feats" without enumerating them.
constexpr size_t    kStatsFeatsListOffset             = 0x0;

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
constexpr uintptr_t kAddrCGuiInGameShowExamineBox     = 0x0062d3e0;
constexpr uintptr_t kAddrCGuiInGameHideExamineBox     = 0x0062d440;

// CSWGuiExamine.message_box.listbox_message lives at panel +0x67c. Kept
// for the kExamineSpec ListBoxPanelSpec entry — if the engine itself ever
// pops the generic message box (e.g. via store "can't afford" or other
// confirmation popups), the spec handles row navigation. We just don't
// open it ourselves for creature examine — that would land on an empty
// TLK-lookup result.
constexpr size_t    kExaminePanelListBoxOffset        = 0x67c;
constexpr size_t    kExaminePanelHandleOffset         = 0x984;

// CClientExoApp::GetObjectName — universal display-name accessor.
// __thiscall(ulong handle, CExoString* outName) -> int. BYTES_PURGED=8
// (verified 2026-05-10 from Lane's symbol table). Returns a localized
// display name for any object kind, falling through the engine's own
// name-resolution chain (template FirstName / appearance.2da
// displayname / racialtypes.2da name / tag). Use this in preference to
// engine_area::GetObjectName when working from a handle (queue targets,
// LastTarget) — the latter falls back to the modder-assigned tag for
// generic enemies whose `first_name` strref is empty.
constexpr uintptr_t kAddrCClientExoAppGetObjectName   = 0x005ed350;

// CSWGuiInGameMessages — combat log + dialog history panel.
//   panel        @+0x0
//   messages_lb  @+0x64    (combat-feedback log)
//   dialog_lb    @+0x344   (dialog history)
//   show_button  @+0x76c   (toggles between feedback / dialog view)
//   exit_button  @+0x930
constexpr size_t kInGameMessagesMessagesListBoxOffset = 0x64;
constexpr size_t kInGameMessagesDialogListBoxOffset   = 0x344;
constexpr size_t kInGameMessagesShowButtonOffset      = 0x76c;
constexpr size_t kInGameMessagesExitButtonOffset      = 0x930;

// CSWGuiDialog (and Cinematic / ComputerCamera variants which share base
// layout):
//   panel             @+0x0
//   replies_listbox   @+0x19c4
//   message_label     @+0x1ca4
constexpr size_t kDialogRepliesListBoxOffset          = 0x19c4;
constexpr size_t kDialogMessageLabelOffset            = 0x1ca4;

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
constexpr size_t kServerObjectDialogOwnerOffset       = 0x54;

// CSWSCreature inline appearance cache at +0xa4c per Lane's struct, but
// VERIFIED LIVE 2026-05-30 to read 0 even for fully-initialised speakers
// (Carth, Larrim) — the engine doesn't populate this cache reliably. Use
// CSWSCreatureStats.appearance_type at stats+0x186 instead (real value).
// Kept here as a constant for future re-investigation, NOT used by
// dialog_speech.
constexpr size_t kCreatureAppearanceTypeOffset        = 0xa4c;

// CSWSCreature.creature_stats — pointer to CSWSCreatureStats.
constexpr size_t kCreatureStatsPointerOffset          = 0xa74;

// CSWSCreatureStats.race (ushort; enum RACE values: DROID=5, HUMAN=6).
// Diagnostic-only — the enum collapses every humanoid species (Twi'lek,
// Cathar, Echani, Mandalorian) to HUMAN, so we discriminate by
// appearance_type, not by race. Race is logged so future overrides can be
// designed on observed (race, appearance_type) pairs from the diagnostic.
constexpr size_t kCreatureStatsRaceOffset             = 0xdc;

// CSWSCreatureStats.appearance_type (ushort, indexes appearance.2da).
// Verified from Lane's exported header @0x186 (line 15707 in swkotor.exe.h).
// THIS is the authoritative species discriminator — the CSWSCreature inline
// cache at +0xa4c is unreliable.
constexpr size_t kCreatureStatsAppearanceTypeOffset   = 0x186;

// CSWGuiDialogComputer adds a terminal-output listbox above the embedded
// replies listbox.
//   message_listbox  @+0x2cfc   (terminal output text)
//   obscure_label    @+0x34dc
constexpr size_t kDialogComputerMessageListBoxOffset  = 0x2cfc;

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
constexpr size_t kCGuiInGameDialogSpeakerOffset       = 0x170;

// CSWGuiBarkBubble.object_id @+0x1c0 — the bark speaker's CLIENT object id,
// written by CSWGuiBarkBubble::SetBark @0x006a9920 (this->object_id = param_1)
// and consumed by ::Draw @0x006a9ce0 via CClientExoApp::GetGameObject(client,
// object_id) → AsSWCObject for the 6m proximity/cull test. Sentinel
// 0x7f000000 means "no owning creature" — system/loudspeaker barks (camera
// zone messages, area feedback). Resolve a real id through
// ClientToServerObjectId → ResolveServerObjectHandle to classify the speaker,
// exactly as the dialog-speaker path does for CGuiInGame +0x170.
constexpr size_t kBarkBubbleObjectIdOffset            = 0x1c0;

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
constexpr uintptr_t kVtableCSWGuiStore                     = 0x00756e38;
constexpr uintptr_t kVtableCSWGuiStoreItemEntry            = 0x00756850;

// CSWGuiInGameItemEntry — rows of CSWGuiInGameInventory.item_listbox AND
// Container's loot listbox. Same shape as CSWGuiStoreItemEntry: button at
// offset 0, item_game_object_id at +0x1c4. Resolves through
// ResolveItemFromClientHandle and reads stack_size via the same offsets
// above.
constexpr uintptr_t kVtableCSWGuiInGameItemEntry           = 0x007568f8;

constexpr size_t    kStoreShopItemsListBoxOffset           = 0x1480;
constexpr size_t    kStoreInvItemsListBoxOffset            = 0x1760;
constexpr size_t    kStoreDescriptionListBoxOffset         = 0x1a40;
constexpr size_t    kStoreCancelButtonOffset               = 0x1d20;
constexpr size_t    kStoreToggleButtonOffset               = 0x1ee4;  // examine_button in struct DB
constexpr size_t    kStoreAcceptButtonOffset               = 0x20a8;
constexpr size_t    kStoreItemIdOffset                     = 0x226c;
constexpr size_t    kStoreCostValueLabelOffset             = 0xbc0;
constexpr size_t    kStoreStockValueLabelOffset            = 0xe40;
constexpr size_t    kStoreCreditsValueLabelOffset          = 0x1200;

// CSWGuiStore.field31_0x2270 — int — the player's gold cached on the
// store struct. Written by PopulateStore via CSWSCreature::GetGold,
// updated again after every SellItem / BuyItem. OnControlStoreAButton
// reads this to decide whether the player can afford the trade:
//   if (field31_0x2270 < GetItemBuyValue(item)) ShowExamineBox(strref 0xa3de)
// We use it for the same gate, but speak our own "not enough credits"
// line instead of letting the engine pop its examine box.
constexpr size_t    kStorePlayerGoldOffset                 = 0x2270;

// Bit 1 (0x02) of the listbox's CSWGuiControl.bit_flags is set on the
// "visible" listbox by ShowBuyGUI / ShowSellGUI. Same offset (+0x44)
// every other CSWGuiControl uses.
constexpr size_t    kControlBitFlagsOffset                 = 0x44;
constexpr uint32_t  kStoreListBoxVisibleBit                = 0x2;
// The same bit_flags 0x02 is the general CSWGuiControl "shown" bit. The
// StatusSummary popup lays out one label per notification type and sets
// this bit only on the row(s) it actually displays — hidden template rows
// (still reading "<CUSTOM0>" placeholders) leave it clear, with stale float
// data in the adjacent flag fields (verified via PopupGeom dump 2026-06-03).
constexpr uint32_t  kControlVisibleBit                     = 0x2;

// CSWGuiStoreItemEntry.obj_id @ +0x1c4 — the client-side game-object
// handle for the row's CSWSItem. Resolve via ClientToServerObjectId then
// GetItemByGameObjectID.
constexpr size_t    kStoreItemEntryObjIdOffset             = 0x1c4;

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
constexpr size_t    kSwsItemStackSizeOffset                = 0x28c;
constexpr size_t    kSwsItemBitFlagsOffset                 = 0x288;
constexpr uint32_t  kSwsItemInfiniteStockBit               = 0x4;

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
constexpr size_t    kSwsItemChargesOffset                  = 0x258;
constexpr size_t    kSwsItemMaxChargesOffset               = 0x25c;

// CSWGuiStore::GetItemBuyValue / GetItemSellValue — __thiscall returning
// ulong, single CSWSItem* argument. Both pop 4 bytes (callee).
constexpr uintptr_t kAddrCSWGuiStoreGetItemBuyValue        = 0x006c0790;
constexpr uintptr_t kAddrCSWGuiStoreGetItemSellValue       = 0x006c07f0;

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
constexpr uintptr_t kAddrCSWGuiStoreOnControlInvAButton    = 0x006c0f40;
constexpr uintptr_t kAddrCSWGuiStoreOnControlStoreAButton  = 0x006c1130;

// CServerExoApp::ClientToServerObjectId — __thiscall(ulong) -> ulong.
// CServerExoApp::GetItemByGameObjectID — __thiscall(ulong) -> CSWSItem*.
constexpr uintptr_t kAddrServerExoAppClientToServerObjectId = 0x004aea30;
constexpr uintptr_t kAddrServerExoAppGetItemByGameObjectID  = 0x004ae760;

// CSWSItem::GetPropertyDescription — __thiscall(CExoString* out) -> CExoString*.
// Returns the full formatted property description block (the text that
// Inventory/Store/Equip render into their description listbox on hover):
// damage, feats, defence, on-hit, base description, etc. The caller passes
// uninitialised stack memory for `out`; the function constructs a CExoString
// in place by allocating a heap c_string. We then read out the c_string and
// deliberately leak the allocation rather than calling ~CExoString (heap
// ownership across the DLL/EXE boundary risks CRT mismatch; see the same
// pattern in LookupTlk above).
constexpr uintptr_t kAddrCSWSItemGetPropertyDescription     = 0x0055f340;

// AppManager indirection to CServerExoApp. AppManager+0x8 → CServerExoApp*.
// (Same constant as engine_player.h's kAppManagerServerOffsetPlayer.)
constexpr size_t    kAppManagerServerExoAppOffset          = 0x8;

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
constexpr size_t    kJournalQuestItemsButtonOffset         = 0x8a4;
constexpr size_t    kJournalSwapTextButtonOffset           = 0xa68;
constexpr size_t    kJournalSortButtonOffset               = 0xc2c;
constexpr size_t    kJournalExitButtonOffset               = 0xdf0;

// CSWGuiInGameJournal::PopulateItemListBox @0x00645330 — clears items_listbox
// and rebuilds one CSWGuiJournalItemEntry per quest in the current
// active/done + sort mode, then clears the journal's HasChanged flag (so the
// next Draw won't repopulate again).
constexpr uintptr_t kAddrJournalPopulateItemListBox        = 0x00645330;

// CSWGuiJournalItemEntry rows are CSWGuiButton-derived (size 0x1cc) with
// their own vtable. The journal's OnControlEntered fires on mouse hover
// over a row and rewrites item_description_label with the full text.
constexpr uintptr_t kVtableCSWGuiJournalItemEntry          = 0x007518c0;
