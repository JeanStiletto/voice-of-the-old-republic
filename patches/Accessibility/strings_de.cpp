// German string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE4=?, \xF6=?,
// \xFC=?, \xDF=?, \xC4=?, \xD6=?, \xDC=?). Prism's ANSI overload converts
// from CP_ACP via MultiByteToWideChar; on a German Windows install
// CP_ACP = Windows-1252, so the literal bytes here pass through
// unchanged. UTF-8 source would also work but only with /utf-8 set, and
// the patch build script (create-patch.bat) doesn't pass that flag.
//
// Direction frame: German nav uses "auf X Uhr" ("at X o'clock") rather
// than the English "X o'clock" ? the bare "X Uhr" form would read as
// time-of-day. Plurals: empty-state phrases use the plural form
// (T\xFCren, not T\xFCr) since "Keine T\xFCr in Reichweite" sounds odd.

#include "strings.h"

namespace acc::strings::lang_de {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "T\xFCr";              // T?r
        case Id::CategoryNpc:         return "Person";
        case Id::CategoryContainer:   return "Beh\xE4lter";         // Beh?lter
        case Id::CategoryItem:        return "Gegenstand";
        case Id::CategoryLandmark:    return "Ort";
        case Id::CategoryTransition:  return "\xDC" "bergang";       // ?bergang
        case Id::CategoryMapHint:     return "Hinweis";

        case Id::EmptyDoors:          return "Keine T\xFCren in Reichweite";        // T?ren
        case Id::EmptyNpcs:           return "Keine Personen in Reichweite";
        case Id::EmptyContainers:     return "Keine Beh\xE4lter in Reichweite";     // Beh?lter (sg=pl)
        case Id::EmptyItems:          return "Keine Gegenst\xE4nde in Reichweite";  // Gegenst?nde
        case Id::EmptyLandmarks:      return "Keine Orte in Reichweite";
        case Id::EmptyTransitions:    return "Keine \xDC" "berg\xE4nge in Reichweite"; // ?berg?nge
        case Id::EmptyMapHints:       return "Keine Hinweise auf dieser Karte";
        case Id::EmptyAll:            return "Keine Objekte in Reichweite";
        case Id::CycleNoTarget:       return "Kein Ziel";

        case Id::MapPinNoText:        return "Marke";
        case Id::MapPinAltDashUnsupported: return "Marke: Alt+Bindestrich nicht unterst\xFCtzt"; // unterst?tzt
        case Id::MapPinInteractHint:  return "Marke. Strg+Bindestrich f\xFCr Beacon."; // f?r

        case Id::FmtSavedMarkerAutoNumber:   return "Marke %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marke %d";
        case Id::FmtSavedMarkerPlaced:       return "Marke gespeichert: %s";
        case Id::SavedMarkerFailed:          return "Marke konnte nicht gespeichert werden";

        case Id::HintBackEntrance:    return "Hintereingang";
        case Id::HintRebelCorpse:     return "Toter Rebell";
        case Id::HintBanthaHerd:      return "Banthaherde";
        case Id::MapNoteFrontExit:    return "Vorderer Ausgang";
        case Id::MapNoteBackExit:     return "Hinterer Ausgang";

        case Id::FmtAnnounceWithClock: return "%s, auf %d Uhr, %d Meter";
        case Id::FmtAnnounceNoClock:   return "%s, %d Meter";
        case Id::FmtCategoryItem:      return "%s. %s";
        case Id::FmtTrapDetected:      return "Falle entdeckt: %s";
        case Id::MineNoun:             return "Mine";

        case Id::FmtGuidingTo:         return "Gehe zu %s";
        case Id::FmtGuidingFailed:     return "Gehe zu %s fehlgeschlagen";
        case Id::GuidanceNoFocus:      return "Kein Ziel ausgew\xE4hlt";  // ausgew?hlt
        case Id::GuidingToPoint:       return "Gehe zum Punkt";

        case Id::MovementCancelled:    return "Bewegung abgebrochen";
        case Id::InteractWayBlocked:   return "Bewegung abgebrochen, Weg versperrt";
        case Id::FmtInteractWayBlockedTarget: return "Bewegung abgebrochen, Weg versperrt. %s, %d Meter, %s";
        case Id::SpectatorBattleDoomed: return "Die republikanischen Soldaten sind verloren. Du kannst ihnen nicht mehr helfen. Der Weg ist versperrt. Eile weiter zur Br\xFC" "cke!";  // Br?cke

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Beacon zu %s";
        case Id::BeaconCancelled:      return "Beacon abgebrochen";
        case Id::FmtBeaconNoPath:      return "Kein Pfad zu %s";
        case Id::BeaconAlreadyAtDest:  return "Bereits am Ziel";
        // Route description.
        // Args: name, total metres, joined segment list, transition tail.
        // Example: "Route zu T\xFCr (25 Meter): 5 Meter Norden,
        //           4 Meter Nord-Ost, 6 Meter Osten. Kein \xDC" "bergang."
        case Id::FmtRouteHeader:       return "Route zu %s (%d Meter): %s. %s.";
        case Id::FmtRouteSegment:      return "%d Meter %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "Ein \xDC" "bergang";          // ?bergang
        case Id::RouteNoTransition:    return "Kein \xDC" "bergang";         // ?bergang
        case Id::FmtBeaconNextSegment: return "Weiter %d Meter %s";

        case Id::FmtInteractTalk:      return "Sprich mit %s";
        case Id::FmtInteractOpen:      return "\xD6" "ffne %s";                 // ?ffne
        case Id::FmtInteractTake:      return "Hebe %s auf";
        case Id::FmtInteractFailed:    return "Interaktion mit %s fehlgeschlagen";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Aktionsmen\xFC, %s";          // Aktionsmen?
        case Id::FmtMedPickerApplyTo:  return "Anwenden auf: %s";
        case Id::FmtInteractNoActionsRedirect: return "Keine Aktionen verf\xFC" "gbar f\xFCr %s. Enter zum Aktivieren."; // verf?gbar / f?r
        case Id::FmtInteractNoActions: return "Keine Aktionen verf\xFC" "gbar f\xFCr %s.";  // verf?gbar / f?r
        case Id::DoorSealedNoOpen: return "Versiegelt. L\xE4sst sich nicht \xF6" "ffnen.";  // Lasst / offnen
        case Id::EndarDoorBattleHint: return "Diese T\xFCr bleibt verschlossen, bis der Kampf in diesem Bereich vorbei ist.";
        case Id::EndarStuckReloadHint: return "Falls keine Gegner mehr in der N\xE4he sind, steckt das Tutorial m\xF6glicherweise fest. Lade einen fr\xFCheren Spielstand von vor diesem Raum.";

        case Id::FmtActionBarColumnEmpty: return "Spalte %d ist leer";
        case Id::FmtFireAtPosition:       return "%s, Platz %d";
        case Id::ActionRefused:           return "Nicht m\xF6glich.";
        case Id::FmtFireQueueFull:        return "%s, Warteschlange voll";
        case Id::ActionMenuClosed:        return "Aktionsmen? geschlossen.";

        case Id::MenuCatAttacks:       return "Angriffe";
        case Id::MenuCatForcePowers:   return "Machtkr\xE4""fte";       // Machtkr?fte
        case Id::MenuCatItems:         return "Gegenst\xE4nde";          // Gegenst?nde
        case Id::MenuCatSelfPowers:    return "Eigene Machtkr\xE4""fte"; // Eigene Machtkr?fte
        case Id::MenuCatMedical:       return "Medizin";
        case Id::MenuCatMisc:          return "Sonstiges";
        case Id::MenuCatExplosives:    return "Sprengstoffe";
        case Id::MenuCatCombatBehaviour: return "Kampfverhalten";
        case Id::FmtMenuCatMulti:      return "%s: %s, %d Optionen";
        case Id::FmtMenuCatSingle:     return "%s: %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d Optionen";
        case Id::FmtMenuCategoryEmpty: return "%s: leer";

        case Id::NoTooltipAvailable:   return "Keine Beschreibung verf\xFC" "gbar";  // verf?gbar

        case Id::ContainerEmpty:       return "Leer";
        case Id::ContainerOneItem:     return "1 Gegenstand";
        case Id::FmtContainerItems:    return "%d Gegenst\xE4nde";              // Gegenst?nde
        case Id::FmtContainerItemAt:   return "%s, %d von %d";
        case Id::ContainerEmptySuffix: return "leer";
        case Id::PlaceableStateCaptive:     return "gefangen";
        case Id::PlaceableStateFreed:       return "befreit";
        case Id::PlaceableStateActive:      return "aktiv";
        case Id::PlaceableStateDeactivated: return "deaktiviert";
        case Id::FmtItemStackSuffix:   return "%d St\xFC" "ck";                    // St?ck (split: 'c' is a hex digit)
        case Id::FmtItemChargeSuffix:  return "%d Ladungen";

        case Id::EquipSlotHead:        return "Kopf";
        case Id::EquipSlotImplant:     return "Implantat";
        case Id::EquipSlotBody:        return "K\xF6rper";                       // K?rper
        case Id::EquipSlotArmL:        return "Linker Arm";
        case Id::EquipSlotArmR:        return "Rechter Arm";
        case Id::EquipSlotWeapL:       return "Linke Waffe";
        case Id::EquipSlotWeapR:       return "Rechte Waffe";
        case Id::EquipSlotBelt:        return "G\xFCrtel";                       // G?rtel
        case Id::EquipSlotHands:       return "H\xE4nde";                        // H?nde

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, leer";
        case Id::EquipUnequipped:      return "Ausr\xFCstung abgelegt";
        case Id::FmtEquipStillEquipped: return "Gegenstand weiterhin angelegt: %s";
        case Id::FmtWeaponSwitchedOne: return "Waffen gewechselt: %s";
        case Id::FmtWeaponSwitchedTwo: return "Waffen gewechselt: %s und %s";
        case Id::WeaponSwitchedBare:   return "Waffen gewechselt";
        case Id::EquipSetActive:       return "aktiv";
        case Id::EquipSetSecondary:    return "sekund\xE4r";
        case Id::FmtEquipVitality:     return "Vitalit\xE4t %s";                  // Vitalit?t
        case Id::FmtEquipDefense:      return "Verteidigung %s";
        case Id::FmtEquipAttack:       return "Angriff %s";
        case Id::FmtEquipAttackDual:   return "Angriff links %s, rechts %s";
        case Id::FmtEquipDamage:       return "Schaden %s";
        case Id::FmtEquipDamageDual:   return "Schaden links %s, rechts %s";

        case Id::FmtTransitionArea:    return "Bereich: %s";
        case Id::FmtTransitionRoomIndex: return "Raum %d";
        case Id::FmtTransitionLoading: return "Lade: %s";

        case Id::DoorOpen:             return "offen";
        case Id::DoorLocked:           return "verriegelt";
        case Id::DoorUnlocked:         return "entriegelt";
        case Id::DoorCosmetic:         return "kosmetisch";

        case Id::DirNorth:             return "Nord";
        case Id::DirNortheast:         return "Nord-Ost";
        case Id::DirEast:              return "Ost";
        case Id::DirSoutheast:         return "S\xFC" "d-Ost";                  // S?d-Ost
        case Id::DirSouth:             return "S\xFC" "d";                      // S?d
        case Id::DirSouthwest:         return "S\xFC" "d-West";                 // S?d-West
        case Id::DirWest:              return "West";
        case Id::DirNorthwest:         return "Nord-West";

        case Id::StuckFreeDirsPrefix:  return "Frei";
        case Id::StuckAllBlocked:      return "Alles blockiert";


        case Id::FmtMapStateOriented:    return "%s. Blick auf der Karte nach %d Grad, %s.";
        case Id::FmtMapStateUnknownRoom: return "Blick auf der Karte nach %d Grad, %s.";

        case Id::FmtWorldStateOriented:       return "Blick %s.\n%s.";
        case Id::FmtWorldStateUnknownCluster: return "Blick %s.";

        case Id::MouseLookOn:          return "Maussteuerung an";
        case Id::MouseLookOff:         return "Maussteuerung aus";

        case Id::ViewModeOn:           return "Umsehen-Modus an";
        case Id::ViewModeOff:          return "Umsehen-Modus aus";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d von %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d von %d";
        case Id::FmtSaveLoadCharacter: return "%s, Spielst\xE4nde: %d";

