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

// Stamp the engine's per-column "currently-selected variant action_id"
// field at struct offset 0x1bac + slot*4 (six int32s, one per column).
// Reads the descriptor's action_id at `index` and writes it into that
// slot. DoPersonalAction reads this field and searches the column's
// action list for the matching action_id — so stamping the desired
// variant's action_id deterministically routes the next fire to that
// variant.
//
// Decompile finding (2026-05-24, DoPersonalAction @ 0x0068ad60):
//   `param_2` is completely unused inside DoPersonalAction. The
//   variant to fire is selected by searching field5_0x74[slot] for
//   an entry whose `+0x08` action_id matches *(mi + 0x1bac + slot*4).
//   On no match (including the sentinel -1), the function falls
//   back to data[0] — which is why our earlier
//   `DoPersonalAction(slot, 0)` always fired variant 0 regardless of
//   which variant the user thought they had selected.
//
//   The engine's own SelectPrevPersonalAction (0x006888e0) writes
//   the new selected action_id to exactly this field. So stamping
//   it ourselves is the same primitive the engine uses internally.
//   We bypass the labelled OnActionUp/DownArrowPressed handlers
//   because (a) they gate on `param_1->is_active != 0` and the
//   field45_0x771c widgets are uninitialised (see existing memory
//   on the action-bar field45 layout), and (b) OnActionDownArrow is
//   mislabelled — its body calls CSWGuiTargetActionMenu::SelectNextAction
//   on `this`, treating the main_interface as a target_action_menu
//   (a no-op or worse for personal columns).
//
// Returns true when the stamp wrote a valid action_id; false on
// null/out-of-range slot, no-such-variant, action_id==0, or SEH.
bool SelectVariant(void* mainInterface, int slot, int index);

// Drive `CSWGuiMainInterface::DoPersonalAction(this, slot, 0)` at
// 0x0068ad60. Same entry point engine-native bare 4..7 hotkeys hit.
// The function reads `*(this + 0x1bac + slot*4)` to choose which
// variant to fire, so callers MUST invoke `SelectVariant(mi, slot,
// index)` first to stamp the desired variant's action_id. Without
// that, the engine falls back to variant 0.
//
// `slot` is the column index (0..5; only 0..3 are bound to keys 4..7).
// Returns false on null/out-of-range/SEH.
bool FireSelectedVariant(void* mainInterface, int slot);

// Prepare engine state so the engine-native bare 1..3 / 4..7 dispatch
// fires against `targetClientHandle` instead of whatever target was last
// stamped on main_interface.
//
// Decompile finding (2026-05-21, see chat session):
//   Q/E (CClientExoAppInternal::SelectNearestObject) only stores the
//   picked id into main_interface.field1_0x64; it does NOT call
//   PopulateMenus. Per-frame MainLoop only repopulates when the panel
//   was removed from the manager (sub-screen close path); SetCombatMode
//   repopulates on combat-mode transitions. Neither fires between rounds
//   on a Q/E switch. So action_lists' baked creature_ids stay stale, and
//   the engine's DoTargetAction / DoPersonalAction call inside
//   CClientExoAppInternal::HandleInputEvent silently bails at the
//   GetGameObject(creature_id) check.
//
//   The mouse passive-cursor path masks this for sighted users (continuous
//   hit-test repopulates target_action_menu under the cursor); keyboard-
//   only play hits the staleness directly.
//
// This helper closes the gap by calling the engine's own primitives
// synchronously before the engine's switch case runs:
//   1. CGuiInGame::SetMainInterfaceTarget(guiIn, targetClient)
//        → CSWGuiMainInterface::SetTarget(mi, target)
//          stores field1_0x64 + resets field21_0x5cb0 (refresh hint).
//   2. CGuiInGame::RePopulateMainInterface(guiIn) @0x0062b050
//        → CSWGuiMainInterface::PopulateMenus(mi) @0x00689d80
//          rebuilds field5_0x74[0..5] via GetPersonalActions(player) and
//          target_action_menu.action_lists[0..2] via GetTargetActions(
//          player, swc_target, row). Each rebuilt action item carries a
//          fresh creature_id, so the engine's downstream dispatch goes
//          against the target we just stamped.
//
// `targetClientHandle` is the CLIENT-side handle (with the high bit
// 0x80000000 set if applicable). Pass 0x7f000000 (kInvalidObjectId) when
// no narrated target is available — PopulateMenus then builds empty
// target_actions and (for personal-action items the engine classifies as
// hostile-targeted) leaves their creature_ids unresolved so the
// dispatch silently no-ops instead of grenade-at-friend mistargeting.
// Self-buff personal items (Medikit (Selbst), stims) still receive
// creature_id=player from GetPersonalActions regardless of the main-
// interface target, so they fire correctly even without a narrated
// enemy.
//
// Returns true when both engine calls completed without faulting; false
// on chain-unresolved or SEH inside either call.
bool PrepareBareDispatch(uint32_t targetClientHandle);

// Diagnostic: dump per-column state (variant count + first-variant
// label + first-variant action_id) to the patch log. Replaces the
// older widget-text dump which was reading uninitialised field45
// widgets.
void LogState(void* mainInterface, const char* tag);

}  // namespace acc::engine_actionbar
