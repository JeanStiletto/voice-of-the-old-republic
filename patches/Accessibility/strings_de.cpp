// German string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE4=ä, \xF6=ö,
// \xFC=ü, \xDF=ß, \xC4=Ä, \xD6=Ö, \xDC=Ü). Tolk's ANSI overload converts
// from CP_ACP via MultiByteToWideChar; on a German Windows install
// CP_ACP = Windows-1252, so the literal bytes here pass through
// unchanged. UTF-8 source would also work but only with /utf-8 set, and
// the patch build script (create-patch.bat) doesn't pass that flag.
//
// Direction frame: German nav uses "auf X Uhr" ("at X o'clock") rather
// than the English "X o'clock" — the bare "X Uhr" form would read as
// time-of-day. Plurals: empty-state phrases use the plural form
// (T\xFCren, not T\xFCr) since "Keine T\xFCr in Reichweite" sounds odd.

#include "strings.h"

namespace acc::strings::lang_de {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "T\xFCr";              // Tür
        case Id::CategoryNpc:         return "Person";
        case Id::CategoryContainer:   return "Beh\xE4lter";         // Behälter
        case Id::CategoryItem:        return "Gegenstand";
        case Id::CategoryLandmark:    return "Ort";
        case Id::CategoryTransition:  return "\xDC" "bergang";       // Übergang

        case Id::EmptyDoors:          return "Keine T\xFCren in Reichweite";        // Türen
        case Id::EmptyNpcs:           return "Keine Personen in Reichweite";
        case Id::EmptyContainers:     return "Keine Beh\xE4lter in Reichweite";     // Behälter (sg=pl)
        case Id::EmptyItems:          return "Keine Gegenst\xE4nde in Reichweite";  // Gegenstände
        case Id::EmptyLandmarks:      return "Keine Orte in Reichweite";
        case Id::EmptyTransitions:    return "Keine \xDC" "berg\xE4nge in Reichweite"; // Übergänge
        case Id::EmptyAll:            return "Keine Objekte in Reichweite";

        case Id::FmtAnnounceWithClock: return "%s, auf %d Uhr, %d Meter";
        case Id::FmtAnnounceNoClock:   return "%s, %d Meter";
        case Id::FmtCategoryItem:      return "%s. %s";

        case Id::FmtGuidingTo:         return "Gehe zu %s";
        case Id::FmtGuidingFailed:     return "Gehe zu %s fehlgeschlagen";
        case Id::GuidanceNoFocus:      return "Kein Ziel ausgew\xE4hlt";  // ausgewählt

        case Id::MovementCancelled:    return "Bewegung abgebrochen";

        case Id::FmtInteractTalk:      return "Sprich mit %s";
        case Id::FmtInteractOpen:      return "\xD6" "ffne %s";                 // Öffne
        case Id::FmtInteractTake:      return "Hebe %s auf";
        case Id::FmtInteractFailed:    return "Interaktion mit %s fehlgeschlagen";
        case Id::FmtInteractEngine:    return "%s %s";

        case Id::ContainerEmpty:       return "Leer";
        case Id::ContainerOneItem:     return "1 Gegenstand";
        case Id::FmtContainerItems:    return "%d Gegenst\xE4nde";              // Gegenstände
        case Id::FmtContainerItemAt:   return "%s, %d von %d";

        case Id::EquipSlotHead:        return "Kopf";
        case Id::EquipSlotImplant:     return "Implantat";
        case Id::EquipSlotBody:        return "K\xF6rper";                       // Körper
        case Id::EquipSlotArmL:        return "Linker Arm";
        case Id::EquipSlotArmR:        return "Rechter Arm";
        case Id::EquipSlotWeapL:       return "Linke Waffe";
        case Id::EquipSlotWeapR:       return "Rechte Waffe";
        case Id::EquipSlotBelt:        return "G\xFCrtel";                       // Gürtel
        case Id::EquipSlotHands:       return "H\xE4nde";                        // Hände

        case Id::FmtTransitionArea:    return "Bereich: %s";
        case Id::FmtTransitionRoom:    return "Raum: %s";
        case Id::FmtTransitionRoomIndex: return "Raum %d";
        case Id::FmtTransitionLoading: return "Lade: %s";

        case Id::DirNorth:             return "Norden";
        case Id::DirNortheast:         return "Nord-Ost";
        case Id::DirEast:              return "Osten";
        case Id::DirSoutheast:         return "S\xFC" "d-Ost";                  // Süd-Ost
        case Id::DirSouth:             return "S\xFC" "den";                    // Süden
        case Id::DirSouthwest:         return "S\xFC" "d-West";                 // Süd-West
        case Id::DirWest:              return "Westen";
        case Id::DirNorthwest:         return "Nord-West";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_de
