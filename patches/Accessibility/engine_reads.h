// SEH-guarded read helpers for KOTOR GUI controls. Safe from hook
// handlers that may run during engine mid-teardown — every deref is
// __try-wrapped, every potentially-stale pointer is validated before use.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"

namespace acc::engine {

// True iff the control had non-empty tooltip text.
// +0x28 tooltip_string (CExoString), +0x50 id.
bool ReadControlNameFields(void* control, const char*& outTip,
                           uint32_t& outTipLen, int& outId);

// vtable[index](control) as __thiscall.
//
// SEH-wrapped: per-tick monitors walk panels[] and call this on every
// child control. During engine teardown (FireActivate("Spielen") /
// quit-confirm OK) the engine frees children synchronously while the
// panel pointer can still resolve next tick. The freed control's vtable
// slot yields garbage; without SEH the deref would crash. Faults map
// to "not this subclass" so the caller skips the stale control.
void* CallDowncast(void* control, int vtableIndex);

// CExoString at base+offset. True if non-empty and length sane.
bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize);

uint32_t ReadU32(void* base, size_t offset);

// SEH-wrapped TLK lookup. False on invalid strref, missing TLK pointer,
// or any fault.
bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize);

// Mirrors CSWGuiControl::DisplayToolTip @0x418a90:
//   1. tooltip_strref at +0x24 → TLK lookup.
//   2. tooltip_string at +0x28 if non-empty.
//   3. Bubble up to parent_control at +0x14 and retry.
// SEH-guarded.
bool ReadControlTooltip(void* control, char* outBuf, size_t bufSize);

// Read directly from CAurGUIStringInternal — the engine's actually-
// rendered string. CSWGuiText::Draw reads ONLY through gui_string;
// text_params is unused at draw time. gui_string is the ground truth
// when a control has visible rendered text.
//
// guiStringPtrOffset = offset of the CAurGUIStringInternal* field
// within `control` (CSWGuiLabel 0xE4, CSWGuiButton 0x168).
//
// We vtable-check guiString before dereffing at +0x14: chargen Class
// buttons reach our chain in a state where +0x168 transiently holds a
// non-null garbage pointer (engine writes invalid between reads). The
// vtable check skips garbage without relying on SEH to absorb the AV
// — under /GS the AV unwind can fastfail with c0000409 instead of
// being absorbed. SEH stays as defense for unmapped-memory faults.
bool ReadGuiString(void* control, size_t guiStringPtrOffset,
                   char* outBuf, size_t bufSize);

// Inline CExoString, then TLK strref fallback.
bool ExtractTextOrStrRef(void* control,
                         size_t cexoOffset, size_t strRefOffset,
                         char* outBuf, size_t bufSize);

// Try every text path:
//   1. CAurGUIStringInternal at gui_string (preferred — reflects render).
//   2. Inline CExoString (text_params.text).
//   3. strref → TLK lookup.
//   4. text_object indirection (defensive fallback).
//
// gui_string first because overridden subclasses (CSWGuiInGameMenu's
// icon labels are the canonical case) leave the other paths empty.
bool ExtractTextOrStrRefIndirect(void* control,
                                 size_t cexoOffset, size_t strRefOffset,
                                 size_t textObjectOffset,
                                 char* outBuf, size_t bufSize);

// Read a CSWGuiLabel's rendered text — engine's resolved gui_string path
// with the strref / CExoString / text_object fallback chain (i.e. the
// label-specific shape of ExtractTextOrStrRefIndirect). SEH-guarded;
// outBuf = "" on miss. Pass a label-control pointer (NOT a panel — for
// the (panel, offset) convenience form use ReadLabelTextAt below).
bool ReadLabelText(void* label, char* outBuf, size_t bufSize);

// Convenience: panel + label-field offset, for the common case where a
// label is embedded in the panel struct at a known field.
inline bool ReadLabelTextAt(void* panel, size_t offset,
                            char* outBuf, size_t bufSize) {
    if (!panel) {
        if (outBuf && bufSize > 0) outBuf[0] = '\0';
        return false;
    }
    return ReadLabelText(
        reinterpret_cast<unsigned char*>(panel) + offset, outBuf, bufSize);
}

// Read a CSWGuiButton's rendered text — same two-step shape against the
// button's own field offsets (kButton{GuiStringPtr,Text,StrRef,TextObject}
// Offset). SEH-guarded; outBuf = "" on miss.
bool ReadButtonText(void* button, char* outBuf, size_t bufSize);

// Used by ExtractAnnounceableText (which field reads) and
// IsChainNavigable (where chain nav can land).
bool IsToggle(void* control);
bool IsSlider(void* control);
bool IsListBox(void* control);
bool IsEditbox(void* control);

// CSWGuiButtonToggle.field2_0x1c8 bit 0 — HandleInputEvent XORs on
// activate, Draw branches on (& 1) for which border to render.
bool ReadToggleState(void* toggle);

// vtable[0..7] dump. Each CSWGui subclass has a unique vtable address,
// so this correlates unknown controls back to specific classes via SARIF.
void DumpControlVtable(void* control, char* out, size_t outSize);