        case Id::LevelUpOpen:          return "Stufenaufstieg";
        case Id::LevelUpFailed:        return "Stufenaufstieg fehlgeschlagen";
        case Id::LevelUpAlreadyOpen:   return "Stufenaufstieg bereits offen";
        case Id::LevelUpNotReady:      return "Noch nicht genug Erfahrung f\xFCr einen Stufenaufstieg"; // f?r
        case Id::LevelUpScreenHint:    return "W\xE4hle mit den Pfeiltasten eine Kategorie und dr\xFC""cke Enter, um sie zu \xF6""ffnen. Verteile dort deine Punkte und w\xE4hle dann Annehmen, um abzuschlie\xDF""en."; // Wahle druecke oeffnen abschliessen
        case Id::LevelUpGrantedPopupHint: return "Enter zum Best\xE4tigen.";  // Bestaetigen

        case Id::PortraitLabel:        return "Portr\xE4t";              // Portr?t
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "weiblich";
        case Id::PortraitGenderMale:   return "m\xE4nnlich";              // m?nnlich
        case Id::PortraitRaceAsian:    return "asiatisch";
        case Id::PortraitRaceDark:     return "dunkelh\xE4utig";          // dunkelh?utig
        case Id::PortraitRaceLight:    return "hellh\xE4utig";            // hellh?utig
        case Id::PortraitRaceTypeH:    return "Typ H";
        case Id::FmtPortraitDescription: return "%s %s %d";
        case Id::FmtClassNameWithDescription: return "%s. %s";

        case Id::FmtPartyPortraitInTeam:    return "%s, im Team";
        case Id::FmtPartyPortraitAvailable: return "%s, verf\xFC""gbar";    // verf?gbar
        case Id::PartySelectionFull:        return "Gruppe voll";

        case Id::DisabledSuffix:       return ", nicht verf\xFC""gbar";   // nicht verf?gbar
        case Id::ToggleOn:             return ", ein";
        case Id::ToggleOff:            return ", aus";
        case Id::FmtSliderValueLabeled: return "%s %u von %u";
        case Id::FmtSliderValue:       return "%u von %u";
        case Id::EquipMenuName:        return "Ausr\xFC""stung";          // Ausruestung
        case Id::FmtLevelUpDoStepFirst: return "Zuerst %s abschlie\xDF""en.";  // abschlie?en
        case Id::LevelUpStepLocked:    return "Noch nicht an der Reihe.";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Stufe %s. ";
        case Id::FmtCharSheetXp:       return "Erfahrung %s von %s. ";
        case Id::FmtCharSheetHp:       return "Lebenspunkte %s. ";
        case Id::FmtCharSheetFp:       return "Machtpunkte %s. ";
        case Id::FmtCharSheetStr:      return "St\xE4rke %s%s%s. ";          // St?rke
        case Id::FmtCharSheetDex:      return "Geschicklichkeit %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Verfassung %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Intelligenz %s%s%s. ";
        case Id::FmtCharSheetWis:      return "Weisheit %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Charisma %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Gesinnung %u von %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modifikator %s, Preis %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, verbleibende Punkte %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, Modifikator %s, verbleibende Punkte %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, verbleibende Punkte %s, Preis %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, Modifikator %s, verbleibende Punkte %s, Preis %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Preis %s";
        case Id::FmtChargenSkillValueChange: return "%s, verbleibende Punkte %s";

        case Id::ChargenFeatGrantedTitle:    return "Du erh\xE4ltst diese Talente";  // erh?ltst
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d von %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "verf\xFCgbar";              // verf?gbar
        case Id::ChargenFeatStatusExisting:  return "bereits gelernt";
        case Id::ChargenFeatStatusGranted:   return "automatisch erhalten";
        case Id::ChargenFeatStatusLocked:    return "Voraussetzung fehlt";
        case Id::ChargenFeatStatusChosen:    return "ausgew\xE4hlt";              // ausgew?hlt

        case Id::EditboxRole:                return "Eingabefeld";
        case Id::EditboxEmpty:               return "leer";
        case Id::FmtKeyBinding:              return "%s: %s";
        case Id::KeyBindingFixed:            return " (nicht \xe4nderbar)";
        case Id::FmtKeyBindCapture:          return "Dr\xfc""cke die neue Taste f\xfcr %s";
        case Id::KeyBindNotChangeable:       return "Diese Belegung ist nicht \xe4nderbar";

        case Id::CombatBegins:               return "Kampf beginnt";
        case Id::CombatEnds:                 return "Kampf beendet";
        case Id::CombatLeaderAtPeace:        return "Nicht im Kampf";

        case Id::PcStatNoCharacter:          return "Kein Status verf\xFCgbar.";

        // Brief wird in BuildTargetCombatBrief zusammengesetzt: Name,
        // dann optionale Zust\xE4nde / Entfernung / Effekte / Waffen
        // jeweils mit f\xFChrendem Leerzeichen und Punkt am Ende.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "feindlich";
        case Id::FactionFriendly:            return "freundlich";
        case Id::FactionNeutral:             return "neutral";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d Meter.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " Nebenhand %s.";
        case Id::FmtSelfStatusHp:            return "%d Lebenspunkte.";
        case Id::FmtSelfStatusHpOf:          return "%d von %d Lebenspunkten.";
        case Id::FmtSelfStatusFpOf:          return "%d von %d Machtpunkten.";

        case Id::ExamineNoTarget:            return "Kein Ziel zum Untersuchen.";
        case Id::ExamineFailed:              return "Untersuchen fehlgeschlagen.";

        case Id::FmtExamineOpened:           return "Untersuchen: %s. %d Eintr\xE4ge.";
        case Id::FmtExamineRowOf:            return "%s. %d von %d.";
        case Id::ExamineViewClosed:          return "Untersuchen geschlossen.";
        case Id::FmtExamineRowName:          return "Name: %s";
        case Id::FmtExamineRowFaction:       return "Gesinnung: %s";
        case Id::FmtExamineRowHp:            return "Lebenspunkte: %d";
        case Id::FmtExamineRowDistance:      return "Entfernung: %d Meter";
        case Id::FmtExamineRowWeapon:        return "Hauptwaffe: %s";
        case Id::ExamineRowWeaponNone:       return "Hauptwaffe: keine";
        case Id::FmtExamineRowEffect:        return "Effekt: %s";
        case Id::FmtExamineRowFeat:          return "Talent: %s";
        case Id::FmtExamineRowEffectUnknown: return "Effekt #%d";
        case Id::FmtExamineRowFeatUnknown:   return "Talent #%d";
        case Id::ExamineRowNoEffects:        return "Keine aktiven Effekte";
        case Id::ExamineRowNoFeats:          return "Keine Talente";

        case Id::FmtExamineRowHpFull:        return "Lebenspunkte: %d von %d";
        case Id::FmtExamineRowLevel:         return "Stufe: %d";
        case Id::FmtExamineRowCondition:     return "Zustand: %s";
        case Id::DamageLevel0Healthy:        return "unverletzt";
        case Id::DamageLevel1Light:          return "leicht verletzt";
        case Id::DamageLevel2Wounded:        return "verletzt";
        case Id::DamageLevel3Badly:          return "schwer verletzt";
        case Id::DamageLevel4Dying:          return "im Sterben";
        case Id::DamageLevel5Dead:           return "tot";
        case Id::FmtExamineRowOffHand:       return "Nebenhand: %s";
        case Id::FmtExamineRowHead:          return "Kopf: %s";
        case Id::FmtExamineRowTorso:         return "R\xFCstung: %s";
        case Id::FmtExamineRowHands:         return "H\xE4nde: %s";
        case Id::ExamineRowStatusBlind:      return "Blind";

        case Id::FmtQueueOpen:               return "Aktionsschlange, %d Aktionen.";
        case Id::QueueEmpty:                 return "Aktionsschlange ist leer.";
        case Id::FmtQueueRow:                return "%s: %s %s, %d von %d.";
        case Id::FmtQueueRemoved:            return "Entfernt: %s.";
        case Id::QueueCleared:               return "Schlange geleert.";
        case Id::QueueClosed:                return "Schlange geschlossen.";
        case Id::QueueRemoveFailed:          return "Aktion kann nicht entfernt werden.";
        case Id::QueueVerbAttack:            return "Angriff";
        case Id::QueueVerbCastForce:         return "Macht einsetzen";
        case Id::QueueVerbItemCast:          return "Gegenstand benutzen";
        case Id::QueueVerbEquip:             return "Anlegen";
        case Id::QueueVerbUnequip:           return "Ablegen";
        case Id::QueueVerbUseTalent:         return "Talent einsetzen";
        case Id::QueueVerbUnknown:           return "Aktion";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "%s bei N von M"
        // tail. Args reduced to (attacker, target, damage).



        case Id::DialogNoReplyOptions:          return "Keine Antwortm\xF6glichkeiten";  // Keine Antwortmöglichkeiten
        case Id::MessagesTitleCombatLog:     return "Kampfprotokoll.";

        case Id::MapPrevNote:                return "Vorheriger Hinweis";
        case Id::MapNextNote:                return "N\xE4""chster Hinweis";  // N?chster

        case Id::MapCursorUnexplored:        return "Nebel des Krieges";
        case Id::MapCursorWaypointPOI:       return "Punkt von Interesse";
        case Id::MapCursorJunction:          return "Kreuzung";
        case Id::FmtMapCursorDeadEnd:        return "Sackgasse, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Kreuzung, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "T\xFC""r";            // T?r
        case Id::FmtMapCursorDoor:           return "%s %s";               // noun + dir
        case Id::FmtMapCursorDoorTransition: return "%s %s nach %s";       // noun + dir + dest
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";           // noun + dir + landmark
        case Id::FmtMapCursorJunctionDeadEndExit: return "Sackgasse %s";
        case Id::FmtRampExit:                return "Rampe %s";
        case Id::AxisNorthSouth:             return "Nord-S\xFC""d";   // Nord-S?d
        case Id::AxisEastWest:               return "Ost-West";
        case Id::AreaNoun:                   return "Bereich";
        case Id::AreaNounLarge:              return "Gro\xDF""er Bereich";  // Gro?er Bereich
        case Id::FmtAreaAxisExits:           return "%s %s. Ausg\xE4""nge: %s";
        case Id::FmtAreaExits:               return "%s. Ausg\xE4""nge: %s";
        case Id::FmtAreaAxisOnly:            return "%s %s";
        case Id::FarAtNorth:                 return "weit im Norden";
        case Id::FarAtSouth:                 return "weit im S\xFC""den";
        case Id::FarAtEast:                  return "weit im Osten";
        case Id::FarAtWest:                  return "weit im Westen";

