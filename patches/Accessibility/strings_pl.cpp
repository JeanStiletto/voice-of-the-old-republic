// Polish string table.
//
// Machine-translated draft, same quality bar as strings_fr/it/es/ru.cpp and
// flagged for a native-speaker pass. Contributors: correcting wording here is
// safe and self-contained -- only the text inside the quotes should change.
//
// Encoding: Windows-1250 hex escapes for the Polish letters. NOT UTF-8 and NOT
// Windows-1252 -- a and c and e and l and n and s and z with their diacritics
// do not exist in 1252 at all, so this table cannot be written the way the
// German or French ones are. acc::strings::CodepageFor(Lang::Pl) returns 1250
// and core_dllmain pins it through prism::SetSpeechCodepage before the first
// utterance, which is what makes a Polish install speak correctly on an
// English or German Windows.
//
// Escapes were generated from UTF-8 source by tools/scratch escape-cp1250.ps1,
// not typed by hand. Where a hex escape is followed by a literal hex digit the
// string is split with "" so the escape cannot swallow it -- the same trick
// strings_es.cpp and strings_ru.cpp use.
//
// Format placeholders (%s / %d / %u) keep the SAME ORDER as the English table
// -- callers pass arguments positionally and do not reorder per language.
// Where Polish word order wanted a different argument order, the wording was
// adjusted instead of the placeholders.
//
// Combat speech is fed by combat_strings.cpp::kPl (engine anchors extracted
// from the LEM dialog.tlk); this table covers the Id::* speech path.

#include "strings.h"

namespace acc::strings::lang_pl {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "Drzwi";
        case Id::CategoryNpc:         return "Posta\xE6";
        case Id::CategoryContainer:   return "Pojemnik";
        case Id::CategoryItem:        return "Przedmiot";
        case Id::CategoryLandmark:    return "Punkt orientacyjny";
        case Id::CategoryTransition:  return "Przej\x9C""cie";
        case Id::CategoryMapHint:     return "Znacznik mapy";

        case Id::EmptyDoors:          return "Brak drzwi w zasi\xEAgu";
        case Id::EmptyNpcs:           return "Brak postaci w zasi\xEAgu";
        case Id::EmptyContainers:     return "Brak pojemnik\xF3w w zasi\xEAgu";
        case Id::EmptyItems:          return "Brak przedmiot\xF3w w zasi\xEAgu";
        case Id::EmptyLandmarks:      return "Brak punkt\xF3w orientacyjnych w zasi\xEAgu";
        case Id::EmptyTransitions:    return "Brak przej\x9C\xE6 w zasi\xEAgu";
        case Id::EmptyMapHints:       return "Brak znacznik\xF3w na tej mapie";
        case Id::EmptyAll:            return "Brak obiekt\xF3w w zasi\xEAgu";
        case Id::CycleNoTarget:       return "Brak celu";

        case Id::MapPinNoText:        return "Znacznik";
        case Id::MapPinAltDashUnsupported: return "Znacznik: Alt z my\x9Clnikiem nieobs\xB3ugiwany";
        case Id::MapPinInteractHint:  return "Znacznik. Naci\x9Cnij Ctrl z my\x9Clnikiem, aby w\xB3\xB9""czy\xE6 sygna\xB3.";

        case Id::FmtSavedMarkerAutoNumber:   return "Znacznik %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - znacznik %d";
        case Id::FmtSavedMarkerPlaced:       return "Zapisano znacznik: %s";
        case Id::SavedMarkerFailed:          return "Nie uda\xB3o si\xEA zapisa\xE6 znacznika";

        case Id::HintBackEntrance:    return "Tylne wej\x9C""cie";
        case Id::HintRebelCorpse:     return "Martwy rebeliant";
        case Id::HintBanthaHerd:      return "Stado bant";
        case Id::MapNoteFrontExit:    return "Przednie wyj\x9C""cie";
        case Id::MapNoteBackExit:     return "Tylne wyj\x9C""cie";

        case Id::FmtAnnounceWithClock: return "%s, na godzinie %d, %d metr\xF3w";
        case Id::FmtAnnounceNoClock:   return "%s, %d metr\xF3w";
        case Id::FmtCategoryItem:      return "%s. %s";
        case Id::FmtTrapDetected:      return "Wykryto pu\xB3""apk\xEA: %s";
        case Id::MineNoun:             return "Mina";

        case Id::FmtGuidingTo:         return "Prowadz\xEA do %s";
        case Id::FmtGuidingFailed:     return "Prowadzenie do %s nie powiod\xB3o si\xEA";
        case Id::GuidanceNoFocus:      return "Nie wybrano obiektu";
        case Id::GuidingToPoint:       return "Id\xEA do punktu";

        case Id::MovementCancelled:    return "Ruch przerwany";
        case Id::InteractWayBlocked:   return "Ruch przerwany, droga zablokowana";
        case Id::FmtInteractWayBlockedTarget: return "Ruch przerwany, droga zablokowana. %s, %d metr\xF3w, %s";
        case Id::SpectatorBattleDoomed: return "\xAFo\xB3nierze Republiki s\xB9 straceni. Nie mo\xBF""esz im pom\xF3""c. Droga jest zablokowana. Spiesz si\xEA na mostek!";

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Sygna\xB3 do %s";
        case Id::BeaconCancelled:      return "Sygna\xB3 wy\xB3\xB9""czony";
        case Id::FmtBeaconNoPath:      return "Brak drogi do %s";
        case Id::BeaconAlreadyAtDest:  return "Ju\xBF u celu";
        // Route description.
        case Id::FmtRouteHeader:       return "Trasa do %s (%d metr\xF3w): %s. %s.";
        case Id::FmtRouteSegment:      return "%d metr\xF3w %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "Jedno przej\x9C""cie";
        case Id::RouteNoTransition:    return "Bez przej\x9C\xE6";
        case Id::FmtBeaconNextSegment: return "Dalej %d metr\xF3w %s";

        case Id::FmtInteractTalk:      return "Rozmawiaj z %s";
        case Id::FmtInteractOpen:      return "U\xBFyj %s";
        case Id::FmtInteractTake:      return "Podnie\x9C %s";
        case Id::FmtInteractFailed:    return "Interakcja z %s nie powiod\xB3""a si\xEA";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Menu akcji, %s";
        case Id::FmtInteractNoActionsRedirect: return "Brak akcji dla %s. Naci\x9Cnij Enter, aby aktywowa\xE6.";
        case Id::FmtInteractNoActions: return "Brak akcji dla %s.";
        case Id::DoorSealedNoOpen: return "Zapiecz\xEAtowane. Nie otworz\xB9 si\xEA.";
        case Id::EndarDoorBattleHint: return "Te drzwi pozostan\xB9 zamkni\xEAte, dop\xF3ki walka w tym obszarze si\xEA nie sko\xF1""czy.";
        case Id::EndarStuckReloadHint: return "Je\x9Cli w pobli\xBFu nie ma ju\xBF wrog\xF3w, samouczek m\xF3g\xB3 si\xEA zaci\xB9\xE6. Wczytaj wcze\x9Cniejszy zapis sprzed tego pomieszczenia.";

        case Id::FmtActionBarColumnEmpty: return "Kolumna %d jest pusta";
        case Id::FmtFireAtPosition:       return "%s, pozycja %d";
        case Id::FmtFireQueueFull:        return "%s, kolejka pe\xB3na";
        case Id::ActionMenuClosed:        return "Menu akcji zamkni\xEAte.";

        case Id::MenuCatAttacks:       return "Ataki";
        case Id::MenuCatForcePowers:   return "Moce Jedi";
        case Id::MenuCatItems:         return "Przedmioty";
        case Id::MenuCatSelfPowers:    return "Moce na siebie";
        case Id::MenuCatMedical:       return "Medycyna";
        case Id::MenuCatMisc:          return "R\xF3\xBFne";
        case Id::MenuCatExplosives:    return "Materia\xB3y wybuchowe";
        case Id::MenuCatCombatBehaviour: return "Zachowanie w walce";
        case Id::FmtMenuCatMulti:      return "%s: %s, %d opcji";
        case Id::FmtMenuCatSingle:     return "%s: %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d opcji";
        case Id::FmtMenuCategoryEmpty: return "%s: pusto";

        case Id::NoTooltipAvailable:   return "Brak opisu";