// Convert a client-side object id to its server-side id via
// CServerExoApp::ClientToServerObjectId. Returns 0 on null/invalid input or
// any resolution failure. Pairs with engine_area::ResolveServerObjectHandle
// to cross from a client id to the server CSWSObject*.
uint32_t ClientToServerObjectId(uint32_t clientHandle);

// Client-side handles (the kind in row+0x1c4 on CSWGuiStoreItemEntry,
// CSWGuiInGameItemEntry in Container/Equip-picker) → CSWSItem*. Walks
// AppManager → CServerExoApp + ClientToServer + GetItemByGameObjectID
// with SEH per hop. Null on any failure.
void* ResolveItemFromClientHandle(uint32_t clientHandle);

// Multi-line property description (damage / range / on-hit / defence /
// base description). Engine allocates a heap c_string per call which
// we deliberately leak (CRT-mismatch — same pattern as LookupTlk).
bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize);

// Workbench upgrade slot → installed-mod item. Given the CSWGuiUpgrade panel
// and a slot button control (upgrade.gui IDs 12..18), reads the slot button's
// custom_value and returns the installed mod CSWSItem* at
// field35_0x2f74[custom_value], or nullptr when the slot is empty / inputs
// invalid. The returned pointer is a real CSWSItem* (engine-constructed from
// the mod template) and is safe to pass to ReadItemPropertyDescription /
// ExtractTextOrStrRef(+0x280). SEH-guarded. See kUpgradeSlotInstalledItemsOff.
void* GetWorkbenchSlotInstalledItem(void* upgradePanel, void* slotControl);

// CSWGuiInterfaceAction.action_id → human-readable description.
// Dispatches on the high-nibble tag stamped by each entry-creator:
//   0x1xxxxxxx feat        — CSWRules::GetFeat → CSWFeat::GetDescriptionText
//   0x2xxxxxxx force power — CSWSpellArray::GetSpell → spell_description
//                            strref → LookupTlk
//   0x4xxxxxxx item        — CServerExoApp::GetItemByGameObjectID →
//                            CSWSItem::GetPropertyDescription
// All other categories (attack verbs, door open/unlock, computer hack,
// etc.) return false — those are plain verbs the engine never surfaces
// extra text for; caller falls back to localised "no description
// available" cue.
//
// Tag encodings decoded via Ghidra decompile of CSWCCreature::
// CreateUsableItemEntry @0x006193a0, ClassInfo::GetMenuInfo @0x0064a870,
// and EnableFeatForMenu @0x00618a30.
bool ResolveActionDescriptionFromActionId(uint32_t actionId,
                                          char* outBuf, size_t bufSize);

// Returns:
//   > 1   stackable; caller speaks the count suffix.
//     1   single — caller stays silent (saying "1" is noise).
//     0   not an item-entry row, unresolved, or store infinite stock.
//
// Engine renders the count overlay when stack_size >= 2 (SetItem
// confirmed for both inventory + store row vtables).
int ReadItemRowStackCount(void* rowControl);

// Charge readers — for limited-use consumables that carry a current /
// max charge count (CSWSItem.charges / max_charges). Charged items can't
// stack, so a positive result here never coincides with a stack suffix.
//   ReadItemCharges(item)        : >= 0 current charges when max_charges>0,
//                                  else -1 (not a charged item / fault).
//   ReadItemRowCharges(rowCtrl)  : same, resolved from an inventory/store
//                                  row control; -1 when not an item row.
int ReadItemCharges(void* item);
int ReadItemRowCharges(void* rowControl);

// Raw stack_size for a resolved CSWSItem* (0 on fault / infinite stock).
// The row/handle wrappers above are preferred for menu rows; this is for
// callers that already hold the item* (store suffix, action-menu items).
int ReadItemStack(void* item);

// Resolve the CSWSItem* behind an item-tagged CSWGuiInterfaceAction
// action_id (high nibble 0x4xxxxxxx, low 28 bits = server game-object id).
// Returns nullptr for non-item tags / unresolved handles. Lets the action
// menu read stack + charges for its "Gegenstände" entries the same way
// inventory rows do.
void* ItemFromActionId(uint32_t actionId);

// CSWGuiInGameItemEntry rows (Inventory + Container loot listbox).
// Store rows excluded — they get a price+stock suffix from
// menus_store::AnnounceChainStepSuffix instead.
bool IsInventoryItemRow(void* control);

// Read a client creature's live Force pool. Walks CSWCCreature+0x2f8 →
// CSWCLevelUpStats (embeds CSWCCreatureStats at +0), then reads
// max_force_points @+0x11e and the live current pool as the sum of the two
// shorts at +0x122/+0x124 (matching CSWGuiInGameCharacter::SetStats, which
// reads this client struct; the +0x120 "force_points" field is a static base,
// not the live current).
// Same struct combat_query reads HP from at +0x4c/+0x4e. SEH-guarded; returns false
// on null/fault. *outMax == 0 is a VALID result meaning "not a Force
// user" (non-Jedi classes, droids) — callers gate the FP display on
// *outMax > 0, NOT on the return value. `clientCreature` is a
// CSWCCreature* (GetClientLeader(), or the charsheet panel's displayed-
// creature pointer at +0x59e4).
bool ReadCreatureForcePoints(void* clientCreature, int* outCur, int* outMax);

}  // namespace acc::engine
