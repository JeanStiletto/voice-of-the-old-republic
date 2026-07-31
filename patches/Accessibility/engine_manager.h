// CSWGuiManager surface — singleton lookup, panels[]/modal_stack walks,
// cursor + click-sim primitives.
//
// Pure read + raw PFN typedefs. Constants and PFN at file scope (matches
// engine_input.h / engine_offsets.h) so callsites read clean
// (`*kAddrGuiManagerPtr`, reinterpret_cast<PFN_*>(...)).
//
// GoG bytes match Steam — addresses lift directly from Lane's DB.

#pragma once

#include <cstddef>
#include <cstdint>
#include "engine_rebase.h"
#include "engine_offsets_select.h"

namespace acc::engine {

// *kAddrGuiManagerPtr, SEH-guarded. Null before the engine creates it
// (early attach) and after teardown.
void* GetGuiManager();

// Snapshot the manager's panels[] / modal_stack into a caller-owned buffer.
//
// Reading these arrays is the single most-repeated engine access in the
// patch — it appeared eighteen times across eight files, and EIGHT of those
// copies had no SEH guard at all, including two sitting directly above
// GetForegroundPanel in engine_manager.cpp, whose own comment spells out the
// hazard: the null checks cannot catch a STALE manager or panel pointer,
// which is what this array holds during teardown.
//
// Copying (rather than handing back the engine's `data` pointer) is the
// point: the copy happens inside the guard, so callers iterate their own
// memory and a torn-down array cannot fault them mid-loop.
//
// `maxEntries` is the caller's sanity bound against a corrupt size field —
// it is NOT an engine limit. Every site passes 32. The old copies disagreed
// (most 16, four 32), which meant two of our own queries could contradict
// each other about the same panel whenever more than 16 were live. Rare, but
// observed: patch-20260530-112606.log recorded panels.size climbing to 27
// with modal.size 24. Unified on 32 (2026-07-30, user's call) — it is a
// 128-byte stack buffer and can only let a query see more of the truth.
// `outRawCount`, if given, receives the UNCLAMPED size field (the manager
// diagnostics print it).
//
// Returns the number of entries copied; 0 on null manager, empty array or
// fault. Entries may still be stale engine pointers — guard what you DO
// with them.
int ReadPanelArray(void* mgr, void** outPanels, int maxEntries,
                   int* outRawCount = nullptr);
int ReadModalStack(void* mgr, void** outModal, int maxEntries,
                   int* outRawCount = nullptr);

// Fallback when caller doesn't pass owner explicitly — g_currentPanel
// only updates on SetActiveControl; chain rebinds can land here while it
// points at a previous owner. ≤16 panels × ≤32 children, only fires
// from the fallback path.
void* FindOwningPanel(void* control);

// Pointer-equality scan over panels[]. No deref of `panel` — safe with
// stale/wild pointers. Used by FromControl so a freed g_currentPanel
// (e.g. Annehmen destroys CSWGuiLevelUpPanel synchronously during
// FireActivate's vtable[15] dispatch) doesn't reach panel-kind probes
// that deref *panel.
bool IsPanelInManager(void* panel);

// Topmost owner: modal_stack top if non-empty, else last panels[] entry
// (last-pushed draws on top), else nullptr.
void* GetForegroundPanel(void* mgr);

// Dumps panels + modal_stack + GetForegroundPanel. Doubles as live
// verification of the SARIF-derived modal_stack offset.
void LogManagerStack(void* mgr, const char* tag);

}  // namespace acc::engine

// *kAddrGuiManagerPtr; nullptr before engine creates it (early attach).
// KOTOR 2 value from the seeded kotor2_steam_aspyr.db (GUI_MANAGER_PTR); its
// KOTOR 1 value matches this one exactly.
const uintptr_t kAddrGuiManagerPtr = acc::addr::PickGlobal(0x007A39F4, 0x00A1B49C);

