# narrated_target.cpp (109 lines)

Implementation of the unified narrated-target slot. Validates game-object slots via ResolveServerObjectHandle; validates map-pin slots via membership walk of the client area's map_pins[].

## Declarations (in source order)

- L9 — `namespace acc::narrated_target`
- L11 — `namespace { // anonymous`
- L13 — `Slot g_slot;`
- L15 — `} // namespace (anonymous)`
- L17 — `void Stamp(void* obj, uint32_t serverHandle)`
  note: no-ops on zero, 0xFFFFFFFF, and 0x7F000000 (INVALID) handles; only logs when the slot actually changes
- L35 — `void StampMapPin(void* pin, const Vector& pos)`
- L50 — `void Clear()`
- L59 — `namespace { // anonymous`
- L62 — `bool IsMapPinStillPresent(void* pin)`
  note: defensive membership walk; handles quest scripts calling SetMapPinEnabled(off)
- L74 — `} // namespace (anonymous)`
- L76 — `bool TryGet(Slot& out)`
  note: map-pin path: checks IsMapPinStillPresent; game-object path: resolves handle and compares pointer to catch area-switch staleness
