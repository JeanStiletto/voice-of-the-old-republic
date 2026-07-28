# narrated_target.cpp (115 lines)

Implements the unified "last narrated target" slot: a single global `Slot`
that any narration channel (passive_narrate, cycle_input, view_mode) stamps
with a bare-target-name, so activation keys (Enter, Shift/Ctrl/Alt+-, `-`)
all read one place instead of resolving their own focus. Two slot shapes:
game-object (validated via server-handle resolve) and map-pin (validated via
membership in the client area's map_pins[], or via `map_shipped_hints`'
`IsShippedHint` for curated hints that share the slot shape but live outside
the engine pin array — this shipped-hints check is new since the last index
refresh).

## Declarations (in source order)

- L14 — `Slot g_slot` — the single global slot
- L18-L34 — `void Stamp(void* obj, uint32_t serverHandle)` — rejects null/0/0xFFFFFFFF/0x7F000000 handles; clears cached pos (game objects re-read pos at use time)
- L36-L49 — `void StampMapPin(void* pin, const Vector& pos)` — pos is frozen at stamp time
- L51-L58 — `void Clear()`
- L66-L77 — `bool IsMapPinStillPresent(void* pin)` — checks `IsShippedHint` first, else walks `GetMapPinAt` over the current client area
  note: quest scripts can call SetMapPinEnabled(off), so membership is re-checked defensively rather than trusted from stamp time
- L81-L112 — `bool TryGet(Slot& out)` — re-validates on every read; game-object path re-resolves the server handle and requires the resolved pointer to match `g_slot.obj` exactly, clearing on any mismatch
