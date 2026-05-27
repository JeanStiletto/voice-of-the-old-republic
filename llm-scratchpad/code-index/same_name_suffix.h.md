# same_name_suffix.h (32 lines)

Per-object disambiguator for same-LocName objects header. Appends a stable numeric suffix (e.g. "Sith-Soldat 2") when two or more live objects in the current area share a resolved name. Keyed by server handle, assigned on first narration.

## Declarations (in source order)

- L18 — `namespace acc::narration`
- L22 — `bool GetSpokenName(void* gameObject, char* outBuf, size_t bufSize);`
  note: mirrors engine::GetObjectName's contract; calls AppendSuffix and AppendStateLabel internally
- L27 — `void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize);`
  note: uses current outBuf contents as the LocName bucket key; handles re-keying when a placeable script swaps the name
- L30 — `void Reset();`
  note: must be called on area transition (hooked into transitions.cpp's area-reset chain)
