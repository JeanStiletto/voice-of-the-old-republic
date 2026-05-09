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

// ---------------------------------------------------------------------------
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
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
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
// ---------------------------------------------------------------------------
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
