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

// Will DoPersonalAction refuse this entry, and why?
//
// The engine already knows both, and says neither in a way a blind player can
// use: on refusal it stamps one of six "you cannot do that" StrRefs into a
// panel field that is drawn on screen for five seconds, and plays GUI sound 2.
// A sighted player reads the sentence; we heard, at best, a beep — and on
// KOTOR 2 not even that, because its GUI-sound slot never sounds.
//
// So read the same two fields the engine tests, BEFORE dispatching, and let
// the caller speak the engine's own sentence. Reading beats watching the panel
// field: the field is only written when a reason code exists, it is not
// cleared between refusals, and a second press of the same refused entry
// rewrites the same value — so no before/after comparison can tell "refused
// again" from "nothing happened".
//
// Returns true when the entry WILL be refused. *outStrRef receives the
// engine's own reason, or 0 when the refusal carries no reason code (the
// engine beeps and prints nothing in that case too).
bool VariantRefusal(void* mainInterface, int slot, int index,
                    uint32_t* outStrRef);

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

// One log line dumping everything the engine's DoPersonalAction will gate
// on for this entry — action_id, creature_id (+ whether the client object
// array resolves it), handler dword, flag word. Diagnostic for the KOTOR 2
// dead-medical-column investigation (docs/known-issues.md): K2's dispatch
// bails in TOTAL silence when the entry's creature_id fails to resolve,
// and its appender never initialises the flag word for inventory items,
// so only a fire-time dump can say which gate an unexplained no-op died
// at. Call right before dispatching a personal entry.
void LogDispatchDiag(void* mainInterface, int slot, int index);

// True iff this entry carries KOTOR 2's medical-use handler (the two-step
// target-pick flow below). Always false on KOTOR 1.
bool EntryIsMedicalK2(void* mainInterface, int slot, int index);

// KOTOR 2 medical-item use, sent directly.
//
// K2 gives every medical-category item (medkits, stims, antidote and repair
// kits) a TWO-STEP use handler: the press ARMS a target-pick and only a
// mouse click on a party member portrait completes it. From the keyboard
// that flow is unreachable — DoPersonalAction's preamble wipes the pick
// state before the medical handler reads it, so a key press only ever
// re-arms and returns silently (the dead Medicine row, 2026-08-11 flip).
//
// This sends the same client→server use-item request the portrait click's
// consume path sends, aimed at targetClientCreature (a CLIENT creature —
// GetClientLeader / GetPartyCreatureBySlotK2). actionId is the entry's raw
// action id (ReadVariantActionId). The server does the rest: builds the
// UseItem action on that member's round (our AddAction hook announces it)
// and applies the item. True = request sent; the no-op watch still speaks
// if the server drops it.
bool SendUseItemRequestK2(uint32_t actionId, void* targetClientCreature);

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
