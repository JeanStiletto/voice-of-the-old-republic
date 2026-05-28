# core_tick.h (13 lines)

Mod-wide per-tick dispatcher. Owns the OnUpdate hook (CSWGuiManager::Update @0x0040ce76). Dispatch() fans out to every subsystem in fixed order — the file body is the canonical "what fires per tick" list.

## Declarations (in source order)

- L9 — `namespace acc::tick`
- L11 — `void Dispatch()`
