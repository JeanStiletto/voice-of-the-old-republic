# cycle_input.h (29 lines)

Pillar 4 cycle keypress dispatch — routes into cycle_state.cpp. Two ingestion paths share the same per-action handlers so behaviour is identical regardless of arrival route: `TryHandleEvent` sees engine-routed (kotor.ini-bound) keys via `OnHandleInputEvent` and tracks the engine-side shift latch itself; `PollWin32` reads OS-level state directly via `GetAsyncKeyState` and is the PRIMARY path because stock kotor.ini doesn't bind `,`/`.`/`-` so the engine keymap drops them before TryHandleEvent ever sees them. Gated in-game via `GetPlayerPosition`; in menus/chargen/dialog the keys pass through unchanged.

## Declarations (in source order)

- L22 — `namespace acc::cycle_input`
- L25 — `bool TryHandleEvent(int param_1, int param_2)`
  note: returns true iff the event was consumed
- L27 — `void PollWin32()`
