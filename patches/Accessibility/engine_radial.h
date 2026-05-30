// Engine bindings for CSWGuiTargetActionMenu (KOTOR's radial).
//
// Pure read + primitives. The radial isn't a top-level CSWGuiPanel — it
// lives embedded in CSWGuiMainInterface at +0xBC. picker::Drive falls
// here when the default-action descriptor is empty.
//
// PopulateMenus chain — one call populates both surfaces:
//   CSWGuiMainInterface::PopulateMenus @0x00689d80 (wrapper):
//     player    = GetSWParty()->GetPlayerCharacter()
//     gameObj   = CClientExoApp::GetGameObject(field1_0x64)
//     swcTarget = gameObj->vtable->AsSWCObject(gameObj)
//                 ^^ REQUIRED downcast. Raw CGameObject* trips /GS
//                 canary on subsequent SelectNext/Prev/DoTargetAction.
//     1. fills field5_0x74[0..5] via GetPersonalActions(player, i)
//     2. inner PopulateMenus @0x00689410 fills the radial's
//        action_lists[0..2] via GetTargetActions(player, swc, row)
//
// CSWGuiTargetActionMenu layout (at MainInterface + 0xBC):
//   +0x0000  CExoArrayList<CSWGuiInterfaceAction> action_lists[3]  (3×0x0C)
//   +0x0024  int field1[12]   ← 4 target_types × 3 rows. Each int
//                               is the selected action_id; -1 = row
//                               default. SelectNext/Prev mutate, inner
//                               PopulateMenus + DoTargetAction read.
//   +0x0054  CSWGuiMainInterfaceAction target_actions[3]   (3×0x71C)
//   +0x15A8  CSWGuiMainInterface* main_interface
//   +0x15CC  CSWGuiLabel name_label
//   +0x1AE4  ulong  failure_reason_strref
//   +0x1AEA  byte   target_type — indexes field1[]
//
// CSWGuiMainInterfaceAction (0x71C):
//   +0x000 action_button (visible label at +0x16C)
//   +0x1C4 action_label
//   +0x388 up_button
//   +0x54C down_button
//   +0x718 is_action (1 = row populated)
//
// CSWGuiInterfaceAction (0x38):
//   +0x00 CExoString label
//   +0x08 ulong action_id
//   +0x0c void* action_function
//   +0x1c ulong creature_id (action target)
//   +0x20 CResRef icon
//
// Engine entry points (all __thiscall, ECX = TAM*, single int row arg):
//   SelectNextAction @0x006865b0
//   SelectPrevAction @0x00686680
//   DoTargetAction   @0x00689610

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::engine_radial {

constexpr int kRowCount = 3;

// Borrowed pointer; re-resolve each tick.
void* ResolveTargetActionMenu();

// action_lists[row].size. Tick monitor uses max-across-rows == 0 as
// "menu gone" signal.
int RowActionCount(void* tam, int row);

// Reads target_actions[row].action_button via the gui_string path with
// inline CExoString + TLK strref fallback. outBuf is NUL-terminated.
bool ReadRowActionLabel(void* tam, int row, char* outBuf, size_t bufSize);

// name_label at TAM+0x15CC — sanity check (always populated post-
// PopulateMenus via SetNameLabel @0x00685af0).
bool ReadTargetName(void* tam, char* outBuf, size_t bufSize);

// Hex dump + parsed action_lists + per-row is_action / button text.
void LogState(void* tam, const char* tag);

// LogState + field1[12] + per-row embedded buttons (action/label/up/down)
// + action_lists[r].data[0] peek even when size==0 (engine may buffer
// without committing size). One-shot deep diagnostic.
void LogStateWide(void* tam, const char* tag);

// Each: SEH-wrapped, false on null TAM / out-of-range / fault.
// True only means the engine call dispatched; observable effects show
// up via subsequent reads.
bool SelectNextActionInRow(void* tam, int row);
bool SelectPrevActionInRow(void* tam, int row);
bool DispatchRowAction   (void* tam, int row);

// Stamps field1[target_type*3 + row] = action_lists[row].data[index]
// .action_id. DoTargetAction matches against field1 and falls back to
// data[0] on miss. Mirrors engine_actionbar::SelectVariant — together
// they let submenus keep a shadow index across PopulateMenus rebuilds
// (rebuilds reassign action_ids, so re-stamp after each refresh).
bool SelectActionInRow(void* tam, int row, int index);

// target_actions[row].action_button. Coincides with the array entry's
// start (action_button is field 0).
void* GetRowActionButton(void* tam, int row);

// action_id of the descriptor currently shown for target_actions[row].
// Resolves via the engine's own selection-tracking field
// (field1[target_type*3 + row]) and falls back to data[0] when nothing
// matches, mirroring SelectNextAction's lookup loop. Returns 0 on empty
// row / null TAM / fault. Same descriptor the radial / target-action
// submenu Shift+arrow peek reads to derive an item handle for the
// engine's variant-item slots (action-bar slots 1..3 encoding —
// engine_reads::ResolveItemDescriptionFromActionId).
uint32_t ReadSelectedRowActionId(void* tam, int row);

// Resolves targetClient via GetGameObject, downcasts, logs per-class
// fields GetTargetActions checks (doors: cannot_bash/can_use_actions/
// is_hostile/state). Disambiguates "engine has no actions for this
// final state" from "we set preconditions wrong".
void LogTargetDiag(uint32_t targetClient, const char* tag);

}  // namespace acc::engine_radial