// CSWGuiManager layout (Lane's SARIF):
//   panels       @+0x88 — CExoArrayList<CSWGuiPanel*> (data 0x88, size 0x8c, cap 0x90)
//   modal_stack  @+0x94 — same shape (data 0x94, size 0x98, cap 0x9c)
// PushModalPanel/PopModalPanel/GetPosInModalStack confirm modal_stack
// is CSWGuiPanel** indexable like CExoArrayList. LogManagerStack
// validates the offsets at runtime.
// Identical in both games — observed, not inferred. KOTOR 2's own
// CSWGuiManager::HitCheckMouse (FUN_00411030) reads modal_stack size at +0x98,
// falls back to panels size at +0x8c, and indexes the arrays at +0x94 and
// +0x88, exactly as KOTOR 1 does.
//
// CSWGuiManager appears not to have grown at all: its input-code field is at
// +0x68 in both games too (seen in HandleInputEvent). That is unlike
// CSWGuiPanel and CSWGuiControl, which both shift +4 — so the manager is a
// class where "same name, same layout" genuinely holds, and the panel arrays
// that foreground resolution depends on carry over unchanged.
const size_t kMgrPanelsDataOffset      = acc::off::Same(0x88);
const size_t kMgrPanelsSizeOffset      = acc::off::Same(0x8c);
const size_t kMgrModalStackDataOffset  = acc::off::Same(0x94);
const size_t kMgrModalStackSizeOffset  = acc::off::Same(0x98);
// The manager's "current input code" field, written by HandleInputEvent as its
// first action in both games. The KOTOR 2 hook needs it by name: its cut bytes
// perform exactly this store, and they are skipped (skip_original_bytes) because
// their first instruction clobbers EAX — the consume-signal register — so the
// handler replays the store itself. See OnHandleInputEventK2.
const size_t kMgrInputCodeOffset       = acc::off::Same(0x68);

// Cursor move + hover refresh in one call. Updates hover only;
// panel.activeControl lags unless explicitly committed (PanelSetActive-
// Control, or the click-sim path which runs full press+release).
//
// KOTOR 2: 0x00414230. An earlier attempt to find this worked backwards from a
// HandleMouseMove candidate and failed. Working FORWARDS from the callers
// settled it, with three independent signals:
//   * All three KOTOR 1 callers are `OnPanelAdded` — a virtual — so the
//     vtable-slot map names their KOTOR 2 twins directly. CSWGuiContainer,
//     CSWGuiSaveNamePanel and CSWGuiStatusSummary each call StoreCurrent-
//     MousePosition and then this, through the manager singleton, in that
//     order, in both games. The KOTOR 2 pair is 0x00414150 then 0x00414230.
//   * Shape: KOTOR 1's body makes exactly two calls (CExoInput::SetMousePos,
//     CSWGuiManager::HandleMouseMove) and KOTOR 2's makes exactly two.
//   * Its second callee, 0x00412c00, calls 0x00411030 — which is the
//     CSWGuiManager::HitCheckMouse this port identified separately, and which
//     is likewise the first thing KOTOR 1's HandleMouseMove calls. So the
//     callee really is HandleMouseMove, which is what pins this function.
//
// **The KOTOR 2 address is correct but does NOT do what KOTOR 1's does.**
// Tested in game 2026-07-31: calling it in place of the OS-level cursor warp
// regressed KOTOR 2 menu navigation straight back to the original bug (focus
// dragged to one button after every keypress). KOTOR 2's CExoInput::SetMousePos
// writes only the engine's own copy of the cursor, which the engine overwrites
// from the true mouse on the next frame; KOTOR 1's moves the real pointer. So on
// KOTOR 2 this is useful for the hit-test/hover half only, and the cursor has to
// be moved with SetCursorPos — see WarpCursorToControl in menus_focus_k2.cpp.
const uintptr_t kAddrMoveMouseToPosition = acc::addr::Pick(0x0040c790, 0x00414230);
typedef void (__thiscall* PFN_MoveMouseToPosition)(void* gm, int x, int y);

// Click-sim primitives.
//
// HandleLMouseDown(press) @0x40c570:
//   XOR (press & 1) into bit 0 at +0x1c. If no control held: broadcast
//   0x1f9 to input-class-3 panels, run UpdateMouseOverControl, dispatch
//   via mouseOverControl → vtable[6] = HandleLMouseDown. Button-class
//   CaptureMouse sets mouseHeldControl at +0x10. Returns 1 dispatched,
//   0 if a click is already in progress.
//
// HandleLMouseUp @0x40a170:
//   If a control is held (+0x10 non-null AND mouse_held == 1): dispatch
//   mouseHeldControl → vtable[7] = HandleLMouseUp (the activate-fire).
//   Clears bit 0 of +0x1c. Returns 1 dispatched, 0 otherwise.
//
// Calling them in sequence after MoveMouseToPosition settles the cursor
// runs the engine's natural click pipeline end-to-end. Replaces the
// SetActiveControl path that crashed at mgr+5: we no longer skip the
// HandleLMouseDown prelude writes, so the engine's press+release
// invariant stays intact.
const uintptr_t kAddrManagerLMouseDown = acc::addr::R(0x0040c570);
const uintptr_t kAddrManagerLMouseUp = acc::addr::R(0x0040a170);
typedef int (__thiscall* PFN_ManagerLMouseDown)(void* gm, int press);
typedef int (__thiscall* PFN_ManagerLMouseUp)(void* gm);