        case Id::FmtStorePriceBuyFinite:    return "Preis %d Credits, Lager %d";
        case Id::FmtStorePriceBuyUnlimited: return "Preis %d Credits, Lager unbegrenzt";
        case Id::FmtStorePriceSell:         return "Preis %d Credits, du besitzt %d";
        case Id::StoreModeBuy:              return "Modus Kaufen";
        case Id::StoreModeSell:             return "Modus Verkaufen";
        case Id::FmtCraftCost:              return "Kosten %d";
        case Id::FmtCraftYield:             return "Ergibt %d";
        case Id::StoreCannotSell:           return "Kann nicht verkauft werden";
        case Id::StoreCannotBuy:            return "Kann nicht gekauft werden";
        case Id::FmtStoreSoldFor:           return "Verkauft f\xFC""r %d Credits";  // f?r
        case Id::FmtStoreBoughtFor:         return "Gekauft f\xFC""r %d Credits";   // f?r

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Dr\xFC""cke C f\xFC""r deine Hand, T f\xFC""r den Tisch.";
        case Id::PazaakEmpty:            return "leer";
        case Id::PazaakFaceDown:         return "verdeckt";
        case Id::PazaakBoardEmpty:       return "leer";
        case Id::PazaakFmtPlus:          return "plus %d";
        case Id::PazaakFmtMinus:         return "minus %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "plus oder minus %d";
        case Id::PazaakFmtFlipCurrently: return "%s, aktuell %s";
        case Id::PazaakFmtCardTiebreaker: return "%s, Gleichstandskarte";
        case Id::PazaakCardDouble:       return "Verdopplungskarte";
        case Id::PazaakCardTwoFour:      return "2 oder 4";
        case Id::PazaakCardThreeSix:     return "3 oder 6";
        case Id::PazaakCardValue:        return "Wertkarte";
        case Id::PazaakFmtYouDrew:       return "Du ziehst %s. Deine Summe %d.";
        case Id::PazaakOverTwenty:       return "\xDC""ber zwanzig.";
        case Id::PazaakFmtYouPlayed:     return "%s gespielt. Deine Summe %d.";
        case Id::PazaakYourTurn:         return "Du bist am Zug.";
        case Id::PazaakTurnEnded:        return "Zug beendet.";
        case Id::PazaakFmtOppDrew:       return "Gegner zieht %s. Summe %d.";
        case Id::PazaakFmtOppPlayed:     return "Gegner spielt %s. Summe %d.";
        case Id::PazaakFmtOppStands:     return "Gegner bleibt bei %d.";
        case Id::PazaakFmtYouStand:      return "Du bleibst bei %d.";
        case Id::PazaakFmtWinSet:        return "Du gewinnst den Satz. %d zu %d.";
        case Id::PazaakFmtLoseSet:       return "Du verlierst den Satz. %d zu %d.";
        case Id::PazaakWinMatch:         return "Du gewinnst das Spiel!";
        case Id::PazaakLoseMatch:        return "Du verlierst das Spiel.";
        case Id::PazaakTieReplay:        return "Unentschieden. Satz wird wiederholt.";
        case Id::PazaakFmtHand:          return "Hand: %s";
        case Id::PazaakHandEmpty:        return "Hand leer.";
        case Id::PazaakFmtYourBoard:     return "Dein Tisch: %s, Summe %d.";
        case Id::PazaakFmtOppBoard:      return "Gegnertisch: %s, Summe %d.";
        case Id::PazaakNoPlayable:       return "Keine Karten zum Spielen.";
        case Id::PazaakNotYourTurn:      return "Du bist nicht am Zug.";
        case Id::PazaakChooseSign:       return "Vorzeichen w\xE4""hlen. Links oder rechts zum \xC4""ndern, Enter zum Spielen.";
        case Id::PazaakChooseSignAndValue:
            return "Vorzeichen und Wert w\xE4""hlen. Links oder rechts f\xFC""r das Vorzeichen, hoch oder runter f\xFC""r den Wert, Enter zum Spielen.";
        case Id::PazaakCancelled:        return "Abgebrochen.";
        case Id::PazaakDeckAvailable:    return "%s, %d verf\xFC""gbar";
        case Id::PazaakDeckNoneLeft:     return "%s, keine mehr";
        case Id::PazaakDeckSlotFilled:   return "Deck-Platz %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Deck-Platz %d: leer";
        case Id::PazaakDeckPlay:         return "Spielen, %d von 10 im Deck";
        case Id::PazaakDeckCleared:      return "Deck geleert.";
        case Id::PazaakDeckAdded:        return "Hinzugef\xFC""gt %s. %d von 10.";
        case Id::PazaakDeckRemoved:      return "Entfernt %s.";
        case Id::PazaakDeckFull:         return "Deck voll.";
        case Id::PazaakFmtOppHand:       return "Gegner hat %d Handkarten.";
        case Id::PazaakStandLabel:       return "Halten";
        case Id::PazaakEndTurnLabel:     return "Runde beenden";
        case Id::PazaakWagerLess:        return "Einsatz verringern";
        case Id::PazaakWagerMore:        return "Einsatz erh\xF6hen";
        case Id::PazaakFmtWager:         return "Einsatz %d von maximal %d.";
        case Id::PazaakFmtWagerRow:      return "Einsatz %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Nicht genug Credits, brauchst %d, hast %d";
        case Id::JournalQuestItemsButton:   return "Auftrags-Gegenst\xE4nde";

        case Id::FmtCredits:                return "Credits: %s";

        // Workbench slot labels. The seven BTN_UPGRADE3X/4X buttons have
        // no inline text ? their visual content is the installed mod's
        // icon + name set programmatically. Speak "Aufwertungssteckplatz N"
        // (upgrade slot N) for weapon slots and "Kristall-Steckplatz N"
        // for lightsaber crystal slots so the user can tell which slot
        // is focused. Concrete category words ("Vibrationszelle",
        // "Skopus", ?) would require reading LBL_SLOTNAME dynamically
        // for the active slot only ? deferred to a future enrichment.
        case Id::WorkbenchSlotWeapon1:       return "Aufwertungssteckplatz 1";
        case Id::WorkbenchSlotWeapon2:       return "Aufwertungssteckplatz 2";
        case Id::WorkbenchSlotWeapon3:       return "Aufwertungssteckplatz 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Kristall-Steckplatz 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Kristall-Steckplatz 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Kristall-Steckplatz 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Kristall-Steckplatz 4";
        case Id::WorkbenchSlotSaberCrystal5: return "Kristall-Steckplatz 5";
        case Id::WorkbenchSlotSaberCrystal6: return "Kristall-Steckplatz 6";
        case Id::WorkbenchItemsEmpty:        return "Keine aufwertbaren Gegenst\xE4nde in dieser Kategorie";  // Gegenst?nde
        case Id::WorkbenchUpgradesEmpty:     return "Keine kompatiblen Aufwertungen im Inventar";
        case Id::WorkbenchSlotInstalled:     return "Aufwertung eingesetzt";
        case Id::WorkbenchSlotRemoved:       return "Aufwertung entfernt";
        case Id::WorkbenchSlotNoMatch:       return "Keine passende Aufwertung im Inventar";
        case Id::WorkbenchSlotFilled:        return "belegt";
        case Id::WorkbenchSlotPeekEmpty:     return "Leerer Steckplatz, keine Aufwertung eingesetzt";
        case Id::WorkbenchFmtSlotItem:       return "%s, belegt mit %s";
        case Id::WorkbenchPickerInstalled:   return "eingesetzt";
        case Id::WorkbenchPickerNone:        return "Kein Gegenstand";
        case Id::WorkbenchPickerUnnamed:     return "Aufwertung";
        case Id::WorkbenchUpgradeFailed:     return "Aufwertung nicht m\xF6glich";
        case Id::WorkbenchModeCreate:        return "Modus Herstellen";
        case Id::WorkbenchModeBreakdown:     return "Modus Zerlegen";
        case Id::FmtWorkbenchUpgradeTitle:   return "Aufwerten: %s";

        case Id::SoundOptionsMovieVolume:    return "Video-Lautst\xE4rke";  // Lautst?rke

        case Id::SwoopRaceStarted:
            // Terse opener ? concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Swoop-Rennen.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is documented in the manual
            // ? verbose spoken intro got in the way of the start
            // countdown so the shorter form is preferred.
            return "Leertaste zum Hochschalten beim Audiosignal. Mit A und D lenken. Weiche den Hindernissen aus und triff die Beschleunigerfelder, um die Zeit zu schlagen.";
        case Id::SwoopRaceEnded:
            return "Swoop-Rennen beendet.";
        case Id::FmtSwoopRaceGear:
            return "Gang %d";
        case Id::FmtSwoopRaceTime:
            return "Zeit: %d,%02d Sekunden. Swoop-Rennen beendet.";

        case Id::TurretGameStarted:
            // Terse opener ? concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Gesch\xFCtzturm.";  // Gesch?tzturm
        case Id::TurretGameControls:
            return "Mit WASD zielen. Leertaste zum Feuern. Q und E w\xE4hlen Ziele.";
        case Id::TurretGameEnded:
            return "Gesch\xFCtzturm beendet.";
        case Id::FmtTurretTarget:
            return "J\xE4ger %d, %d Meter";  // J?ger
        case Id::FmtTurretDestroyed:
            return "J\xE4ger %d zerst\xF6rt.";  // J?ger %d zerst?rt.
        case Id::TurretNoTargets:
            return "Keine Ziele.";

        // "Mod Einstellungen" / "Erweitertes Wechseln" / "Raumformen" /
        // "Wandger\xe4usche" ? \xe4 = ?, \xfc = ?. Encoding matches the
        // Windows-1252 convention used throughout strings_de.cpp.
        case Id::ModSettingsRootLabel:        return "Mod-Einstellungen";
        case Id::ModSettingsOpened:           return "Mod-Einstellungen ge\xf6""ffnet";
        case Id::ModSettingsClosed:           return "Mod-Einstellungen geschlossen";
        case Id::ModSettingExtendedCycling:   return "Kartenweite Objektauswahl";
        case Id::ModSettingRoomShapes:        return "Raumform-Beschreibungen";
        case Id::ModSettingWallSounds:        return "Wandger\xe4usche";
        case Id::ModSettingHumanSubtitles:    return "Untertitel vertonter Sprecher vorlesen";
        case Id::ModSettingSubtitleSapi:      return "Untertitel mit der Ansagestimme sprechen";
        case Id::ModSettingTurretAutoAim:     return "Automatisches Zielen";
        case Id::ModSettingSkipIntros:        return "Startvideos \xFC" "berspringen";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "Startvideos werden beim n\xE4""chsten Start \xFC" "bersprungen.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "Startvideos werden beim n\xE4""chsten Start abgespielt.";
        case Id::ModSettingSkipIntrosToggleFailed: return "Startvideos konnten nicht umgeschaltet werden. Dateien k\xF6nnten entfernt worden sein.";
        case Id::ModSettingStateOn:           return "an";
        case Id::ModSettingStateOff:          return "aus";
        case Id::FmtModSettingOption:         return "%s: %s";
        case Id::ModSettingCueVolume:         return "Lautst\xe4rke der Hinweist\xf6ne";
        case Id::FmtModSettingSlider:         return "%s: %d Prozent";
        case Id::ModSettingUrgentVolume:      return "Lautst\xe4rke der Sprachansagen";
        case Id::ModSettingUrgentVoice:       return "Ansagestimme";
        case Id::ModSettingUrgentVoiceUnavailable: return "nicht verf\xFCgbar";
        case Id::ModSettingUrgentVolumePreview: return "Beispielansage";
        case Id::ModSettingSupportModder:           return "Modder unterst\xFCtzen";
        case Id::ModSettingLatestChanges:           return "Neueste \xC4nderungen";
        case Id::ModSettingOpenReadme:              return "Anleitung \xF6" "ffnen";
        case Id::ModSettingLinkOpened:              return "Wird im Browser ge\xF6" "ffnet.";
        case Id::ModSettingLinkFailed:              return "Der Link konnte nicht ge\xF6" "ffnet werden.";

        case Id::ModSettingAudioGlossary:           return "Audio-Glossar";
        case Id::ModSettingsAudioGlossaryOpened:    return "Audio-Glossar ge\xf6""ffnet";
        case Id::GlossaryEntryDoorOpen:             return "T\xfc""r offen";
        case Id::GlossaryEntryDoorClosedMetal:      return "Metallt\xfc""r geschlossen";
        case Id::GlossaryEntryDoorClosedWood:       return "Holzt\xfc""r geschlossen";
        case Id::GlossaryEntryDoorClosedStone:      return "Steint\xfc""r geschlossen";
        case Id::GlossaryEntryWall:                 return "Wand";
        case Id::GlossaryEntryHazard:               return "Gefahr";
        case Id::GlossaryEntryCollision:            return "Kollision";
        case Id::GlossaryEntryBeaconActive:         return "Wegweiser aktiv";
        case Id::GlossaryEntryBeaconWaypoint:       return "Wegpunkt erreicht";
        case Id::GlossaryEntryBeaconDestination:    return "Ziel erreicht";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Swoop-Beschleunigerfeld";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Swoop-Hinderniswarnung";
        case Id::GlossaryEntrySwoopWallImpact:      return "Swoop-Wandaufprall";
        case Id::GlossaryEntrySwoopAligned:         return "Swoop auf Kurs";
        case Id::GlossaryEntrySwoopShiftReady:      return "Swoop-Gangwechsel bereit";

        // "dr?cken" ? the hex escape \xFC followed by literal 'c' would be
        // parsed as \xFCc (out of range). Split the literal across two
        // string tokens to keep the escape sequence terminated.
        case Id::FmtUpdateAvailable:    return "Update verf\xFCgbar, Version %s. Im Hauptmen\xFC F5 dr\xFC" "cken, um zu installieren.";
        case Id::UpdateDownloadStarting: return "Download wird gestartet.";
        case Id::UpdateDownloading:     return "Update wird heruntergeladen.";
        case Id::UpdateDownloaded:      return "Update heruntergeladen. Spiel wird beendet, um zu installieren.";
        case Id::UpdateFailed:          return "Download des Updates fehlgeschlagen. F5 dr\xFC" "cken, um es erneut zu versuchen.";
        case Id::FmtUpdateNotAvailable: return "Kein Update verf\xFCgbar. Aktuelle Version %s.";
        case Id::UpdateNotInMenu:       return "Updates k\xF6nnen nur aus dem Hauptmen\xFC installiert werden.";

