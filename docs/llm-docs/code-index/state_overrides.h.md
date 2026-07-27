# state_overrides.h (25 lines)

Declares the per-puzzle state-label registry: KOTOR puzzles often encode
state as a small int on a placeable (e.g. Sith-base "Lights Out" wall
switches) that the engine never surfaces as user-facing text. A compile-time
table maps per-instance tag -> (struct offset, int-to-label map); unmatched
objects stay silent. Future revision may add a local-vars reader behind the
same API for puzzles whose state lives in script locals instead of a struct
field.

## Declarations (in source order)

- L23 — `bool AppendStateLabel(void* gameObject, char* outBuf, size_t bufSize)`
  note: returns true iff a ", <label>" suffix was appended
