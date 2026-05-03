// CSWGuiManager surface — singleton lookup, panels[] / modal_stack walking,
// cursor + click-sim primitives.
//
// Layer: engine/ (pure read-side helpers + raw PFN typedefs; no menu-side
// state, no engine re-entry beyond what the menu code explicitly invokes).
//
// Constants and PFN typedefs are at file scope rather than under
// `acc::engine` to match the convention used by `engine_input.h` /
// `engine_offsets.h` — callsites in the menu code stay readable
// (`*kAddrGuiManagerPtr`, `reinterpret_cast<PFN_MoveMouseToPosition>(...)`).
// Functions live in `acc::engine`.
//
// Address values verified against Lane's Ghidra DB / SARIF; see comments
// inline. K1 Steam build (GoG bytes match — see memory:
// project_ghidra_gog_steam_bytes_match).

#pragma once

#include <cstddef>
#include <cstdint>

namespace acc::engine {

// Find which panel in the manager's panels[] currently owns `control` —
// i.e. which one has it in its controls[]. Used as the fallback when a
// caller doesn't pass an explicit owner: `g_currentPanel` only updates on
// SetActiveControl, so chain rebinds and other indirect paths can land
// here while it still points at a previous focus owner.
//
// Returns nullptr if the manager isn't resolvable yet (early
// DLL_PROCESS_ATTACH) or `control` isn't in any current panel.
//
// Cheap by design: ≤16 panels × ≤32 children, only fires from the
// fallback path.
void* FindOwningPanel(void* control);

// Resolve the topmost (foreground) panel currently owned by the manager.
// Order:
//   1. If modal_stack is non-empty, return its top entry — the modal
//      currently capturing input.
//   2. Otherwise return the last entry in panels[] — last-pushed panel
//      is drawn on top in the engine's iteration order.
//   3. Returns nullptr if both are empty.
void* GetForegroundPanel(void* mgr);

// Diagnostic: dump every panel currently on the manager's panels array
// and modal_stack, plus the GetForegroundPanel result. Doubles as live
// verification that the SARIF-derived modal_stack offset truly points at
// a CSWGuiPanel** array.
void LogManagerStack(void* mgr, const char* tag);

}  // namespace acc::engine

// CSWGuiManager singleton. *kAddrGuiManagerPtr holds the live manager
// pointer; nullptr before the engine creates it (early DLL attach).
constexpr uintptr_t kAddrGuiManagerPtr = 0x007A39F4;

// CSWGuiManager layout (verified via Lane's SARIF, CSWGuiManager DT.Struct):
//   .panels      @ +0x88 — CExoArrayList<CSWGuiPanel*>
//                          data ptr (0x88), size (0x8c), cap (0x90)
//   .modal_stack @ +0x94 — GuiManagerModalStack (same shape as CExoArrayList)
//                          data ptr (0x94), size (0x98), cap (0x9c)
//
// The first 4 bytes of GuiManagerModalStack are unnamed in Ghidra but the
// structure has the same shape as CExoArrayList; PushModalPanel
// (0x0040bd90) and PopModalPanel (0x0040be00) are both RE'd, and
// GetPosInModalStack (0x0040ab70) implies the engine itself does index
// lookups against this stack — so it's a CSWGuiPanel** array. Treating the
// unnamed bytes as the data pointer is validated by LogManagerStack.
constexpr size_t kMgrPanelsDataOffset      = 0x88;
constexpr size_t kMgrPanelsSizeOffset      = 0x8c;
constexpr size_t kMgrModalStackDataOffset  = 0x94;
constexpr size_t kMgrModalStackSizeOffset  = 0x98;

// Cursor primitive: MoveMouseToPosition does cursor move + hover refresh
// (UpdateMouseOverControl) in one call. Updates only hover state —
// panel.activeControl lags behind unless explicitly committed (see
// PanelSetActiveControl in the menu-side code, or the click-sim path
// below which runs the engine's full press+release pipeline).
constexpr uintptr_t kAddrMoveMouseToPosition = 0x0040c790;
typedef void (__thiscall* PFN_MoveMouseToPosition)(void* gm, int x, int y);

// Click-sim primitives (Phase 3 — see docs/menu-nav-design.md).
//
// Decompilation summary (from Lane's gzf):
//
//   HandleLMouseDown(this, int press) @ 0x40c570
//     - XORs `press & 1` into bit 0 of state field at +0x1c.
//     - If no control is currently held: broadcasts event 0x1f9 to panels
//       (input class 3 only), runs UpdateMouseOverControl + tooltip-disable,
//       then dispatches via the engine's mouseOverControl (manager+0x8) →
//       vtable[6] = control's HandleLMouseDown. Button-class
//       HandleLMouseDown calls CaptureMouse, setting mouseHeldControl
//       (manager+0x10).
//     - Returns 1 if dispatched, 0 if a click is already in progress.
//
//   HandleLMouseUp(this) @ 0x40a170
//     - If a control is held (manager+0x10 non-null AND manager.mouse_held
//       == 1): dispatches mouseHeldControl → vtable[7] = control's
//       HandleLMouseUp. That's the function that fires the actual activate
//       event.
//     - Clears bit 0 of manager+0x1c. Returns 1 if dispatched, 0 otherwise.
//
// Calling them in sequence after MoveMouseToPosition has settled the
// cursor runs the engine's natural click pipeline end-to-end. This is
// the replacement for the SetActiveControl-based path that crashed at
// mgr+5 (see docs/tab-crash-investigation.md): we no longer skip the
// prelude HandleLMouseDown writes, so whatever invariant the engine
// maintains across press+release stays intact.
constexpr uintptr_t kAddrManagerLMouseDown = 0x0040c570;
constexpr uintptr_t kAddrManagerLMouseUp   = 0x0040a170;
typedef int (__thiscall* PFN_ManagerLMouseDown)(void* gm, int press);
typedef int (__thiscall* PFN_ManagerLMouseUp)(void* gm);