        case Id::ContainerEmpty:       return "Pusty";
        case Id::ContainerOneItem:     return "1 przedmiot";
        case Id::FmtContainerItems:    return "%d przedmiot\xF3w";
        case Id::FmtContainerItemAt:   return "%s, %d z %d";
        case Id::ContainerEmptySuffix: return "pusty";
        case Id::PlaceableStateCaptive:     return "uwi\xEAziony";
        case Id::PlaceableStateFreed:       return "uwolniony";
        case Id::PlaceableStateActive:      return "aktywny";
        case Id::PlaceableStateDeactivated: return "wy\xB3\xB9""czony";
        case Id::FmtItemStackSuffix:   return "%d w stosie";
        case Id::FmtItemChargeSuffix:  return "%d \xB3""adunk\xF3w";

        case Id::EquipSlotHead:        return "G\xB3owa";
        case Id::EquipSlotImplant:     return "Wszczep";
        case Id::EquipSlotBody:        return "Cia\xB3o";
        case Id::EquipSlotArmL:        return "Lewa r\xEAka";
        case Id::EquipSlotArmR:        return "Prawa r\xEAka";
        case Id::EquipSlotWeapL:       return "Lewa bro\xF1";
        case Id::EquipSlotWeapR:       return "Prawa bro\xF1";
        case Id::EquipSlotBelt:        return "Pas";
        case Id::EquipSlotHands:       return "D\xB3onie";

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, puste";
        case Id::EquipUnequipped:      return "Zdj\xEAto wyposa\xBF""enie";
        case Id::FmtEquipVitality:     return "Witalno\x9C\xE6 %s";
        case Id::FmtEquipDefense:      return "Obrona %s";
        case Id::FmtEquipAttack:       return "Atak %s";
        case Id::FmtEquipAttackDual:   return "Atak lewa %s, prawa %s";
        case Id::FmtEquipDamage:       return "Obra\xBF""enia %s";
        case Id::FmtEquipDamageDual:   return "Obra\xBF""enia lewa %s, prawa %s";

        case Id::FmtTransitionArea:    return "Obszar: %s";
        case Id::FmtTransitionRoomIndex: return "Pomieszczenie %d";
        case Id::FmtTransitionLoading: return "Wczytywanie: %s";

        case Id::DoorOpen:             return "otwarte";
        case Id::DoorLocked:           return "zamkni\xEAte";
        case Id::DoorUnlocked:         return "odblokowane";
        case Id::DoorCosmetic:         return "dekoracyjne";

        case Id::DirNorth:             return "P\xF3\xB3noc";
        case Id::DirNortheast:         return "P\xF3\xB3nocny wsch\xF3""d";
        case Id::DirEast:              return "Wsch\xF3""d";
        case Id::DirSoutheast:         return "Po\xB3udniowy wsch\xF3""d";
        case Id::DirSouth:             return "Po\xB3udnie";
        case Id::DirSouthwest:         return "Po\xB3udniowy zach\xF3""d";
        case Id::DirWest:              return "Zach\xF3""d";
        case Id::DirNorthwest:         return "P\xF3\xB3nocny zach\xF3""d";

        case Id::StuckFreeDirsPrefix:  return "Wolne";
        case Id::StuckAllBlocked:      return "Wszystko zablokowane";


        case Id::FmtMapStateOriented:    return "%s. Kierunek %d stopni na mapie, %s.";
        case Id::FmtMapStateUnknownRoom: return "Kierunek %d stopni na mapie, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Rozgl\xB9""danie mysz\xB9 w\xB3\xB9""czone";
        case Id::MouseLookOff:         return "Rozgl\xB9""danie mysz\xB9 wy\xB3\xB9""czone";

        case Id::ViewModeOn:           return "Tryb obserwacji w\xB3\xB9""czony";
        case Id::ViewModeOff:          return "Tryb obserwacji wy\xB3\xB9""czony";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d z %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d z %d";

        case Id::LevelUpOpen:          return "Awans";
        case Id::LevelUpFailed:        return "Awans nie powi\xF3""d\xB3 si\xEA";
        case Id::LevelUpAlreadyOpen:   return "Ekran awansu jest ju\xBF otwarty";
        case Id::LevelUpNotReady:      return "Za ma\xB3o do\x9Cwiadczenia na awans";
        case Id::LevelUpScreenHint:    return "Strza\xB3kami wybierz kategori\xEA i naci\x9Cnij Enter, aby j\xB9 otworzy\xE6. Rozdziel tam punkty, a potem wybierz Akceptuj, aby zako\xF1""czy\xE6.";

        case Id::PortraitLabel:        return "Portret";
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "kobieta";
        case Id::PortraitGenderMale:   return "m\xEA\xBF""czyzna";
        case Id::PortraitRaceAsian:    return "azjatycka";
        case Id::PortraitRaceDark:     return "ciemna karnacja";
        case Id::PortraitRaceLight:    return "jasna karnacja";
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtPartyPortraitInTeam:    return "%s, w dru\xBFynie";
        case Id::FmtPartyPortraitAvailable: return "%s, dost\xEApny";
        case Id::PartySelectionFull:        return "Dru\xBFyna pe\xB3na";

        case Id::DisabledSuffix:       return ", niedost\xEApne";
        case Id::ToggleOn:             return ", w\xB3\xB9""czone";
        case Id::ToggleOff:            return ", wy\xB3\xB9""czone";
        case Id::FmtSliderValueLabeled: return "%s %u z %u";
        case Id::FmtSliderValue:       return "%u z %u";
        case Id::EquipMenuName:        return "Wyposa\xBF""enie";
        case Id::FmtLevelUpDoStepFirst: return "Najpierw zako\xF1""cz %s.";
        case Id::LevelUpStepLocked:    return "Jeszcze nie twoja kolej.";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Poziom %s. ";
        case Id::FmtCharSheetXp:       return "Do\x9Cwiadczenie %s z %s. ";
        case Id::FmtCharSheetHp:       return "Punkty \xBFycia %s. ";
        case Id::FmtCharSheetFp:       return "Punkty Mocy %s. ";
        case Id::FmtCharSheetStr:      return "Si\xB3""a %s%s%s. ";
        case Id::FmtCharSheetDex:      return "Zr\xEA""czno\x9C\xE6 %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Kondycja %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Inteligencja %s%s%s. ";
        case Id::FmtCharSheetWis:      return "M\xB9""dro\x9C\xE6 %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Charyzma %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Charakter %u z %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modyfikator %s, koszt %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, pozosta\xB3""e punkty %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, modyfikator %s, pozosta\xB3""e punkty %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, pozosta\xB3""e punkty %s, koszt %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, modyfikator %s, pozosta\xB3""e punkty %s, koszt %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Koszt %s";
        case Id::FmtChargenSkillValueChange: return "%s, pozosta\xB3""e punkty %s";

        case Id::ChargenFeatGrantedTitle:    return "Otrzymujesz te atuty";
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d z %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "dost\xEApny";
        case Id::ChargenFeatStatusExisting:  return "ju\xBF wyuczony";
        case Id::ChargenFeatStatusGranted:   return "przyznany automatycznie";
        case Id::ChargenFeatStatusLocked:    return "brak wymaga\xF1";
        case Id::ChargenFeatStatusChosen:    return "wybrany";

        case Id::EditboxRole:                return "pole edycji";
        case Id::EditboxEmpty:               return "puste";
        case Id::FmtKeyBinding:              return "%s: %s";
        case Id::KeyBindingFixed:            return " (nie do zmiany)";
        case Id::FmtKeyBindCapture:          return "Naci\x9Cnij nowy klawisz dla %s";
        case Id::KeyBindNotChangeable:       return "Tego przypisania nie mo\xBFna zmieni\xE6";

        case Id::CombatBegins:               return "Walka si\xEA zaczyna";
        case Id::CombatEnds:                 return "Walka si\xEA ko\xF1""czy";
        case Id::CombatLeaderAtPeace:        return "Poza walk\xB9";

        case Id::PcStatNoCharacter:          return "Brak informacji o postaci.";

        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "wrogi";
        case Id::FactionFriendly:            return "przyjazny";
        case Id::FactionNeutral:             return "neutralny";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d metr\xF3w.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " druga r\xEAka %s.";
        case Id::FmtSelfStatusHp:            return "%d punkt\xF3w \xBFycia.";
        case Id::FmtSelfStatusHpOf:          return "%d z %d punkt\xF3w \xBFycia.";
        case Id::FmtSelfStatusFpOf:          return "%d z %d punkt\xF3w Mocy.";

        case Id::ExamineNoTarget:            return "Brak celu do zbadania.";
        case Id::ExamineFailed:              return "Badanie nie powiod\xB3o si\xEA.";

