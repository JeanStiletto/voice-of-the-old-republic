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

        case Id::FmtActionBarOpened:      return "Aktionsmen\xFC Spalte %d: %s, %d Optionen"; // Aktionsmenü
        case Id::FmtActionBarColumnEmpty: return "Spalte %d ist leer";
        case Id::ActionBarColumnEmpty:    return "Spalte ist leer";
        case Id::FmtActionBarFired:       return "%s eingesetzt";
        case Id::FmtFireAtPosition:       return "%s, Platz %d";
        case Id::FmtFireQueueFull:        return "%s, Warteschlange voll";
        case Id::ActionBarCancelled:      return "Abgebrochen";

        case Id::NoTooltipAvailable:   return "Keine Beschreibung verf\xFC" "gbar";  // verfügbar

        case Id::ContainerEmpty:       return "Leer";
        case Id::ContainerOneItem:     return "1 Gegenstand";
        case Id::FmtContainerItems:    return "%d Gegenst\xE4nde";              // Gegenstände
        case Id::FmtContainerItemAt:   return "%s, %d von %d";
        case Id::FmtItemStackSuffix:   return "%d St\xFC" "ck";                    // Stück (split: 'c' is a hex digit)

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

        case Id::DisabledSuffix:       return ", nicht verf\xFC""gbar";   // nicht verfügbar

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

        case Id::CombatBegins:               return "Kampf beginnt";
        case Id::CombatEnds:                 return "Kampf beendet";

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

        case Id::FmtDialogReplies:           return "%d Antworten verf\xFCgbar.";
        case Id::DialogReplyUnavailable:     return "nicht verf\xFCgbar";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d von %d";

        case Id::MessagesTitleCombatLog:     return "Kampfprotokoll.";
        case Id::MessagesTitleDialogLog:     return "Dialogverlauf.";

        case Id::MapPrevNote:                return "Vorheriger Hinweis";
        case Id::MapNextNote:                return "N\xE4""chster Hinweis";  // Nächster

        case Id::MapCursorUnexplored:        return "Nebel des Krieges";
        case Id::MapCursorWaypointPOI:       return "Punkt von Interesse";
        case Id::MapCursorOpenArea:          return "Offene Fl\xE4""che";   // Offene Fläche
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
        case Id::FmtMapCursorPlazaDirs:      return "Platz, %s";
        case Id::AxisNorthSouth:             return "Nord-S\xFC""d";   // Nord-Süd
        case Id::AxisEastWest:               return "Ost-West";

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
        case Id::FmtStoreNotEnoughCredits:  return "Nicht genug Credits, brauchst %d, hast %d";

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
            return "Leertaste beschleunigt. A und D zum Lenken.";
        case Id::SwoopRaceEnded:
            return "Swoop-Rennen beendet.";
        case Id::SwoopRaceObstacleNear:
            return "Hindernis in %d Metern";
        case Id::FmtSwoopRaceGear:
            return "Gang %d";

        // "Mod Einstellungen" / "Erweitertes Wechseln" / "Raumformen" /
        // "Wandger\xe4usche" — \xe4 = ä, \xfc = ü. Encoding matches the
        // Windows-1252 convention used throughout strings_de.cpp.
        case Id::ModSettingsRootLabel:        return "Mod-Einstellungen";
        case Id::ModSettingsOpened:           return "Mod-Einstellungen ge\xf6""ffnet";
        case Id::ModSettingsClosed:           return "Mod-Einstellungen geschlossen";
        case Id::ModSettingExtendedCycling:   return "Kartenweite Objektauswahl";
        case Id::ModSettingRoomShapes:        return "Raumform-Beschreibungen";
        case Id::ModSettingWallSounds:        return "Wandger\xe4usche";
        case Id::ModSettingHumanSubtitles:    return "Untertitel menschlicher Sprecher vorlesen";
        case Id::ModSettingStateOn:           return "an";
        case Id::ModSettingStateOff:          return "aus";
        case Id::FmtModSettingOption:         return "%s: %s";

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
        case Id::GlossaryEntrySwoopAccelTick:       return "Swoop-Gangbalken-Tick";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Swoop-Beschleunigerfeld";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Swoop-Hinderniswarnung";
        case Id::GlossaryEntrySwoopWallImpact:      return "Swoop-Wandaufprall";

        // "drücken" — the hex escape \xFC followed by literal 'c' would be
        // parsed as \xFCc (out of range). Split the literal across two
        // string tokens to keep the escape sequence terminated.
        case Id::FmtUpdateAvailable:    return "Update verf\xFCgbar, Version %s. Im Hauptmen\xFC F5 dr\xFC" "cken, um zu installieren.";
        case Id::UpdateDownloading:     return "Update wird heruntergeladen.";
        case Id::UpdateDownloaded:      return "Update heruntergeladen. Spiel wird beendet, um zu installieren.";
        case Id::UpdateFailed:          return "Download des Updates fehlgeschlagen.";
        case Id::FmtUpdateNotAvailable: return "Kein Update verf\xFCgbar. Aktuelle Version %s.";
        case Id::UpdateNotInMenu:       return "Updates k\xF6nnen nur aus dem Hauptmen\xFC installiert werden.";

        case Id::PanelTitleMainMenu:    return "Hauptmen\xFC";
        case Id::LoadingPleaseWait:     return "Spiel l\xE4""dt noch, bitte warten.";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_de
