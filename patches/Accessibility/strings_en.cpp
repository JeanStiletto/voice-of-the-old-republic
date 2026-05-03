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

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_en