        case Id::PanelTitleMainMenu:    return "Hauptmen\xFC";
        case Id::LoadingPleaseWait:     return "Spiel l\xE4""dt noch, bitte warten.";
        case Id::LoadingStuckWorkaround: return "Men\xFC reagiert immer noch nicht. Alt F4 dr\xFC""cken und den Beenden-Dialog abbrechen, um es zu wecken.";

        case Id::GamePaused:            return "Pause.";
        case Id::GameResumed:           return "Pause aufgehoben.";

        case Id::GalaxyMapTitle:        return "Galaxiekarte";

        // ---- Help system ----
        case Id::HelpGroupGeneral:      return "Navigation";
        case Id::HelpGroupMovement:     return "Bewegung und Kamera";
        case Id::HelpGroupInteraction:  return "Ziele und Interaktion";
        case Id::HelpGroupCombat:       return "Kampf und Aktionen";
        case Id::HelpGroupExploration:  return "Erkundung und Orientierung";
        case Id::HelpGroupScreens:      return "Bildschirme";
        case Id::HelpGroupMap:          return "Karte";
        case Id::HelpGroupPazaak:              return "Pazaak";
        case Id::HelpGroupMod:          return "Mod-Funktionen";
        case Id::HelpGroupController:    return "Controller";

        case Id::HelpKeyUpDown:          return "Pfeil hoch und runter: durch Listen und Men\xFC""eintr\xE4ge bewegen";
        case Id::HelpKeyLeftRight:       return "Pfeil links und rechts: Kategorie wechseln oder Wert \xE4ndern";
        case Id::HelpKeyHomeEnd:         return "Pos1 und Ende: zum ersten oder letzten Eintrag springen";
        case Id::HelpKeyEnter:           return "Eingabetaste: den fokussierten Eintrag ausl\xF6sen";
        case Id::HelpKeyEsc:             return "Escape: Bildschirm schlie\xDF""en oder zur\xFC""ck";
        case Id::HelpKeyReadDescription: return "Umschalt und eine Pfeiltaste: ganze Beschreibung vorlesen, ohne zu wechseln";
        case Id::HelpKeySwitchWindows:   return "Q und E: Fenster oder Reiter wechseln, Bildschirme im Spielmen\xFC sowie Modi in L\xE4""den und Beh\xE4ltern";
        case Id::HelpKeyF1:              return "F1: diese Tastenliste \xF6""ffnen oder schlie\xDF""en";
        case Id::HelpKeyCtrlF1:          return "Strg und F1: die Tasten f\xFCr den aktuellen Bildschirm vorlesen";

        case Id::HelpKeyWalk:           return "W und S: vorw\xE4rts und r\xFC""ckw\xE4rts gehen";
        case Id::HelpKeyCameraRotate:   return "Y und C: Kamera nach links und rechts drehen";
        case Id::HelpKeyStrafe:         return "A und D: nach links und rechts treten";
        case Id::HelpKeyPause:          return "Leertaste: Spiel anhalten und fortsetzen";
        case Id::HelpKeyViewMode:       return "B: Umsehen-Modus, Position halten und die Kamera drehen";
        case Id::HelpKeySwitchLeader:   return "Tabulator: gesteuertes Gruppenmitglied wechseln";

        case Id::HelpKeyCycleTargets:   return "Q und E: aktuell sichtbare Objekte durchschalten";
        case Id::HelpKeyInteract:       return "Eingabetaste: mit dem anvisierten Ziel interagieren oder angreifen";
        case Id::HelpKeyOpenActionMenu: return "Umschalt und Eingabetaste: Aktionsmen\xFC f\xFCr das anvisierte Ziel \xF6""ffnen";
        case Id::HelpKeySelfStatus:     return "H: eigene Gesundheit, Effekte und Waffe ansagen";
        case Id::HelpKeyAnnounceFocus:  return "Minus: das fokussierte Objekt ansagen";
        case Id::HelpKeyWalkToFocus:    return "Umschalt und Minus: zum fokussierten Objekt gehen";
        case Id::HelpKeyBeacon:         return "Strg und Minus: Wegweiser zum fokussierten Objekt starten";
        case Id::HelpKeyDialogRepeat:   return "R: die aktuelle gesprochene Zeile wiederholen";

        case Id::HelpKeyCycleObjects:   return "Komma und Punkt: bereits entdeckte Objekte der aktuellen Kategorie durchschalten";
        case Id::HelpKeyCycleCategory:  return "Umschalt und Komma oder Punkt: vorherige oder n\xE4""chste Kategorie";
        case Id::HelpKeyCycleEnds:      return "Strg und Komma oder Punkt: zum n\xE4""chsten oder entferntesten Objekt springen";
        case Id::HelpKeyHeading:        return "Rechte Alt-Taste: genaue Blickrichtung in Grad ansagen";
        case Id::HelpKeyCameraOrient:   return "N: Kamera zur n\xE4""chsten Richtung drehen oder zum n\xE4""chsten Wegpunkt ausrichten";
        case Id::HelpKeyDropMarker:     return "Umschalt und N: eine Kartenmarkierung an deiner Position setzen";

        case Id::FmtHelpNumberActions:   return "1 bis 7: die zuletzt benutzte Aktion einer Kategorie einsetzen. 1 %s, 2 %s, 3 %s, 4 %s, 5 %s, 6 %s, 7 %s";
        case Id::HelpKeyOpenCategory:    return "Umschalt und 1 bis 7: die jeweilige Kategorie zum Ausw\xE4hlen \xF6""ffnen";
        case Id::FmtHelpNumberActions8:  return "8: das aktuelle %s einsetzen. Umschalt und 8 \xF6""ffnet die Kategorie zur Auswahl";
        case Id::HelpKeyActionQueue:     return "Umschalt und H: Aktionswarteschlange \xF6""ffnen";
        case Id::HelpKeyLevelUp:         return "Umschalt und L: Stufenaufstieg \xF6""ffnen";
        case Id::HelpKeyCancelCombat:    return "F: Kampf abbrechen";

        case Id::HelpKeyScreenMap:       return "M: Karte \xF6""ffnen";
        case Id::HelpKeyScreenMessages:  return "J: Nachrichten und R\xFC""ckmeldungen";
        case Id::HelpKeyScreenQuests:    return "L: Aufgaben";
        case Id::HelpKeyScreenAbilities: return "K: F\xE4higkeiten, Talente und Machtkr\xE4""fte";
        case Id::HelpKeyScreenCharacter: return "P: Charakterbogen";
        case Id::HelpKeyScreenInventory: return "I: Gruppeninventar";
        case Id::HelpKeyScreenEquip:     return "U: Charakter ausr\xFCsten";
        case Id::HelpKeyScreenOptions:   return "O: Optionen";

        case Id::HelpKeyMapCursor:       return "W, A, S und D: Kartencursor bewegen, um Gel\xE4nde und Markierungen zu lesen";
        case Id::HelpKeyMapPosition:     return "Rechte Alt-Taste: Position und Blickrichtung auf der Karte ansagen";
        case Id::HelpKeyPazaakNav:             return "Pfeiltasten: zwischen Hand, beiden Tischen und den Aktionen wechseln. Enter spielt die gew\xE4hlte Karte";
        case Id::HelpKeyPazaakSign:            return "Bei einer Plus-Minus-Karte: links oder rechts w\xE4hlt das Vorzeichen, Enter spielt sie, Escape bricht ab";
        case Id::HelpKeyPazaakReview:          return "C: deine Hand. T: beide Tische. Umschalt und C: wie viele Karten der Gegner noch hat";
        case Id::HelpKeyPazaakTurn:            return "S: bleiben. E: Zug beenden";


        case Id::HelpKeyModSettings:     return "Mod-Einstellungen findest du in den Optionen, ganz unten in der Liste";
        case Id::HelpKeyCopyLastSpoken:  return "Strg und R: den zuletzt gesprochenen Text in die Zwischenablage kopieren";
        case Id::HelpKeyPadMenuNav:          return "In Men\xFCs: Steuerkreuz oder linker Stick bewegen, A best\xE4tigt, B geht zur\xFC" "ck";
        case Id::HelpKeyPadTooltip:          return "Y halten und Steuerkreuz hoch oder runter: ganze Beschreibung vorlesen, ohne zu wechseln";
        case Id::HelpKeyPadInteract:          return "A: Standardaktion am fokussierten Ziel (angreifen, \xF6" "ffnen, sprechen, aufheben)";
        case Id::HelpKeyPadCycleObjects:     return "Steuerkreuz links und rechts: vorheriges und n\xE4" "chstes Objekt";
        case Id::HelpKeyPadCycleCategory:    return "Steuerkreuz hoch und runter: vorherige und n\xE4" "chste Kategorie";
        case Id::HelpKeyPadMapHints:         return "Kartenbildschirm, Steuerkreuz links und rechts: vorheriger und n\xE4" "chster Hinweis";
        case Id::HelpKeyPadActionMenuToggle: return "Linker Trigger: Aktionsmen\xFC \xF6" "ffnen; erneut dr\xFC" "cken schlie\xDFt es";
        case Id::HelpKeyPadActionMenuNav:    return "Aktionsmen\xFC, Steuerkreuz: links und rechts wechseln die Kategorie, hoch und runter den Eintrag, A l\xF6st aus, B oder linker Trigger schlie\xDFt";
        case Id::HelpKeyPadSelfStatus:       return "Linker Trigger mit X: dein eigener Status";
        case Id::HelpKeyPadActionQueue:      return "Linker Trigger mit B: die Aktionswarteschlange. Steuerkreuz hoch und runter geht durch die Eintr\xE4ge, A entfernt den zuletzt eingereihten, Y leert die ganze Warteschlange, B schlie\xDFt";
        case Id::HelpKeyPadWalkToFocus:      return "Rechter Trigger mit rechter Schultertaste: zum fokussierten Objekt laufen";
        case Id::HelpKeyPadBeacon:           return "Linker Trigger mit linker Schultertaste: Audio-Bake zum fokussierten Objekt";
        case Id::HelpKeyPadDegrees:          return "Rechter Trigger: Blickrichtung in Grad ansagen";
        case Id::HelpKeyPadCameraOrient:     return "Rechten Stick dr\xFC" "cken: Kamera zum n\xE4" "chsten Wegpunkt der Bake drehen, sonst zur n\xE4" "chsten Himmelsrichtung";
        case Id::HelpKeyPadHelp:             return "Beide Trigger: Tasten f\xFCr diesen Bildschirm. Der Eintrag Hilfe im Schnellmen\xFC \xF6" "ffnet diese Liste";
        case Id::HelpKeyPadQuickMenu:        return "Y: Schnellmen\xFC";
        case Id::HelpKeyPadCycleTargets:     return "Linke und rechte Schultertaste: Ziele durchschalten";
        case Id::HelpKeyPadTradeMode:        return "Beh\xE4lter und Laden, Schultertasten: Modus wechseln";
        case Id::HelpKeyPadOptions:          return "Zur\xFC" "ck-Taste: Optionsmen\xFC";
        case Id::HelpKeyPadPause:            return "Start: Pause";
        case Id::HelpKeyPadSwitchLeader:     return "X: Gruppenanf\xFChrer wechseln";

        // Controller rows added with the Pazaak board bindings, the D-Pad's
        // hold-to-jump gesture and the map's marker press.
        case Id::HelpKeyPadCycleEnds:
            return "Steuerkreuz links oder rechts gedr\xFC" "ckt halten: zum n\xE4" "chsten oder entferntesten Objekt springen";
        case Id::HelpKeyPadListEnds:
            return "Steuerkreuz hoch oder runter gedr\xFC" "ckt halten: zum ersten oder letzten Eintrag einer Liste springen";
        case Id::HelpKeyPadDropMarker:
            return "Kartenbildschirm, rechten Stick dr\xFC" "cken: eigene Markierung an der Cursorposition setzen";
        case Id::HelpKeyPadDialogRepeat:
            return "Im Dialog, beide Trigger: die aktuelle gesprochene Zeile wiederholen";
        case Id::HelpKeyPadPazaakNav:
            return "Pazaak-Brett, Steuerkreuz: hoch und runter wechseln zwischen deiner Hand, den beiden Brettseiten und den Aktionen; links und rechts bewegen darin. A spielt die Karte, B bricht die Vorzeichenwahl ab";
        case Id::HelpKeyPadPazaakSign:
            return "Pazaak-Brett, bei einer Plus-Minus-Karte: links und rechts w\xE4hlen das Vorzeichen, A spielt sie, B bricht ab";
        case Id::HelpKeyPadPazaakTurn:
            return "Pazaak-Brett, Y: Halten. X: Runde beenden";

