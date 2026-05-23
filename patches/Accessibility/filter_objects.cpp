#include "filter_objects.h"

#include "engine_player.h"   // GetPlayerServerCreature

namespace acc::filter {

const char* CategoryName(CycleCategory c) {
    switch (c) {
        case CycleCategory::Door:        return "Door";
        case CycleCategory::Npc:         return "NPC";
        case CycleCategory::Container:   return "Container";
        case CycleCategory::Item:        return "Item";
        case CycleCategory::Landmark:    return "Landmark";
        case CycleCategory::Transition:  return "Transition";
        case CycleCategory::MapPin:      return "MapPin";
        case CycleCategory::Count_:      return "?";
    }
    return "?";
}

bool ObjectMatches(void* gameObject, CycleCategory category) {
    // Player creature is in the area object list and classifies as a
    // Creature (so it would otherwise pass Npc) — but it's at the
    // listener's exact position (pan-center, no spatial info) and the
    // user shouldn't see themselves as a cyclable object either.
    // Single source of truth for "is this a Pillar 4 vocabulary object";
    // every consumer (T1, T2, cycle, passive_narrate) inherits the
    // exclusion.
    if (gameObject != nullptr &&
        gameObject == acc::engine::GetPlayerServerCreature()) {
        return false;
    }

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
        case CycleCategory::MapPin:
            // Map pins are NOT in CSWSArea.game_objects[] — they live
            // on CSWCArea.map_pins[]. cycle_state has the out-of-band
            // iterator for the map context; in-band ObjectMatches
            // never sees one, and World context returns empty for this
            // category (so Shift+,/. in-world skips it silently).
            return false;
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

bool IsMapCycleable(CycleCategory c) {
    // Sighted-parity gate, derived from CSWGuiMapHider::Draw @0x006943d0
    // (decompiled 2026-05-23): the in-game area-map renderer iterates only
    // waypoints with map_note_enabled + IsWorldPointExplored and draws
    // party arrows. Doors, triggers/transitions, items, NPCs, containers,
    // and CSWCMapPin entries are never rendered to the panel. We expose
    // only the categories sighted players actually see as icons; Landmark
    // is further narrowed in cycle_state::BuildCategoryListing to require
    // map_note_enabled too (so it matches the engine's GetNextMapNote /
    // GetPrevMapNote curated subset — what we surface as "Map hint").
    //
    // MapPin stays cycleable for now: even though the engine doesn't
    // render CSWCMapPin to the area panel either, quest-script pins
    // remain useful as an out-of-band "active quest objective" channel
    // for blind players. Worth re-evaluating once we confirm whether
    // SetMapPinEnabled in real K1 modules acts on these or on waypoints.
    switch (c) {
        case CycleCategory::Landmark:
        case CycleCategory::MapPin:
            return true;
        case CycleCategory::Door:
        case CycleCategory::Transition:
        case CycleCategory::Npc:
        case CycleCategory::Container:
        case CycleCategory::Item:
        case CycleCategory::Count_:
            return false;
    }
    return false;
}

}  // namespace acc::filter
