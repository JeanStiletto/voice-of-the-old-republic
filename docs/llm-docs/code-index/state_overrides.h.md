# state_overrides.h (25 lines)

Per-puzzle state-label registry. Maps per-instance object tags to (state offset, int→label map). Used to append localised state suffixes (e.g. ", Aus"/"off") to spoken placeable names for puzzles like the Sith-base Lights Out wall switches.

## Declarations (in source order)

- L20 — `namespace acc::state`
- L23 — `bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize)`
  note: returns true iff a suffix was appended; reads tag at CSWSObject+0x18, looks up offset+label map