        // Controller rows that close the last of the keyboard-only gaps: the
        // stick pair, the two routes into the game's screens, and the face-button
        // chord grid.
        case Id::HelpKeyPadMove:
            return "Linker Stick: laufen und seitw\xE4rts treten. Rechter Stick: Kamera drehen und damit auch deine Laufrichtung";
        case Id::HelpKeyPadTabs:
            return "Linke und rechte Schultertaste: Reiter wechseln und zwischen den Spielbildschirmen bl\xE4ttern";
        case Id::HelpKeyPadScreens:
            return "Y, Eintrag Men\xFCs: die Spielbildschirme \xF6" "ffnen. Mit den Schultertasten wechselst du dann zwischen Karte, Meldungen, Quests, F\xE4higkeiten, Charakter, Inventar und Ausr\xFCstung";
        case Id::HelpKeyPadMapCursor:
            return "Kartenbildschirm, linker Stick: den Kartencursor bewegen, um Gel\xE4nde und Markierungen zu lesen";
        case Id::HelpKeyPadAnnounceFocus:
            return "Linker Trigger mit A: das fokussierte Objekt ansagen";
        case Id::HelpKeyPadViewMode:
            return "Rechter Trigger mit A: Umsehen-Modus, Position halten, w\xE4hrend du die Kamera drehst";
        case Id::HelpKeyPadLevelUp:
            return "Rechter Trigger mit Y: den Levelaufstieg \xF6" "ffnen";
        case Id::HelpKeyPadCancelCombat:
            return "Linker Trigger mit Y: Kampf abbrechen";
        case Id::HelpKeyPadCopyLastSpoken:
            return "Rechter Trigger mit B: den zuletzt gesprochenen Text in die Zwischenablage kopieren";
        case Id::HelpKeyPadPazaakReview:
            return "Pazaak-Brett, linke Schultertaste: deine Hand. Rechte Schultertaste: beide Tische. Linken Stick dr\xFC" "cken: wie viele Karten der Gegner noch hat";

        case Id::PadQuickMenuOpened:
            return "Schnellmen\xFC. Hoch und runter zum Ausw\xE4hlen, "
                   "A best\xE4tigt, B schlie\xDFt.";
        case Id::PadQuickMenuClosed:         return "Schnellmen\xFC geschlossen.";
        case Id::PadQuickMenus:              return "Men\xFCs";
        case Id::PadQuickPartyLeader:        return "Gruppenanf\xFChrer";
        case Id::PadQuickSoloMode:           return "Einzelmodus";
        case Id::PadQuickStealth:            return "Stealth-Modus";
        case Id::PadQuickSave:               return "Schnellspeichern";
        case Id::PadQuickHelp:               return "Hilfe";

        case Id::HelpMenuOpened:    return "Tastenhilfe. Hoch und runter zum Lesen, Escape zum Schlie\xDF""en.";
        case Id::HelpMenuClosed:    return "Tastenhilfe geschlossen.";
        case Id::FmtHelpRowOf:      return "%s. %d von %d";
        case Id::FmtHelpGroupHeader: return "Abschnitt: %s";

        case Id::HelpContextNothing: return "Keine besonderen Tasten f\xFCr diesen Bildschirm.";
        case Id::FmtHelpContextLine: return "%s %s.";
        case Id::HelpContextWorld:       return "In der Welt.";
        case Id::HelpContextMenu:        return "Men\xFC.";
        case Id::HelpContextMap:         return "Karte.";
        case Id::HelpContextActionMenu:  return "Aktionsmen\xFC.";
        case Id::HelpContextDialog:      return "Gespr\xE4""ch.";
        case Id::HelpContextContainer:   return "Beh\xE4lter.";
        case Id::HelpContextStore:       return "Laden.";
        case Id::HelpContextPazaak:            return "Pazaak.";

        case Id::InputBlockedBigPicture:
            return "Das Spiel kann deine Tasteneingaben nicht empfangen, "
                   "weil der Steam-Big-Picture-Modus im Vordergrund ist.";

        case Id::InputKeyboardLost:
            return "Das Spiel hat die Tastatur verloren. Bitte starte das "
                   "Spiel neu.";

        // ---- Tastenbelegung (mod keybind configurator) ----
        case Id::KeybindsRootLabel:       return "Tastenbelegung";
        case Id::KeybindsOpened:          return "Tastenbelegung ge\xF6""ffnet";
        case Id::KeybindCatWorld:         return "Welt und Aktionen";
        case Id::KeybindCatExploration:   return "Erkundung und Kamera";
        case Id::KeybindCatMenus:         return "Men\xFCs und Eingabe";
        case Id::KeybindCatMinigames:     return "Minispiele";
        case Id::KeybindCatGeneral:       return "Allgemein";
        case Id::KeybindResetAll:         return "Standard wiederherstellen";
        case Id::KeybindResetDone:        return "Tastenbelegung zur\xFC""ckgesetzt";
        case Id::FmtKeybindCapturePrompt: return "Neue Taste f\xFCr %s dr\xFC""cken. Escape bricht ab.";
        case Id::FmtKeybindRebound:       return "%s neu belegt: %s";
        case Id::FmtKeybindConflictMod:   return "Bereits belegt mit %s. Andere Taste dr\xFC""cken.";
        case Id::KeybindConflictEngine:   return "Vom Spiel belegt. Andere Taste dr\xFC""cken.";
        case Id::KeybindCaptureCancelled: return "Abgebrochen";
        case Id::FmtKeymapModConflict:    return "Achtung: Der Mod nutzt diese Taste f\xFCr %s";
        // World & actions
        case Id::KbNameInteractTarget:      return "Interagieren";
        case Id::KbNameInteractForceRadial: return "Radialmen\xFC erzwingen";
        case Id::KbNameInteractForceRadialSecondary: return "Radialmen\xFC erzwingen (Sekund\xE4rtaste)";
        case Id::KbNameTargetKey1:          return "Zieltaste 1";
        case Id::KbNameTargetKey2:          return "Zieltaste 2";
        case Id::KbNameTargetKey3:          return "Zieltaste 3";
        case Id::KbNamePersonalKey1:        return "Eigene Aktion 1";
        case Id::KbNamePersonalKey2:        return "Eigene Aktion 2";
        case Id::KbNamePersonalKey3:        return "Eigene Aktion 3";
        case Id::KbNamePersonalKey4:        return "Eigene Aktion 4";
        case Id::KbNamePersonalKey5:        return "Eigene Aktion 5";
        case Id::KbNameActionBarOpen1:      return "Aktionsleiste 1 \xF6""ffnen";
        case Id::KbNameActionBarOpen2:      return "Aktionsleiste 2 \xF6""ffnen";
        case Id::KbNameActionBarOpen3:      return "Aktionsleiste 3 \xF6""ffnen";
        case Id::KbNameActionBarOpen4:      return "Aktionsleiste 4 \xF6""ffnen";
        case Id::KbNameActionBarOpen5:      return "Aktionsleiste 5 \xF6""ffnen";
        case Id::KbNameTargetActionOpen1:   return "Zielaktion 1 \xF6""ffnen";
        case Id::KbNameTargetActionOpen2:   return "Zielaktion 2 \xF6""ffnen";
        case Id::KbNameTargetActionOpen3:   return "Zielaktion 3 \xF6""ffnen";
        case Id::KbNameLevelUpOpen:         return "Stufenaufstieg";
        case Id::KbNameExamineOpen:         return "Untersuchen";
        case Id::KbNameCombatQueueOpen:     return "Aktionswarteschlange";
        case Id::KbNameSelfStatusAnnounce:  return "Eigener Status";
        // Exploration & camera
        case Id::KbNameCycleItemPrev:       return "Objekt zur\xFC""ck";
        case Id::KbNameCycleCategoryPrev:   return "Kategorie zur\xFC""ck";
        case Id::KbNameCycleItemNext:       return "Objekt vor";
        case Id::KbNameCycleCategoryNext:   return "Kategorie vor";
        case Id::KbNameCycleItemFirst:      return "Erstes Objekt";
        case Id::KbNameCycleItemLast:       return "Letztes Objekt";
        case Id::KbNameAnnounceFocus:       return "Fokus ansagen";
        case Id::KbNamePathfindFocus:       return "Zum Fokus gehen";
        case Id::KbNamePathfindFocusForce:  return "Zum Fokus gehen erzwingen";
        case Id::KbNameBeaconFocus:         return "Beacon zum Fokus";
        case Id::KbNameAnnounceDegrees:     return "Blickrichtung in Grad";
        case Id::KbNamePartyLeaderAnnounce: return "Gruppenanf\xFChrer ansagen";
        case Id::KbNameCameraOrient:        return "Kamera ausrichten";
        case Id::KbNameSaveMarkerAtCursor:  return "Marke setzen";
        case Id::KbNameViewModeToggle:      return "Ansichtsmodus";
        // Menus & input
        case Id::KbNameNavUp:               return "Men\xFC hoch";
        case Id::KbNameNavDown:             return "Men\xFC runter";
        case Id::KbNameNavLeft:             return "Men\xFC links";
        case Id::KbNameNavRight:            return "Men\xFC rechts";
        case Id::KbNameNavHome:             return "Zum Anfang";
        case Id::KbNameNavEnd:              return "Zum Ende";
        case Id::KbNameSubmenuEsc:          return "Men\xFC schlie\xDF""en";
        case Id::KbNameQueueClearAll:       return "Warteschlange leeren";
        case Id::KbNameContainerGiveMode:   return "Beh\xE4lter Geben-Modus";
        case Id::KbNameStoreModeToggle:     return "Laden Kaufen oder Verkaufen";
        case Id::KbNameCraftViewToggle:     return "Werkbank Erstellen oder Zerlegen";
        case Id::KbNameEditboxReReadUp:     return "Eingabefeld erneut lesen hoch";
        case Id::KbNameEditboxReReadDown:   return "Eingabefeld erneut lesen runter";
        case Id::KbNameEditboxSubmit:       return "Eingabe best\xE4tigen";
        case Id::KbNameEditboxCancel:       return "Eingabe abbrechen";
        // Minigames
        case Id::KbNamePazaakStand:         return "Pazaak: Stehen";
        case Id::KbNamePazaakEndTurn:       return "Pazaak: Zug beenden";
        case Id::KbNamePazaakReviewHand:    return "Pazaak: Handkarten";
        case Id::KbNamePazaakReviewTable:   return "Pazaak: Tisch";
        case Id::KbNamePazaakNextCard:      return "Pazaak: N\xE4""chste Karte";
        case Id::KbNamePazaakPrevCard:      return "Pazaak: Vorige Karte";
        case Id::KbNamePazaakPlay:          return "Pazaak: Karte spielen";
        case Id::KbNamePazaakOptLeft:       return "Pazaak: Option links";
        case Id::KbNamePazaakOptRight:      return "Pazaak: Option rechts";
        case Id::KbNamePazaakCancel:        return "Pazaak: Abbrechen";
        case Id::KbNamePazaakOppHand:       return "Pazaak: Gegnerhand";
        case Id::KbNameTurretCyclePrev:     return "Gesch\xFCtz: Ziel zur\xFC""ck";
        case Id::KbNameTurretCycleNext:     return "Gesch\xFCtz: Ziel vor";
        // General
        case Id::KbNameHelpMenuOpen:        return "Tastenhilfe";
        case Id::KbNameHelpContext:         return "Kontexthilfe";
        case Id::KbNameCheckForUpdate:      return "Nach Update suchen";
        case Id::KbNameDialogRepeatLine:    return "Dialogzeile wiederholen";
        case Id::KbNameCopyLastSpoken:      return "Zuletzt gesprochenen Text kopieren";

        // ---- Zwischenablage ----
        case Id::ClipboardCopied:        return "In die Zwischenablage kopiert";
        case Id::ClipboardNothingToCopy: return "Nichts zu kopieren";
        case Id::ClipboardFailed:        return "Zwischenablage nicht verf\xFCgbar";

