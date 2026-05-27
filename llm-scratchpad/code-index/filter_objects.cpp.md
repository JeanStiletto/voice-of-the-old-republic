# filter_objects.cpp (94 lines)

Implementation of the six-category object filter predicates.

## Declarations (in source order)

- L5 — `namespace acc::filter`
- L7 — `const char* CategoryName(CycleCategory c)`
- L20 — `bool ObjectMatches(void* gameObject, CycleCategory category)`
  note: Container maps to Placeable+IsUsablePlaceable; Landmark maps to Waypoint+IsLandmarkWaypoint; Transition maps to Trigger+IsTransitionTrigger; player creature is always excluded
- L59 — `CycleCategory NextCategory(CycleCategory c)`
- L64 — `CycleCategory PrevCategory(CycleCategory c)`
- L70 — `bool IsMapCycleable(CycleCategory c)`
  note: derived from CSWGuiMapHider::Draw @0x006943d0 decompile; only Landmark returns true
