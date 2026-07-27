# map_shipped_hints.h (50 lines)

Header for the small static table of mod-shipped map hints (module resref +
world position + localised label). Speech/cycle-only: entries fold into the
map hint cycle listing, fog-of-war-gated through the engine's own
IsWorldPointExplored, and are never drawn as engine map pins.

## Declarations (in source order)

- L28 — `struct ShippedHint { const char* module; Vector pos; acc::strings::Id label; }`
- L37 — `int CollectForCurrentModule(const ShippedHint** out, int maxOut)` — returns count, 0 if module resolves to none
- L42 — `bool IsShippedHint(const void* p)` — table row AND current-module match (staleness contract narrated_target needs)
- L47 — `bool GetLabel(const void* p, char* outBuf, size_t bufSize)` — false on a foreign pointer; outBuf always NUL-terminated
