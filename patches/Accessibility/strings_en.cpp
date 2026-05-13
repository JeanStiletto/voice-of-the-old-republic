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
        case Id::GuidingToPoint:       return "Walking to point";

        case Id::MovementCancelled:    return "Movement cancelled";

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Beacon to %s";
        case Id::BeaconCancelled:      return "Beacon cancelled";
        case Id::FmtBeaconNoPath:      return "No path to %s";
        case Id::BeaconAlreadyAtDest:  return "Already at destination";
        // Route description.
        // Example: "Route to Door (25 metres): 5 metres North,
        //           4 metres Northeast, 6 metres East. No transition."
        case Id::FmtRouteHeader:       return "Route to %s (%d metres): %s. %s.";
        case Id::FmtRouteSegment:      return "%d metres %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "One transition";
        case Id::RouteNoTransition:    return "No transition";
        case Id::FmtBeaconNextSegment: return "Continue %d metres %s";

        case Id::FmtInteractTalk:      return "Talk to %s";
        case Id::FmtInteractOpen:      return "Use %s";
        case Id::FmtInteractTake:      return "Pick up %s";
        case Id::FmtInteractFailed:    return "Interact with %s failed";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Action menu, %s";

        case Id::FmtActionBarOpened:      return "Action bar column %d: %s, %d options";
        case Id::FmtActionBarColumnEmpty: return "Column %d is empty";
        case Id::ActionBarColumnEmpty:    return "Column is empty";
        case Id::FmtActionBarFired:       return "%s used";
        case Id::ActionBarCancelled:      return "Cancelled";

        case Id::ContainerEmpty:       return "Empty";
        case Id::ContainerOneItem:     return "1 item";
        case Id::FmtContainerItems:    return "%d items";
        case Id::FmtContainerItemAt:   return "%s, %d of %d";

        case Id::EquipSlotHead:        return "Head";
        case Id::EquipSlotImplant:     return "Implant";
        case Id::EquipSlotBody:        return "Body";
        case Id::EquipSlotArmL:        return "Left arm";
        case Id::EquipSlotArmR:        return "Right arm";
        case Id::EquipSlotWeapL:       return "Left weapon";
        case Id::EquipSlotWeapR:       return "Right weapon";
        case Id::EquipSlotBelt:        return "Belt";
        case Id::EquipSlotHands:       return "Hands";

        case Id::FmtTransitionArea:    return "Area: %s";
        case Id::FmtTransitionRoom:    return "Room: %s";
        case Id::FmtTransitionRoomIndex: return "Room %d";
        case Id::FmtTransitionLoading: return "Loading: %s";

        case Id::DoorOpen:             return "open";
        case Id::DoorLocked:           return "locked";

        case Id::DirNorth:             return "North";
        case Id::DirNortheast:         return "Northeast";
        case Id::DirEast:              return "East";
        case Id::DirSoutheast:         return "Southeast";
        case Id::DirSouth:             return "South";
        case Id::DirSouthwest:         return "Southwest";
        case Id::DirWest:              return "West";
        case Id::DirNorthwest:         return "Northwest";

        case Id::FmtCompassDegrees:    return "%d degrees";

        case Id::MouseLookOn:          return "Mouse Look on";
        case Id::MouseLookOff:         return "Mouse Look off";

        case Id::ViewModeOn:           return "View mode on";
        case Id::ViewModeOff:          return "View mode off";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d of %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d of %d";

        case Id::LevelUpOpen:          return "Level Up";
        case Id::LevelUpFailed:        return "Level Up failed";

        case Id::PortraitLabel:        return "Portrait";
        case Id::PortraitArrowPrev:    return "Previous portrait";
        case Id::PortraitArrowNext:    return "Next portrait";
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "female";
        case Id::PortraitGenderMale:   return "male";
        case Id::PortraitRaceAsian:    return "Asian";
        case Id::PortraitRaceDark:     return "dark-skinned";
        case Id::PortraitRaceLight:    return "light-skinned";
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Level %s. ";
        case Id::FmtCharSheetXp:       return "Experience %s of %s. ";
        case Id::FmtCharSheetHp:       return "Hit points %s. ";
        case Id::FmtCharSheetFp:       return "Force points %s. ";
        case Id::FmtCharSheetStr:      return "Strength %s%s%s. ";
        case Id::FmtCharSheetDex:      return "Dexterity %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Constitution %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Intelligence %s%s%s. ";
        case Id::FmtCharSheetWis:      return "Wisdom %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Charisma %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Alignment %u of %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modifier %s, Cost %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, remaining points %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, Modifier %s, remaining points %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, remaining points %s, Cost %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, Modifier %s, remaining points %s, Cost %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Cost %s";
        case Id::FmtChargenSkillValueChange: return "%s, remaining points %s";

        case Id::ChargenFeatGrantedTitle:    return "You receive these Talents";
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d of %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "available";
        case Id::ChargenFeatStatusExisting:  return "already learned";
        case Id::ChargenFeatStatusGranted:   return "automatically granted";
        case Id::ChargenFeatStatusLocked:    return "prerequisite missing";
        case Id::ChargenFeatStatusChosen:    return "chosen";

        case Id::EditboxRole:                return "edit field";
        case Id::EditboxEmpty:               return "empty";
        case Id::EditboxEnd:                 return "end";

        case Id::CombatBegins:               return "Combat begins";
        case Id::CombatEnds:                 return "Combat ends";

        case Id::PcStatHeader:               return "Status.";
        case Id::FmtPcStatHpFp:              return "Hit points %d of %d, Force points %d of %d.";
        case Id::FmtPcStatAc:                return "Defense %d.";
        case Id::FmtPcStatAttrs:             return "Strength %d, Dexterity %d, Constitution %d, Intelligence %d, Wisdom %d, Charisma %d.";
        case Id::FmtPcStatSaves:             return "Saves: Fortitude %d, Reflex %d, Will %d.";
        case Id::FmtPcStatAlignment:         return "Alignment %d.";
        case Id::FmtPcStatEffectsHeader:     return "Active effects: %d.";
        case Id::PcStatNoCharacter:          return "No character status available.";

        // Skeleton: max-HP / AC reads are not yet safe (suspected engine
        // accessors). Args reduced to (name, faction_word, hp_cur).
        case Id::FmtTargetCombatBrief:       return "%s, %s, %d hit points.";
        case Id::FactionHostile:             return "hostile";
        case Id::FactionFriendly:            return "friendly";
        case Id::FactionNeutral:             return "neutral";
        case Id::TargetIsDead:               return "dead";

        case Id::ExamineOpened:              return "Examine.";
        case Id::ExamineNoTarget:            return "No target to examine.";
        case Id::ExamineFailed:              return "Examine failed.";

        case Id::FmtQueueOpen:               return "Action queue, %d actions.";
        case Id::QueueEmpty:                 return "Action queue is empty.";
        case Id::FmtQueueRow:                return "%s %s, %d of %d.";
        case Id::FmtQueueRemoved:            return "Removed: %s.";
        case Id::QueueCleared:               return "Queue cleared.";
        case Id::QueueClosed:                return "Queue closed.";
        case Id::QueueRemoveFailed:          return "Cannot remove this action.";
        case Id::QueueVerbAttack:            return "Attack";
        case Id::QueueVerbCastForce:         return "Cast Force power";
        case Id::QueueVerbItemCast:          return "Use item";
        case Id::QueueVerbEquip:             return "Equip";
        case Id::QueueVerbUnequip:           return "Unequip";
        case Id::QueueVerbMove:              return "Move";
        case Id::QueueVerbHeal:              return "Heal";
        case Id::QueueVerbUseTalent:         return "Use talent";
        case Id::QueueVerbCutscene:          return "Cutscene";
        case Id::QueueVerbUnknown:           return "Action";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "X of Y hp"
        // tail. Args reduced to (attacker, target, damage).
        case Id::FmtAttackHit:               return "%s hits %s for %d damage.";
        case Id::FmtAttackMiss:              return "%s misses %s.";
        case Id::FmtAttackCrit:              return "Critical! %s strikes %s for %d damage.";
        case Id::FmtAttackDeflected:         return "%s's attack on %s is deflected.";

        case Id::FmtSavingThrowSucceeded:    return "%s saves: %s %d versus %d.";
        case Id::FmtSavingThrowFailed:       return "%s fails save: %s %d versus %d.";
        case Id::SaveTypeFort:               return "Fortitude";
        case Id::SaveTypeReflex:             return "Reflex";
        case Id::SaveTypeWill:               return "Will";

        case Id::FmtDialogReplies:           return "%d replies available.";
        case Id::DialogReplyUnavailable:     return "unavailable";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d of %d";

        case Id::MessagesTitleCombatLog:     return "Combat log.";
        case Id::MessagesTitleDialogLog:     return "Dialog log.";

        case Id::MapPrevNote:                return "Previous map note";
        case Id::MapNextNote:                return "Next map note";

        case Id::MapCursorUnexplored:        return "Unexplored";
        case Id::MapCursorWaypointPOI:       return "Point of interest";
        case Id::MapCursorOpenArea:          return "Open area";
        case Id::MapCursorJunction:          return "Junction";
        case Id::MapCursorOffPath:           return "Wall";
        case Id::FmtMapCursorCorridor:       return "Corridor %s, %.0f meters";
        case Id::FmtMapCursorDeadEnd:        return "Dead end, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Junction, %s";
        case Id::FmtMapCursorCorridorDir:    return "Corridor %s";
        case Id::MapCursorTransitionDoor:    return "Doorway";
        case Id::AxisNorthSouth:             return "north-south";
        case Id::AxisEastWest:               return "east-west";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_en