        case Id::FmtExamineOpened:           return "Badanie: %s. %d pozycji.";
        case Id::FmtExamineRowOf:            return "%s. %d z %d.";
        case Id::ExamineViewClosed:          return "Badanie zamkni\xEAte.";
        case Id::FmtExamineRowName:          return "Nazwa: %s";
        case Id::FmtExamineRowFaction:       return "Nastawienie: %s";
        case Id::FmtExamineRowHp:            return "Punkty \xBFycia: %d";
        case Id::FmtExamineRowDistance:      return "Odleg\xB3o\x9C\xE6: %d metr\xF3w";
        case Id::FmtExamineRowWeapon:        return "G\xB3\xF3wna r\xEAka: %s";
        case Id::ExamineRowWeaponNone:       return "G\xB3\xF3wna r\xEAka: brak";
        case Id::FmtExamineRowEffect:        return "Efekt: %s";
        case Id::FmtExamineRowFeat:          return "Atut: %s";
        case Id::FmtExamineRowEffectUnknown: return "Efekt numer %d";
        case Id::FmtExamineRowFeatUnknown:   return "Atut numer %d";
        case Id::ExamineRowNoEffects:        return "Brak aktywnych efekt\xF3w";
        case Id::ExamineRowNoFeats:          return "Brak atut\xF3w";

        case Id::FmtExamineRowHpFull:        return "Punkty \xBFycia: %d z %d";
        case Id::FmtExamineRowLevel:         return "Poziom: %d";
        case Id::FmtExamineRowCondition:     return "Stan: %s";
        case Id::DamageLevel0Healthy:        return "bez obra\xBF""e\xF1";
        case Id::DamageLevel1Light:          return "lekko ranny";
        case Id::DamageLevel2Wounded:        return "ranny";
        case Id::DamageLevel3Badly:          return "ci\xEA\xBFko ranny";
        case Id::DamageLevel4Dying:          return "umieraj\xB9""cy";
        case Id::DamageLevel5Dead:           return "martwy";
        case Id::FmtExamineRowOffHand:       return "Druga r\xEAka: %s";
        case Id::FmtExamineRowHead:          return "G\xB3owa: %s";
        case Id::FmtExamineRowTorso:         return "Pancerz: %s";
        case Id::FmtExamineRowHands:         return "D\xB3onie: %s";
        case Id::ExamineRowStatusBlind:      return "O\x9Clepiony";

        case Id::FmtQueueOpen:               return "Kolejka akcji, %d akcji.";
        case Id::QueueEmpty:                 return "Kolejka akcji jest pusta.";
        case Id::FmtQueueRow:                return "%s: %s %s, %d z %d.";
        case Id::FmtQueueRemoved:            return "Usuni\xEAto: %s.";
        case Id::QueueCleared:               return "Kolejka wyczyszczona.";
        case Id::QueueClosed:                return "Kolejka zamkni\xEAta.";
        case Id::QueueRemoveFailed:          return "Nie mo\xBFna usun\xB9\xE6 tej akcji.";
        case Id::QueueVerbAttack:            return "Atak";
        case Id::QueueVerbCastForce:         return "U\xBFyj mocy";
        case Id::QueueVerbItemCast:          return "U\xBFyj przedmiotu";
        case Id::QueueVerbEquip:             return "Za\xB3\xF3\xBF";
        case Id::QueueVerbUnequip:           return "Zdejmij";
        case Id::QueueVerbUseTalent:         return "U\xBFyj atutu";
        case Id::QueueVerbUnknown:           return "Akcja";

        case Id::MessagesTitleCombatLog:     return "Dziennik walki.";

        case Id::MapPrevNote:                return "Poprzedni znacznik mapy";
        case Id::MapNextNote:                return "Nast\xEApny znacznik mapy";

        case Id::MapCursorUnexplored:        return "Niezbadane";
        case Id::MapCursorWaypointPOI:       return "Ciekawe miejsce";
        case Id::MapCursorJunction:          return "Skrzy\xBFowanie";
        case Id::FmtMapCursorDeadEnd:        return "\x8Clepy zau\xB3""ek, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Skrzy\xBFowanie, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Drzwi";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s do %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::FmtMapCursorJunctionDeadEndExit: return "\x9Clepy zau\xB3""ek %s";
        case Id::AxisNorthSouth:             return "p\xF3\xB3noc-po\xB3udnie";
        case Id::AxisEastWest:               return "wsch\xF3""d-zach\xF3""d";
        case Id::AreaNoun:                   return "Obszar";
        case Id::AreaNounLarge:              return "Du\xBFy obszar";
        case Id::FmtAreaAxisExits:           return "%s %s. Wyj\x9C""cia: %s";
        case Id::FmtAreaExits:               return "%s. Wyj\x9C""cia: %s";
        case Id::FmtAreaAxisOnly:            return "%s %s";

        case Id::FmtStorePriceBuyFinite:    return "Cena %d kredyt\xF3w, w zapasie %d";
        case Id::FmtStorePriceBuyUnlimited: return "Cena %d kredyt\xF3w, zapas nieograniczony";
        case Id::FmtStorePriceSell:         return "Cena %d kredyt\xF3w, posiadasz %d";
        case Id::StoreModeBuy:              return "Tryb kupna";
        case Id::StoreModeSell:             return "Tryb sprzeda\xBFy";
        case Id::StoreCannotSell:           return "Nie mo\xBFna sprzeda\xE6";
        case Id::StoreCannotBuy:            return "Nie mo\xBFna kupi\xE6";
        case Id::FmtStoreSoldFor:           return "Sprzedano za %d kredyt\xF3w";
        case Id::FmtStoreBoughtFor:         return "Kupiono za %d kredyt\xF3w";

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Naci\x9Cnij C, aby odczyta\xE6 r\xEAk\xEA, T dla sto\xB3u.";
        case Id::PazaakEmpty:            return "pusto";
        case Id::PazaakFaceDown:         return "zakryta";
        case Id::PazaakBoardEmpty:       return "pusto";
        case Id::PazaakFmtPlus:          return "plus %d";
        case Id::PazaakFmtMinus:         return "minus %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "plus lub minus %d";
        case Id::PazaakFmtFlipCurrently: return "%s, obecnie %s";
        case Id::PazaakFmtYouDrew:       return "Dobra\xB3""e\x9C %s. Twoja suma %d.";
        case Id::PazaakOverTwenty:       return "Ponad dwadzie\x9C""cia.";
        case Id::PazaakFmtYouPlayed:     return "Zagrano %s. Twoja suma %d.";
        case Id::PazaakYourTurn:         return "Twoja kolej.";
        case Id::PazaakTurnEnded:        return "Koniec tury.";
        case Id::PazaakFmtOppDrew:       return "Przeciwnik dobra\xB3 %s. Suma %d.";
        case Id::PazaakFmtOppPlayed:     return "Przeciwnik zagra\xB3 %s. Suma %d.";
        case Id::PazaakFmtOppStands:     return "Przeciwnik pasuje przy %d.";
        case Id::PazaakFmtYouStand:      return "Pasujesz przy %d.";
        case Id::PazaakFmtWinSet:        return "Wygrywasz set. %d do %d.";
        case Id::PazaakFmtLoseSet:       return "Przegrywasz set. %d do %d.";
        case Id::PazaakWinMatch:         return "Wygrywasz mecz!";
        case Id::PazaakLoseMatch:        return "Przegrywasz mecz.";
        case Id::PazaakTieReplay:        return "Remis. Powt\xF3rka setu.";
        case Id::PazaakFmtHand:          return "R\xEAka: %s";
        case Id::PazaakHandEmpty:        return "R\xEAka pusta.";
        case Id::PazaakFmtYourBoard:     return "Tw\xF3j st\xF3\xB3: %s, suma %d.";
        case Id::PazaakFmtOppBoard:      return "St\xF3\xB3 przeciwnika: %s, suma %d.";
        case Id::PazaakNoPlayable:       return "Brak kart do zagrania.";
        case Id::PazaakNotYourTurn:      return "Nie twoja kolej.";
        case Id::PazaakChooseSign:       return "Wybierz znak. Lewo lub prawo zmienia, Enter zagrywa.";
        case Id::PazaakCancelled:        return "Anulowano.";
        case Id::PazaakDeckAvailable:    return "%s, dost\xEApne %d";
        case Id::PazaakDeckNoneLeft:     return "%s, brak";
        case Id::PazaakDeckSlotFilled:   return "Miejsce w talii %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Miejsce w talii %d: puste";
        case Id::PazaakDeckPlay:         return "Graj, %d z 10 w talii";
        case Id::PazaakDeckAdded:        return "Dodano %s. %d z 10.";
        case Id::PazaakDeckRemoved:      return "Usuni\xEAto %s.";
        case Id::PazaakDeckFull:         return "Talia pe\xB3na.";
        case Id::PazaakFmtOppHand:       return "Przeciwnik ma %d kart na r\xEA""ce.";
        case Id::PazaakStandLabel:       return "Do\x9C\xE6";
        case Id::PazaakEndTurnLabel:     return "Zako\xF1""cz tur\xEA";
        case Id::PazaakWagerLess:        return "Zmniejsz stawk\xEA";
        case Id::PazaakWagerMore:        return "Zwi\xEAksz stawk\xEA";
        case Id::PazaakFmtWager:         return "Stawka %d z %d maksymalnie.";
        case Id::PazaakFmtWagerRow:      return "Stawka %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Za ma\xB3o kredyt\xF3w, potrzeba %d, masz %d";
        case Id::JournalQuestItemsButton:   return "Przedmioty zada\xF1";

