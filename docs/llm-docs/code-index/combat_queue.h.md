# combat_queue.h (37 lines)

Combat action-queue submenu (Phase 3). Active mode: Open speaks count, Up/Down cycles
entries, Enter removes focused tail entry, Shift+Enter clears all, Esc closes.
Self-disarms when queue empties between ticks. Positional (non-tail) remove is unresolved.

## Declarations (in source order)

- L20 — `namespace acc::combat::queue`
- L24 — `bool Open()`
  note: builds flat row list from party; speaks count + first row; returns false (and speaks "leer") if queue empty
- L26 — `bool IsActive()`
- L28 — `bool HandleInputEvent(int code, int value)`
  note: press-edge only; rebuilds rows on every keypress to stay consistent with engine-draining the queue between ticks
- L31 — `void ForceDisarm(const char* reason)`
  note: resets active state without engine-side cleanup (engine owns the queue)
- L33 — `void Tick()`
  note: auto-disarms if queue drains between keypresses; also obeys module-load latch
- L34 — `void PollWin32Hotkey()`
  note: opens queue on Action::CombatQueueOpen (Shift+K); self-gates on GetPlayerPosition
