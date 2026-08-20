// Layout + behaviour shared by menus_chargen_attr + menus_chargen_skills.
//
// Both panels are the same screen with different data: an N-button +
// N-label + N-plus + N-minus grid (N = 6 abilities, 8 skills), a
// "points remaining" label, and a description listbox the engine
// repopulates from whichever points button it thinks is hovered. Only
// the offsets, the count and the log tag differ.
//
// Everything that follows from that sameness lives here, parameterised
// by a PanelDesc the panel supplies once. The per-panel namespaces keep
// their public names (IsChargenAttributesPanel, AbilityIndexFromButton,
// SyncSelectedAbilityFromChainFocus, …) as thin wrappers, because they
// have callers across menus.cpp, menus_chain*, menus_focus,
// menus_monitors and menus_pending.
//
// What deliberately does NOT live here is the genuine domain variation:
// the attributes panel computes a D&D modifier and a point-buy cost and
// tracks breakpoint crossings across four message variants; the skills
// panel has no modifier and a flat class/cross-class cost. Those stay in
// their own files — see the "not recommended for merging" note in
// archiev/refactoring/reports/phase-3-menus-chain-chargen.md.

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::menus::chargen_layout {

// Everything that distinguishes the two panels. One constant instance
// per panel .cpp; every function below takes it instead of a long
// parameter list.
struct PanelDesc {
    uintptr_t   vtable;              // kVtableCSWGui{Abilities,Skills}CharGen
    size_t      buttonsOffset;       // value-button array, stride kCSWGuiButtonSize
    size_t      labelsOffset;        // name-label array, stride kCSWGuiLabelSize
    size_t      selectedOffset;      // int "selected row" field the engine reads
    size_t      descListBoxOffset;   // description listbox, inline in the panel
    uintptr_t   onEnterPointsButton; // engine's per-button description populator
    int         count;               // 6 abilities / 8 skills
    const char* logTag;              // "Menus.ChargenAttr" / "Menus.ChargenSkill"
    const char* selectedFieldName;   // log-only: "selected_ability" / "selected_skill"
    // Engine-binding probe (0 = this panel has no probe and is always
    // treated as ready). See IsBindingReady below for why it exists.
    size_t      creatureOffset;      // panel -> chargen_creature
    size_t      creatureStatsOffset; // chargen_creature -> lvl_up_stats
};

// True when the panel's engine binding resolves — i.e. when the engine's
// own per-button handlers can safely touch this panel.
//
// The engine's description populator (OnEnterPointsButton) reaches
// IsClassSkill, which dereferences `panel->chargen_creature->lvl_up_stats`
// with NO null check in either game. A KOTOR 2 level-up Fähigkeiten panel
// was observed with that pointer still garbage right after the screen
// opened: our cost call faulted into its SEH handler (spoke "Preis ?"),
// and one keystroke later the cursor warp let the ENGINE walk the same
// chain outside any guard of ours and the process died
// (patch-20260819-063818, crash inside K2 0x009103a0 +0xF).
//
// So this probe walks exactly the same two derefs under SEH first. If it
// survives, the engine's will too; if it faults, no code path of ours may
// make the engine touch the panel — not the cost call, not the
// description refresh, and not the cursor warp that triggers the engine's
// own hover handler. Panels with creatureOffset == 0 always return true.
bool IsBindingReady(const PanelDesc& d, void* panel);

// Diagnostic dump of the panel's binding word (log-only, nothing
// branches on it). `when` tags the call site in the log line.
void LogBindingWord(const PanelDesc& d, void* panel, const char* when);

// True iff `panel`'s vtable is this panel kind. SEH-guarded.
bool IsPanel(const PanelDesc& d, void* panel);

// Index 0..count-1 if `control` is one of the panel's value buttons;
// -1 otherwise (not in range, not aligned to the stride, out of bounds).
int IndexFromButton(const PanelDesc& d, void* panel, void* control);

// Row pitch (~30-45 px) from buttons[0] / buttons[1] extents, or 0 if
// `control` is not a value button or the extents read implausibly. Used
// by the chain-step click-sim to compensate for the engine's hit-test
// shift on these panels.
int RowPitchForCursorWarp(const PanelDesc& d, void* panel, void* control);

// Point the panel's "selected row" field at whatever the chain has
// focused, so the engine's own +/- handling acts on the row the user is
// actually on. No-op unless the chain is bound to this panel kind.
void SyncSelectedFromChainFocus(const PanelDesc& d);

// Bind each row's NAME label to its value button in the cycle-category
// cache. Without this the generic capture resolves the category to the
// button's own inline text — the value ("8", "0") rather than the name —
// so FromControl would say "8" instead of "Stärke, 8".
void CaptureLabels(const PanelDesc& d, void* panel);

// Speak the focused row's description. Calls the engine's
// OnEnterPointsButton with the FOCUSED button so the description listbox
// is populated for the right row, then reads and speaks controls[0].
//
// This bypasses the engine's hover/cursor-warp path on purpose: that path
// is resolution-dependent and its hit-test can resolve to the neighbouring
// row and paint the wrong description (label overlap makes Y compensation
// insufficient here). The call fires SetActive on the description listbox,
// which trips our listbox hook — IsDescriptionListbox below is what lets
// menus.cpp silence that engine-side echo so this stays the only speaker.
bool AnnounceDescription(const PanelDesc& d, void* panel, void* control);

// True iff `listBox` is this panel's description listbox. Dual of
// AnnounceDescription — see there.
bool IsDescriptionListbox(const PanelDesc& d, void* listBox);

}  // namespace acc::menus::chargen_layout
