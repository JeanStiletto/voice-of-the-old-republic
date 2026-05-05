// Engine bindings for CSWGuiTargetActionMenu — KOTOR's radial action menu.
//
// Layer: engine/ (pure read+primitives; no menu state, no speech). Mirrors
// the engine_picker pattern: resolve through the AppManager → ClientExoApp →
// Internal → CGuiInGame → CSWGuiMainInterface chain to reach the embedded
// CSWGuiTargetActionMenu, expose its row+action surface, and call the
// engine's own select/dispatch primitives.
//
// Why this exists:
//   When `acc::picker::Drive` finds an empty default-action descriptor it
//   falls back to `CSWGuiMainInterface::PopulateMenus @0x00689d80`, which
//   populates `target_action_menu.action_lists[3]` (per-row action arrays)
//   and `.target_actions[3]` (currently-visible row buttons). The radial is
//   NOT a top-level CSWGuiPanel (no entry in CGuiInGame's named-slot table —
//   see engine_panels.h), so our PanelKind classifier and chain navigation
//   never see it. Without this module, "Aktionsmenü" is announced but the
//   user has no way to select an action.
//
// Verified surfaces (k1_win_gog_swkotor.exe.xml + headless decomp 2026-05-05):
//   CSWGuiTargetActionMenu::SelectNextAction @0x006865b0  (row index)
//   CSWGuiTargetActionMenu::SelectPrevAction @0x00686680  (row index)
//   CSWGuiTargetActionMenu::DoTargetAction   @0x00689610  (row index)
//   IsTargetActionMenuControl                @0x00684ed0  (predicate; unused)
//
// How the radial gets populated (decomp of 0x00689d80 + 0x00689410):
//   `CSWGuiMainInterface::PopulateMenus(this) @0x00689d80` (no args, this =
//   main_interface) is the public entry point — it's what
//   HandleMouseClickInWorld calls on first-click and what we now call from
//   the empty-descriptor branch of `acc::picker::Drive`.
//   Internally it:
//     1. player_creature = GetSWParty()->GetPlayerCharacter()
//     2. game_object     = CClientExoApp::GetGameObject(field1_0x64)
//     3. swc_target      = game_object->vtable->AsSWCObject(game_object)
//        ^^^ this downcast is required — passing a raw CGameObject* to the
//        inner @0x00689410 corrupts target_type and trips /GS canary on
//        subsequent SelectNext/Prev/DoTargetAction calls.
//     4. Refills 6 personal-action lists on main_interface (field5_0x74[0..5])
//        via `CSWCCreature::GetPersonalActions(player, i, &list)` for i=0..5.
//     5. Calls inner `CSWGuiTargetActionMenu::PopulateMenus(player, mode,
//        swc_target, &result) @0x00689410` which populates THIS object's
//        action_lists[0..2] via `CSWCCreature::GetTargetActions(player,
//        swc_target, row, &list)` for row=0..2.
//   So one wrapper call produces both the surrounding UI's personal actions
//   AND the radial's per-row target actions. We don't need (and shouldn't
//   make) a direct inner call.
//
// Struct layout (swkotor.exe.h:10405 + line 11611 MEMBER table, with
// field1[12] semantics confirmed by inner-PopulateMenus + SelectNextAction
// + SelectPrevAction + DoTargetAction decomps):
//   CSWGuiTargetActionMenu lives at CSWGuiMainInterface + 0xBC.
//     +0x00  CExoArrayList<CSWGuiInterfaceAction> action_lists[3]   (3*0x0C)
//     +0x24  int field1[12]   ← 4 target_types × 3 rows = 12 ints. Each
//                              int is the *currently-selected action_id*
//                              for (target_type, row); -1 means "none, use
//                              row default". SelectNext/Prev mutate this
//                              slot; inner-PopulateMenus + DoTargetAction
//                              read it to find the active action within
//                              action_lists[row].
//     +0x54  CSWGuiMainInterfaceAction target_actions[3]            (3*0x71C)
//     +0x15A8 CSWGuiMainInterface* main_interface
//     +0x15CC CSWGuiLabel name_label
//     +0x1AE4 ulong  failure_reason_strref
//     +0x1AEA byte   target_type        ← set by inner-PopulateMenus from
//                                         GetTargetInterfaceTargetType.
//                                         Indexes field1[].
//
//   CSWGuiMainInterfaceAction (size 0x71C):
//     +0x000 CSWGuiButton action_button   ← visible action label (at +0x16C)
//     +0x1C4 CSWGuiButton action_label
//     +0x388 CSWGuiButton up_button
//     +0x54C CSWGuiButton down_button
//     +0x718 int          is_action       ← 1 when row is populated
//
//   CSWGuiInterfaceAction (size 0x38, struct in engine_picker.cpp):
//     +0x00 CExoString label
//     +0x08 ulong action_id
//     +0x0c void* action_function
//     +0x1c ulong creature_id (the action target)
//     +0x20 CResRef icon
//
// All engine entry points take CSWGuiTargetActionMenu* in ECX (__thiscall).
// SelectNextAction/SelectPrevAction/DoTargetAction take an int row index
// (0..2) as their single stack argument.

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::engine_radial {

// Maximum rows in CSWGuiTargetActionMenu.target_actions[]. Hard-coded into
// the engine struct (size 3); exposed as a constant so callers can size
// loops without re-stating the magic number.
constexpr int kRowCount = 3;

// Resolve the live CSWGuiTargetActionMenu* via the standard chain
// (AppManager → ClientExoApp → Internal → GuiInGame → MainInterface +
// 0xBC). Returns nullptr when any link is null (e.g. between modules,
// during DLL-attach, or after MainInterface is torn down).
//
// Pointer is borrowed — not stable across module transitions. Callers
// must re-resolve every tick rather than caching.
void* ResolveTargetActionMenu();

// Number of CSWGuiInterfaceActions populated for `row` (0..2). Read from
// action_lists[row].size. Returns 0 for out-of-range rows or null TAM.
//
// This is the gate-of-truth for "is this row navigable" — PopulateMenus
// fills it on populate, the engine's dispatch path (DoTargetAction)
// presumably tears it down on completion. Tick monitor uses
// max-across-rows == 0 as the "menu is gone" signal.
int RowActionCount(void* tam, int row);

// Read the visible action label for `row` from
// target_actions[row].action_button via the gui_string path. Falls back
// to the inline CExoString and TLK strref the same way menus.cpp's
// ExtractAnnounceableText does for any other CSWGuiButton.
//
// Returns true when at least one byte was written to outBuf (which
// always ends NUL-terminated). False when the row is unpopulated, the
// button text is empty, or any read faulted under SEH.
bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize);

