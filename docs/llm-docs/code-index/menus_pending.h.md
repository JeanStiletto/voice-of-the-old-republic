# menus_pending.h (136 lines)

Public interface for the deferred menu op queue. Explains why deferral is
needed (invoking deep engine functions mid-input-dispatch recurses through
HandleMouseMove → UpdateMouseOverControl, destabilising earlier listbox
hooks) and that queue depth is exactly one op — Prism speech still fires
synchronously from the input hook so the audible response feels instant even
though the engine action lands next tick (~16ms).

## Declarations (in source order)

- L33 — `bool QueueMoveCursor(int x, int y, void* target)`
- L40 — `bool QueueClickAt(int x, int y, void* target)` — cursor warp + LMouseDown/Up, for tab buttons gated on is_active
- L46 — `bool QueueActivate(void* target)` — direct vtable[15].HandleInputEvent(0x27,1), bypasses hit-test
- L52 — `bool QueueEquipSelect(void* panel, void* slot)`
- L58 — `bool QueueEquipCommit(void* panel, void* row, void* btn)`
- L67 — `bool QueueWorkbenchSlotSelect(void* panel, void* slot)`
- L77 — `bool QueueWorkbenchUpgradeCommit(void* panel, void* row, void* btnAssemble)`
- L83 — `bool QueueWorkbenchPickerCancel(void* panel)`
- L90 — `bool QueueSliderInput(void* target, int code)` — code 500 inc / 501 dec
- L106 — `bool QueueStoreItemActivate(void* panel, void* row)`
- L114 — `bool QueueGalaxyInput(void* panel, int engineCode, bool announcePlanet)`
- L122 — `bool QueueWagerInput(void* panel, int code)` — pazaak wager popup, code 0x2f dec / 0x30 inc
- L128 — `bool IsPending()`
- L133 — `void Drain(void* gm)` — call once per tick after all monitors have run; resets + logs if `gm` is null

note: `QueuePrevSWInGameGui` documented in a prior index revision no longer exists in this header.
