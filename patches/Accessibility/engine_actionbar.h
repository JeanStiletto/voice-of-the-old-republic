// Engine bindings for the player action bar (Aktionsmenü).
//
// Pure read + primitive layer. Mirrors engine_radial: resolve through
// the standard chain, per-column reads, drive engine widgets via the
// vtable[15] activate path. actionbar_menu wires this into a navigable
// Shift+4..Shift+7 submenu.
//
// CSWGuiMainInterface.field45_0x771c[6] — six CSWGuiMainInterfaceAction
// (stride 0x71C). Each column:
//   +0x000 action_button   — icon (fire = use)
//   +0x1C4 action_label    — text label of current variant
//   +0x388 up_button       — cycle next
//   +0x54C down_button     — cycle prev
//   +0x718 is_action       — 1 when populated
//
// CSWGuiMainInterface.field5_0x74[6] — CExoArrayList<CSWGuiInterfaceAction>
// stride 0x0C. Source of truth for variant count (PopulateMenus fills
// from CSWCCreature::GetPersonalActions).
//
// CSWGuiMainInterface::DoPersonalAction @0x0068ad60 — what bare 4..7 hit.
// param_2 unused — variant chosen by reading *(mi + 0x1bac + slot*4).

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::engine_actionbar {

constexpr int kColumnCount = 6;

// Borrowed pointer — re-resolve each tick, don't cache.
void* ResolveMainInterface();

// Reads field5_0x74[slot].size. The earlier-attempted is_action field
// (+0x718) returned pointer garbage incrementing by column stride; the
// embedded field45 widgets are populated lazily by Update on render so
// reading them returns empty. The descriptor list is the right source.
int VariantCount(void* mainInterface, int slot);

// CSWGuiInterfaceAction.label CExoString at +0x00, stride 0x38.
// Always NUL-terminates outBuf. True iff ≥1 byte written.
bool ReadVariantLabel(void* mainInterface, int slot, int index,
                      char* outBuf, size_t bufSize);

// CSWGuiInterfaceAction +0x08 (ulong). 0 on read fault.
uint32_t ReadVariantActionId(void* mainInterface, int slot, int index);

// field45_0x771c[slot].action_button — safe to pass to acc::engine::
// ReadControlTooltip / ReadGuiString (CSWGuiButton embeds CSWGuiControl
// at offset 0).
void* GetColumnActionButton(void* mainInterface, int slot);

// Stamps *(mi + 0x1bac + slot*4) = descriptor[index].action_id, then
// DoPersonalAction reads that field and searches for matching id.
//
// We bypass the labelled OnActionUp/DownArrowPressed handlers because
// (a) they gate on `param_1->is_active != 0` and the field45 widgets
// are uninitialised, and (b) OnActionDownArrow is mislabelled (calls
// CSWGuiTargetActionMenu::SelectNextAction on `this`, treating the
// main interface as a target_action_menu).
bool SelectVariant(void* mainInterface, int slot, int index);

// Same entry as bare 4..7. SelectVariant first or it fires variant 0.
bool FireSelectedVariant(void* mainInterface, int slot);

// Prep engine state so the engine's bare 1..3 / 4..7 dispatch fires
// against targetClientHandle instead of whatever target was last
// stamped.
//
// Q/E (SelectNearestObject) only writes main_interface.field1_0x64 —
// it does NOT call PopulateMenus. Per-frame MainLoop only repopulates
// on sub-screen close; SetCombatMode repopulates on combat-mode
// transitions. Between-rounds Q/E switch leaves action_lists' baked
// creature_ids stale, and the engine's downstream dispatch silently
// bails at GetGameObject(creature_id).
//
// This synchronously runs the engine's own primitives:
//   1. CGuiInGame::SetMainInterfaceTarget → MainInterface::SetTarget
//      (field1_0x64 + reset field21_0x5cb0).
//   2. CGuiInGame::RePopulateMainInterface @0x0062b050 →
//      MainInterface::PopulateMenus @0x00689d80 (rebuilds personal
//      action lists + target action lists with fresh creature_ids).
//
// targetClientHandle is CLIENT-side (with 0x80000000 high bit). Pass
// kInvalidObjectId when no narrated target — PopulateMenus then leaves
// hostile-targeted action items with unresolved creature_ids so
// dispatch silently no-ops instead of mistargeting. Self-buff items
// (Medikit Selbst, stims) still receive creature_id=player so they
// fire regardless.
bool PrepareBareDispatch(uint32_t targetClientHandle);

void LogState(void* mainInterface, const char* tag);

}  // namespace acc::engine_actionbar
