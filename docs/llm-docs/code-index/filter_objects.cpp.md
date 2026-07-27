# filter_objects.cpp (90 lines)

Single source of truth for "is this a Pillar 4 vocabulary object" — the six
locked cycle categories (Door/Npc/Container/Item/Landmark/Transition).
ObjectMatches excludes the player's own creature (self at listener position,
no useful spatial info) and defers to engine_area's GetObjectKind plus
sub-state predicates (IsUsablePlaceable, IsLandmarkWaypoint,
IsTransitionTrigger). Transition additionally excludes floor_puzzle's plate
triggers (they structurally pass IsTransitionTrigger but are puzzle
machinery). IsMapCycleable narrows to Landmark only, matching
CSWGuiMapHider::Draw's engine-rendered icon set.

## Declarations (in source order)

- L6 — `namespace acc::filter`
- L8 — `const char* CategoryName(CycleCategory c)`
- L21 — `bool ObjectMatches(void* gameObject, CycleCategory category)`
  note: excludes GetPlayerServerCreature() explicitly; every consumer (cycle, T1/T2, passive_narrate) inherits this.
- L66 — `CycleCategory NextCategory(CycleCategory c)`
- L71 — `CycleCategory PrevCategory(CycleCategory c)`
- L77 — `bool IsMapCycleable(CycleCategory c)`
  note: derived from decompiled CSWGuiMapHider::Draw @0x006943d0 — only waypoints with map_note_enabled render as map icons.
