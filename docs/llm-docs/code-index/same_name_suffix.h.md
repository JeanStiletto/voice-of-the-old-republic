# same_name_suffix.h (69 lines)

Public interface for the per-object disambiguator that appends a stable numeric
suffix when two or more live objects in the current area share the same
resolved LocName (e.g. 5x "Wandverkleidung" in a corridor). Wraps
`engine::GetObjectName`; keyed by server handle so a member's slot survives
after others die. Talks to `filter_objects.h` (CycleCategory/ObjectMatches) and
is the shared numbering source for both Q/E narration and the in-world cycle.

## Declarations (in source order)

- L26 — `bool GetSpokenName(void* gameObject, CycleCategory category, char* outBuf, size_t bufSize)`
  note: mirrors GetObjectName's true/false-empty-buf contract
- L37 — `void AppendDisambiguator(void* gameObject, CycleCategory category, char* outBuf, size_t bufSize)`
  note: single decision point — Npc uses AppendSuffix (encounter serial), everything else uses AppendAreaPositionOrdinal
- L45 — `void AppendSuffix(void* gameObject, char* outBuf, size_t bufSize)`
  note: handle-keyed serial for mobile creatures; spatial rank would renumber them as they walk
- L55 — `void AppendAreaPositionOrdinal(void* gameObject, CycleCategory category, char* outBuf, size_t bufSize)`
  note: global north-to-south ordinal for static objects; position-stable across discovery/save/cycle-direction
- L64 — `bool PositionLess(const Vector& a, const Vector& b)`
  note: greatest-Y-first = northmost (KOTOR +Y is north); X then Z break ties
- L67 — `void Reset()`
  note: hooked into transitions.cpp's area-reset chain
