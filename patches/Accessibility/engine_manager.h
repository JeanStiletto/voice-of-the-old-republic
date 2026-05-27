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

namespace acc::engine {

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
constexpr uintptr_t kAddrGuiManagerPtr = 0x007A39F4;

// CSWGuiManager layout (Lane's SARIF):
//   panels       @+0x88 — CExoArrayList<CSWGuiPanel*> (data 0x88, size 0x8c, cap 0x90)
//   modal_stack  @+0x94 — same shape (data 0x94, size 0x98, cap 0x9c)
// PushModalPanel/PopModalPanel/GetPosInModalStack confirm modal_stack
// is CSWGuiPanel** indexable like CExoArrayList. LogManagerStack
// validates the offsets at runtime.
constexpr size_t kMgrPanelsDataOffset      = 0x88;
constexpr size_t kMgrPanelsSizeOffset      = 0x8c;
constexpr size_t kMgrModalStackDataOffset  = 0x94;
constexpr size_t kMgrModalStackSizeOffset  = 0x98;

// Cursor move + hover refresh in one call. Updates hover only;
// panel.activeControl lags unless explicitly committed (PanelSetActive-
// Control, or the click-sim path which runs full press+release).
constexpr uintptr_t kAddrMoveMouseToPosition = 0x0040c790;
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
constexpr uintptr_t kAddrManagerLMouseDown = 0x0040c570;
constexpr uintptr_t kAddrManagerLMouseUp   = 0x0040a170;
typedef int (__thiscall* PFN_ManagerLMouseDown)(void* gm, int press);
typedef int (__thiscall* PFN_ManagerLMouseUp)(void* gm);
