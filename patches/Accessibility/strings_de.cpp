// German string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE4=ä, \xF6=ö,
// \xFC=ü, \xDF=ß, \xC4=Ä, \xD6=Ö, \xDC=Ü). Prism's ANSI overload converts
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
        case Id::CategoryMapHint:     return "Hinweis";

        case Id::EmptyDoors:          return "Keine T\xFCren in Reichweite";        // Türen
        case Id::EmptyNpcs:           return "Keine Personen in Reichweite";
        case Id::EmptyContainers:     return "Keine Beh\xE4lter in Reichweite";     // Behälter (sg=pl)
        case Id::EmptyItems:          return "Keine Gegenst\xE4nde in Reichweite";  // Gegenstände
        case Id::EmptyLandmarks:      return "Keine Orte in Reichweite";
        case Id::EmptyTransitions:    return "Keine \xDC" "berg\xE4nge in Reichweite"; // Übergänge
        case Id::EmptyMapHints:       return "Keine Hinweise auf dieser Karte";
        case Id::EmptyAll:            return "Keine Objekte in Reichweite";
        case Id::CycleNoTarget:       return "Kein Ziel";

        case Id::MapPinNoText:        return "Marke";
        case Id::MapPinShiftDashHint: return "Marke l\xE4sst sich nicht direkt ansteuern. Strg+Bindestrich f\xFCr Beacon."; // lässt, für
        case Id::MapPinAltDashUnsupported: return "Marke: Alt+Bindestrich nicht unterst\xFCtzt"; // unterstützt
        case Id::MapPinInteractHint:  return "Marke. Strg+Bindestrich f\xFCr Beacon."; // für

        case Id::FmtSavedMarkerAutoNumber:   return "Marke %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marke %d";
        case Id::FmtSavedMarkerPlaced:       return "Marke gespeichert: %s";
        case Id::SavedMarkerFailed:          return "Marke konnte nicht gespeichert werden";

        case Id::FmtAnnounceWithClock: return "%s, auf %d Uhr, %d Meter";
        case Id::FmtAnnounceNoClock:   return "%s, %d Meter";
        case Id::FmtCategoryItem:      return "%s. %s";

        case Id::FmtGuidingTo:         return "Gehe zu %s";
        case Id::FmtGuidingFailed:     return "Gehe zu %s fehlgeschlagen";
        case Id::GuidanceNoFocus:      return "Kein Ziel ausgew\xE4hlt";  // ausgewählt
        case Id::GuidingToPoint:       return "Gehe zum Punkt";

        case Id::MovementCancelled:    return "Bewegung abgebrochen";
        case Id::InteractWayBlocked:   return "Bewegung abgebrochen, Weg versperrt";
        case Id::FmtInteractWayBlockedTarget: return "Bewegung abgebrochen, Weg versperrt. %s, %d Meter, %s";

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
        case Id::RouteOneTransition:   return "Ein \xDC" "bergang";          // Übergang
        case Id::RouteNoTransition:    return "Kein \xDC" "bergang";         // Übergang
        case Id::FmtBeaconNextSegment: return "Weiter %d Meter %s";

        case Id::FmtInteractTalk:      return "Sprich mit %s";
        case Id::FmtInteractOpen:      return "\xD6" "ffne %s";                 // Öffne
        case Id::FmtInteractTake:      return "Hebe %s auf";
        case Id::FmtInteractFailed:    return "Interaktion mit %s fehlgeschlagen";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Aktionsmen\xFC, %s";          // Aktionsmenü
        case Id::FmtInteractNoActionsRedirect: return "Keine Aktionen verf\xFC" "gbar f\xFCr %s. Enter zum Aktivieren."; // verfügbar / für
        case Id::FmtInteractNoActions: return "Keine Aktionen verf\xFC" "gbar f\xFCr %s.";  // verfügbar / für

        case Id::FmtActionBarOpened:      return "Aktionsmen\xFC Spalte %d: %s, %d Optionen"; // Aktionsmenü
        case Id::FmtActionBarColumnEmpty: return "Spalte %d ist leer";
        case Id::ActionBarColumnEmpty:    return "Spalte ist leer";
        case Id::FmtActionBarFired:       return "%s eingesetzt";
        case Id::FmtFireAtPosition:       return "%s, Platz %d";
        case Id::FmtFireQueueFull:        return "%s, Warteschlange voll";
        case Id::ActionMenuClosed:        return "Aktionsmenü geschlossen.";

        case Id::MenuCatAttacks:       return "Angriffe";
        case Id::MenuCatForcePowers:   return "Machtkr\xE4""fte";       // Machtkräfte
        case Id::MenuCatItems:         return "Gegenst\xE4nde";          // Gegenstände
        case Id::MenuCatSelfPowers:    return "Eigene Machtkr\xE4""fte"; // Eigene Machtkräfte
        case Id::MenuCatMedical:       return "Medizin";
        case Id::MenuCatMisc:          return "Sonstiges";
        case Id::MenuCatExplosives:    return "Sprengstoffe";
        case Id::FmtMenuCatMulti:      return "%s: %s, %d Optionen";
        case Id::FmtMenuCatSingle:     return "%s: %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d Optionen";
        case Id::FmtMenuCategoryEmpty: return "%s: leer";

        case Id::NoTooltipAvailable:   return "Keine Beschreibung verf\xFC" "gbar";  // verfügbar

        case Id::ContainerEmpty:       return "Leer";
        case Id::ContainerOneItem:     return "1 Gegenstand";
        case Id::FmtContainerItems:    return "%d Gegenst\xE4nde";              // Gegenstände
        case Id::FmtContainerItemAt:   return "%s, %d von %d";
        case Id::ContainerEmptySuffix: return "leer";
        case Id::FmtItemStackSuffix:   return "%d St\xFC" "ck";                    // Stück (split: 'c' is a hex digit)
        case Id::FmtItemChargeSuffix:  return "%d Ladungen";

        case Id::EquipSlotHead:        return "Kopf";
        case Id::EquipSlotImplant:     return "Implantat";
        case Id::EquipSlotBody:        return "K\xF6rper";                       // Körper
        case Id::EquipSlotArmL:        return "Linker Arm";
        case Id::EquipSlotArmR:        return "Rechter Arm";
        case Id::EquipSlotWeapL:       return "Linke Waffe";
        case Id::EquipSlotWeapR:       return "Rechte Waffe";
        case Id::EquipSlotBelt:        return "G\xFCrtel";                       // Gürtel
        case Id::EquipSlotHands:       return "H\xE4nde";                        // Hände

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, leer";
        case Id::EquipUnequipped:      return "Ausr\xFCstung abgelegt";
        case Id::FmtEquipVitality:     return "Vitalit\xE4t %s";                  // Vitalität
        case Id::FmtEquipDefense:      return "Verteidigung %s";
        case Id::FmtEquipAttack:       return "Angriff %s";
        case Id::FmtEquipAttackDual:   return "Angriff links %s, rechts %s";
        case Id::FmtEquipDamage:       return "Schaden %s";
        case Id::FmtEquipDamageDual:   return "Schaden links %s, rechts %s";

        case Id::FmtTransitionArea:    return "Bereich: %s";
        case Id::FmtTransitionRoom:    return "Raum: %s";
        case Id::FmtTransitionRoomIndex: return "Raum %d";
        case Id::FmtTransitionLoading: return "Lade: %s";

        case Id::DoorOpen:             return "offen";
        case Id::DoorLocked:           return "verriegelt";
        case Id::DoorCosmetic:         return "kosmetisch";

        case Id::DirNorth:             return "Nord";
        case Id::DirNortheast:         return "Nord-Ost";
        case Id::DirEast:              return "Ost";
        case Id::DirSoutheast:         return "S\xFC" "d-Ost";                  // Süd-Ost
        case Id::DirSouth:             return "S\xFC" "d";                      // Süd
        case Id::DirSouthwest:         return "S\xFC" "d-West";                 // Süd-West
        case Id::DirWest:              return "West";
        case Id::DirNorthwest:         return "Nord-West";

        case Id::StuckFreeDirsPrefix:  return "Frei";
        case Id::StuckAllBlocked:      return "Alles blockiert";

        case Id::FmtCompassDegrees:    return "%d Grad";

        case Id::FmtMapStateOriented:    return "%s. Blick auf der Karte nach %d Grad, %s.";
        case Id::FmtMapStateUnknownRoom: return "Blick auf der Karte nach %d Grad, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Maussteuerung an";
        case Id::MouseLookOff:         return "Maussteuerung aus";

        case Id::ViewModeOn:           return "Umsehen-Modus an";
        case Id::ViewModeOff:          return "Umsehen-Modus aus";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d von %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d von %d";

        case Id::LevelUpOpen:          return "Stufenaufstieg";
        case Id::LevelUpFailed:        return "Stufenaufstieg fehlgeschlagen";
        case Id::LevelUpAlreadyOpen:   return "Stufenaufstieg bereits offen";
        case Id::LevelUpNotReady:      return "Noch nicht genug Erfahrung f\xFCr einen Stufenaufstieg"; // für

        case Id::PortraitLabel:        return "Portr\xE4t";              // Porträt
        case Id::PortraitArrowPrev:    return "Vorheriges Portr\xE4t";  // Porträt
        case Id::PortraitArrowNext:    return "N\xE4" "chstes Portr\xE4t"; // Nächstes Porträt
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "weiblich";
        case Id::PortraitGenderMale:   return "m\xE4nnlich";              // männlich
        case Id::PortraitRaceAsian:    return "asiatisch";
        case Id::PortraitRaceDark:     return "dunkelh\xE4utig";          // dunkelhäutig
        case Id::PortraitRaceLight:    return "hellh\xE4utig";            // hellhäutig
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtPartyPortraitInTeam:    return "%s, im Team";
        case Id::FmtPartyPortraitAvailable: return "%s, verf\xFC""gbar";    // verfügbar
        case Id::PartySelectionFull:        return "Gruppe voll";

        case Id::DisabledSuffix:       return ", nicht verf\xFC""gbar";   // nicht verfügbar
        case Id::FmtLevelUpDoStepFirst: return "Zuerst %s abschlie\xDF""en.";  // abschließen
        case Id::LevelUpStepLocked:    return "Noch nicht an der Reihe.";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Stufe %s. ";
        case Id::FmtCharSheetXp:       return "Erfahrung %s von %s. ";
        case Id::FmtCharSheetHp:       return "Lebenspunkte %s. ";
        case Id::FmtCharSheetFp:       return "Machtpunkte %s. ";
        case Id::FmtCharSheetStr:      return "St\xE4rke %s%s%s. ";          // Stärke
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

        case Id::ChargenFeatGrantedTitle:    return "Du erh\xE4ltst diese Talente";  // erhältst
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d von %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "verf\xFCgbar";              // verfügbar
        case Id::ChargenFeatStatusExisting:  return "bereits gelernt";
        case Id::ChargenFeatStatusGranted:   return "automatisch erhalten";
        case Id::ChargenFeatStatusLocked:    return "Voraussetzung fehlt";
        case Id::ChargenFeatStatusChosen:    return "ausgew\xE4hlt";              // ausgewählt

        case Id::EditboxRole:                return "Eingabefeld";
        case Id::EditboxEmpty:               return "leer";
        case Id::EditboxEnd:                 return "Ende";
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
        case Id::TargetIsDead:               return "tot";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d Meter.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " Nebenhand %s.";
        case Id::FmtBriefEffectsCount:       return " %d aktive Effekte.";
        case Id::FmtBriefFeatsCount:         return " %d Talente.";
        case Id::FmtSelfStatusHp:            return "%d Lebenspunkte.";
        case Id::FmtSelfStatusHpOf:          return "%d von %d Lebenspunkten.";
        case Id::FmtSelfStatusFpOf:          return "%d von %d Machtpunkten.";

        case Id::ExamineOpened:              return "Untersuchen.";
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
        case Id::ExamineRowStatusInvisible:  return "Unsichtbar";
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
        case Id::QueueVerbMove:              return "Bewegen";
        case Id::QueueVerbHeal:              return "Heilen";
        case Id::QueueVerbUseTalent:         return "Talent einsetzen";
        case Id::QueueVerbCutscene:          return "Zwischensequenz";
        case Id::QueueVerbUnknown:           return "Aktion";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "%s bei N von M"
        // tail. Args reduced to (attacker, target, damage).
        case Id::FmtAttackHit:               return "%s trifft %s f\xFCr %d Schaden.";
        case Id::FmtAttackMiss:              return "%s verfehlt %s.";
        case Id::FmtAttackCrit:              return "Kritischer Treffer! %s trifft %s f\xFCr %d Schaden.";
        case Id::FmtAttackDeflected:         return "%s wird von %s pariert.";

        case Id::FmtSavingThrowSucceeded:    return "%s besteht: %s %d gegen %d.";
        case Id::FmtSavingThrowFailed:       return "%s scheitert: %s %d gegen %d.";
        case Id::SaveTypeFort:               return "Z\xE4higkeit";
        case Id::SaveTypeReflex:             return "Reflex";
        case Id::SaveTypeWill:               return "Willen";

        case Id::DialogReplyUnavailable:     return "nicht verf\xFCgbar";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d von %d";

        case Id::MessagesTitleCombatLog:     return "Kampfprotokoll.";
        case Id::MessagesTitleDialogLog:     return "Dialogverlauf.";

        case Id::MapPrevNote:                return "Vorheriger Hinweis";
        case Id::MapNextNote:                return "N\xE4""chster Hinweis";  // Nächster

        case Id::MapCursorUnexplored:        return "Nebel des Krieges";
        case Id::MapCursorWaypointPOI:       return "Punkt von Interesse";
        case Id::MapCursorJunction:          return "Kreuzung";
        case Id::MapCursorOffPath:           return "Wand";
        case Id::FmtMapCursorCorridor:       return "%s, %.0f Meter";
        case Id::FmtMapCursorDeadEnd:        return "Sackgasse, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Kreuzung, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "T\xFC""r";            // Tür
        case Id::FmtMapCursorDoor:           return "%s %s";               // noun + dir
        case Id::FmtMapCursorDoorTransition: return "%s %s nach %s";       // noun + dir + dest
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";           // noun + dir + landmark
        case Id::MapCursorTransitionDoor:    return "T\xFC""rschwelle";  // Türschwelle
        case Id::FmtMapCursorJunctionDeadEndExit: return "Sackgasse %s";
        case Id::AxisNorthSouth:             return "Nord-S\xFC""d";   // Nord-Süd
        case Id::AxisEastWest:               return "Ost-West";
        case Id::AreaNoun:                   return "Bereich";
        case Id::AreaNounLarge:              return "Gro\xDF""er Bereich";  // Großer Bereich
        case Id::FmtAreaAxisExits:           return "%s %s. Ausg\xE4""nge: %s";
        case Id::FmtAreaExits:               return "%s. Ausg\xE4""nge: %s";
        case Id::FmtAreaAxisOnly:            return "%s %s";

        case Id::FmtStorePriceBuyFinite:    return "Preis %d Credits, Lager %d";
        case Id::FmtStorePriceBuyUnlimited: return "Preis %d Credits, Lager unbegrenzt";
        case Id::FmtStorePriceSell:         return "Preis %d Credits, du besitzt %d";
        case Id::StoreModeBuy:              return "Modus Kaufen";
        case Id::StoreModeSell:             return "Modus Verkaufen";
        case Id::StoreSold:                 return "Verkauft";
        case Id::StoreBought:               return "Gekauft";
        case Id::StoreCannotSell:           return "Kann nicht verkauft werden";
        case Id::StoreCannotBuy:            return "Kann nicht gekauft werden";
        case Id::FmtStoreSoldFor:           return "Verkauft f\xFC""r %d Credits";  // für
        case Id::FmtStoreBoughtFor:         return "Gekauft f\xFC""r %d Credits";   // für

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
        case Id::PazaakSelectCardFirst:  return "W\xE4""hle zuerst eine Karte.";
        case Id::PazaakChooseSign:       return "Vorzeichen w\xE4""hlen. Links oder rechts zum \xC4""ndern, Enter zum Spielen.";
        case Id::PazaakCancelled:        return "Abgebrochen.";
        case Id::PazaakDeckAvailable:    return "%s, %d verf\xFC""gbar";
        case Id::PazaakDeckNoneLeft:     return "%s, keine mehr";
        case Id::PazaakDeckSlotFilled:   return "Deck-Platz %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Deck-Platz %d: leer";
        case Id::PazaakDeckPlay:         return "Spielen, %d von 10 im Deck";
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
        // no inline text — their visual content is the installed mod's
        // icon + name set programmatically. Speak "Aufwertungssteckplatz N"
        // (upgrade slot N) for weapon slots and "Kristall-Steckplatz N"
        // for lightsaber crystal slots so the user can tell which slot
        // is focused. Concrete category words ("Vibrationszelle",
        // "Skopus", …) would require reading LBL_SLOTNAME dynamically
        // for the active slot only — deferred to a future enrichment.
        case Id::WorkbenchSlotWeapon1:       return "Aufwertungssteckplatz 1";
        case Id::WorkbenchSlotWeapon2:       return "Aufwertungssteckplatz 2";
        case Id::WorkbenchSlotWeapon3:       return "Aufwertungssteckplatz 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Kristall-Steckplatz 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Kristall-Steckplatz 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Kristall-Steckplatz 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Kristall-Steckplatz 4";
        case Id::WorkbenchItemsEmpty:        return "Keine aufwertbaren Gegenst\xE4nde in dieser Kategorie";  // Gegenstände
        case Id::WorkbenchUpgradesEmpty:     return "Keine kompatiblen Aufwertungen im Inventar";
        case Id::WorkbenchSlotInstalled:     return "Aufwertung eingesetzt";
        case Id::WorkbenchSlotRemoved:       return "Aufwertung entfernt";
        case Id::WorkbenchSlotNoMatch:       return "Keine passende Aufwertung im Inventar";
        case Id::WorkbenchSlotFilled:        return "belegt";
        case Id::WorkbenchSlotPeekEmpty:     return "Leerer Steckplatz, keine Aufwertung eingesetzt";
        case Id::WorkbenchFmtSlotItem:       return "%s, belegt mit %s";
        case Id::WorkbenchPickerInstalled:   return "eingesetzt";

        case Id::SoundOptionsMovieVolume:    return "Video-Lautst\xE4rke";  // Lautstärke

        case Id::SwoopRaceStarted:
            // Terse opener — concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Swoop-Rennen.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is documented in the manual
            // — verbose spoken intro got in the way of the start
            // countdown so the shorter form is preferred.
            return "Leertaste zum Hochschalten beim Audiosignal. Mit A und D lenken. Weiche den Hindernissen aus und triff die Beschleunigerfelder, um die Zeit zu schlagen.";
        case Id::SwoopRaceEnded:
            return "Swoop-Rennen beendet.";
        case Id::SwoopRaceObstacleNear:
            return "Hindernis in %d Metern";
        case Id::FmtSwoopRaceGear:
            return "Gang %d";
        case Id::FmtSwoopRaceTime:
            return "Zeit: %d,%02d Sekunden. Swoop-Rennen beendet.";

        case Id::TurretGameStarted:
            // Terse opener — concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Gesch\xFCtzturm.";  // Geschützturm
        case Id::TurretGameControls:
            return "Mit WASD zielen. Leertaste zum Feuern. Q und E w\xE4hlen Ziele.";
        case Id::TurretGameEnded:
            return "Gesch\xFCtzturm beendet.";
        case Id::FmtTurretTarget:
            return "J\xE4ger %d, %d Meter";  // Jäger
        case Id::FmtTurretDestroyed:
            return "J\xE4ger %d zerst\xF6rt.";  // Jäger %d zerstört.
        case Id::TurretNoTargets:
            return "Keine Ziele.";
        case Id::TurretTargetLost:
            return "Ziel verloren.";

        // "Mod Einstellungen" / "Erweitertes Wechseln" / "Raumformen" /
        // "Wandger\xe4usche" — \xe4 = ä, \xfc = ü. Encoding matches the
        // Windows-1252 convention used throughout strings_de.cpp.
        case Id::ModSettingsRootLabel:        return "Mod-Einstellungen";
        case Id::ModSettingsOpened:           return "Mod-Einstellungen ge\xf6""ffnet";
        case Id::ModSettingsClosed:           return "Mod-Einstellungen geschlossen";
        case Id::ModSettingExtendedCycling:   return "Kartenweite Objektauswahl";
        case Id::ModSettingRoomShapes:        return "Raumform-Beschreibungen";
        case Id::ModSettingWallSounds:        return "Wandger\xe4usche";
        case Id::ModSettingHumanSubtitles:    return "Untertitel vertonter Sprecher vorlesen";
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
        case Id::ModSettingUrgentVolumePreview: return "Beispielansage";

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

        // "drücken" — the hex escape \xFC followed by literal 'c' would be
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
        case Id::HelpGroupMod:          return "Mod-Funktionen";

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
        case Id::HelpKeyCameraRotate:   return "A und D: Kamera nach links und rechts drehen";
        case Id::HelpKeyStrafe:         return "Z und C: nach links und rechts treten";
        case Id::HelpKeyPause:          return "Leertaste: Spiel anhalten und fortsetzen";
        case Id::HelpKeyViewMode:       return "B: Umsehen-Modus, Position halten und die Kamera drehen";
        case Id::HelpKeySwitchLeader:   return "Tabulator: gesteuertes Gruppenmitglied wechseln";

        case Id::HelpKeyCycleTargets:   return "Q und E: nahe Ziele durchschalten";
        case Id::HelpKeyInteract:       return "Eingabetaste: mit dem anvisierten Ziel interagieren oder angreifen";
        case Id::HelpKeyOpenActionMenu: return "Umschalt und Eingabetaste: Aktionsmen\xFC f\xFCr das anvisierte Ziel \xF6""ffnen";
        case Id::HelpKeySelfStatus:     return "H: eigene Gesundheit, Effekte und Waffe ansagen";
        case Id::HelpKeyAnnounceFocus:  return "Minus: das fokussierte Objekt ansagen";
        case Id::HelpKeyWalkToFocus:    return "Umschalt und Minus: zum fokussierten Objekt gehen";
        case Id::HelpKeyBeacon:         return "Strg und Minus: Wegweiser zum fokussierten Objekt starten";
        case Id::HelpKeyDialogRepeat:   return "R: die aktuelle gesprochene Zeile wiederholen";

        case Id::HelpKeyCycleObjects:   return "Komma und Punkt: Objekte der aktuellen Kategorie durchschalten";
        case Id::HelpKeyCycleCategory:  return "Umschalt und Komma oder Punkt: vorherige oder n\xE4""chste Kategorie";
        case Id::HelpKeyCycleEnds:      return "Strg und Komma oder Punkt: zum n\xE4""chsten oder entferntesten Objekt springen";
        case Id::HelpKeyHeading:        return "Rechte Alt-Taste: genaue Blickrichtung in Grad ansagen";
        case Id::HelpKeyCameraOrient:   return "N: Kamera zur n\xE4""chsten Richtung drehen oder zum n\xE4""chsten Wegpunkt ausrichten";
        case Id::HelpKeyDropMarker:     return "Umschalt und N: eine Kartenmarkierung an deiner Position setzen";

        case Id::FmtHelpNumberActions:   return "1 bis 7: die zuletzt benutzte Aktion einer Kategorie einsetzen. 1 %s, 2 %s, 3 %s, 4 %s, 5 %s, 6 %s, 7 %s";
        case Id::HelpKeyOpenCategory:    return "Umschalt und 1 bis 7: die jeweilige Kategorie zum Ausw\xE4hlen \xF6""ffnen";
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

        case Id::HelpKeyMapCursor:       return "Pfeiltasten: Kartencursor bewegen, um Gel\xE4nde und Markierungen zu lesen";
        case Id::HelpKeyMapPosition:     return "Rechte Alt-Taste: Position und Blickrichtung auf der Karte ansagen";


        case Id::HelpKeyModSettings:     return "Mod-Einstellungen findest du in den Optionen, ganz unten in der Liste";

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

        case Id::InputBlockedBigPicture:
            return "Das Spiel kann deine Tasteneingaben nicht empfangen, "
                   "weil der Steam-Big-Picture-Modus im Vordergrund ist.";

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
        case Id::KbNameTargetKey1:          return "Zieltaste 1";
        case Id::KbNameTargetKey2:          return "Zieltaste 2";
        case Id::KbNameTargetKey3:          return "Zieltaste 3";
        case Id::KbNamePersonalKey1:        return "Eigene Aktion 1";
        case Id::KbNamePersonalKey2:        return "Eigene Aktion 2";
        case Id::KbNamePersonalKey3:        return "Eigene Aktion 3";
        case Id::KbNamePersonalKey4:        return "Eigene Aktion 4";
        case Id::KbNameActionBarOpen1:      return "Aktionsleiste 1 \xF6""ffnen";
        case Id::KbNameActionBarOpen2:      return "Aktionsleiste 2 \xF6""ffnen";
        case Id::KbNameActionBarOpen3:      return "Aktionsleiste 3 \xF6""ffnen";
        case Id::KbNameActionBarOpen4:      return "Aktionsleiste 4 \xF6""ffnen";
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

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_de