        // ---- Endar Spire tutorial keyboard hints (Surface 1: popups) ----
        case Id::TutHintCombatFeat:
            return "Kampf-Talente findest du im Aktionsmen\xFC. \xD6""ffne die Angriffe mit Umschalt und 1 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutHintGrenade:
            return "Granaten wirfst du \xFC""ber das Aktionsmen\xFC. \xD6""ffne Sprengstoff mit Umschalt und 7 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutHintMine:
            return "Minen legst du \xFC""ber das Aktionsmen\xFC. \xD6""ffne Sprengstoff mit Umschalt und 7 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutHintFriendlyPower:
            return "Freundliche Macht-F\xE4higkeiten findest du im Aktionsmen\xFC. \xD6""ffne die eigenen Kr\xE4""fte mit Umschalt und 4 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutHintHostilePower:
            return "Offensive Macht-F\xE4higkeiten findest du im Aktionsmen\xFC. \xD6""ffne die Kr\xE4""fte gegen dein Ziel mit Umschalt und 2 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutHintActionMenuScroll:
            return "Im Aktionsmen\xFC wechselst du die Eintr\xE4ge mit Pfeil hoch und runter und die Kategorie mit Pfeil links und rechts.";
        case Id::TutHintOpenScreens:
            return "Spielmen\xFCs \xF6""ffnest du direkt: M Karte, J Nachrichten, L Auftr\xE4ge, K F\xE4higkeiten, P Charakter, I Inventar, U Ausr\xFCstung, O Optionen. Mit Escape zur\xFC""ck.";
        case Id::TutHintCycleTargets:
            return "Ziele w\xE4hlst du mit Q und E. Objekte schaltest du mit Komma und Punkt durch; die Minus-Taste sagt das fokussierte Objekt an. Im Kampf zielst du nur auf Feinde.";
        case Id::TutHintEquipSlot:
            return "Im Ausr\xFCstungsbildschirm w\xE4hlst du einen Steckplatz mit den Pfeiltasten und der Eingabetaste, dann w\xE4hlst du aus der Liste einen Gegenstand. Die Beschreibung eines Gegenstands liest du mit Umschalt und einer Pfeiltaste.";
        case Id::TutHintEnemyNear:
            return "Ein Feind ist in der N\xE4he. Schalte Ziele mit Q und E durch und greife mit der Eingabetaste an.";
        case Id::TutHintBash:
            return "Zum Aufbrechen anvisiere die T\xFCr oder den Beh\xE4lter, \xF6""ffne mit Umschalt und Eingabetaste das Aktionsmen\xFC und w\xE4hle Schlagen.";
        case Id::TutHintAttack:
            return "Anvisiere einen Feind mit Q und E und greife mit der Eingabetaste an. Ein einziger Druck gen\xFCgt; deine Figur greift dann jede Runde automatisch an, bis du ein anderes Ziel w\xE4hlst.";
        case Id::TutHintAttackAuto:
            return "Denke daran: Ein einziger Druck auf die Eingabetaste greift jede Runde automatisch an. Erst ein neues Ziel \xE4ndert das.";
        case Id::TutHintMovement:
            return "Dr\xFC""cke jederzeit F1 f\xFCr die Tastenhilfe, eine Liste aller Tasten, und Strg und F1 f\xFCr die Tasten des aktuellen Bildschirms. Zum Bewegen dr\xFC""ckst du W und S f\xFCr vorw\xE4rts und r\xFC""ckw\xE4rts, mit A und D trittst du zur Seite nach links und rechts, und mit Y und C drehst du dich nach links und rechts. Y und C drehen die Kamera und damit zugleich deine Laufrichtung, die immer in dieselbe Richtung zeigen. T\xF6ne warnen dich, wenn du dich einer Wand n\xE4herst, und w\xE4hrend du gehst, wird die Form des Raums um dich herum beschrieben. Mit der rechten Alt-Taste wiederholst du deine Blickrichtung und den aktuellen Raum.";
        case Id::TutHintMapScreen:
            return "Auf der Karte sind alle erforschten Gebiete eines Levels eingezeichnet. Wichtige Bereiche werden markiert. Einige Gebiete, zum Beispiel St\xE4""dte, sind bereits kartografiert, bevor du sie erforschst. Mit W, A, S und D bewegst du dich auf der Karte. Mit Komma und Punkt wechselst du zwischen den Kartenhinweisen. Mit Umschalt und N setzt du an deiner Position einen eigenen Kartenhinweis. Mit Strg und Minus startest du einen Wegweiser, der dich per Ton zum Kartenhinweis f\xFChrt, mit Umschalt und Minus gehst du automatisch zum ausgew\xE4hlten Kartenhinweis.";
        case Id::TutHintJournal:
            return "Im Auftragsbildschirm findest du Informationen \xFC""ber deine Fortschritte und Tipps zur weiteren Vorgehensweise. \xD6""ffne ihn mit L. W\xE4hle einen Eintrag mit den Pfeiltasten und lies seine Einzelheiten mit Umschalt und einer Pfeiltaste.";
        case Id::TutHintInventory:
            return "Das Inventar listet die Gegenst\xE4nde deiner Gruppe, auf die alle Mitglieder Zugriff haben. \xD6""ffne es mit I. Bl\xE4ttere mit den Pfeiltasten durch die Gegenst\xE4nde und lies die Beschreibung eines Gegenstands mit Umschalt und einer Pfeiltaste.";
        case Id::TutHintMessages:
            return "Im Nachrichtenbildschirm siehst du Werte wie Erfahrungspunkte, Angriffs- und Schadenswerte sowie \xC4nderungen an deinem Charakter. \xD6""ffne ihn mit J.";
        case Id::TutHintPartyDies:
            return "Ein Mitglied deiner Gruppe ist gefallen. Im Kampf neutralisierte Mitglieder werden nach dem Kampf wiederbelebt; sterben jedoch alle Mitglieder, ist das Spiel vorbei. Mit dem Tabulator steuerst du ein anderes Mitglied.";

        // ---- Endar Spire tutorial keyboard hints (Surface 2: Trask / pop) ----
        case Id::TutTraskGettingStarted:
            return "Du hast jetzt die Kontrolle. Die Grundlagen: Dr\xFC""cke jederzeit F1 f\xFCr die vollst\xE4ndige Tastenliste oder Strg und F1 f\xFCr die Tasten des aktuellen Bildschirms. W und S gehen vorw\xE4rts und r\xFC""ckw\xE4rts, mit A und D trittst du zur Seite nach links und rechts, und mit Y und C drehst du dich nach links und rechts. Y und C drehen die Kamera und damit deine Laufrichtung, die immer gleich ausgerichtet sind. Mit Q und E wechselst du zwischen Zielen und Objekten, und mit der Eingabetaste interagierst du mit dem fokussierten Objekt.";
        case Id::TutTraskEquipOpen:
            return "Im Ausr\xFCstungsbildschirm w\xE4hlst du R\xFCstung und Waffen. \xD6""ffne ihn mit U.";
        case Id::TutTraskEquipBrowse:
            return "Bl\xE4ttere mit den Pfeiltasten durch die Steckpl\xE4tze; jedem Steckplatz kannst du einen Gegenstand zuweisen. Die Beschreibung liest du mit Umschalt und einer Pfeiltaste.";
        case Id::TutTraskEquipWeapon:
            return "W\xE4hle den Waffen-Steckplatz und best\xE4tige mit der Eingabetaste, dann w\xE4hle in der Liste eine Waffe.";
        case Id::TutTraskCamera:
        case Id::TutTraskFootlocker:
            return "Die Kamera und damit deine Laufrichtung ver\xE4nderst du mit Y und C. N dreht dich um 90 Grad. Q und E schalten durch alle sichtbaren Objekte. Komma und Punkt navigieren eine Liste aller Objekte, die du schon einmal gesehen hast. Umschalt und Komma oder Punkt wechselt zwischen den Objektkategorien. Mit der Eingabetaste gehst du zu einem ausgew\xE4hlten Objekt und aktivierst es.";
        case Id::TutTraskPickItem:
            return "W\xE4hle einen Steckplatz mit den Pfeiltasten und der Eingabetaste, bl\xE4ttere durch die Liste und best\xE4tige den Gegenstand mit der Eingabetaste.";
        case Id::TutTraskLeader:
            return "Du steuerst den Anf\xFChrer der Gruppe. Mit dem Tabulator wechselst du das gesteuerte Mitglied.";
        case Id::TutTraskMakeLeader:
            return "Wechsle mit dem Tabulator zu mir als Anf\xFChrer und \xF6""ffne dann die T\xFCr.";
        case Id::TutTraskMenus:
            return "Spielmen\xFCs \xF6""ffnest du direkt mit Tasten: L Auftr\xE4ge, M Karte und weitere.";
        case Id::TutTraskTabs:
            return "Zwischen Reitern wechselst du mit Q und E.";
        case Id::TutTraskActionMenu:
            return "Das Aktionsmen\xFC \xF6""ffnest du mit Umschalt und Eingabetaste f\xFCr dein Ziel. Eine Zahl von 1 bis 7 setzt die zuletzt benutzte Aktion der jeweiligen Kategorie ein, mit Umschalt und einer Zahl von 1 bis 7 \xF6""ffnest du die Kategorie zum Ausw\xE4hlen.";
        case Id::TutTraskMedkit:
            return "Medikits benutzt du \xFC""ber das Aktionsmen\xFC. \xD6""ffne Medizin mit Umschalt und 5 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutTraskOpenDoor:
            return "Anvisiere die T\xFCr mit Komma und Punkt und \xF6""ffne sie mit der Eingabetaste.";
        case Id::TutTraskActivateEntry:
            return "Im Aktionsmen\xFC w\xE4hlst du einen Eintrag mit Pfeil hoch und runter und l\xF6st ihn mit der Eingabetaste aus.";
        case Id::TutTraskTargetMenu:
            return "Anvisiere ein Ziel mit Q und E oder Objekte mit Komma und Punkt. Mit der Eingabetaste f\xFChrst du die Standardaktion aus, mit Umschalt und Eingabetaste \xF6""ffnest du das Aktionsmen\xFC.";
        case Id::TutTraskConfirmEntry:
            return "Markiere den Eintrag mit Pfeil hoch und runter und best\xE4tige mit der Eingabetaste.";
        case Id::TutTraskPaused:
            return "Auch in der Pause w\xE4hlst du Ziele mit Q und E und wechselst mit dem Tabulator das gesteuerte Mitglied.";
        case Id::TutTraskWalkTo:
            return "Anvisiere die Feldkiste mit Q und E und \xF6""ffne sie mit der Eingabetaste.";
        case Id::TutTraskSecurity:
            return "Anvisiere die T\xFCr, \xF6""ffne mit Umschalt und Eingabetaste das Aktionsmen\xFC und w\xE4hle Sicherheit.";
        case Id::TutTraskHealWounded:
            return "Wechsle mit dem Tabulator zur verletzten Figur, \xF6""ffne Medizin mit Umschalt und 5 und w\xE4hle das Medikit.";
        case Id::TutLevelUp:
            return "Zum Stufenaufstieg dr\xFC""cke Umschalt und L und folge den Anweisungen.";
        case Id::TutStealthMode:
            return "Dr\xFC""cke G, um den Tarnmodus ein- oder auszuschalten. Daf\xFC""r musst du einen Tarnfeldgenerator tragen.";
        case Id::DialogRepeatLineHint:
            return "Dr\xFC""cke R, um die letzte Dialogzeile zu wiederholen.";

