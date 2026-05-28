# menus_pending.h (122 lines)

Public surface for the deferred-op queue (operations queued during input dispatch, drained on the next tick to avoid re-entrancy). Declares `namespace acc::menus::pending`.

## Declarations (in source order)

- L1 — `namespace acc::menus::pending`
- L10 — `bool QueueMoveCursor(int x, int y, void* target)` — MoveMouseToPosition only, no click
- L14 — `bool QueueClickAt(int x, int y, void* target)` — MoveMouseToPosition + HandleLMouseDown/Up
- L18 — `bool QueueActivate(void* target)` — vtable[15] FireActivate via HandleInputEvent(0x27, 1)
- L22 — `bool QueueEquipSelect(void* panel, void* slot)` — OnEnterSlot + OnSelectSlot for equip screen
- L26 — `bool QueueEquipCommit(void* panel, void* row, void* btn)` — OnItemSelected + OnOKPressed
- L30 — `bool QueueWorkbenchSlotSelect(void* panel, void* slot)` — OnEnterSlot + OnSlotSelected for workbench
- L34 — `bool QueueWorkbenchUpgradeCommit(void* panel, void* row, void* btnAssemble)` — OnUpgradeSelected + OnAssemble
- L38 — `bool QueueSliderInput(void* target, int code)` — vtable[15] HandleInputEvent with code 500/501
- L42 — `bool QueuePrevSWInGameGui()` — calls engine's CallPrevSWInGameGui to pop active sub-screen
- L46 — `bool QueueStoreItemActivate(void* panel, void* row)` — dispatches per-mode store click handler
- L50 — `bool IsPending()` — true when a non-None op is queued
- L52 — `void Drain(void* gm)` — execute the pending op against the GuiManager singleton
