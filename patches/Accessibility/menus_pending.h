// KOTOR Accessibility — deferred menu-side operation queue.
//
// Step 3 of the menus.cpp refactor. The six (g_pendingClick,
// g_pendingActivate, g_pendingCursorMove, g_pendingSliderInput,
// g_pendingEquipSelect, g_pendingEquipCommit) flag/target pairs collapse
// to a single tagged op stored here. Input handlers in menus.cpp call
// `Queue*` to enqueue; `Drain` runs once per tick from `TickPendingOps`
// and dispatches the op against deep engine functions (MoveMouseToPosition,
// manager LMouseDown/Up, vtable[15] HandleInputEvent, OnEnterSlot/
// OnSelectSlot, OnItemSelected/OnOKPressed).
//
// Why deferred: invoking these mid-input-dispatch recurses through
// HandleMouseMove → UpdateMouseOverControl, the same re-entrancy class
// that destabilised earlier listbox-entry hooks. Deferring to the next
// CSWGuiManager::Update tick (~16ms at 60fps; inaudible) keeps deep
// engine re-entry off the input-hook stack. Tolk speech still fires
// synchronously from the input hook so the audible response feels
// instantaneous.
//
// Queue depth: one op at a time. The `ClickAt` op atomically does both
// cursor-warp and LMouseDown/Up on drain — that's what tab-button
// activation needs. Every Queue* returns false if an op is already
// pending; callers log + treat the keypress as consumed.

#pragma once

namespace acc::menus::pending {

// Queue a cursor-warp to (x, y). On drain calls MoveMouseToPosition(gm, x, y).
// `target` is the chain control we're warping to — kept on the op struct
// for diagnostic logging only (the self-dedup that used to consume it via
// CursorMoveTarget() / ClearCursorMoveTarget() was replaced by the
// channel-0 dedup primed from AnnounceControl's MarkSpoken call).
bool QueueMoveCursor(int x, int y, void* target);

// Queue a click-at-point. On drain runs MoveMouseToPosition(gm, x, y) then
// the manager's LMouseDown(gm, 1) + LMouseUp(gm). Used for tab-button
// activation, where direct vtable[15] FireActivate no-ops (tabs gate on
// is_active which only HandleLMouseDown sets). `target` is the intended
// click target — diagnostic logging only (see QueueMoveCursor above).
bool QueueClickAt(int x, int y, void* target);

// Queue a direct-activate via vtable[15].HandleInputEvent(0x27, 1). Bypasses
// hit-test entirely — used when the click target is covered by another
// control's extent (Schliess. inside an Options listbox, settings buttons,
// cycle-arrow flankers, Esc → cancel/close button, container give-mode).
bool QueueActivate(void* target);

// Queue an equip-screen slot activation. On drain raises slot.is_active = 1
// then calls OnEnterSlot(panel, slot) + OnSelectSlot(panel, slot). Bypasses
// click-sim because the equip panel's labels cover the slot buttons in
// z-order (see docs/equip-flow-investigation.md).
bool QueueEquipSelect(void* panel, void* slot);

// Queue an equip-row commit. On drain raises row.is_active and btn.is_active,
// then calls OnItemSelected(panel, row) + OnOKPressed(panel, btn).
// OnItemSelected is what actually calls EquipItem; OnOKPressed is cleanup
// (closes the description popup).
bool QueueEquipCommit(void* panel, void* row, void* btn);

// Queue a slider value adjustment via vtable[15].HandleInputEvent(code, 1)
// where code is 500 (increment) or 501 (decrement). The slider's handler
// runs the full pipeline: SetCurValue + bounds clamp + gui_object callback
// + PlayGuiSound feedback. Per-frame focus monitor catches the cur_value
// change at +0x74 on the next tick and re-announces.
bool QueueSliderInput(void* target, int code);

// Queue a CGuiInGame::PrevSWInGameGui dispatch. On drain calls
// acc::engine::CallPrevSWInGameGui() which pops the active in-game
// sub-screen via the engine's own primitive (cleaner than FireActivate on
// each sub-screen's Schliess button — see drill-back Esc handler in
// menus.cpp). Deferred to drain for the same reason as the other ops:
// modifying panels[] mid-input-dispatch can re-enter through the engine's
// hover/focus paths.
bool QueuePrevSWInGameGui();

// Queue a CGuiInGame::SwitchToSWInGameGui dispatch with the given engine
// GUI_id (0..7 — see acc::engine::CallSwitchToSWInGameGui for the mapping).
// On drain calls acc::engine::CallSwitchToSWInGameGui(guiId), which goes
// through our redrill-cleanup detour at 0x62cf2d, so the previous
// sub-screen is popped from panels[] before the new one is pushed.
// Used by the Tab / Shift+Tab inter-panel cycle handler in menus.cpp.
bool QueueSwitchSubScreen(int guiId);

// True if an op is currently queued. All input-handler debounce sites
// uniformly check this before queueing — single-slot queue means there's
// no per-kind discrimination to do (the old code's per-site debounce
// subsets were inconsistent; uniformly safe now).
bool IsPending();

// Drain the queue. Called once per tick from TickPendingOps after all
// monitors have run. `gm` is the CSWGuiManager singleton; if null, the
// queue is reset and a diagnostic logged.
void Drain(void* gm);

}  // namespace acc::menus::pending