// Read the radial's target name from name_label (CSWGuiLabel at TAM +
// 0x15CC). Returns true on non-empty result. Used as a sanity-check
// (the engine sets it via SetNameLabel @0x00685af0 during PopulateMenus
// so a successfully-opened radial always has a name).
bool ReadTargetName(void* tam, char* outBuf, size_t bufSize);

// Diagnostic: dump current TAM state to the patch log (hex window of
// the first 0x40 bytes, plus the parsed action_lists[0..2] and per-row
// is_action / action_button text length). Used right after a
// PopulateMenus call to disambiguate "engine populated nothing" from
// "we're reading the wrong fields".
void LogState(void* tam, const char* tag);

// Wider variant of LogState. Dumps everything LogState does, plus:
//   - field1[12] (the 12 ints between action_lists[3] and target_actions[3])
//   - For each row (target_actions[r]): the four embedded CSWGuiButtons
//     (action_button @+0x000, action_label @+0x1C4, up_button @+0x388,
//     down_button @+0x54C) — each read via gui_string AND inline
//     CExoString. field4_0x710 / field5_0x714 raw uint32 values.
//   - For each row's action_lists[r] with cap > 0 and data != null,
//     peek the FIRST CSWGuiInterfaceAction at data[0] (label, action_id,
//     target_id, icon) — even when size==0, because the engine might
//     buffer the action without committing the size update.
//
// Intended for one-shot deep-investigation logging right after a
// PopulateMenus call where the standard LogState reported all-empty.
// More expensive than LogState but fine for diagnostic use.
void LogStateWide(void* tam, const char* tag);

// Engine primitives. All take TAM* as ECX, row index as the single arg.
// Each is SEH-wrapped and returns false on null TAM, out-of-range row,
// or a fault inside the engine call. Successful return only means we
// reached the engine — observable side effects (action_button text
// changing, action dispatched) are visible to the caller via subsequent
// reads.
bool SelectNextActionInRow(void* tam, int row);
bool SelectPrevActionInRow(void* tam, int row);
bool DispatchRowAction   (void* tam, int row);

}  // namespace acc::engine_radial