        case Id::FmtCredits:                return "Kredyty: %s";

        case Id::WorkbenchSlotWeapon1:       return "Gniazdo ulepszenia 1";
        case Id::WorkbenchSlotWeapon2:       return "Gniazdo ulepszenia 2";
        case Id::WorkbenchSlotWeapon3:       return "Gniazdo ulepszenia 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Gniazdo kryszta\xB3u 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Gniazdo kryszta\xB3u 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Gniazdo kryszta\xB3u 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Gniazdo kryszta\xB3u 4";
        case Id::WorkbenchItemsEmpty:        return "Brak przedmiot\xF3w do ulepszenia w tej kategorii";
        case Id::WorkbenchUpgradesEmpty:     return "Brak pasuj\xB9""cych ulepsze\xF1 w ekwipunku";
        case Id::WorkbenchSlotInstalled:     return "Zamontowano ulepszenie";
        case Id::WorkbenchSlotRemoved:       return "Usuni\xEAto ulepszenie";
        case Id::WorkbenchSlotNoMatch:       return "Brak pasuj\xB9""cego ulepszenia w ekwipunku";
        case Id::WorkbenchSlotFilled:        return "zaj\xEAte";
        case Id::WorkbenchSlotPeekEmpty:     return "Puste gniazdo, brak ulepszenia";
        case Id::WorkbenchFmtSlotItem:       return "%s, zawiera %s";
        case Id::WorkbenchPickerInstalled:   return "zamontowane";

        case Id::SoundOptionsMovieVolume:    return "G\xB3o\x9Cno\x9C\xE6 film\xF3w";

        case Id::SwoopRaceStarted:
            return "Wy\x9C""cig \x9C""cigaczy.";
        case Id::SwoopRaceControls:
            return "Naci\x9Cnij spacj\xEA, aby zmieni\xE6 bieg na sygna\xB3 d\x9Fwi\xEAkowy. Steruj klawiszami A i D. Omijaj przeszkody i naje\xBF""d\xBF""aj na pola przyspieszenia, aby pobi\xE6 czas.";
        case Id::SwoopRaceEnded:
            return "Koniec wy\x9C""cigu.";
        case Id::FmtSwoopRaceGear:
            return "Bieg %d";
        case Id::FmtSwoopRaceTime:
            return "Czas: %d.%02d sekundy. Koniec wy\x9C""cigu.";

        case Id::TurretGameStarted:
            return "Wie\xBFyczka.";
        case Id::TurretGameControls:
            return "Celuj klawiszami W, A, S i D. Spacja strzela. Q i E wybieraj\xB9 cele.";
        case Id::TurretGameEnded:
            return "Koniec wie\xBFyczki.";
        case Id::FmtTurretTarget:
            return "My\x9Cliwiec %d, %d metr\xF3w";
        case Id::FmtTurretDestroyed:
            return "My\x9Cliwiec %d zniszczony.";
        case Id::TurretNoTargets:
            return "Brak cel\xF3w.";

        case Id::ModSettingsRootLabel:        return "Ustawienia moda";
        case Id::ModSettingsOpened:           return "Otwarto ustawienia moda";
        case Id::ModSettingsClosed:           return "Zamkni\xEAto ustawienia moda";
        case Id::ModSettingExtendedCycling:   return "Wyb\xF3r obiekt\xF3w na ca\xB3""ej mapie";
        case Id::ModSettingRoomShapes:        return "Opisy kszta\xB3tu pomieszcze\xF1";
        case Id::ModSettingWallSounds:        return "D\x9Fwi\xEAki \x9C""cian";
        case Id::ModSettingHumanSubtitles:    return "Czytaj napisy m\xF3wionych kwestii";
        case Id::ModSettingTurretAutoAim:     return "Automatyczne celowanie";
        case Id::ModSettingSkipIntros:        return "Pomijaj filmy wst\xEApne";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "Filmy wst\xEApne zostan\xB9 pomini\xEAte przy nast\xEApnym uruchomieniu.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "Filmy wst\xEApne zostan\xB9 odtworzone przy nast\xEApnym uruchomieniu.";
        case Id::ModSettingSkipIntrosToggleFailed: return "Nie uda\xB3o si\xEA prze\xB3\xB9""czy\xE6 film\xF3w wst\xEApnych. Pliki mog\xB3y zosta\xE6 usuni\xEAte.";
        case Id::ModSettingStateOn:           return "w\xB3\xB9""czone";
        case Id::ModSettingStateOff:          return "wy\xB3\xB9""czone";
        case Id::FmtModSettingOption:         return "%s: %s";
        case Id::ModSettingCueVolume:         return "G\xB3o\x9Cno\x9C\xE6 d\x9Fwi\xEAk\xF3w podpowiedzi";
        case Id::FmtModSettingSlider:         return "%s: %d procent";
        case Id::ModSettingUrgentVolume:      return "G\xB3o\x9Cno\x9C\xE6 komunikat\xF3w m\xF3wionych";
        case Id::ModSettingUrgentVolumePreview: return "Przyk\xB3""adowy komunikat";

        case Id::ModSettingAudioGlossary:           return "S\xB3ownik d\x9Fwi\xEAk\xF3w";
        case Id::ModSettingsAudioGlossaryOpened:    return "Otwarto s\xB3ownik d\x9Fwi\xEAk\xF3w";
        case Id::GlossaryEntryDoorOpen:             return "Drzwi otwarte";
        case Id::GlossaryEntryDoorClosedMetal:      return "Metalowe drzwi zamkni\xEAte";
        case Id::GlossaryEntryDoorClosedWood:       return "Drewniane drzwi zamkni\xEAte";
        case Id::GlossaryEntryDoorClosedStone:      return "Kamienne drzwi zamkni\xEAte";
        case Id::GlossaryEntryWall:                 return "\x8C""ciana";
        case Id::GlossaryEntryHazard:               return "Zagro\xBF""enie";
        case Id::GlossaryEntryCollision:            return "Zderzenie";
        case Id::GlossaryEntryBeaconActive:         return "Sygna\xB3 aktywny";
        case Id::GlossaryEntryBeaconWaypoint:       return "Osi\xB9gni\xEAto punkt sygna\xB3u";
        case Id::GlossaryEntryBeaconDestination:    return "Osi\xB9gni\xEAto cel sygna\xB3u";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Pole przyspieszenia \x9C""cigacza";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Ostrze\xBF""enie o przeszkodzie";
        case Id::GlossaryEntrySwoopWallImpact:      return "Uderzenie w \x9C""cian\xEA";
        case Id::GlossaryEntrySwoopAligned:         return "\x8C""cigacz na torze";
        case Id::GlossaryEntrySwoopShiftReady:      return "Gotowo\x9C\xE6 do zmiany biegu";

        case Id::FmtUpdateAvailable:    return "Dost\xEApna aktualizacja, wersja %s. Naci\x9Cnij F5 w menu g\xB3\xF3wnym, aby zainstalowa\xE6.";
        case Id::UpdateDownloadStarting: return "Rozpoczynam pobieranie.";
        case Id::UpdateDownloading:     return "Pobieram aktualizacj\xEA.";
        case Id::UpdateDownloaded:      return "Pobrano aktualizacj\xEA. Zamykam gr\xEA, aby zainstalowa\xE6.";
        case Id::UpdateFailed:          return "Pobieranie aktualizacji nie powiod\xB3o si\xEA. Naci\x9Cnij F5, aby spr\xF3""bowa\xE6 ponownie.";
        case Id::FmtUpdateNotAvailable: return "Brak aktualizacji. Masz wersj\xEA %s.";
        case Id::UpdateNotInMenu:       return "Aktualizacje mo\xBFna instalowa\xE6 tylko z menu g\xB3\xF3wnego.";

