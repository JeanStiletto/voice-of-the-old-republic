#include "filter_objects.h"

namespace acc::filter {

const char* CategoryName(CycleCategory c) {
    switch (c) {
        case CycleCategory::Door:        return "Door";
        case CycleCategory::Npc:         return "NPC";
        case CycleCategory::Container:   return "Container";
        case CycleCategory::Item:        return "Item";
        case CycleCategory::Landmark:    return "Landmark";
        case CycleCategory::Transition:  return "Transition";
        case CycleCategory::Count_:      return "?";
    }
    return "?";
}

bool ObjectMatches(void* gameObject, CycleCategory category) {
    int kind = acc::engine::GetObjectKind(gameObject);
    if (kind < 0) return false;

    using K = acc::engine::GameObjectKind;
    switch (category) {
        case CycleCategory::Door:
            return kind == int(K::Door);
        case CycleCategory::Npc:
            return kind == int(K::Creature);
        case CycleCategory::Container:
            // TODO lay-off 4: AND on (usable=true OR has_inventory=true) at
            // CSWSPlaceable +0x328 / +0x334 (investigation Q5).
            return kind == int(K::Placeable);
        case CycleCategory::Item:
            return kind == int(K::Item);
        case CycleCategory::Landmark:
            // TODO lay-off 4: AND on has_map_note=true at CSWSWaypoint
            // +0x228 (investigation Q5).
            return kind == int(K::Waypoint);
        case CycleCategory::Transition:
            // TODO lay-off 4: AND on transition_destination set at
            // CSWSTrigger +0x30c (investigation Q5).
            return kind == int(K::Trigger);
        case CycleCategory::Count_:
            return false;
    }
    return false;
}

CycleCategory NextCategory(CycleCategory c) {
    int next = (int(c) + 1) % int(CycleCategory::Count_);
    return CycleCategory(next);
}

CycleCategory PrevCategory(CycleCategory c) {
    int prev = (int(c) + int(CycleCategory::Count_) - 1) %
               int(CycleCategory::Count_);
    return CycleCategory(prev);
}

}  // namespace acc::filter
