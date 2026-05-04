// English string table. Plain ASCII — no escape sequences needed.
//
// See strings.h for the encoding convention and string-id semantics.

#include "strings.h"

namespace acc::strings::lang_en {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "Door";
        case Id::CategoryNpc:         return "NPC";
        case Id::CategoryContainer:   return "Container";
        case Id::CategoryItem:        return "Item";
        case Id::CategoryLandmark:    return "Landmark";
        case Id::CategoryTransition:  return "Transition";

        case Id::EmptyDoors:          return "No doors in range";
        case Id::EmptyNpcs:           return "No NPCs in range";
        case Id::EmptyContainers:     return "No containers in range";
        case Id::EmptyItems:          return "No items in range";
        case Id::EmptyLandmarks:      return "No landmarks in range";
        case Id::EmptyTransitions:    return "No transitions in range";
        case Id::EmptyAll:            return "No objects in range";

        case Id::FmtAnnounceWithClock: return "%s, %d o'clock, %d metres";
        case Id::FmtAnnounceNoClock:   return "%s, %d metres";
        case Id::FmtCategoryItem:      return "%s. %s";

        case Id::FmtGuidingTo:         return "Guiding to %s";
        case Id::FmtGuidingFailed:     return "Guidance to %s failed";
        case Id::GuidanceNoFocus:      return "No object focused";

        case Id::FmtInteractTalk:      return "Talk to %s";
        case Id::FmtInteractOpen:      return "Use %s";
        case Id::FmtInteractTake:      return "Pick up %s";
        case Id::FmtInteractFailed:    return "Interact with %s failed";

        case Id::DirNorth:             return "North";
        case Id::DirNortheast:         return "Northeast";
        case Id::DirEast:              return "East";
        case Id::DirSoutheast:         return "Southeast";
        case Id::DirSouth:             return "South";
        case Id::DirSouthwest:         return "Southwest";
        case Id::DirWest:              return "West";
        case Id::DirNorthwest:         return "Northwest";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_en
