// chargen "Fähigkeiten" panel (step 3 of Eigener
// Charakter) tweaks.
//
// Mirror of menus_chargen_attr.{h,cpp} for CSWGuiSkillsCharGen — the
// engine struct shape is the same: 8 skill_buttons + 8 skill_labels +
// 8 plus_buttons + 8 minus_buttons + a panel-level
// selected_skill_index that the engine's shared OnPlusButton /
// OnMinusButton handlers read to pick which skill changes. Same
// fixes apply:
//
//   1. Mirror chain focus → selected_skill_index so Left/Right
//      modifies the focused row instead of a default top row.
//   2. Capture skill_buttons[i] → skill_labels[i] text into the
//      cycle-category cache so FromControl announces "Computer, 0"
//      instead of bare "0".
//   3. Compensate the cursor-warp Y by one row pitch so the engine's
//      hit-test resolves to the right row (same shift Options' tab
//      cluster needs g_tabClickOffsetY for, and that the Attribute
//      panel needs RowPitchForCursorWarp for).
//
// Differences from the Attribute panel:
//
//   * 8 skills instead of 6 abilities; skill order is straight
//     top-to-bottom (no struct vs visual swap).
//   * Skills don't have a D&D modifier — only rank + cost-per-+1.
//   * Cost is constant per skill (1 for class, 2 for cross-class)
//     throughout the row's normal range. We compute it ourselves via
//     the engine's CSWGuiSkillsCharGen::IsClassSkill predicate
//     instead of reading the cost_value label (same hit-test /
//     refresh-timing race we fought through on the Attribute panel),
//     and the value-change announce never repeats it (the user heard
//     it in the chain-step suffix).

#pragma once

namespace acc::menus::chargen_skills {

// True iff `panel`'s vtable matches CSWGuiSkillsCharGen.
bool IsChargenSkillsPanel(void* panel);

// Index 0..7 in struct order if `control` is one of the panel's
// skill_buttons[]; -1 otherwise. Struct order is Computer,
// Demolitions, Stealth, Awareness, Persuade, Repair, Security,
// Treat Injury — selected_skill_index uses the same order.
int SkillIndexFromButton(void* panel, void* control);

// Mirror chain focus into the panel's selected_skill_index so the
// next +/- press routes OnPlusButton / OnMinusButton to the focused
// skill rather than the default top row. No-op when `panel` isn't a
// chargen skills panel or `control` isn't a skill button. Called
// from chain rebind / step + per-tick (same reasoning as the
// Attribute panel — engine OnEnter overwrites our value, per-tick
// re-assert beats the FireActivate drain).
void SyncSelectedSkillFromChainFocus();

// Capture each skill_button → its matching skill_label text into the
// cycle-category cache, so FromControl produces "Computer, 0" etc.
void CaptureLabelsIfApplicable(void* panel);

// Cursor-Y compensation for the chain-step click-sim when focus is a
// skill_button. Returns the row pitch (~30 px) computed from
// skill_buttons[0]/[1] extents, or 0 when no compensation applies.
// See menus_chargen_attr.h for the underlying engine quirk.
int RowPitchForCursorWarp(void* panel, void* control);

// Speak the per-row info suffix ("Preis 1" / "Preis 2") synchronously
// from the chain-step input handler, right after the existing
// "Computer, 0" announce. Cost is computed from
// CSWGuiSkillsCharGen::IsClassSkill rather than the label — see
// menus_chargen_attr.h for why we avoid the engine's cost_value
// label here.
void AnnounceChainStepSuffix(void* panel, void* control);

// Speak the focused skill's description text by reading
// skill_descriptions[i] directly from the panel struct, bypassing the
// engine's hover-driven listbox population. The engine's path is
// reliably off-by-one on this panel (the cursor warp's hit-test
// resolves to skill_labels[i-1] regardless of Y compensation —
// labels overlap the cursor's row in a way Attribute labels don't),
// so we read the source-of-truth array instead. Returns true when a
// non-empty description was spoken.
//
// IsChargenSkillsDescriptionListbox below is the dual: callers in
// the listbox-text speech path use it to suppress the engine's
// stale-content speech for this panel.
bool AnnounceChainStepDescription(void* panel, void* control);

// True iff `listBox` is the description_list_box of a chargen Skills
// panel currently in the chain. Used by OnListBoxSetActiveControl to
// silence the engine's hover-driven description speech (off-by-one
// — see AnnounceChainStepDescription).
bool IsChargenSkillsDescriptionListbox(void* listBox);

// On a +/- press, override the default "{label}, {value}"
// re-announcement with "{value}, verbleibende Punkte {remaining}".
// Returns true when the override fired. The label is omitted on this
// path (fresh in context); cost is omitted because it's constant for
// a given skill throughout its normal range (the user heard it in
// the chain-step suffix).
//
// Caller is responsible for keeping the focus monitor's last-text
// snapshot in sync regardless of return value.
bool AnnounceValueChange(void* panel, void* control);

}  // namespace acc::menus::chargen_skills