        // ---- KOTOR 2 tutorial hints (tutorial_hints.cpp K2 tables). The
        //      shared rows 0-42 reuse the TutHint* wordings above; these
        //      cover KOTOR 2's own popups and the Ebon Hawk prologue lines.
        case Id::TutK2SwoopRace:
            return "Schalte mit der Leertaste hoch, wenn das Audiosignal ert\xF6nt. Mit A und D lenkst du, weichst Hindernissen aus und triffst die Beschleunigerfelder.";
        case Id::TutK2ActionQueue:
            return "Mit der Eingabetaste f\xFC""gst du die Standardaktion in die Befehlsliste ein; auch Eintr\xE4ge aus dem Aktionsmen\xFC reihen sich dort ein. Mehrere Aktionen werden der Reihe nach ausgef\xFChrt.";
        case Id::TutK2ActionRemove:
            return "Aktionen lassen sich nur w\xE4hrend eines Kampfes aus der Befehlsliste l\xF6schen; die zuletzt hinzugef\xFC""gte Aktion wird zuerst entfernt.";
        case Id::TutK2PlasmaTorch:
            return "Besch\xE4""digte T\xFCren schl\xE4gst du am besten mit einem Werkzeug wie dem Plasmabrenner ein. \xD6""ffne mit U den Ausr\xFCstungsbildschirm und r\xFCste ihn aus. Anvisiere dann die T\xFCr, \xF6""ffne mit Umschalt und Eingabetaste das Aktionsmen\xFC und w\xE4hle Schlagen.";
        case Id::TutK2FirstPerson:
            return "Mit der Feststelltaste wechselst du in die Ich-Perspektive und zur\xFC""ck. F\xFCr das Spielen mit Bildschirmleser brauchst du sie nicht.";
        case Id::TutK2SpaceSuit:
            return "Anvisiere den Raumanzug mit Komma und Punkt und dr\xFC""cke die Eingabetaste, um ihn an- oder abzulegen.";
        case Id::TutK2CommRoom:
            return "W\xE4hle eine Konsole mit Komma und Punkt und aktiviere sie mit der Eingabetaste. F\xFCr die Computer brauchst du die Programmiersonde aus dem Beh\xE4lter im Cockpit.";
        case Id::TutK2Mines:
            return "Minen und Fallen entdeckst du mit deiner Bewusstseinsf\xE4higkeit, wenn du stehen bleibst. Anvisiere eine entdeckte Mine mit Komma und Punkt, \xF6""ffne mit Umschalt und Eingabetaste das Aktionsmen\xFC und w\xE4hle Entsch\xE4rfen oder Bergen. Entsch\xE4rfe oder berge jetzt drei Splitter des Photonentorpedos im Umkreis der Rakete.";
        case Id::TutK2Vibrocutter:
            return "Der Vibroschneider ist eine Nahkampfwaffe; im Nahkampf ist er Blastern \xFC""berlegen. \xD6""ffne mit U den Ausr\xFCstungsbildschirm und lege ihn in einen Waffenplatz. Die sekund\xE4ren Waffenpl\xE4tze halten eine zweite Konfiguration bereit.";
        case Id::TutK2DialogueChoice:
            return "Antworten w\xE4hlst du mit Pfeil hoch und runter und best\xE4tigst mit der Eingabetaste. Mit R wiederholst du die letzte Dialogzeile.";
        case Id::TutK2Pause:
            return "Mit der Leertaste h\xE4ltst du das Spiel an und setzt es fort. In der Pause planst du deine Aktionen in Ruhe.";
        case Id::TutK2EquipWeapon:
            return "\xD6""ffne mit U den Ausr\xFCstungsbildschirm, w\xE4hle einen Waffenplatz mit den Pfeiltasten und der Eingabetaste und w\xE4hle die Waffe aus der Liste.";
        case Id::TutK2WeaponConfig:
            return "Im Ausr\xFCstungsbildschirm gibt es zwei Waffen-Konfigurationen, aktiv und sekund\xE4r, etwa f\xFCr Nahkampf- und Fernwaffen. Belege die Pl\xE4tze mit den Pfeiltasten und der Eingabetaste.";
        case Id::TutK2TakeItem:
            return "Anvisiere den Beh\xE4lter mit Komma und Punkt und \xF6""ffne ihn mit der Eingabetaste. Nimm die Programmiersonde und greife damit auf das Computerterminal zu.";
        case Id::TutK2GalaxyMap:
            return "Nimm Kurs auf Peragus: W\xE4hle auf der Sternkarte mit Pfeil links und rechts Peragus aus; die Eingabetaste startet die Reise.";
        case Id::TutK2SoloMode:
            return "Hinter diese T\xFCr kommst du nur im Solo-Modus: Dr\xFC""cke V, dann bewegt sich nur das gesteuerte Mitglied. Bringe 3C-FD allein zum Sicherheitscomputer, damit er die T\xFCr \xF6""ffnet.";
        case Id::TutK2RepairKit:
            return "Mit Reparatur-Kits reparierst du Droiden. Benutze sie \xFC""ber das Aktionsmen\xFC: \xD6""ffne Medizin mit Umschalt und 5 und l\xF6se mit der Eingabetaste aus.";
        case Id::TutK2SecuritySpike:
            return "Dieses Schloss \xFC""bersteigt deine F\xE4higkeiten; benutze einen System\xFC""berbr\xFC""cker. Anvisiere die Kiste, \xF6""ffne mit Umschalt und Eingabetaste das Aktionsmen\xFC und w\xE4hle Sicherheit.";

        // ---- KOTOR 2 J/L swap: KOTOR 2's default keymap puts Quests on J
        //      and Messages on L, the mirror image of KOTOR 1 (its keymap.2da
        //      rows 13/14). Same texts as the TutHint* twins above with the two
        //      letters exchanged; the pad wordings name no letter keys, so
        //      kTwins points these at the existing *Pad ids.
        case Id::TutK2OpenScreens:
            return "Spielmen\xFCs \xF6""ffnest du direkt: M Karte, L Nachrichten, J Auftr\xE4ge, K F\xE4higkeiten, P Charakter, I Inventar, U Ausr\xFCstung, O Optionen. Mit Escape zur\xFC""ck.";
        case Id::TutK2Messages:
            return "Im Nachrichtenbildschirm siehst du Werte wie Erfahrungspunkte, Angriffs- und Schadenswerte sowie \xC4nderungen an deinem Charakter. \xD6""ffne ihn mit L.";
        case Id::TutK2Journal:
            return "Im Auftragsbildschirm findest du Informationen \xFC""ber deine Fortschritte und Tipps zur weiteren Vorgehensweise. \xD6""ffne ihn mit J. W\xE4hle einen Eintrag mit den Pfeiltasten und lies seine Einzelheiten mit Umschalt und einer Pfeiltaste.";

        // ---- Controller wording for the tutorial hints above. Selected by
        //      binding_hints::Text() when a gamepad is present; see strings.h.

        case Id::TutHintCombatFeatPad:
            return "Kampftalente stehen im Aktionsmen\xFC. Ziehe den linken Trigger, um es zu \xF6" "ffnen, gehe mit dem Steuerkreuz links und rechts zu Angriffe und dr\xFC" "cke A.";
        case Id::TutHintGrenadePad:
            return "Granaten wirfst du \xFC" "ber das Aktionsmen\xFC. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu Sprengstoffe und dr\xFC" "cke A.";
        case Id::TutHintMinePad:
            return "Minen legst du \xFC" "ber das Aktionsmen\xFC. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu Sprengstoffe und dr\xFC" "cke A.";
        case Id::TutHintFriendlyPowerPad:
            return "Freundliche Machtkr\xE4" "fte stehen im Aktionsmen\xFC. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu den eigenen Kr\xE4" "ften und dr\xFC" "cke A.";
        case Id::TutHintHostilePowerPad:
            return "Feindliche Machtkr\xE4" "fte stehen im Aktionsmen\xFC. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu den Zielkr\xE4" "ften und dr\xFC" "cke A.";
        case Id::TutHintActionMenuScrollPad:
            return "Im Aktionsmen\xFC wechselst du mit dem Steuerkreuz hoch und runter zwischen den Eintr\xE4gen und mit links und rechts zwischen den Kategorien.";
        case Id::TutHintOpenScreensPad:
            return "Dr\xFC" "cke Y f\xFCr das Schnellmen\xFC und w\xE4hle Men\xFCs, um die Spielbildschirme zu \xF6" "ffnen. Mit der linken und rechten Schultertaste wechselst du dann zwischen Karte, Meldungen, Quests, F\xE4higkeiten, Charakter, Inventar und Ausr\xFCstung. B geht zur\xFC" "ck.";
        case Id::TutHintCycleTargetsPad:
            return "Ziele w\xE4hlst du mit der linken und rechten Schultertaste. Mit dem Steuerkreuz links und rechts gehst du durch die bereits entdeckten Objekte. Im Kampf kannst du nur Gegner anvisieren.";
        case Id::TutHintEquipSlotPad:
            return "Im Ausr\xFCstungsbildschirm w\xE4hlst du einen Platz mit dem Steuerkreuz und A und suchst dann einen Gegenstand aus der Liste. Halte Y und dr\xFC" "cke das Steuerkreuz hoch oder runter, um die Beschreibung zu lesen.";
        case Id::TutHintEnemyNearPad:
            return "Ein Gegner ist in der N\xE4he. Schalte Ziele mit der linken und rechten Schultertaste durch und greife mit A an.";
        case Id::TutHintBashPad:
            return "Zum Aufbrechen fokussierst du die T\xFCr oder den Beh\xE4lter, ziehst den linken Trigger f\xFCr das Aktionsmen\xFC und w\xE4hlst Aufbrechen.";
        case Id::TutHintAttackPad:
            return "Fokussiere einen Gegner mit der linken und rechten Schultertaste und greife mit A an. Ein Druck gen\xFCgt; dein Charakter greift dann jede Runde automatisch an, bis du ein anderes Ziel w\xE4hlst.";
        case Id::TutHintAttackAutoPad:
            return "Denk daran: ein Druck auf A greift jede Runde automatisch an. Nur ein neues Ziel \xE4ndert das.";
        case Id::TutHintMovementPad:
            return "Ziehe jederzeit beide Trigger zusammen f\xFCr die Tasten des aktuellen Bildschirms, und dr\xFC" "cke Y f\xFCr das Schnellmen\xFC, Eintrag Hilfe, f\xFCr die vollst\xE4ndige Liste. Zum Gehen dr\xFC" "ckst du den linken Stick in eine beliebige Richtung. Mit dem rechten Stick nach links oder rechts drehst du die Kamera und damit auch deine Laufrichtung, sodass beide immer gleich zeigen. T\xF6ne warnen dich, wenn du dich einer Wand n\xE4herst, und w\xE4hrend du dich bewegst, wird die Form des Raumes um dich beschrieben. Ziehe den rechten Trigger, um Blickrichtung und aktuellen Raum zu wiederholen.";
        case Id::TutHintMapScreenPad:
            return "Die Karte zeigt jeden erkundeten Bereich einer Ebene. Wichtige Bereiche sind markiert, und manche Gebiete, etwa St\xE4" "dte, sind schon vor der Erkundung eingezeichnet. Bewege dich mit dem linken Stick \xFC" "ber die Karte. Mit dem Steuerkreuz links und rechts wechselst du zwischen den Kartenhinweisen. Ziehe den linken Trigger zusammen mit der linken Schultertaste f\xFCr eine Bake, die dich per Ton zum Hinweis f\xFChrt, oder den rechten Trigger mit der rechten Schultertaste, um automatisch dorthin zu laufen.";
        case Id::TutHintJournalPad:
            return "Der Questbildschirm zeigt deinen Fortschritt und Hinweise, was als N\xE4" "chstes zu tun ist. \xD6" "ffne ihn \xFC" "ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und wechsle mit den Schultertasten dorthin. W\xE4hle einen Eintrag mit dem Steuerkreuz und lies die Details, indem du Y h\xE4ltst und das Steuerkreuz hoch oder runter dr\xFC" "ckst.";
        case Id::TutHintMessagesPad:
            return "Der Meldungsbildschirm zeigt Werte wie Erfahrungspunkte, Angriffs- und Schadensw\xFCrfe und Ver\xE4nderungen an deinem Charakter. \xD6" "ffne ihn \xFC" "ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und wechsle mit den Schultertasten dorthin.";
        case Id::TutHintPartyDiesPad:
            return "Ein Gruppenmitglied ist gefallen. Im Kampf niedergestreckte Mitglieder werden nach dem Kampf wiederbelebt, aber wenn alle sterben, ist das Spiel vorbei. Dr\xFC" "cke X, um ein anderes Mitglied zu steuern.";
        case Id::TutHintInventoryPad:
            return "Das Inventar listet die Gegenst\xE4nde deiner Gruppe, geteilt von allen Mitgliedern. \xD6" "ffne es \xFC" "ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und wechsle mit den Schultertasten dorthin. Gehe mit dem Steuerkreuz durch die Gegenst\xE4nde und lies eine Beschreibung, indem du Y h\xE4ltst und das Steuerkreuz hoch oder runter dr\xFC" "ckst.";
        case Id::TutTraskGettingStartedPad:
            return "Du hast jetzt die Kontrolle. Die Grundlagen: Ziehe beide Trigger zusammen f\xFCr die Tasten des aktuellen Bildschirms, oder dr\xFC" "cke Y f\xFCr das Schnellmen\xFC, Eintrag Hilfe, f\xFCr die vollst\xE4ndige Liste. Mit dem linken Stick l\xE4ufst du, mit dem rechten Stick nach links oder rechts drehst du die Kamera und damit deine Laufrichtung, sodass beide immer gleich zeigen. Die linke und rechte Schultertaste wechseln zwischen Zielen, das Steuerkreuz links und rechts geht durch Objekte, und A interagiert mit dem fokussierten Objekt.";
        case Id::TutTraskEquipOpenPad:
            return "Im Ausr\xFCstungsbildschirm w\xE4hlst du R\xFCstung und Waffen. \xD6" "ffne ihn \xFC" "ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und wechsle mit den Schultertasten dorthin.";
        case Id::TutTraskEquipBrowsePad:
            return "Gehe mit dem Steuerkreuz durch die Ausr\xFCstungspl\xE4tze; jeder Platz nimmt einen Gegenstand auf. Halte Y und dr\xFC" "cke das Steuerkreuz hoch oder runter, um die Beschreibung zu lesen.";
        case Id::TutTraskEquipWeaponPad:
            return "W\xE4hle den Waffenplatz und dr\xFC" "cke A, dann suche eine Waffe aus der Liste.";
        case Id::TutTraskCameraPad:
        case Id::TutTraskFootlockerPad:
            return "Die Kamera und damit deine Laufrichtung \xE4nderst du mit dem rechten Stick. Dr\xFC" "cke den rechten Stick, um dich zur n\xE4" "chsten Himmelsrichtung zu drehen. Die linke und rechte Schultertaste gehen durch alle sichtbaren Objekte. Das Steuerkreuz links und rechts f\xFChrt durch eine Liste aller schon gesehenen Objekte, hoch und runter wechselt die Objektkategorie. Dr\xFC" "cke A, um zu einem gew\xE4hlten Objekt zu laufen und es zu benutzen.";
        case Id::TutTraskPickItemPad:
            return "W\xE4hle einen Platz mit dem Steuerkreuz und A, gehe durch die Liste und best\xE4tige den Gegenstand mit A.";
        case Id::TutTraskLeaderPad:
            return "Du steuerst den Gruppenanf\xFChrer. Dr\xFC" "cke X, um das gesteuerte Mitglied zu wechseln.";
        case Id::TutTraskMakeLeaderPad:
            return "Dr\xFC" "cke X, um mich zum Anf\xFChrer zu machen, und \xF6" "ffne dann die T\xFCr.";
        case Id::TutTraskMenusPad:
            return "\xD6" "ffne die Spielbildschirme \xFC" "ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs. Mit den Schultertasten wechselst du dann zwischen ihnen.";
        case Id::TutTraskTabsPad:
            return "Wechsle zwischen den Reitern mit der linken und rechten Schultertaste.";
        case Id::TutTraskActionMenuPad:
            return "Ziehe den linken Trigger, um das Aktionsmen\xFC f\xFCr dein Ziel zu \xF6" "ffnen. Das Steuerkreuz links und rechts wechselt die Kategorie, hoch und runter den Eintrag, und A f\xFChrt den Eintrag aus, auf dem du stehst.";
        case Id::TutTraskMedkitPad:
            return "Medipacks benutzt du \xFC" "ber das Aktionsmen\xFC. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu Medizin und dr\xFC" "cke A.";
        case Id::TutTraskOpenDoorPad:
            return "Fokussiere die T\xFCr mit dem Steuerkreuz links und rechts und \xF6" "ffne sie mit A.";
        case Id::TutTraskActivateEntryPad:
            return "Im Aktionsmen\xFC w\xE4hlst du einen Eintrag mit dem Steuerkreuz hoch und runter und f\xFChrst ihn mit A aus.";
        case Id::TutTraskTargetMenuPad:
            return "Fokussiere ein Ziel mit der linken und rechten Schultertaste oder Objekte mit dem Steuerkreuz links und rechts. Dr\xFC" "cke A f\xFCr die Standardaktion oder ziehe den linken Trigger f\xFCr das Aktionsmen\xFC.";
        case Id::TutTraskConfirmEntryPad:
            return "Markiere den Eintrag mit dem Steuerkreuz hoch und runter und best\xE4tige mit A.";
        case Id::TutTraskPausedPad:
            return "Auch in der Pause w\xE4hlst du Ziele mit der linken und rechten Schultertaste und wechselst das gesteuerte Mitglied mit X.";
        case Id::TutTraskWalkToPad:
            return "Fokussiere die Fu\xDFtruhe mit der linken und rechten Schultertaste und \xF6" "ffne sie mit A.";
        case Id::TutTraskSecurityPad:
            return "Fokussiere die T\xFCr, ziehe den linken Trigger f\xFCr das Aktionsmen\xFC und w\xE4hle Sicherheit.";
        case Id::TutTraskHealWoundedPad:
            return "Wechsle mit X zum verwundeten Charakter, ziehe den linken Trigger und gehe zu Medizin, und w\xE4hle das Medipack.";
        case Id::TutLevelUpPad:
            return "F\xFCr den Levelaufstieg ziehst du den linken Trigger und dr\xFC" "ckst Y.";
        case Id::TutStealthModePad:
            return "Dr\xFC" "cke Y f\xFCr das Schnellmen\xFC und w\xE4hle Tarnung, um den Tarnmodus ein- oder auszuschalten. Dazu musst du einen Tarnfeldgenerator ausger\xFCstet haben.";
        case Id::DialogRepeatLineHintPad:
            return "Ziehe beide Trigger zusammen, um die letzte Dialogzeile zu wiederholen.";

