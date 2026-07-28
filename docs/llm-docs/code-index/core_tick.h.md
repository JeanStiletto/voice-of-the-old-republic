# core_tick.h (13 lines)

Mod-wide per-tick dispatcher interface. Owns the `OnUpdate` hook (`CSWGuiManager::Update` @0x0040ce76); one `Dispatch()` call fans out to every subsystem in a fixed, explicit order.

## Declarations (in source order)

- L9 — `namespace acc::tick`
- L11 — `void Dispatch()`
