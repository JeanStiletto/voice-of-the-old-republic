# cycle_input.h (29 lines)

Pillar 4 cycle input header. Two ingestion paths (TryHandleEvent from OnHandleInputEvent; PollWin32 from OnUpdate) share the same per-action handlers. In-game gate: GetPlayerPosition. In menus/chargen/dialog the keys pass through unchanged.

## Declarations (in source order)

- L22 — `namespace acc::cycle_input`
- L25 — `bool TryHandleEvent(int param_1, int param_2);`
  note: returns true if the event was consumed (caller must not forward it to engine)
- L27 — `void PollWin32();`