        case Id::PanelTitleMainMenu:    return "Menu g\xB3\xF3wne";
        case Id::LoadingPleaseWait:     return "Gra si\xEA jeszcze wczytuje, prosz\xEA czeka\xE6.";
        case Id::LoadingStuckWorkaround: return "Menu nadal nie odpowiada. Naci\x9Cnij Alt F4 i anuluj okno wyj\x9C""cia, aby je obudzi\xE6.";

        case Id::GamePaused:            return "Pauza.";
        case Id::GameResumed:           return "Wznowiono.";

        case Id::GalaxyMapTitle:        return "Mapa galaktyki";

        // ---- Help system ----
        case Id::HelpGroupGeneral:      return "Nawigacja";
        case Id::HelpGroupMovement:     return "Ruch i kamera";
        case Id::HelpGroupInteraction:  return "Cele i interakcja";
        case Id::HelpGroupCombat:       return "Walka i akcje";
        case Id::HelpGroupExploration:  return "Eksploracja i orientacja";
        case Id::HelpGroupScreens:      return "Ekrany";
        case Id::HelpGroupMap:          return "Mapa";
        case Id::HelpGroupMod:          return "Funkcje moda";
        case Id::HelpGroupController:    return "Kontroler";

        case Id::HelpKeyUpDown:          return "Strza\xB3ka w g\xF3r\xEA i w d\xF3\xB3: poruszanie si\xEA po listach i pozycjach menu";
        case Id::HelpKeyLeftRight:       return "Strza\xB3ka w lewo i w prawo: zmiana kategorii lub warto\x9C""ci";
        case Id::HelpKeyHomeEnd:         return "Home i End: skok do pierwszej lub ostatniej pozycji";
        case Id::HelpKeyEnter:           return "Enter: aktywuje wybran\xB9 pozycj\xEA";
        case Id::HelpKeyEsc:             return "Escape: zamyka ekran lub cofa";
        case Id::HelpKeyReadDescription: return "Shift ze strza\xB3k\xB9: czyta pe\xB3ny opis bez zmiany pozycji";
        case Id::HelpKeySwitchWindows:   return "Q i E: prze\xB3\xB9""czaj\xB9 okna lub zak\xB3""adki, ekrany menu gry oraz tryby w sklepach i pojemnikach";
        case Id::HelpKeyF1:              return "F1: otwiera lub zamyka t\xEA list\xEA klawiszy";
        case Id::HelpKeyCtrlF1:          return "Ctrl i F1: czyta klawisze bie\xBF\xB9""cego ekranu";

        case Id::HelpKeyWalk:           return "W i S: chodzenie do przodu i do ty\xB3u";
        case Id::HelpKeyCameraRotate:   return "Z i C: obracaj\xB9 kamer\xEA w lewo i w prawo";
        case Id::HelpKeyStrafe:         return "A i D: kroki w lewo i w prawo";
        case Id::HelpKeyPause:          return "Spacja: pauza i wznowienie gry";
        case Id::HelpKeyViewMode:       return "B: tryb rozgl\xB9""dania, stoisz w miejscu, obracaj\xB9""c kamer\xEA";
        case Id::HelpKeySwitchLeader:   return "Tab: zmienia kontrolowanego cz\xB3onka dru\xBFyny";

        case Id::HelpKeyCycleTargets:   return "Q i E: prze\xB3\xB9""czaj\xB9 obiekty w polu widzenia";
        case Id::HelpKeyInteract:       return "Enter: interakcja z wybranym celem lub atak na niego";
        case Id::HelpKeyOpenActionMenu: return "Shift i Enter: otwiera menu akcji dla wybranego celu";
        case Id::HelpKeySelfStatus:     return "H: podaje twoje zdrowie, efekty i bro\xF1";
        case Id::HelpKeyAnnounceFocus:  return "Minus: podaje wybrany obiekt";
        case Id::HelpKeyWalkToFocus:    return "Shift i minus: id\x9F do wybranego obiektu";
        case Id::HelpKeyBeacon:         return "Ctrl i minus: uruchamia sygna\xB3 prowadz\xB9""cy do wybranego obiektu";
        case Id::HelpKeyDialogRepeat:   return "R: powtarza bie\xBF\xB9""c\xB9 wypowied\x9F";

        case Id::HelpKeyCycleObjects:   return "Przecinek i kropka: prze\xB3\xB9""czaj\xB9 odkryte obiekty w bie\xBF\xB9""cej kategorii";
        case Id::HelpKeyCycleCategory:  return "Shift z przecinkiem lub kropk\xB9: poprzednia lub nast\xEApna kategoria";
        case Id::HelpKeyCycleEnds:      return "Ctrl z przecinkiem lub kropk\xB9: skok do najbli\xBFszego lub najdalszego obiektu";
        case Id::HelpKeyHeading:        return "Prawy Alt: podaje dok\xB3""adny kierunek w stopniach";
        case Id::HelpKeyCameraOrient:   return "N: obraca kamer\xEA na nast\xEApny kierunek lub w stron\xEA kolejnego punktu sygna\xB3u";
        case Id::HelpKeyDropMarker:     return "Shift i N: stawia znacznik mapy w twoim po\xB3o\xBF""eniu";

        case Id::FmtHelpNumberActions:   return "1 do 7: u\xBFycie ostatniej akcji z kategorii. 1 %s, 2 %s, 3 %s, 4 %s, 5 %s, 6 %s, 7 %s";
        case Id::HelpKeyOpenCategory:    return "Shift i 1 do 7: otwiera kategori\xEA, aby wybra\xE6 akcj\xEA";
        case Id::FmtHelpNumberActions8:  return "8: zastosuj bie\xBF\xB9""ce %s. Shift i 8 otwiera kategori\xEA, aby wybra\xE6 inne";
        case Id::HelpKeyActionQueue:     return "Shift i H: otwiera kolejk\xEA akcji";
        case Id::HelpKeyLevelUp:         return "Shift i L: otwiera ekran awansu";
        case Id::HelpKeyCancelCombat:    return "F: przerywa walk\xEA";

        case Id::HelpKeyScreenMap:       return "M: otwiera map\xEA";
        case Id::HelpKeyScreenMessages:  return "J: wiadomo\x9C""ci i informacje";
        case Id::HelpKeyScreenQuests:    return "L: zadania";
        case Id::HelpKeyScreenAbilities: return "K: umiej\xEAtno\x9C""ci, atuty i moce";
        case Id::HelpKeyScreenCharacter: return "P: karta postaci";
        case Id::HelpKeyScreenInventory: return "I: ekwipunek dru\xBFyny";
        case Id::HelpKeyScreenEquip:     return "U: wyposa\xBF""enie postaci";
        case Id::HelpKeyScreenOptions:   return "O: opcje";

        case Id::HelpKeyMapCursor:       return "W, A, S i D: przesuwaj\xB9 kursor mapy, aby odczyta\xE6 teren i znaczniki";
        case Id::HelpKeyMapPosition:     return "Prawy Alt: podaje twoje po\xB3o\xBF""enie i kierunek na mapie";


