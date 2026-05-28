# state_overrides.cpp (107 lines)

Implementation of the per-puzzle state-label registry. Compile-time table of `StateOverride` entries mapping object tags to field offsets and `LabelEntry` sentinel-terminated arrays. Currently registered: wall1..wall5 (Sith-base Lights Out switches, position at +0x260).

## Declarations (in source order)

- L12 — `namespace acc::state`
- L16 — `struct LabelEntry` (anonymous namespace)
- L22 — `struct StateOverride` (anonymous namespace)
- L54 — `static const StateOverride* FindOverride(const char* tag)` (anonymous namespace)
- L62 — `static const char* PickLabel(const LabelEntry* labels, int value)` (anonymous namespace)
- L73 — `bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize)`
