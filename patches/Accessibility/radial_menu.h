// Radial action menu — input gate, state, speech.
//
// Radial isn't a top-level CSWGuiPanel — it lives embedded inside
// CSWGuiMainInterface. So the panel-kind dispatch has nothing to bind to;
// we track our own active flag set by acc::picker::Drive after a
// successful PopulateMenus.
//
// While active:
//   Up/Down     — switch row (skips empty rows), speaks new selection.
//   Left/Right  — engine's SelectPrev/NextAction within the row.
//   Enter       — DoTargetAction(row) → engine walk-and-dispatch.
//   Esc         — drop the active flag (engine state stays; refreshed
//                 on next click / PopulateMenus).
//
// Cursor-decoupling (memory project_radial_cursor_coupling): the engine
// re-derives the target-action menu from the mouse cursor on every
// mouse-move, so a drifting cursor (common for keyboard-only / windowed
// users) silently empties our forced menu between Shift+Enter and Enter.
// We no longer trust the live menu between presses: each keypress
// re-anchors our target via picker::ReanchorRadial (rebuilds the menu for
// OUR target) before reading or dispatching, and the within-row variant
// index is tracked in our own state and re-applied after each rebuild.
// The radial stays armed until Esc / Enter-dispatch / re-arm / the target
// genuinely losing all actions — it is no longer torn down by cursor churn.

#pragma once

#include <cstdint>

namespace acc::radial_menu {

// True iff at least one row was populated (gate armed). targetServerHandle
// is cached for the per-keypress re-anchor (see header note above).
bool ArmAfterPopulate(const char* targetName, uint32_t targetServerHandle);

bool IsActive();

// Press-edge only; release events of consumed keys still pass through.
bool HandleInputEvent(int code, int value);

void Tick();

// First-cut accepts that engine state may persist until next click; we
// just give the user keyboard control back.
void ForceDisarm(const char* reason);

// Schedule N ticks of wide TAM diagnostic dumps. frames clamped [1..10].
// SEH-safe — reads only.
void ScheduleWideDiag(int frames, const char* tag);

}  // namespace acc::radial_menu