        case Id::HelpKeyModSettings:     return "Ustawienia moda s\xB9 w Opcjach, na dole listy";
        case Id::HelpKeyPadMenuNav:          return "W menu: krzy\xBF" "ak lub lewa ga\xB3ka przesuwaj\xB9, A zatwierdza, B cofa";
        case Id::HelpKeyPadInteract:          return "A: domy\x9Clna akcja na wybranym celu (atak, otwarcie, rozmowa, podniesienie)";
        case Id::HelpKeyPadModeSwitch:       return "Lewy spust: prze\xB3\xB9" "cz krzy\xBF" "ak mi\xEA" "dzy wyborem obiekt\xF3w a menu akcji";
        case Id::HelpKeyPadCycleObjects:     return "Wyb\xF3r obiekt\xF3w, krzy\xBF" "ak w lewo i w prawo: poprzedni i nast\xEApny obiekt";
        case Id::HelpKeyPadCycleCategory:    return "Wyb\xF3r obiekt\xF3w, krzy\xBF" "ak w g\xF3r\xEA i w d\xF3\xB3: poprzednia i nast\xEApna kategoria";
        case Id::HelpKeyPadActionMenuNav:    return "Menu akcji, krzy\xBF" "ak: w lewo i w prawo zmienia kategori\xEA, w g\xF3r\xEA i w d\xF3\xB3 pozycj\xEA, A wykonuje";
        case Id::HelpKeyPadWalkToFocus:      return "Prawy spust z prawym bumperem: id\x9F do wybranego obiektu";
        case Id::HelpKeyPadBeacon:           return "Lewy spust z lewym bumperem: sygna\xB3 d\x9Fwi\xEAkowy do wybranego obiektu";
        case Id::HelpKeyPadDegrees:          return "Prawy spust: podaj kierunek w stopniach";
        case Id::HelpKeyPadCameraOrient:     return "Naci\x9Cni\xEA" "cie prawej ga\xB3ki: obr\xF3\xE6 kamer\xEA do nast\xEApnego punktu sygna\xB3u albo do nast\xEApnej strony \x9Cwiata";
        case Id::HelpKeyPadHelp:             return "Oba spusty: klawisze tego ekranu. Pozycja Pomoc w szybkim menu otwiera t\xEA list\xEA";
        case Id::HelpKeyPadQuickMenu:        return "Y: szybkie menu";
        case Id::HelpKeyPadCycleTargets:     return "Lewy i prawy bumper: zmiana celu";
        case Id::HelpKeyPadOptions:          return "Przycisk Back: menu opcji";
        case Id::PadModeCycle:               return "Wyb\xF3r obiekt\xF3w";
        case Id::PadModeActionMenu:          return "Menu akcji";

        case Id::HelpMenuOpened:    return "Pomoc klawiszy. G\xF3ra i d\xF3\xB3 czytaj\xB9, Escape zamyka.";
        case Id::HelpMenuClosed:    return "Pomoc klawiszy zamkni\xEAta.";
        case Id::FmtHelpRowOf:      return "%s. %d z %d";
        case Id::FmtHelpGroupHeader: return "Sekcja: %s";

        case Id::HelpContextNothing: return "Brak specjalnych klawiszy dla tego ekranu.";
        case Id::FmtHelpContextLine: return "%s %s.";
        case Id::HelpContextWorld:       return "W \x9Cwiecie.";
        case Id::HelpContextMenu:        return "Menu.";
        case Id::HelpContextMap:         return "Mapa.";
        case Id::HelpContextActionMenu:  return "Menu akcji.";
        case Id::HelpContextDialog:      return "Rozmowa.";
        case Id::HelpContextContainer:   return "Pojemnik.";
        case Id::HelpContextStore:       return "Sklep.";

        case Id::InputBlockedBigPicture:
            return "Gra nie odbiera naci\x9Cni\xEA\xE6 klawiszy, poniewa\xBF na wierzchu "
                   "jest tryb Steam Big Picture.";

        // ---- Key bindings (mod keybind configurator) ----
        case Id::KeybindsRootLabel:       return "Przypisania klawiszy";
        case Id::KeybindsOpened:          return "Otwarto przypisania klawiszy";
        case Id::KeybindCatWorld:         return "\x8Cwiat i akcje";
        case Id::KeybindCatExploration:   return "Eksploracja i kamera";
        case Id::KeybindCatMenus:         return "Menu i sterowanie";
        case Id::KeybindCatMinigames:     return "Minigry";
        case Id::KeybindCatGeneral:       return "Og\xF3lne";
        case Id::KeybindResetAll:         return "Przywr\xF3\xE6 domy\x9Clne";
        case Id::KeybindResetDone:        return "Przywr\xF3""cono domy\x9Clne przypisania klawiszy";
        case Id::FmtKeybindCapturePrompt: return "Naci\x9Cnij nowy klawisz dla %s. Escape anuluje.";
        case Id::FmtKeybindRebound:       return "%s przypisano do %s";
        case Id::FmtKeybindConflictMod:   return "Ju\xBF przypisane do %s. Naci\x9Cnij inny klawisz.";
        case Id::KeybindConflictEngine:   return "Zaj\xEAte przez gr\xEA. Naci\x9Cnij inny klawisz.";
        case Id::KeybindCaptureCancelled: return "Anulowano";
        case Id::FmtKeymapModConflict:    return "Uwaga: mod u\xBFywa tego klawisza do: %s";
        // World & actions
        case Id::KbNameInteractTarget:      return "Interakcja";
        case Id::KbNameInteractForceRadial: return "Radialne menu Mocy";
        case Id::KbNameInteractForceRadialSecondary: return "Radialne menu Mocy (drugie)";
        case Id::KbNameTargetKey1:          return "Klawisz celu 1";
        case Id::KbNameTargetKey2:          return "Klawisz celu 2";
        case Id::KbNameTargetKey3:          return "Klawisz celu 3";
        case Id::KbNamePersonalKey1:        return "Akcja osobista 1";
        case Id::KbNamePersonalKey2:        return "Akcja osobista 2";
        case Id::KbNamePersonalKey3:        return "Akcja osobista 3";
        case Id::KbNamePersonalKey4:        return "Akcja osobista 4";
        case Id::KbNamePersonalKey5:        return "Akcja osobista 5";
        case Id::KbNameActionBarOpen1:      return "Otw\xF3rz pasek akcji 1";
        case Id::KbNameActionBarOpen2:      return "Otw\xF3rz pasek akcji 2";
        case Id::KbNameActionBarOpen3:      return "Otw\xF3rz pasek akcji 3";
        case Id::KbNameActionBarOpen4:      return "Otw\xF3rz pasek akcji 4";
        case Id::KbNameActionBarOpen5:      return "Otw\xF3rz pasek akcji 5";
        case Id::KbNameTargetActionOpen1:   return "Otw\xF3rz akcj\xEA celu 1";
        case Id::KbNameTargetActionOpen2:   return "Otw\xF3rz akcj\xEA celu 2";
        case Id::KbNameTargetActionOpen3:   return "Otw\xF3rz akcj\xEA celu 3";
        case Id::KbNameLevelUpOpen:         return "Awans";
        case Id::KbNameExamineOpen:         return "Badanie";
        case Id::KbNameCombatQueueOpen:     return "Kolejka akcji";
        case Id::KbNameSelfStatusAnnounce:  return "W\xB3""asny stan";
        // Exploration & camera
        case Id::KbNameCycleItemPrev:       return "Poprzedni obiekt";
        case Id::KbNameCycleCategoryPrev:   return "Poprzednia kategoria";
        case Id::KbNameCycleItemNext:       return "Nast\xEApny obiekt";
        case Id::KbNameCycleCategoryNext:   return "Nast\xEApna kategoria";
        case Id::KbNameCycleItemFirst:      return "Pierwszy obiekt";
        case Id::KbNameCycleItemLast:       return "Ostatni obiekt";
        case Id::KbNameAnnounceFocus:       return "Podaj wybrany obiekt";
        case Id::KbNamePathfindFocus:       return "Id\x9F do wybranego";
        case Id::KbNamePathfindFocusForce:  return "Id\x9F do wybranego, wymu\x9C";
        case Id::KbNameBeaconFocus:         return "Sygna\xB3 do wybranego";
        case Id::KbNameAnnounceDegrees:     return "Kierunek w stopniach";
        case Id::KbNamePartyLeaderAnnounce: return "Podaj przyw\xF3""dc\xEA dru\xBFyny";
        case Id::KbNameCameraOrient:        return "Ustaw kamer\xEA";
        case Id::KbNameSaveMarkerAtCursor:  return "Postaw znacznik";
        case Id::KbNameViewModeToggle:      return "Tryb obserwacji";
        // Menus & input
        case Id::KbNameNavUp:               return "Menu w g\xF3r\xEA";
        case Id::KbNameNavDown:             return "Menu w d\xF3\xB3";
        case Id::KbNameNavLeft:             return "Menu w lewo";
        case Id::KbNameNavRight:            return "Menu w prawo";
        case Id::KbNameNavHome:             return "Na pocz\xB9tek";
        case Id::KbNameNavEnd:              return "Na koniec";
        case Id::KbNameSubmenuEsc:          return "Zamknij menu";
        case Id::KbNameQueueClearAll:       return "Wyczy\x9C\xE6 kolejk\xEA";
        case Id::KbNameContainerGiveMode:   return "Tryb oddawania do pojemnika";
        case Id::KbNameStoreModeToggle:     return "Sklep: kupno lub sprzeda\xBF";
        case Id::KbNameEditboxReReadUp:     return "Pole edycji: czytaj w g\xF3r\xEA";
        case Id::KbNameEditboxReReadDown:   return "Pole edycji: czytaj w d\xF3\xB3";
        case Id::KbNameEditboxSubmit:       return "Zatwierd\x9F wpis";
        case Id::KbNameEditboxCancel:       return "Anuluj wpis";
        // Minigames
        case Id::KbNamePazaakStand:         return "Pazaak: Pas";
        case Id::KbNamePazaakEndTurn:       return "Pazaak: Zako\xF1""cz tur\xEA";
        case Id::KbNamePazaakReviewHand:    return "Pazaak: Przejrzyj r\xEAk\xEA";
        case Id::KbNamePazaakReviewTable:   return "Pazaak: Przejrzyj st\xF3\xB3";
        case Id::KbNamePazaakNextCard:      return "Pazaak: Nast\xEApna karta";
        case Id::KbNamePazaakPrevCard:      return "Pazaak: Poprzednia karta";
        case Id::KbNamePazaakPlay:          return "Pazaak: Zagraj kart\xEA";
        case Id::KbNamePazaakOptLeft:       return "Pazaak: Opcja w lewo";
        case Id::KbNamePazaakOptRight:      return "Pazaak: Opcja w prawo";
        case Id::KbNamePazaakCancel:        return "Pazaak: Anuluj";
        case Id::KbNamePazaakOppHand:       return "Pazaak: R\xEAka przeciwnika";
        case Id::KbNameTurretCyclePrev:     return "Wie\xBFyczka: Poprzedni cel";
        case Id::KbNameTurretCycleNext:     return "Wie\xBFyczka: Nast\xEApny cel";
        // General
        case Id::KbNameHelpMenuOpen:        return "Pomoc klawiszy";
        case Id::KbNameHelpContext:         return "Pomoc kontekstowa";
        case Id::KbNameCheckForUpdate:      return "Sprawd\x9F aktualizacje";
        case Id::KbNameDialogRepeatLine:    return "Powt\xF3rz kwesti\xEA";

