# filter_objects.h (45 lines)

Header for the Pillar 4 object filter: the CycleCategory enum (six locked
categories), the pure ObjectMatches predicate over engine_area's
GameObjectKind, and CycleContext (World vs Map) for fog-of-war-gated map
cycling.

## Declarations (in source order)

- L11 — `namespace acc::filter`
- L14 — `enum class CycleCategory : int { Door, Npc, Container, Item, Landmark, Transition, Count_ }`
- L24 — `const char* CategoryName(CycleCategory c)`
- L27 — `bool ObjectMatches(void* gameObject, CycleCategory category)`
- L29 — `CycleCategory NextCategory(CycleCategory c)`
- L30 — `CycleCategory PrevCategory(CycleCategory c)`
- L34 — `enum class CycleContext : int { World, Map }`
- L43 — `bool IsMapCycleable(CycleCategory c)`
