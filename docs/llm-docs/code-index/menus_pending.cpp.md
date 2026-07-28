# menus_pending.cpp (802 lines)

Single-slot deferred menu-operation queue: input handlers across the menu
modules call `Queue*` to stage one op (cursor move, click, activate, equip
select/commit, workbench slot select/upgrade-commit/picker-cancel, slider,
store trade, galaxy-map input, pazaak wager input); `Drain(gm)` runs once per
tick from the update loop and dispatches deep engine calls that are unsafe to
run mid-input-dispatch. Talks to `engine_manager`/`engine_offsets` (control
offsets, CExoArrayList), `menus_chain` (chain invalidate/validate after
Activate), `menus_journal`/`menus_listbox`/`menus_store`/`menus_galaxymap`/
`pazaak.h` for per-panel post-dispatch repair, and `prism`/`log` for
speech/diagnostics.

## Declarations (in source order)

- L43-L59 — `enum class Kind`: MoveCursor, ClickAt, Activate, EquipSelect, EquipCommit, WorkbenchSlotSelect, WorkbenchUpgradeCommit, WorkbenchPickerCancel, SliderInput, StoreItemActivate, GalaxyInput, WagerInput
  note: `PrevSWInGameGui` from an earlier revision is gone; `WorkbenchPickerCancel`, `GalaxyInput`, `WagerInput` are new since the last index refresh
- L61-L69 — `struct PendingOp { kind, x, y, a, b, c, code }`
- L71 — `PendingOp g_op` — the single queue slot
- L76-L77 — `kVtableHandleInputEvent=15`, `PFN_ControlHandleInputEvent` typedef (local copy; ODR-safe duplicate of menus.cpp's)
- L79-L81 — `void Reset()`
- L85-L182 — `Queue*` family (QueueMoveCursor, QueueClickAt, QueueActivate, QueueEquipSelect, QueueEquipCommit, QueueWorkbenchSlotSelect, QueueWorkbenchUpgradeCommit, QueueWorkbenchPickerCancel, QueueSliderInput, QueueStoreItemActivate, QueueGalaxyInput, QueueWagerInput)
  note: every Queue* returns false if `g_op.kind != Kind::None` (uniform debounce, replacing the old code's inconsistent per-site subsets)
- L184-L186 — `bool IsPending()`
- L188-L799 — `void Drain(void* gm)`: snapshot-then-clear-then-dispatch switch over `Kind`
  note: Activate branch raises `is_active` 0→1 only (never clobbers non-zero engine bookkeeping — 5→1 clobber caused a CSWRoomSurfaceMesh crash once), nulls the chain entry only for InGameLevelUp (self-destroying Annehmen button), and repairs Journal Sort/Swap + chargen sub-screen chain invalidation post-dispatch
  note: WorkbenchSlotSelect infers install/remove/no-match by diffing `field35_0x2f74[slot_idx]` before/after and looks up the slot's TLK name via `kAddrUpgradeSlotTypeTable`
  note: GalaxyInput and WagerInput branches are thin forwards to `menus::galaxymap::DispatchInput` / `pazaak::DispatchWagerInput`
