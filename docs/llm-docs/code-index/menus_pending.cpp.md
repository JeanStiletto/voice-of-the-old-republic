# menus_pending.cpp (705 lines)

Deferred-op queue implementation. All operations are queued from input hooks and executed in `Drain` which is called from `core_tick::Dispatch` after monitors run.

## Declarations (in source order)

- L39 — `enum class Kind` — None, MoveCursor, ClickAt, Activate, EquipSelect, EquipCommit, WorkbenchSlotSelect, WorkbenchUpgradeCommit, SliderInput, PrevSWInGameGui, StoreItemActivate (anonymous ns)
- L53 — `struct PendingOp` — fields: kind, a/b/c (void* params), x/y (int coords), code (int) (anonymous ns)
- L63 — `PendingOp g_op` — the single pending-op slot (anonymous ns)
- L68 — `constexpr int kVtableHandleInputEvent = 15` (anonymous ns)
- L68 — `typedef ... PFN_ControlHandleInputEvent` (anonymous ns)
- L71 — `void Reset()` — zeroes g_op (anonymous ns)
- L77 — `bool acc::menus::pending::QueueMoveCursor(int x, int y, void* target)`
- L86 — `bool acc::menus::pending::QueueClickAt(int x, int y, void* target)`
- L95 — `bool acc::menus::pending::QueueActivate(void* target)`
- L102 — `bool acc::menus::pending::QueueEquipSelect(void* panel, void* slot)`
- L110 — `bool acc::menus::pending::QueueEquipCommit(void* panel, void* row, void* btn)`
- L119 — `bool acc::menus::pending::QueueWorkbenchSlotSelect(void* panel, void* slot)`
- L127 — `bool acc::menus::pending::QueueWorkbenchUpgradeCommit(void* panel, void* row, void* btnAssemble)`
- L136 — `bool acc::menus::pending::QueueSliderInput(void* target, int code)`
- L144 — `bool acc::menus::pending::QueuePrevSWInGameGui()`
- L151 — `bool acc::menus::pending::QueueStoreItemActivate(void* panel, void* row)`
- L159 — `bool acc::menus::pending::IsPending()`
- L163 — `void acc::menus::pending::Drain(void* gm)` — executes the pending op via a switch on Kind; handles all 10 non-None kinds; includes LevelUp Annehmen self-destroy guard, chargen-sub-close chain-invalidate, and WorkbenchSlotSelect non-saber install/remove announce
