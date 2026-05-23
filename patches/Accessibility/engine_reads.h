// SEH-guarded read helpers for KOTOR GUI controls.
//
// Layer: engine/ (pure read-side helpers; no menu-side state, no engine
// re-entry). Every function in this header is safe to call from a hook handler
// that may be invoked while the engine is mid-teardown — every dereference of
// engine memory is wrapped in __try/__except, every potentially-stale pointer
// is validated before use.
//
// Functions are namespaced under `acc::engine` to match the convention from
// `engine_input.h`. The struct-offset constants and engine-data structs live
// in `engine_offsets.h` at file scope (also matching the input convention) so
// menu-side callsites stay readable.

#pragma once

#include <cstddef>
#include <cstdint>

#include "engine_offsets.h"

namespace acc::engine {

// Read CSWGuiControl name fields at known offsets (no engine re-entry).
// Returns true if the control had non-empty tooltip text.
//   0x28: tooltip_string (CExoString = char* c_string; uint32 length)
//   0x50: id (int)
bool ReadControlNameFields(void* control, const char*& outTip,
                           uint32_t& outTipLen, int& outId);

// Generic vtable downcast caller: invokes vtable[index](control) as __thiscall.
//
// SEH-wrapped because the per-tick monitors (MonitorPanelContents,
// MonitorDialogReplies) walk panels[] and call this on every child control.
// During an engine teardown — e.g. inside FireActivate("Spielen") starting
// the new-game flow, or FireActivate("OK") on the quit-confirm — the engine
// frees a panel's child controls synchronously while the panel pointer can
// still resolve from panels[] on the next MainLoop tick. Reading the freed
// control's vtable slot then yields garbage (observed: 0xbf800000 = float
// -1.0 bits, the engine reused the page for model data) and dereferencing
// vtable[index] crashes. Treating any fault as "not this subclass" lets the
// caller skip the stale control without taking down the process.
void* CallDowncast(void* control, int vtableIndex);

// Read a CExoString at (base + offset) into outBuf. Returns true if non-empty
// and length is sane (< bufSize). CExoString = { char* c_string; uint32 length }.
bool ReadCExoString(void* base, size_t offset, char* outBuf, size_t bufSize);

// Read a uint32 at base+offset.
uint32_t ReadU32(void* base, size_t offset);

// Resolve a strref via the engine's TLK lookup, with SEH guard. Returns false
// for invalid strref values, missing TLK pointer, or any raised exception.
bool LookupTlk(uint32_t strref, char* outBuf, size_t bufSize);

// Read a null-terminated c_string from CAurGUIStringInternal. Bypasses all
// the CExoString / strref / text_object indirection — goes straight to the
// engine's actually-rendered string.
//
// Confirmed by decompiling CSWGuiText::Initialize (0x00417310) and
// CAurGUIStringInternal::CAurGUIStringInternal (0x0045B990): the constructor
// allocates a heap buffer and copies the c_string into it at offset +0x14
// of CAurGUIStringInternal (Ghidra-named field5_0x14). CSWGuiText::Draw at
// 0x00416240 reads ONLY through gui_string — text_params is unused at draw
// time. So gui_string is the ground-truth source whenever a control has
// rendered visible text.
//
// `guiStringPtrOffset` is the offset within `control` to the
// `CAurGUIStringInternal*` field (i.e. CSWGuiText.gui_string). For
// CSWGuiLabel that's 0xE4; for CSWGuiButton that's 0x168.
//
// Vtable check on guiString validates this is actually a
// CAurGUIStringInternal before we deref it at +0x14. Motivation: chargen
// Class panel buttons (vtable=0x73E658, the standard CSWGuiButton) reach
// our chain in a state where `[control + 0x168]` is sometimes a non-null
// garbage value (observed: 0xae0f1673 in patch-20260503-162139.log,
// crashing at `mov ecx,[ecx+14h]` / DLL RVA 0x2c9e). The same control
// reads safely as null on a prior tick — i.e. the engine transiently
// writes an invalid pointer into this field between reads. Verifying the
// vtable matches CAurGUIStringInternal's known address skips garbage
// values without depending on SEH to absorb the AV (the crash report
// shows that under /GS the AV unwind can fastfail with c0000409 instead
// of being absorbed by our __try/__except). SEH is kept as defense for
// the rarer case where guiString itself points at unmapped memory and
// the vtable read faults.
bool ReadGuiString(void* control, size_t guiStringPtrOffset,
                   char* outBuf, size_t bufSize);

// Resolve the visible text of a button/label-like subclass: try the inline
// CExoString first, then fall back to TLK str_ref lookup.
bool ExtractTextOrStrRef(void* control,
                         size_t cexoOffset, size_t strRefOffset,
                         char* outBuf, size_t bufSize);

// Resolve text trying every known path:
//   1. CAurGUIStringInternal at gui_string (the engine's actual render
//      source — works for any control with visible rendered text)
//   2. inline CExoString (text_params.text)
//   3. strref → TLK lookup
//   4. text_object indirection (rarely used, kept as defensive fallback)
//
// gui_string is tried first because it reflects the rendered state — the
// other paths can be empty for overridden subclasses (CSWGuiInGameMenu icon
// labels are the canonical case) but gui_string is always populated when
// the control has visible text.
//
// SEH-guarded reads across the indirection paths.
bool ExtractTextOrStrRefIndirect(void* control,
                                 size_t cexoOffset, size_t strRefOffset,
                                 size_t textObjectOffset,
                                 char* outBuf, size_t bufSize);

// Element-class identity helpers. Used by ExtractAnnounceableText to decide
// which subclass-specific field reads to perform, and by IsChainNavigable to
// decide which controls keyboard chain-navigation can land on.
bool IsToggle(void* control);
bool IsSlider(void* control);
bool IsListBox(void* control);
bool IsEditbox(void* control);

// Read CSWGuiButtonToggle.field2_0x1c8's bit 0. Decompiled HandleInputEvent
// XOR's this bit on every activate, and SetSelected masks param to bit 0;
// Draw branches on (field2 & 1) to pick which border to render. So the
// rendered "checked" state is exactly bit 0 of this field.
bool ReadToggleState(void* toggle);

// Read vtable[0..7] from a control. Used as a diagnostic: dumping the vtable
// pointer + a few entries lets us correlate unknown controls back to specific
// CSWGui subclasses via the SARIF (each class has a unique vtable address).
// Caller is responsible for null-checks.
void DumpControlVtable(void* control, char* out, size_t outSize);

// Resolve a client-side game-object handle (the kind stored in row+0x1c4 on
// CSWGuiStoreItemEntry, CSWGuiInGameItemEntry inside Container/Equip-picker)
// to a CSWSItem*. Walks AppManager → CServerExoApp, then runs
// ClientToServerObjectId → GetItemByGameObjectID with SEH guarding at each
// hop. Returns nullptr on any null link, invalid handle (0 / 0xffffffff),
// or raised exception. Safe to call from monitor ticks during teardown.
void* ResolveItemFromClientHandle(uint32_t clientHandle);

// Same shape as ResolveItemFromClientHandle but skips the ClientToServer
// translation — for handles already known to be server-side, e.g. the
// per-slot ulongs stored in CSWInventory which the equip panel mirrors
// into its own cache. Returns nullptr on null link, invalid handle, or
// raised exception.
void* ResolveItemFromServerHandle(uint32_t serverHandle);

// Read a CSWSItem's full property description (the multi-line text the
// Inventory/Store/Equip panels render into their description listbox on
// hover): damage, range, on-hit, defence, base description, etc. Writes up
// to `bufSize-1` bytes to `outBuf` and null-terminates; returns true on a
// non-empty result. The engine allocates a fresh heap c_string for each
// call which we deliberately leak (see same pattern in LookupTlk).
bool ReadItemPropertyDescription(void* item, char* outBuf, size_t bufSize);

// Resolve a CSWGuiInGameItemEntry / CSWGuiStoreItemEntry row control to its
// owned-stack count. Returns:
//   > 1   — stackable item with that many copies (caller speaks suffix)
//     1   — single instance (caller stays silent — saying "1" is noise)
//     0   — not an item-entry row, item didn't resolve, or store-side
//           infinite-stock flag is set. Caller stays silent.
//
// The engine renders the count overlaid on the item icon when
// stack_size >= 2 (CSWGuiStoreItemEntry::SetItem decomp confirms the same
// SetItem code path is used for both row vtables). For screen-reader users
// this overlay is invisible; this helper exposes the value so the caller
// can speak it.
int ReadItemRowStackCount(void* rowControl);

// True iff `control` is a CSWGuiInGameItemEntry (rows of
// CSWGuiInGameInventory.item_listbox and CSWGuiContainer's loot listbox).
// Used at chain-announce time to gate the stack-count suffix: store rows
// are deliberately excluded because they already get their own
// price + stock suffix from menus_store::AnnounceChainStepSuffix.
bool IsInventoryItemRow(void* control);

}  // namespace acc::engine
