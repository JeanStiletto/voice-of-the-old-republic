// KOTOR Accessibility — chargen attributes panel (step 2 of Eigener
// Charakter) tweaks.
//
// Two fixes for CSWGuiAbilitiesCharGen, both rooted in the same engine
// quirk: the panel's six +/- buttons all dispatch to OnPlusButton /
// OnMinusButton, which read selected_ability — they do NOT inspect the
// fired button's identity. So:
//
//   1. Each value button's chain-announce is just its number ("8") with
//      no ability name, leaving the user blind to which row they're on.
//      We mirror the matching ability_label's text into the cycle-
//      category cache; FromControl's existing prefix logic then says
//      "Stärke, 8" instead of bare "8".
//
//   2. selected_ability stays at 0 (STR) for the panel's lifetime
//      because the engine only writes it on a real mouse click — our
//      chain-nav cursor warp doesn't trigger OnEnterPointsButton
//      reliably (hit-test resolves one row above the cursor here too).
//      So every Left/Right press, regardless of focused row, increments
//      STR. We sync chain focus → selected_ability on every chain
//      rebind and Up/Down step so +/- targets the row the user is on.
//
// Engine offsets in engine_offsets.h. Caller-side wiring in menus.cpp
// (CaptureLabelsIfApplicable from WalkAndCaptureOnFirstSight, sync
// from chain step) and menus_chain.cpp (sync from end of RebindChain).

#pragma once

namespace acc::menus::chargen_attr {

// True iff `panel`'s vtable matches CSWGuiAbilitiesCharGen.
bool IsChargenAttributesPanel(void* panel);

// Index 0..5 in struct order if `control` is one of the panel's
// ability_buttons[]; -1 otherwise. Struct order is STR, DEX, CON,
// WIS, INT, CHA — selected_ability uses the same order.
int AbilityIndexFromButton(void* panel, void* control);

// Cursor-Y compensation for the chain-step click-sim when the focus
// is one of the ability_buttons[]. Returns the row pitch (~45 px)
// computed from ability_buttons[0]/[1] extents, or 0 when no
// compensation applies. The engine's hit-test on this panel resolves
// one row ABOVE the cursor (same shift Options' tab cluster needs);
// without this offset the engine fires OnEnterPointsButton for the
// row above the focused row, which (a) shows the wrong description
// in description_listbox and (b) overwrites selected_ability to the
// wrong index. Mirror of g_tabClickOffsetY's role on Options panels.
int RowPitchForCursorWarp(void* panel, void* control);

// Write selected_ability for the chain's currently-focused button.
// No-op when the chain panel isn't a chargen attributes panel or
// chain focus isn't on a value button.
//
// Called from two sites:
//   * Chain rebind / Up-Down step in menus_chain.cpp / menus.cpp —
//     immediate sync at the input event so the next FireActivate
//     queued by Left/Right sees the right value if no other write
//     intervenes.
//   * Per-tick from TickGeneralMonitors — re-asserts the value because
//     the engine's OnEnterPointsButton (triggered by our cursor-warp
//     hit-test landing on the row-above's label, due to the same
//     hit-test shift that needs g_tabClickOffsetY on Options panels)
//     silently overwrites selected_ability between our chain-step
//     sync and the queued FireActivate. Without this re-assert, every
//     +/- press routes to the visual row above the focused row.
//     Per-tick monitors run in the same OnUpdate before
//     TickPendingOps drains FireActivate, so OnPlusButton sees our
//     value when it reads selected_ability.
void SyncSelectedAbilityFromChainFocus();

// Capture each ability_button → its matching ability_label text into
// the cycle-category cache, so FromControl produces "Stärke, 8" etc.
// No-op when `panel` isn't a chargen attributes panel.
void CaptureLabelsIfApplicable(void* panel);

}  // namespace acc::menus::chargen_attr
