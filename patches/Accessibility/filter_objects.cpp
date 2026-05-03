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
            return kind == int(K::Placeable) &&
                   acc::engine::IsUsablePlaceable(gameObject);
        case CycleCategory::Item:
            return kind == int(K::Item);
        case CycleCategory::Landmark:
            return kind == int(K::Waypoint) &&
                   acc::engine::IsLandmarkWaypoint(gameObject);
        case CycleCategory::Transition:
            return kind == int(K::Trigger) &&
                   acc::engine::IsTransitionTrigger(gameObject);
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
