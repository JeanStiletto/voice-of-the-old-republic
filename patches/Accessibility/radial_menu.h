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
// Self-dismisses when all action_lists[].size == 0 (engine cleared).

#pragma once

namespace acc::radial_menu {

// True iff at least one row was populated (gate armed).
bool ArmAfterPopulate(const char* targetName);

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
