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
        case Id::CategoryMapHint:     return "Map hint";

        case Id::EmptyDoors:          return "No doors in range";
        case Id::EmptyNpcs:           return "No NPCs in range";
        case Id::EmptyContainers:     return "No containers in range";
        case Id::EmptyItems:          return "No items in range";
        case Id::EmptyLandmarks:      return "No landmarks in range";
        case Id::EmptyTransitions:    return "No transitions in range";
        case Id::EmptyMapHints:       return "No map hints on this map";
        case Id::EmptyAll:            return "No objects in range";
        case Id::CycleNoTarget:       return "No target";

        case Id::MapPinNoText:        return "Marker";
        case Id::MapPinShiftDashHint: return "Marker can't be auto-walked. Press Ctrl+Dash to beacon.";
        case Id::MapPinAltDashUnsupported: return "Marker: Alt+Dash not supported";
        case Id::MapPinInteractHint:  return "Marker. Press Ctrl+Dash to beacon.";

        case Id::FmtSavedMarkerAutoNumber:   return "Marker %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marker %d";
        case Id::FmtSavedMarkerPlaced:       return "Marker saved: %s";
        case Id::SavedMarkerFailed:          return "Could not save marker";

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
        case Id::FmtInteractNoActionsRedirect: return "No actions available for %s. Press Enter to activate.";
        case Id::FmtInteractNoActions: return "No actions available for %s.";

        case Id::FmtActionBarOpened:      return "Action bar column %d: %s, %d options";
        case Id::FmtActionBarColumnEmpty: return "Column %d is empty";
        case Id::ActionBarColumnEmpty:    return "Column is empty";
        case Id::FmtActionBarFired:       return "%s used";
        case Id::FmtFireAtPosition:       return "%s, position %d";
        case Id::FmtFireQueueFull:        return "%s, queue full";
        case Id::ActionBarCancelled:      return "Cancelled";

        case Id::NoTooltipAvailable:   return "No description available";

        case Id::ContainerEmpty:       return "Empty";
        case Id::ContainerOneItem:     return "1 item";
        case Id::FmtContainerItems:    return "%d items";
        case Id::FmtContainerItemAt:   return "%s, %d of %d";
        case Id::FmtItemStackSuffix:   return "%d in stack";

        case Id::EquipSlotHead:        return "Head";
        case Id::EquipSlotImplant:     return "Implant";
        case Id::EquipSlotBody:        return "Body";
        case Id::EquipSlotArmL:        return "Left arm";
        case Id::EquipSlotArmR:        return "Right arm";
        case Id::EquipSlotWeapL:       return "Left weapon";
        case Id::EquipSlotWeapR:       return "Right weapon";
        case Id::EquipSlotBelt:        return "Belt";
        case Id::EquipSlotHands:       return "Hands";

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, empty";
        case Id::FmtEquipVitality:     return "Vitality %s";
        case Id::FmtEquipDefense:      return "Defense %s";
        case Id::FmtEquipAttack:       return "Attack %s";
        case Id::FmtEquipAttackDual:   return "Attack left %s, right %s";
        case Id::FmtEquipDamage:       return "Damage %s";
        case Id::FmtEquipDamageDual:   return "Damage left %s, right %s";

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

        case Id::StuckFreeDirsPrefix:  return "Clear";
        case Id::StuckAllBlocked:      return "All blocked";

        case Id::FmtCompassDegrees:    return "%d degrees";

        case Id::FmtMapStateOriented:    return "%s. Facing %d degrees on the map, %s.";
        case Id::FmtMapStateUnknownRoom: return "Facing %d degrees on the map, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Mouse Look on";
        case Id::MouseLookOff:         return "Mouse Look off";

        case Id::ViewModeOn:           return "View mode on";
        case Id::ViewModeOff:          return "View mode off";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d of %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d of %d";

        case Id::LevelUpOpen:          return "Level Up";
        case Id::LevelUpFailed:        return "Level Up failed";
        case Id::LevelUpAlreadyOpen:   return "Level Up already open";

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

        case Id::FmtPartyPortraitInTeam:    return "%s, in party";
        case Id::FmtPartyPortraitAvailable: return "%s, available";

        case Id::DisabledSuffix:       return ", unavailable";

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

        case Id::PcStatNoCharacter:          return "No character status available.";

        // Brief is composed in BuildTargetCombatBrief: name, then optional
        // condition / distance / effects / weapons clauses each with a
        // leading space and trailing period.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "hostile";
        case Id::FactionFriendly:            return "friendly";
        case Id::FactionNeutral:             return "neutral";
        case Id::TargetIsDead:               return "dead";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d meters.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " off-hand %s.";
        case Id::FmtBriefEffectsCount:       return " %d active effects.";
        case Id::FmtBriefFeatsCount:         return " %d feats.";
        case Id::FmtSelfStatusHp:            return "%d hit points.";
        case Id::FmtSelfStatusHpOf:          return "%d of %d hit points.";

        case Id::ExamineOpened:              return "Examine.";
        case Id::ExamineNoTarget:            return "No target to examine.";
        case Id::ExamineFailed:              return "Examine failed.";

        case Id::FmtExamineOpened:           return "Examine: %s. %d entries.";
        case Id::FmtExamineRowOf:            return "%s. %d of %d.";
        case Id::ExamineViewClosed:          return "Examine closed.";
        case Id::FmtExamineRowName:          return "Name: %s";
        case Id::FmtExamineRowFaction:       return "Disposition: %s";
        case Id::FmtExamineRowHp:            return "Hit points: %d";
        case Id::FmtExamineRowDistance:      return "Distance: %d meters";
        case Id::FmtExamineRowWeapon:        return "Main hand: %s";
        case Id::ExamineRowWeaponNone:       return "Main hand: none";
        case Id::FmtExamineRowEffect:        return "Effect: %s";
        case Id::FmtExamineRowFeat:          return "Feat: %s";
        case Id::FmtExamineRowEffectUnknown: return "Effect #%d";
        case Id::FmtExamineRowFeatUnknown:   return "Feat #%d";
        case Id::ExamineRowNoEffects:        return "No active effects";
        case Id::ExamineRowNoFeats:          return "No feats";

        case Id::FmtExamineRowHpFull:        return "Hit points: %d of %d";
        case Id::FmtExamineRowLevel:         return "Level: %d";
        case Id::FmtExamineRowCondition:     return "Condition: %s";
        case Id::DamageLevel0Healthy:        return "uninjured";
        case Id::DamageLevel1Light:          return "lightly wounded";
        case Id::DamageLevel2Wounded:        return "wounded";
        case Id::DamageLevel3Badly:          return "badly wounded";
        case Id::DamageLevel4Dying:          return "dying";
        case Id::DamageLevel5Dead:           return "dead";
        case Id::FmtExamineRowOffHand:       return "Off hand: %s";
        case Id::FmtExamineRowHead:          return "Head: %s";
        case Id::FmtExamineRowTorso:         return "Armor: %s";
        case Id::FmtExamineRowHands:         return "Hands: %s";
        case Id::ExamineRowStatusInvisible:  return "Invisible";
        case Id::ExamineRowStatusBlind:      return "Blind";

        case Id::FmtQueueOpen:               return "Action queue, %d actions.";
        case Id::QueueEmpty:                 return "Action queue is empty.";
        case Id::FmtQueueRow:                return "%s: %s %s, %d of %d.";
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
        case Id::FmtMapCursorCorridor:       return "%s, %.0f meters";
        case Id::FmtMapCursorDeadEnd:        return "Dead end, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Junction, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Door";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s to %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::MapCursorTransitionDoor:    return "Doorway";
        case Id::FmtMapCursorJunctionDeadEndExit: return "dead end %s";
        case Id::FmtMapCursorPlazaDirs:      return "Place, %s";
        case Id::AxisNorthSouth:             return "north-south";
        case Id::AxisEastWest:               return "east-west";

        case Id::FmtStorePriceBuyFinite:    return "Price %d credits, stock %d";
        case Id::FmtStorePriceBuyUnlimited: return "Price %d credits, unlimited stock";
        case Id::FmtStorePriceSell:         return "Price %d credits, you own %d";
        case Id::StoreModeBuy:              return "Buy mode";
        case Id::StoreModeSell:             return "Sell mode";
        case Id::StoreSold:                 return "Sold";
        case Id::StoreBought:               return "Bought";
        case Id::StoreCannotSell:           return "Cannot be sold";
        case Id::StoreCannotBuy:            return "Cannot be bought";
        case Id::FmtStoreSoldFor:           return "Sold for %d credits";
        case Id::FmtStoreBoughtFor:         return "Bought for %d credits";

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Press C to read your hand, T for the table.";
        case Id::PazaakEmpty:            return "empty";
        case Id::PazaakFaceDown:         return "face down";
        case Id::PazaakBoardEmpty:       return "empty";
        case Id::PazaakFmtPlus:          return "plus %d";
        case Id::PazaakFmtMinus:         return "minus %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "plus or minus %d";
        case Id::PazaakFmtFlipCurrently: return "%s, currently %s";
        case Id::PazaakFmtYouDrew:       return "You drew %s. Your total %d.";
        case Id::PazaakOverTwenty:       return "Over twenty.";
        case Id::PazaakFmtYouPlayed:     return "Played %s. Your total %d.";
        case Id::PazaakYourTurn:         return "Your turn.";
        case Id::PazaakTurnEnded:        return "Turn ended.";
        case Id::PazaakFmtOppDrew:       return "Opponent drew %s. Total %d.";
        case Id::PazaakFmtOppPlayed:     return "Opponent played %s. Total %d.";
        case Id::PazaakFmtOppStands:     return "Opponent stands at %d.";
        case Id::PazaakFmtYouStand:      return "You stand at %d.";
        case Id::PazaakFmtWinSet:        return "You win the set. %d to %d.";
        case Id::PazaakFmtLoseSet:       return "You lose the set. %d to %d.";
        case Id::PazaakWinMatch:         return "You win the match!";
        case Id::PazaakLoseMatch:        return "You lose the match.";
        case Id::PazaakTieReplay:        return "Tie. Replaying set.";
        case Id::PazaakFmtHand:          return "Hand: %s";
        case Id::PazaakHandEmpty:        return "Hand empty.";
        case Id::PazaakFmtYourBoard:     return "Your board: %s, total %d.";
        case Id::PazaakFmtOppBoard:      return "Opponent board: %s, total %d.";
        case Id::PazaakNoPlayable:       return "No cards to play.";
        case Id::PazaakNotYourTurn:      return "Not your turn.";
        case Id::PazaakSelectCardFirst:  return "Select a card first.";
        case Id::PazaakChooseSign:       return "Choose sign. Left or right to change, Enter to play.";
        case Id::PazaakCancelled:        return "Cancelled.";
        case Id::PazaakDeckAvailable:    return "%s, %d available";
        case Id::PazaakDeckNoneLeft:     return "%s, none left";
        case Id::PazaakDeckSlotFilled:   return "Deck slot %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Deck slot %d: empty";
        case Id::PazaakDeckPlay:         return "Play, %d of 10 in deck";
        case Id::PazaakDeckAdded:        return "Added %s. %d of 10.";
        case Id::PazaakDeckRemoved:      return "Removed %s.";
        case Id::PazaakDeckFull:         return "Deck full.";
        case Id::PazaakFmtOppHand:       return "Opponent has %d hand cards.";
        case Id::PazaakStandLabel:       return "Stand";
        case Id::PazaakEndTurnLabel:     return "End turn";
        case Id::PazaakWagerLess:        return "Decrease wager";
        case Id::PazaakWagerMore:        return "Increase wager";
        case Id::PazaakFmtWager:         return "Wager %d of %d maximum.";
        case Id::PazaakFmtWagerRow:      return "Wager %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Not enough credits, need %d, have %d";

        case Id::FmtCredits:                return "Credits: %s";

        case Id::WorkbenchSlotWeapon1:       return "Upgrade slot 1";
        case Id::WorkbenchSlotWeapon2:       return "Upgrade slot 2";
        case Id::WorkbenchSlotWeapon3:       return "Upgrade slot 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Crystal slot 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Crystal slot 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Crystal slot 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Crystal slot 4";
        case Id::WorkbenchItemsEmpty:        return "No upgradable items in this category";
        case Id::WorkbenchUpgradesEmpty:     return "No compatible upgrades in inventory";
        case Id::WorkbenchSlotInstalled:     return "Upgrade installed";
        case Id::WorkbenchSlotRemoved:       return "Upgrade removed";
        case Id::WorkbenchSlotNoMatch:       return "No matching upgrade in inventory";

        case Id::SoundOptionsMovieVolume:    return "Movie volume";

        case Id::SwoopRaceStarted:
            // Terse opener — concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Swoop race.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is in the manual — verbose
            // spoken intro got in the way of the start countdown.
            return "Press space to speed up. A and D to steer.";
        case Id::SwoopRaceEnded:
            return "Swoop race ended.";
        case Id::SwoopRaceObstacleNear:
            return "Obstacle %d meters ahead";
        case Id::FmtSwoopRaceGear:
            return "Gear %d";

        case Id::ModSettingsRootLabel:        return "Mod settings";
        case Id::ModSettingsOpened:           return "Mod settings opened";
        case Id::ModSettingsClosed:           return "Mod settings closed";
        case Id::ModSettingExtendedCycling:   return "Map-wide object selection";
        case Id::ModSettingRoomShapes:        return "Room shape descriptions";
        case Id::ModSettingWallSounds:        return "Wall sounds";
        case Id::ModSettingHumanSubtitles:    return "Read human-speaker subtitles";
        case Id::ModSettingSkipIntros:        return "Skip launch intro movies";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "Intros will be skipped on next launch.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "Intros will play on next launch.";
        case Id::ModSettingSkipIntrosToggleFailed: return "Could not toggle intro movies. Files may have been removed.";
        case Id::ModSettingStateOn:           return "on";
        case Id::ModSettingStateOff:          return "off";
        case Id::FmtModSettingOption:         return "%s: %s";

        case Id::ModSettingAudioGlossary:           return "Audio glossary";
        case Id::ModSettingsAudioGlossaryOpened:    return "Audio glossary opened";
        case Id::GlossaryEntryDoorOpen:             return "Door open";
        case Id::GlossaryEntryDoorClosedMetal:      return "Metal door closed";
        case Id::GlossaryEntryDoorClosedWood:       return "Wood door closed";
        case Id::GlossaryEntryDoorClosedStone:      return "Stone door closed";
        case Id::GlossaryEntryWall:                 return "Wall";
        case Id::GlossaryEntryHazard:               return "Hazard";
        case Id::GlossaryEntryCollision:            return "Collision";
        case Id::GlossaryEntryBeaconActive:         return "Beacon active";
        case Id::GlossaryEntryBeaconWaypoint:       return "Beacon waypoint reached";
        case Id::GlossaryEntryBeaconDestination:    return "Beacon destination reached";
        case Id::GlossaryEntrySwoopAccelTick:       return "Swoop gear bar tick";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Swoop accelerator pad";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Swoop obstacle warning";
        case Id::GlossaryEntrySwoopWallImpact:      return "Swoop wall impact";

        case Id::FmtUpdateAvailable:    return "Update available, version %s. Press F5 from the main menu to install.";
        case Id::UpdateDownloading:     return "Downloading update.";
        case Id::UpdateDownloaded:      return "Update downloaded. Closing game to install.";
        case Id::UpdateFailed:          return "Update download failed.";
        case Id::FmtUpdateNotAvailable: return "No update available. You are on version %s.";
        case Id::UpdateNotInMenu:       return "Updates can only be installed from the main menu.";

        case Id::PanelTitleMainMenu:    return "Main menu";
        case Id::LoadingPleaseWait:     return "Game is still loading, please wait.";
        case Id::LoadingStuckWorkaround: return "Menu still unresponsive. Press Alt F4 and cancel the quit dialog to wake it.";

        case Id::GamePaused:            return "Paused.";
        case Id::GameResumed:           return "Unpaused.";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_en
