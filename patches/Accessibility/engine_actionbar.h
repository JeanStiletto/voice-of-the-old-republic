// Engine bindings for the player action bar (Aktionsmenü).
//
// Layer: engine/ (pure read+primitives; no menu state, no speech). Mirrors
// engine_radial: resolve through the standard chain (AppManager →
// ClientExoApp → Internal → CGuiInGame → CSWGuiMainInterface), expose
// per-column reads, and drive the engine's existing up/down/action-button
// widgets via the vtable[15] activate path.
//
// Why this exists:
//   The combat tutorial after the Endar Spire prologue teaches the player
//   to use medikits via the AKTIONSMENÜ — six columns of category icons
//   sitting next to each party-member portrait at the bottom of the
//   screen. Engine-native hotkeys 4..7 (per the manual) immediately fire
//   `DoPersonalAction(slot)` for the *currently-selected variant* of
//   columns 1..4, but a screen-reader user has no way to know which
//   variant is current, no way to cycle within a column, and no way to
//   discover the column contents at all. This module provides the
//   building blocks; actionbar_menu wires them into a navigable submenu
//   armed by Shift+4..Shift+7.
//
// Verified surfaces (from swkotor.exe.h + k1_win_gog_swkotor.exe.xml +
// docs/action-menu-investigation.md, 2026-05-05):
//   CSWGuiMainInterface.field45_0x771c[6] — array of CSWGuiMainInterfaceAction
//     stride 0x71C. Same column primitive the radial uses for its
//     target_actions[3], just six instances on the player bar.
//   CSWGuiMainInterfaceAction:
//     +0x000 CSWGuiButton action_button   (icon — fire = use)
//     +0x1C4 CSWGuiButton action_label    (text label — variant name)
//     +0x388 CSWGuiButton up_button       (cycle next variant)
//     +0x54C CSWGuiButton down_button     (cycle prev variant)
//     +0x718 int          is_action       (1 when column is populated)
//   CSWGuiMainInterface.field5_0x74[6] — six CExoArrayList<CSWGuiInterfaceAction>
//     starting at +0x74, stride 0x0C (data, size, capacity). Source of truth
//     for variant count per column. Populated by PopulateMenus via
//     CSWCCreature::GetPersonalActions(player, slot, &list) for slot=0..5.
//   CSWGuiMainInterface::DoPersonalAction @ 0x0068ad60 — fires the column
//     (what hotkeys 4..7 invoke). Two int args; second arg semantics
//     not yet decoded — we drive `action_button` via vtable[15] activate
//     instead, which is what mouse-clicking the icon does and avoids the
//     direct-call signature uncertainty.

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::engine_actionbar {

// Total columns on the player action bar. Hard-coded into
// CSWGuiMainInterface (field45_0x771c[6]).
constexpr int kColumnCount = 6;

// Resolve the live CSWGuiMainInterface* via the standard chain. Returns
// nullptr when any link is null (between modules, during DLL-attach,
// after main-interface teardown). Pointer is borrowed — re-resolve every
// tick rather than caching.
void* ResolveMainInterface();

// Number of variants populated for `slot` (0..5). Read from
// field5_0x74[slot].size at +0x74 + slot*0x0C + 0x04. Returns 0 for
// out-of-range slots, null main_interface, or read fault.
//
// This is the gate-of-truth for "column has anything to fire" — verified
// 2026-05-05 (slot 1 = 2 medikit variants matches in-game observation).
// The previously-attempted is_action field (+0x718) returned pointer-
// like garbage that incremented by the column stride; Ghidra's `?`
// annotation was correctly hedged. field45_0x771c widget reads
// (action_button.gui_string, action_label.gui_string, etc.) similarly
// returned empty — those embedded buttons aren't live widgets in this
// build, possibly populated lazily by CSWGuiMainInterface::Update on
// render. We sidestep the whole field45 widget structure and read
// straight from the descriptor list.
int VariantCount(void* mainInterface, int slot);

// Read the variant label at `index` within column `slot`'s descriptor
// list (CSWGuiInterfaceAction.label, CExoString at +0x00 of each entry,
// stride 0x38). Source of truth — same descriptor layout the engine's
// own GetPersonalActions populates. Returns true when at least one byte
// was written. Always NUL-terminates outBuf.
bool ReadVariantLabel(void* mainInterface, int slot, int index,
                      char* outBuf, size_t bufSize);

// Read the variant action_id at `index` (CSWGuiInterfaceAction +0x08,
// ulong). Used for diagnostics + as a candidate `param_2` value if
// DoPersonalAction(slot, 0) turns out to need an explicit action_id.
// Returns 0 on read fault / out-of-range index.
uint32_t ReadVariantActionId(void* mainInterface, int slot, int index);

// Drive the engine's labelled per-column cycle handlers. Each takes
// the up_button / down_button widget address as a CSWGuiControl* (the
// handler resolves which slot from the pointer). Even though those
// embedded widgets read empty when probed, the handler treats them
// as identity tokens — pointer math against the field45 array base
// recovers the slot index regardless of whether the widget itself is
// fully initialised.
//
//   OnActionUpArrowPressed   @ 0x0068af70  (this, CSWGuiControl*)
//   OnActionDownArrowPressed @ 0x0068afe0  (this, int [param_1])
//
// Returns false on null/out-of-range/SEH.
bool CycleNextVariant(void* mainInterface, int slot);
bool CyclePrevVariant(void* mainInterface, int slot);

// Drive `CSWGuiMainInterface::DoPersonalAction(this, slot, param_2)` at
// 0x0068ad60. Same entry point engine-native bare 4..7 hotkeys hit.
// param_2's role is currently undecoded — we pass 0 as the initial
// guess; if dispatch silently no-ops the next iteration tries the
// variant's action_id (descriptor +0x08) instead.
//
// `slot` is the column index (0..5; only 0..3 are bound to keys 4..7).
// Returns false on null/out-of-range/SEH.
bool FireSelectedVariant(void* mainInterface, int slot);

// Diagnostic: dump per-column state (variant count + first-variant
// label + first-variant action_id) to the patch log. Replaces the
// older widget-text dump which was reading uninitialised field45
// widgets.
void LogState(void* mainInterface, const char* tag);

}  // namespace acc::engine_actionbar
