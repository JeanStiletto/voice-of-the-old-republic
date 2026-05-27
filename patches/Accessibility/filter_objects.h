// Pillar 4 object filter — six locked categories.
//
// Pure predicate over engine_area's GameObjectKind + sub-state. Sub-state
// filters: Container needs usable=true OR has_inventory=true; Landmark
// needs has_map_note=true; Transition needs transition_destination set.

#pragma once

#include "engine_area.h"

namespace acc::filter {

// Cycle order returned by NextCategory / PrevCategory.
enum class CycleCategory : int {
    Door         = 0,
    Npc          = 1,
    Container    = 2,
    Item         = 3,
    Landmark     = 4,
    Transition   = 5,
    Count_       = 6,
};

const char* CategoryName(CycleCategory c);

// SEH-guarded via engine_area::GetObjectKind.
bool ObjectMatches(void* gameObject, CycleCategory category);

CycleCategory NextCategory(CycleCategory c);
CycleCategory PrevCategory(CycleCategory c);

// World = in-world cycle over CSWSArea object list; Map = same data
// projected onto the area map, fog-of-war-gated.
enum class CycleContext : int {
    World = 0,
    Map   = 1,
};

// Which categories the engine renders as map icons. CSWGuiMapHider::Draw
// iterates only waypoints with map_note_enabled + IsWorldPointExplored —
// doors/triggers/items/NPCs/containers never reach the map panel.
// Currently: Landmark only.
bool IsMapCycleable(CycleCategory c);

}  // namespace acc::filter