        // ---- Endar Spire tutorial keyboard hints (Surface 1: popups) ----
        case Id::TutHintCombatFeat:
            return "Atuty bojowe s\xB9 w menu akcji. Otw\xF3rz Ataki klawiszami Shift i 1, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutHintGrenade:
            return "Granaty rzucasz z menu akcji. Otw\xF3rz Materia\xB3y wybuchowe klawiszami Shift i 7, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutHintMine:
            return "Miny stawiasz z menu akcji. Otw\xF3rz Materia\xB3y wybuchowe klawiszami Shift i 7, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutHintFriendlyPower:
            return "Przyjazne moce s\xB9 w menu akcji. Otw\xF3rz moce na siebie klawiszami Shift i 4, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutHintHostilePower:
            return "Wrogie moce s\xB9 w menu akcji. Otw\xF3rz moce na cel klawiszami Shift i 2, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutHintActionMenuScroll:
            return "W menu akcji poruszasz si\xEA mi\xEA""dzy pozycjami strza\xB3kami g\xF3ra i d\xF3\xB3, a mi\xEA""dzy kategoriami w lewo i w prawo.";
        case Id::TutHintOpenScreens:
            return "Ekrany gry otwierasz bezpo\x9Crednio: M mapa, J wiadomo\x9C""ci, L zadania, K umiej\xEAtno\x9C""ci, P posta\xE6, I ekwipunek, U wyposa\xBF""enie, O opcje. Escape wraca.";
        case Id::TutHintCycleTargets:
            return "Cele wybierasz klawiszami Q i E. Obiekty prze\xB3\xB9""czasz przecinkiem i kropk\xB9, a minus podaje wybrany obiekt. W walce mo\xBF""esz bra\xE6 na cel tylko wrog\xF3w.";
        case Id::TutHintEquipSlot:
            return "Na ekranie wyposa\xBF""enia wybierz gniazdo strza\xB3kami i Enterem, a potem wska\xBF przedmiot z listy. Opis przedmiotu odczytasz Shiftem ze strza\xB3k\xB9.";
        case Id::TutHintEnemyNear:
            return "Wr\xF3g jest blisko. Prze\xB3\xB9""czaj cele klawiszami Q i E, a atakuj Enterem.";
        case Id::TutHintBash:
            return "Aby wywa\xBFy\xE6, wybierz drzwi lub pojemnik, otw\xF3rz menu akcji klawiszami Shift i Enter i wybierz Wywa\xBF.";
        case Id::TutHintAttack:
            return "Wybierz wroga klawiszami Q i E i zaatakuj Enterem. Jedno naci\x9Cni\xEA""cie wystarczy; twoja posta\xE6 atakuje potem automatycznie w ka\xBF""dej rundzie, a\xBF wybierzesz inny cel.";
        case Id::TutHintAttackAuto:
            return "Pami\xEAtaj: jedno naci\x9Cni\xEA""cie Enteru atakuje automatycznie w ka\xBF""dej rundzie. Zmienia to tylko nowy cel.";
        case Id::TutHintMovement:
            return "Naci\x9Cnij F1 w dowolnej chwili, aby otworzy\xE6 menu pomocy z list\xB9 wszystkich klawiszy, albo Ctrl i F1, aby pozna\xE6 klawisze bie\xBF\xB9""cego ekranu. Aby si\xEA porusza\xE6, naciskaj W i S, by i\x9C\xE6 do przodu i do ty\xB3u, A i D, by zrobi\xE6 krok w lewo i w prawo, oraz Z i C, by skr\xEA""ca\xE6 w lewo i w prawo. Z i C obracaj\xB9 kamer\xEA, a wraz z ni\xB9 kierunek marszu, wi\xEA""c oba zawsze wskazuj\xB9 to samo. D\x9Fwi\xEAki ostrzegaj\xB9, gdy zbli\xBF""asz si\xEA do \x9C""ciany, a w trakcie ruchu opisywany jest kszta\xB3t pomieszczenia wok\xF3\xB3 ciebie. Naci\x9Cnij prawy Alt, aby powt\xF3rzy\xE6 kierunek i bie\xBF\xB9""ce pomieszczenie.";
        case Id::TutHintMapScreen:
            return "Mapa pokazuje ka\xBF""dy zbadany obszar poziomu. Wa\xBFne miejsca s\xB9 oznaczone, a niekt\xF3re obszary, na przyk\xB3""ad miasta, s\xB9 naniesione, zanim je zbadasz. Po mapie poruszasz si\xEA klawiszami W, A, S i D. Mi\xEA""dzy znacznikami prze\xB3\xB9""czasz si\xEA przecinkiem i kropk\xB9. Naci\x9Cnij Shift i N, aby postawi\xE6 w\xB3""asny znacznik w swoim po\xB3o\xBF""eniu. Naci\x9Cnij Ctrl i minus, aby uruchomi\xE6 sygna\xB3 prowadz\xB9""cy do znacznika d\x9Fwi\xEAkiem, albo Shift i minus, aby doj\x9C\xE6 do wybranego znacznika automatycznie.";
        case Id::TutHintJournal:
            return "Ekran zada\xF1 pokazuje twoje post\xEApy i podpowiedzi, co robi\xE6 dalej. Otw\xF3rz go klawiszem L. Wybierz pozycj\xEA strza\xB3kami i odczytaj szczeg\xF3\xB3y Shiftem ze strza\xB3k\xB9.";
        case Id::TutHintInventory:
            return "Ekwipunek zawiera przedmioty ca\xB3""ej dru\xBFyny, wsp\xF3lne dla wszystkich. Otw\xF3rz go klawiszem I. Poruszaj si\xEA po przedmiotach strza\xB3kami, a opis odczytaj Shiftem ze strza\xB3k\xB9.";
        case Id::TutHintMessages:
            return "Ekran wiadomo\x9C""ci pokazuje warto\x9C""ci takie jak punkty do\x9Cwiadczenia, rzuty na atak i obra\xBF""enia oraz zmiany w twojej postaci. Otw\xF3rz go klawiszem J.";
        case Id::TutHintPartyDies:
            return "Cz\xB3onek dru\xBFyny pad\xB3. Powaleni w walce wracaj\xB9 do si\xB3 po jej zako\xF1""czeniu, ale je\x9Cli zgin\xB9 wszyscy, gra si\xEA ko\xF1""czy. Naci\x9Cnij Tab, aby przej\xB9\xE6 kontrol\xEA nad innym cz\xB3onkiem.";