        // ---- KOTOR 2 controller twins (deliberate omissions: see strings.h).
        case Id::TutK2SwoopRacePad:
            return "Dr\xFC""cke A, um zu beschleunigen und hochzuschalten, wenn das Audiosignal ert\xF6nt. Mit dem linken Stick weichst du Hindernissen aus und triffst die Beschleunigerfelder.";
        case Id::TutK2ActionQueuePad:
            return "Mit A f\xFC""gst du die Standardaktion in die Befehlsliste ein; auch Eintr\xE4ge aus dem Aktionsmen\xFC reihen sich dort ein. Mehrere Aktionen werden der Reihe nach ausgef\xFChrt.";
        case Id::TutK2PlasmaTorchPad:
            return "Besch\xE4""digte T\xFCren schl\xE4gst du mit einem Werkzeug wie dem Plasmabrenner ein. \xD6""ffne den Ausr\xFCstungsbildschirm \xFC""ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, wechsle mit den Schultertasten dorthin und r\xFCste ihn aus. Fokussiere dann die T\xFCr, ziehe den linken Trigger und w\xE4hle Aufbrechen.";
        case Id::TutK2SpaceSuitPad:
            return "Fokussiere den Raumanzug mit dem Steuerkreuz links und rechts und dr\xFC""cke A, um ihn an- oder abzulegen.";
        case Id::TutK2CommRoomPad:
            return "Fokussiere eine Konsole mit dem Steuerkreuz links und rechts und aktiviere sie mit A. F\xFCr die Computer brauchst du die Programmiersonde aus dem Beh\xE4lter im Cockpit.";
        case Id::TutK2MinesPad:
            return "Minen und Fallen entdeckst du mit deiner Bewusstseinsf\xE4higkeit, wenn du stehen bleibst. Fokussiere eine entdeckte Mine mit dem Steuerkreuz links und rechts, ziehe den linken Trigger f\xFCr das Aktionsmen\xFC und w\xE4hle Entsch\xE4rfen oder Bergen. Entsch\xE4rfe oder berge jetzt drei Splitter des Photonentorpedos im Umkreis der Rakete.";
        case Id::TutK2VibrocutterPad:
            return "Der Vibroschneider ist eine Nahkampfwaffe; im Nahkampf ist er Blastern \xFC""berlegen. \xD6""ffne den Ausr\xFCstungsbildschirm \xFC""ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und lege ihn in einen Waffenplatz. Die sekund\xE4ren Waffenpl\xE4tze halten eine zweite Konfiguration bereit.";
        case Id::TutK2DialogueChoicePad:
            return "Antworten w\xE4hlst du mit dem Steuerkreuz hoch und runter und best\xE4tigst mit A. Ziehe beide Trigger zusammen, um die letzte Dialogzeile zu wiederholen.";
        case Id::TutK2EquipWeaponPad:
            return "\xD6""ffne den Ausr\xFCstungsbildschirm \xFC""ber das Schnellmen\xFC auf Y, Eintrag Men\xFCs, und wechsle mit den Schultertasten dorthin. W\xE4hle einen Waffenplatz mit dem Steuerkreuz und A und suche die Waffe aus der Liste.";
        case Id::TutK2WeaponConfigPad:
            return "Im Ausr\xFCstungsbildschirm gibt es zwei Waffen-Konfigurationen, aktiv und sekund\xE4r, etwa f\xFCr Nahkampf- und Fernwaffen. Belege die Pl\xE4tze mit dem Steuerkreuz und A.";
        case Id::TutK2TakeItemPad:
            return "Fokussiere den Beh\xE4lter mit dem Steuerkreuz links und rechts und \xF6""ffne ihn mit A. Nimm die Programmiersonde und greife damit auf das Computerterminal zu.";
        case Id::TutK2GalaxyMapPad:
            return "Nimm Kurs auf Peragus: W\xE4hle auf der Sternkarte mit dem Steuerkreuz links und rechts Peragus aus; A startet die Reise.";
        case Id::TutK2SoloModePad:
            return "Hinter diese T\xFCr kommst du nur im Solo-Modus: Schalte ihn \xFC""ber das Schnellmen\xFC auf Y um, Eintrag Solo-Modus; dann bewegt sich nur das gesteuerte Mitglied. Bringe 3C-FD allein zum Sicherheitscomputer, damit er die T\xFCr \xF6""ffnet.";
        case Id::TutK2RepairKitPad:
            return "Mit Reparatur-Kits reparierst du Droiden. Ziehe den linken Trigger, gehe mit dem Steuerkreuz links und rechts zu Medizin und dr\xFC""cke A.";
        case Id::TutK2SecuritySpikePad:
            return "Dieses Schloss \xFC""bersteigt deine F\xE4higkeiten; benutze einen System\xFC""berbr\xFC""cker. Fokussiere die Kiste, ziehe den linken Trigger f\xFCr das Aktionsmen\xFC und w\xE4hle Sicherheit.";

        case Id::FloorPuzzleIntro:
            return "Bodenplatten-R\xE4tsel: neun Platten in drei mal drei Anordnung, "
                   "R\xFC""cksetzfeld auf der S\xFC""dseite.";  // R?tsel, R?cksetzfeld, S?dseite
        case Id::FmtFloorReadHint:
            return "Dr\xFC""cke %s, um den Plattenzustand anzusagen.";
        case Id::FloorPuzzleStoryHint:
            return "Du solltest einen Story-Hinweis gefunden haben, der dir hilft, das R\xE4tsel zu l\xF6sen.";
        case Id::FmtFloorSoloHint:
            return "Tipp: Solo-Modus aktivieren, Taste %s, sonst schalten Begleiter Platten mit um.";
        case Id::FloorPartyToggled:  return "Begleiter hat Platten umgeschaltet";
        case Id::FmtPlateName:       return "Platte %s";
        case Id::PlateCenterWord:    return "Mitte";
        case Id::PlateResetName:     return "R\xFC""cksetzfeld";        // R?cksetzfeld
        case Id::FmtPlateEntered:    return "%s betreten";
        case Id::FmtPlateLit:        return "%s leuchtet auf";
        case Id::FmtPlateDark:       return "%s erlischt";
        case Id::FmtPlateLitCount:   return "%d von 9 leuchten";
        case Id::PlatesAllDark:      return "Alle Platten dunkel";
        case Id::FloorPuzzleSolved:
            return "R\xE4tsel gel\xF6st. Die massive T\xFCr \xF6""ffnet sich.";  // R?tsel gel?st, T?r ?ffnet

        case Id::FmtModLoadedVersion:   return "Voice of the Old Republic geladen, Version %s";
        case Id::FmtChargenFeatUnnamed: return "Talent %u";
        case Id::ChargenBtnRecommended: return "Empfohlen";
        case Id::ChargenBtnAccept:      return "Akzeptieren";
        case Id::ChargenBtnBack:        return "Zur\xfc""ck";
        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_de
