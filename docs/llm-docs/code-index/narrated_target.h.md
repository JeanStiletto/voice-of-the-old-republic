# narrated_target.h (49 lines)

Declares the unified narrated-target slot shared across narration channels.
Non-name narration (pre-rolls, empty-state phrases, heading/turn/beacon
waypoints) deliberately does NOT stamp — only channels that spoke a bare
target name do.

## Declarations (in source order)

- L26-L32 — `struct Slot { obj, handle, pos, tickStamp, isMapPin }`
- L36 — `void Stamp(void* obj, uint32_t serverHandle)` — both args must be server-side (use `engine::GetObjectHandle` if only a client-side handle is on hand)
- L39 — `void StampMapPin(void* pin, const Vector& pos)`
- L41 — `void Clear()`
- L46 — `bool TryGet(Slot& out)` — false + zeroed out on stale slots (destroyed object, area switch, pin removed)