        // ---- Endar Spire tutorial keyboard hints (Surface 2: Trask / pop) ----
        case Id::TutTraskGettingStarted:
            return "Masz teraz kontrol\xEA. Podstawy: naci\x9Cnij F1 w dowolnej chwili, aby zobaczy\xE6 pe\xB3n\xB9 list\xEA klawiszy, albo Ctrl i F1, aby pozna\xE6 klawisze bie\xBF\xB9""cego ekranu. W i S to marsz do przodu i do ty\xB3u, A i D to kroki w lewo i w prawo, a Z i C skr\xEA""caj\xB9 w lewo i w prawo. Z i C obracaj\xB9 kamer\xEA, a wraz z ni\xB9 kierunek marszu, wi\xEA""c oba zawsze wskazuj\xB9 to samo. Q i E prze\xB3\xB9""czaj\xB9 cele i obiekty, a Enter wykonuje interakcj\xEA z wybranym obiektem.";
        case Id::TutTraskEquipOpen:
            return "Na ekranie wyposa\xBF""enia wybierasz pancerz i bro\xF1. Otw\xF3rz go klawiszem U.";
        case Id::TutTraskEquipBrowse:
            return "Poruszaj si\xEA po gniazdach wyposa\xBF""enia strza\xB3kami; w ka\xBF""dym gnie\x9F""dzie mo\xBF""esz umie\x9C""ci\xE6 przedmiot. Opis przedmiotu odczytasz Shiftem ze strza\xB3k\xB9.";
        case Id::TutTraskEquipWeapon:
            return "Wybierz gniazdo broni i naci\x9Cnij Enter, a potem wska\xBF bro\xF1 z listy.";
        case Id::TutTraskCamera:
        case Id::TutTraskFootlocker:
            return "Kamer\xEA, a wraz z ni\xB9 kierunek marszu, zmieniasz klawiszami Z i C. N obraca ci\xEA o 90 stopni. Q i E prze\xB3\xB9""czaj\xB9 wszystkie widoczne obiekty. Przecinek i kropka prowadz\xB9 po li\x9C""cie wszystkich obiekt\xF3w, kt\xF3re ju\xBF widzia\xB3""e\x9C. Shift z przecinkiem lub kropk\xB9 prze\xB3\xB9""cza kategorie obiekt\xF3w. Naci\x9Cnij Enter, aby podej\x9C\xE6 do wybranego obiektu i go u\xBFy\xE6.";
        case Id::TutTraskPickItem:
            return "Wybierz gniazdo strza\xB3kami i Enterem, przejrzyj list\xEA i zatwierd\x9F przedmiot Enterem.";
        case Id::TutTraskLeader:
            return "Kierujesz przyw\xF3""dc\xB9 dru\xBFyny. Naci\x9Cnij Tab, aby zmieni\xE6 kontrolowanego cz\xB3onka.";
        case Id::TutTraskMakeLeader:
            return "Naci\x9Cnij Tab, aby uczyni\xE6 mnie przyw\xF3""dc\xB9, a potem otw\xF3rz drzwi.";
        case Id::TutTraskMenus:
            return "Ekrany gry otwierasz bezpo\x9Crednio klawiszami: L zadania, M mapa i inne.";
        case Id::TutTraskTabs:
            return "Zak\xB3""adki prze\xB3\xB9""czasz klawiszami Q i E.";
        case Id::TutTraskActionMenu:
            return "Menu akcji dla twojego celu otwierasz klawiszami Shift i Enter. Cyfra od 1 do 7 uruchamia ostatnio u\xBFyt\xB9 akcj\xEA danej kategorii; Shift z cyfr\xB9 od 1 do 7 otwiera kategori\xEA do wyboru.";
        case Id::TutTraskMedkit:
            return "Apteczek u\xBFywasz z menu akcji. Otw\xF3rz Medycyn\xEA klawiszami Shift i 5, a nast\xEApnie zatwierd\x9F Enterem.";
        case Id::TutTraskOpenDoor:
            return "Wybierz drzwi przecinkiem i kropk\xB9 i otw\xF3rz je Enterem.";
        case Id::TutTraskActivateEntry:
            return "W menu akcji wybierz pozycj\xEA strza\xB3kami g\xF3ra i d\xF3\xB3 i zatwierd\x9F j\xB9 Enterem.";
        case Id::TutTraskTargetMenu:
            return "Cel wybierz klawiszami Q i E, a obiekty przecinkiem i kropk\xB9. Naci\x9Cnij Enter, aby wykona\xE6 domy\x9Cln\xB9 akcj\xEA, albo Shift i Enter, aby otworzy\xE6 menu akcji.";
        case Id::TutTraskConfirmEntry:
            return "Pod\x9Cwietl pozycj\xEA strza\xB3kami g\xF3ra i d\xF3\xB3 i zatwierd\x9F Enterem.";
        case Id::TutTraskPaused:
            return "Nawet podczas pauzy mo\xBF""esz wybiera\xE6 cele klawiszami Q i E oraz zmienia\xE6 kontrolowanego cz\xB3onka klawiszem Tab.";
        case Id::TutTraskWalkTo:
            return "Wybierz skrzyni\xEA klawiszami Q i E i otw\xF3rz j\xB9 Enterem.";
        case Id::TutTraskSecurity:
            return "Wybierz drzwi, otw\xF3rz menu akcji klawiszami Shift i Enter i wybierz Zabezpieczenia.";
        case Id::TutTraskHealWounded:
            return "Prze\xB3\xB9""cz si\xEA na rann\xB9 posta\xE6 klawiszem Tab, otw\xF3rz Medycyn\xEA klawiszami Shift i 5 i wybierz apteczk\xEA.";
        case Id::TutLevelUp:
            return "Aby awansowa\xE6, naci\x9Cnij Shift i L i post\xEApuj zgodnie ze wskaz\xF3wkami.";
        case Id::TutStealthMode:
            return "Naci\x9Cnij G, aby w\xB3\xB9""czy\xE6 lub wy\xB3\xB9""czy\xE6 tryb ukrycia. Musisz mie\xE6 za\xB3o\xBFony generator pola maskuj\xB9""cego, aby z niego korzysta\xE6.";
        case Id::DialogRepeatLineHint:
            return "Naci\x9Cnij R, aby powt\xF3rzy\xE6 ostatni\xB9 kwesti\xEA dialogu.";

        case Id::FloorPuzzleIntro:
            return "Zagadka p\xB3yt pod\xB3ogowych: dziewi\xEA\xE6 p\xB3yt w siatce trzy na trzy, "
                   "p\xB3yta resetuj\xB9""ca po stronie po\xB3udniowej.";
        case Id::FmtFloorReadHint:
            return "Naci\x9Cnij %s, aby odczyta\xE6 stan p\xB3yt.";
        case Id::FloorPuzzleStoryHint:
            return "Powiniene\x9C by\xB3 znale\x9F\xE6 wskaz\xF3wk\xEA fabularn\xB9, kt\xF3ra pomo\xBF""e ci rozwi\xB9za\xE6 t\xEA zagadk\xEA.";
        case Id::FmtFloorSoloHint:
            return "Wskaz\xF3wka: w\xB3\xB9""cz tryb samotny, klawisz %s, bo inaczej towarzysze te\xBF b\xEA""d\xB9 prze\xB3\xB9""cza\xE6 p\xB3yty.";
        case Id::FloorPartyToggled:  return "Towarzysz prze\xB3\xB9""czy\xB3 p\xB3yty";
        case Id::FmtPlateName:       return "P\xB3yta %s";
        case Id::PlateCenterWord:    return "\x8Crodek";
        case Id::PlateResetName:     return "P\xB3yta resetuj\xB9""ca";
        case Id::FmtPlateEntered:    return "Wszed\xB3""e\x9C na %s";
        case Id::FmtPlateLit:        return "%s zapala si\xEA";
        case Id::FmtPlateDark:       return "%s ga\x9Cnie";
        case Id::FmtPlateLitCount:   return "%d z 9 zapalonych";
        case Id::PlatesAllDark:      return "Wszystkie p\xB3yty zgaszone";
        case Id::FloorPuzzleSolved:
            return "Zagadka rozwi\xB9zana. Masywne drzwi si\xEA otwieraj\xB9.";

        case Id::FmtModLoadedVersion:   return "Voice of the Old Republic wczytany, wersja %s";
        case Id::FmtChargenFeatUnnamed: return "Atut %u";
        case Id::ChargenBtnRecommended: return "Zalecane";
        case Id::ChargenBtnAccept:      return "Akceptuj";
        case Id::ChargenBtnBack:        return "Cofnij";
        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_pl
