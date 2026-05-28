# filter_objects.h (45 lines)

Pillar 4 object filter header. Pure predicates over engine_area's GameObjectKind plus sub-state (usable, has_map_note, transition_destination). Defines the six locked cycle categories and the World/Map context split.

## Declarations (in source order)

- L11 — `namespace acc::filter`
- L14 — `enum class CycleCategory : int`
  note: cycle order: Door=0, Npc=1, Container=2, Item=3, Landmark=4, Transition=5, Count_=6
- L24 — `const char* CategoryName(CycleCategory c);`
- L27 — `bool ObjectMatches(void* gameObject, CycleCategory category);`
  note: SEH-guarded via engine_area::GetObjectKind; excludes the player's own creature pointer regardless of category
- L29 — `CycleCategory NextCategory(CycleCategory c);`
- L30 — `CycleCategory PrevCategory(CycleCategory c);`
- L34 — `enum class CycleContext : int`
  note: World=0 (full area scan), Map=1 (fog-of-war gated, only map-cycleable categories)
- L43 — `bool IsMapCycleable(CycleCategory c);`
  note: currently returns true only for Landmark; derived from CSWGuiMapHider::Draw sighted-parity
