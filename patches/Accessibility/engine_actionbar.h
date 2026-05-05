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

// Read the column's is_action flag (+0x718). Returns 0 when slot is
// out of range or main_interface is null.
int IsColumnActive(void* mainInterface, int slot);

// Number of variants populated for `slot` (0..5). Read from
// field5_0x74[slot].size at +0x74 + slot*0x0C + 0x04. Returns 0 for
// out-of-range slots, null main_interface, or read fault.
int VariantCount(void* mainInterface, int slot);

// Read the column's currently-rendered variant label (action_button text
// via the gui_string path; same indirection as
// project_kotor_text_indirection). Returns true when at least one byte
// was written. Always NUL-terminates outBuf.
bool ReadColumnLabel(void* mainInterface, int slot,
                     char* outBuf, size_t bufSize);

// Drive engine widgets via vtable[15] HandleInputEvent(kInputActivate=0x27).
// Equivalent to a mouse click on the widget — invokes whatever the widget
// has bound to its activate handler. Each returns false on null/oor/SEH.
bool FireUpButton    (void* mainInterface, int slot);
bool FireDownButton  (void* mainInterface, int slot);
bool FireActionButton(void* mainInterface, int slot);

// Diagnostic: dump per-column state (is_action + action_button label +
// variant count) to the patch log. Used right after a submenu Open to
// disambiguate "engine has no variants populated" from "we're reading
// the wrong fields". Cheap; safe to call on disarm too.
void LogState(void* mainInterface, const char* tag);

}  // namespace acc::engine_actionbar
