// Italian string table.
//
// Encoding: Windows-1252 hex escapes for non-ASCII (\xE0=?, \xE8=?,
// \xE9=?, \xEC=?, \xF2=?, \xF9=?). Prism's ANSI overload converts from
// CP_ACP via MultiByteToWideChar; on an Italian Windows install CP_ACP
// = Windows-1252, so the literal bytes pass through unchanged.
//
// Combat speech is fed by combat_strings.cpp::kIt (engine anchors
// extracted from dialog_it.tlk); this table covers the Id::* speech
// path.

#include "strings.h"

namespace acc::strings::lang_it {

const char* Get(Id id) {
    switch (id) {
        case Id::CategoryDoor:        return "Porta";
        case Id::CategoryNpc:         return "PNG";
        case Id::CategoryContainer:   return "Contenitore";
        case Id::CategoryItem:        return "Oggetto";
        case Id::CategoryLandmark:    return "Luogo";
        case Id::CategoryTransition:  return "Transizione";
        case Id::CategoryMapHint:     return "Indicazione";

        case Id::EmptyDoors:          return "Nessuna porta nelle vicinanze";
        case Id::EmptyNpcs:           return "Nessun PNG nelle vicinanze";
        case Id::EmptyContainers:     return "Nessun contenitore nelle vicinanze";
        case Id::EmptyItems:          return "Nessun oggetto nelle vicinanze";
        case Id::EmptyLandmarks:      return "Nessun luogo nelle vicinanze";
        case Id::EmptyTransitions:    return "Nessuna transizione nelle vicinanze";
        case Id::EmptyMapHints:       return "Nessuna indicazione su questa mappa";
        case Id::EmptyAll:            return "Nessun oggetto nelle vicinanze";
        case Id::CycleNoTarget:       return "Nessun bersaglio";

        case Id::MapPinNoText:        return "Marcatore";
        case Id::MapPinShiftDashHint: return "Il marcatore non pu\xF2 essere raggiunto direttamente. Ctrl+Trattino per il segnale.";
        case Id::MapPinAltDashUnsupported: return "Marcatore: Alt+Trattino non supportato";
        case Id::MapPinInteractHint:  return "Marcatore. Ctrl+Trattino per il segnale.";

        case Id::FmtSavedMarkerAutoNumber:   return "Marcatore %d";
        case Id::FmtSavedMarkerAutoWithRoom: return "%s - Marcatore %d";
        case Id::FmtSavedMarkerPlaced:       return "Marcatore salvato: %s";
        case Id::SavedMarkerFailed:          return "Impossibile salvare il marcatore";

        case Id::FmtAnnounceWithClock: return "%s, alle ore %d, %d metri";
        case Id::FmtAnnounceNoClock:   return "%s, %d metri";
        case Id::FmtCategoryItem:      return "%s. %s";
        case Id::FmtTrapDetected:      return "Trappola individuata: %s";
        case Id::MineNoun:             return "Mina";

        case Id::FmtGuidingTo:         return "Guida verso %s";
        case Id::FmtGuidingFailed:     return "Guida verso %s fallita";
        case Id::GuidanceNoFocus:      return "Nessun bersaglio selezionato";
        case Id::GuidingToPoint:       return "Cammina verso il punto";

        case Id::MovementCancelled:    return "Movimento annullato";
        case Id::InteractWayBlocked:   return "Movimento annullato, percorso bloccato";
        case Id::FmtInteractWayBlockedTarget: return "Movimento annullato, percorso bloccato. %s, %d metri, %s";

        // Beacon (Ctrl+-).
        case Id::FmtBeaconStarted:     return "Segnale verso %s";
        case Id::BeaconCancelled:      return "Segnale annullato";
        case Id::FmtBeaconNoPath:      return "Nessun percorso verso %s";
        case Id::BeaconAlreadyAtDest:  return "Gi\xE0 a destinazione";
        // Route description.
        // Example: "Percorso verso Porta (25 metri): 5 metri Nord,
        //           4 metri Nord-Est, 6 metri Est. Nessuna transizione."
        case Id::FmtRouteHeader:       return "Percorso verso %s (%d metri): %s. %s.";
        case Id::FmtRouteSegment:      return "%d metri %s";
        case Id::RouteJoinSeparator:   return ", ";
        case Id::RouteOneTransition:   return "Una transizione";
        case Id::RouteNoTransition:    return "Nessuna transizione";
        case Id::FmtBeaconNextSegment: return "Continua per %d metri %s";

        case Id::FmtInteractTalk:      return "Parla con %s";
        case Id::FmtInteractOpen:      return "Usa %s";
        case Id::FmtInteractTake:      return "Raccogli %s";
        case Id::FmtInteractFailed:    return "Interazione con %s fallita";
        case Id::FmtInteractEngine:    return "%s %s";
        case Id::FmtInteractRadial:    return "Menu azioni, %s";
        case Id::FmtInteractNoActionsRedirect: return "Nessuna azione disponibile per %s. Premi Invio per attivare.";
        case Id::FmtInteractNoActions: return "Nessuna azione disponibile per %s.";

        case Id::FmtActionBarOpened:      return "Barra azioni colonna %d: %s, %d opzioni";
        case Id::FmtActionBarColumnEmpty: return "La colonna %d \xE8 vuota";
        case Id::ActionBarColumnEmpty:    return "Colonna vuota";
        case Id::FmtActionBarFired:       return "%s usato";
        case Id::FmtFireAtPosition:       return "%s, posizione %d";
        case Id::FmtFireQueueFull:        return "%s, coda piena";
        case Id::ActionMenuClosed:        return "Menu azioni chiuso.";

        case Id::MenuCatAttacks:       return "Attacchi";
        case Id::MenuCatForcePowers:   return "Poteri della Forza";
        case Id::MenuCatItems:         return "Oggetti";
        case Id::MenuCatSelfPowers:    return "Poteri personali";
        case Id::MenuCatMedical:       return "Medico";
        case Id::MenuCatMisc:          return "Varie";
        case Id::MenuCatExplosives:    return "Esplosivi";
        case Id::FmtMenuCatMulti:      return "%s: %s, %d opzioni";
        case Id::FmtMenuCatSingle:     return "%s: %s";
        case Id::FmtMenuPlainMulti:    return "%s, %d opzioni";
        case Id::FmtMenuCategoryEmpty: return "%s: vuoto";

        case Id::NoTooltipAvailable:   return "Nessuna descrizione disponibile";

        case Id::ContainerEmpty:       return "Vuoto";
        case Id::ContainerOneItem:     return "1 oggetto";
        case Id::FmtContainerItems:    return "%d oggetti";
        case Id::FmtContainerItemAt:   return "%s, %d di %d";
        case Id::ContainerEmptySuffix: return "vuoto";
        case Id::FmtItemStackSuffix:   return "%d nella pila";
        case Id::FmtItemChargeSuffix:  return "%d cariche";

        case Id::EquipSlotHead:        return "Testa";
        case Id::EquipSlotImplant:     return "Impianto";
        case Id::EquipSlotBody:        return "Corpo";
        case Id::EquipSlotArmL:        return "Braccio sinistro";
        case Id::EquipSlotArmR:        return "Braccio destro";
        case Id::EquipSlotWeapL:       return "Arma sinistra";
        case Id::EquipSlotWeapR:       return "Arma destra";
        case Id::EquipSlotBelt:        return "Cintura";
        case Id::EquipSlotHands:       return "Mani";

        case Id::FmtEquipSlotItem:     return "%s, %s";
        case Id::FmtEquipSlotEmpty:    return "%s, vuoto";
        case Id::EquipUnequipped:      return "Equipaggiamento rimosso";
        case Id::FmtEquipVitality:     return "Vitalit\xE0 %s";
        case Id::FmtEquipDefense:      return "Difesa %s";
        case Id::FmtEquipAttack:       return "Attacco %s";
        case Id::FmtEquipAttackDual:   return "Attacco sinistro %s, destro %s";
        case Id::FmtEquipDamage:       return "Danni %s";
        case Id::FmtEquipDamageDual:   return "Danni sinistro %s, destro %s";

        case Id::FmtTransitionArea:    return "Area: %s";
        case Id::FmtTransitionRoom:    return "Stanza: %s";
        case Id::FmtTransitionRoomIndex: return "Stanza %d";
        case Id::FmtTransitionLoading: return "Caricamento: %s";

        case Id::DoorOpen:             return "aperta";
        case Id::DoorLocked:           return "chiusa a chiave";
        case Id::DoorCosmetic:         return "decorativa";

        case Id::DirNorth:             return "Nord";
        case Id::DirNortheast:         return "Nord-Est";
        case Id::DirEast:              return "Est";
        case Id::DirSoutheast:         return "Sud-Est";
        case Id::DirSouth:             return "Sud";
        case Id::DirSouthwest:         return "Sud-Ovest";
        case Id::DirWest:              return "Ovest";
        case Id::DirNorthwest:         return "Nord-Ovest";

        case Id::StuckFreeDirsPrefix:  return "Libero";
        case Id::StuckAllBlocked:      return "Tutto bloccato";

        case Id::FmtCompassDegrees:    return "%d gradi";

        case Id::FmtMapStateOriented:    return "%s. Orientato a %d gradi sulla mappa, %s.";
        case Id::FmtMapStateUnknownRoom: return "Orientato a %d gradi sulla mappa, %s.";

        case Id::FmtWorldStateOriented:       return "%s. %s.";
        case Id::FmtWorldStateUnknownCluster: return "%s.";

        case Id::MouseLookOn:          return "Vista mouse attiva";
        case Id::MouseLookOff:         return "Vista mouse disattiva";

        case Id::ViewModeOn:           return "Modalit\xE0 osservazione attiva";
        case Id::ViewModeOff:          return "Modalit\xE0 osservazione disattiva";

        case Id::FmtSaveLoadRow:       return "%s, %s, %s, %d di %d";
        case Id::FmtSaveLoadRowNoLoc:  return "%s, %d di %d";

        case Id::LevelUpOpen:          return "Aumento di livello";
        case Id::LevelUpFailed:        return "Aumento di livello fallito";
        case Id::LevelUpAlreadyOpen:   return "Aumento di livello gi\xE0 aperto";
        case Id::LevelUpNotReady:      return "Esperienza non ancora sufficiente per aumentare di livello";

        case Id::PortraitLabel:        return "Ritratto";
        case Id::PortraitArrowPrev:    return "Ritratto precedente";
        case Id::PortraitArrowNext:    return "Ritratto successivo";
        case Id::FmtPortraitArrow:     return "%s: %s";
        case Id::FmtPortraitArrowId:   return "%s %d";
        case Id::PortraitGenderFemale: return "femminile";
        case Id::PortraitGenderMale:   return "maschile";
        case Id::PortraitRaceAsian:    return "asiatico";
        case Id::PortraitRaceDark:     return "di pelle scura";
        case Id::PortraitRaceLight:    return "di pelle chiara";
        case Id::FmtPortraitDescription: return "%s %s %d";

        case Id::FmtPartyPortraitInTeam:    return "%s, nel gruppo";
        case Id::FmtPartyPortraitAvailable: return "%s, disponibile";
        case Id::PartySelectionFull:        return "Gruppo al completo";

        case Id::DisabledSuffix:       return ", non disponibile";
        case Id::FmtLevelUpDoStepFirst: return "Completa prima %s.";
        case Id::LevelUpStepLocked:    return "Non ancora disponibile.";

        case Id::FmtCharSheetClass:    return "%s. ";
        case Id::FmtCharSheetLevel:    return "Livello %s. ";
        case Id::FmtCharSheetXp:       return "Esperienza %s di %s. ";
        case Id::FmtCharSheetHp:       return "Punti vita %s. ";
        case Id::FmtCharSheetFp:       return "Punti Forza %s. ";
        case Id::FmtCharSheetStr:      return "Forza %s%s%s. ";
        case Id::FmtCharSheetDex:      return "Destrezza %s%s%s. ";
        case Id::FmtCharSheetCon:      return "Costituzione %s%s%s. ";
        case Id::FmtCharSheetInt:      return "Intelligenza %s%s%s. ";
        case Id::FmtCharSheetWis:      return "Saggezza %s%s%s. ";
        case Id::FmtCharSheetCha:      return "Carisma %s%s%s. ";
        case Id::FmtCharSheetAlignment: return "Allineamento %u di %u.";

        case Id::FmtChargenAttrInfoSuffix:               return "Modificatore %s, Costo %s";
        case Id::FmtChargenAttrValueChangeBare:          return "%s, punti rimanenti %s";
        case Id::FmtChargenAttrValueChangeWithMod:       return "%s, Modificatore %s, punti rimanenti %s";
        case Id::FmtChargenAttrValueChangeWithCost:      return "%s, punti rimanenti %s, Costo %s";
        case Id::FmtChargenAttrValueChangeWithModAndCost: return "%s, Modificatore %s, punti rimanenti %s, Costo %s";

        case Id::FmtChargenSkillInfoSuffix:  return "Costo %s";
        case Id::FmtChargenSkillValueChange: return "%s, punti rimanenti %s";

        case Id::ChargenFeatGrantedTitle:    return "Ricevi questi talenti";
        case Id::FmtChargenFeatGrantedRow:   return "%s, %d di %d";

        case Id::FmtChargenFeatChartCell:    return "%s, %s";
        case Id::ChargenFeatStatusAvailable: return "disponibile";
        case Id::ChargenFeatStatusExisting:  return "gi\xE0 appreso";
        case Id::ChargenFeatStatusGranted:   return "concesso automaticamente";
        case Id::ChargenFeatStatusLocked:    return "prerequisito mancante";
        case Id::ChargenFeatStatusChosen:    return "scelto";

        case Id::EditboxRole:                return "campo di testo";
        case Id::EditboxEmpty:               return "vuoto";
        case Id::EditboxEnd:                 return "fine";
        case Id::FmtKeyBinding:              return "%s: %s";
        case Id::KeyBindingFixed:            return " (non modificabile)";
        case Id::FmtKeyBindCapture:          return "Premi il nuovo tasto per %s";
        case Id::KeyBindNotChangeable:       return "Questa assegnazione non pu\xf2 essere modificata";

        case Id::CombatBegins:               return "Inizio del combattimento";
        case Id::CombatEnds:                 return "Fine del combattimento";
        case Id::CombatLeaderAtPeace:        return "Fuori combattimento";

        case Id::PcStatNoCharacter:          return "Nessuno stato del personaggio disponibile.";

        // Brief is composed in BuildTargetCombatBrief: name, then optional
        // condition / distance / effects / weapons clauses each with a
        // leading space and trailing period.
        case Id::FmtTargetCombatBrief:       return "%s.";
        case Id::FactionHostile:             return "ostile";
        case Id::FactionFriendly:            return "amichevole";
        case Id::FactionNeutral:             return "neutrale";
        case Id::TargetIsDead:               return "morto";

        case Id::FmtBriefCondition:          return " %s.";
        case Id::FmtBriefDistanceMeters:     return " %d metri.";
        case Id::FmtBriefEffects:            return " %s.";
        case Id::FmtBriefWielding:           return " %s.";
        case Id::FmtBriefOffHand:            return " mano secondaria %s.";
        case Id::FmtBriefEffectsCount:       return " %d effetti attivi.";
        case Id::FmtBriefFeatsCount:         return " %d talenti.";
        case Id::FmtSelfStatusHp:            return "%d punti vita.";
        case Id::FmtSelfStatusHpOf:          return "%d di %d punti vita.";
        case Id::FmtSelfStatusFpOf:          return "%d di %d punti Forza.";

        case Id::ExamineOpened:              return "Esamina.";
        case Id::ExamineNoTarget:            return "Nessun bersaglio da esaminare.";
        case Id::ExamineFailed:              return "Esame fallito.";

        case Id::FmtExamineOpened:           return "Esamina: %s. %d voci.";
        case Id::FmtExamineRowOf:            return "%s. %d di %d.";
        case Id::ExamineViewClosed:          return "Esame chiuso.";
        case Id::FmtExamineRowName:          return "Nome: %s";
        case Id::FmtExamineRowFaction:       return "Disposizione: %s";
        case Id::FmtExamineRowHp:            return "Punti vita: %d";
        case Id::FmtExamineRowDistance:      return "Distanza: %d metri";
        case Id::FmtExamineRowWeapon:        return "Mano principale: %s";
        case Id::ExamineRowWeaponNone:       return "Mano principale: nessuna";
        case Id::FmtExamineRowEffect:        return "Effetto: %s";
        case Id::FmtExamineRowFeat:          return "Talento: %s";
        case Id::FmtExamineRowEffectUnknown: return "Effetto n. %d";
        case Id::FmtExamineRowFeatUnknown:   return "Talento n. %d";
        case Id::ExamineRowNoEffects:        return "Nessun effetto attivo";
        case Id::ExamineRowNoFeats:          return "Nessun talento";

        case Id::FmtExamineRowHpFull:        return "Punti vita: %d di %d";
        case Id::FmtExamineRowLevel:         return "Livello: %d";
        case Id::FmtExamineRowCondition:     return "Condizione: %s";
        case Id::DamageLevel0Healthy:        return "illeso";
        case Id::DamageLevel1Light:          return "leggermente ferito";
        case Id::DamageLevel2Wounded:        return "ferito";
        case Id::DamageLevel3Badly:          return "gravemente ferito";
        case Id::DamageLevel4Dying:          return "morente";
        case Id::DamageLevel5Dead:           return "morto";
        case Id::FmtExamineRowOffHand:       return "Mano secondaria: %s";
        case Id::FmtExamineRowHead:          return "Testa: %s";
        case Id::FmtExamineRowTorso:         return "Armatura: %s";
        case Id::FmtExamineRowHands:         return "Mani: %s";
        case Id::ExamineRowStatusInvisible:  return "Invisibile";
        case Id::ExamineRowStatusBlind:      return "Cieco";

        case Id::FmtQueueOpen:               return "Coda azioni, %d azioni.";
        case Id::QueueEmpty:                 return "La coda azioni \xE8 vuota.";
        case Id::FmtQueueRow:                return "%s: %s %s, %d di %d.";
        case Id::FmtQueueRemoved:            return "Rimosso: %s.";
        case Id::QueueCleared:               return "Coda svuotata.";
        case Id::QueueClosed:                return "Coda chiusa.";
        case Id::QueueRemoveFailed:          return "Impossibile rimuovere questa azione.";
        case Id::QueueVerbAttack:            return "Attacco";
        case Id::QueueVerbCastForce:         return "Lancia potere della Forza";
        case Id::QueueVerbItemCast:          return "Usa oggetto";
        case Id::QueueVerbEquip:             return "Equipaggia";
        case Id::QueueVerbUnequip:           return "Rimuovi";
        case Id::QueueVerbMove:              return "Muovi";
        case Id::QueueVerbHeal:              return "Cura";
        case Id::QueueVerbUseTalent:         return "Usa talento";
        case Id::QueueVerbCutscene:          return "Cinematica";
        case Id::QueueVerbUnknown:           return "Azione";

        // Skeleton: max-HP is not yet read safely (suspected engine
        // accessor), so the hit / crit messages omit the "X of Y hp"
        // tail. Args reduced to (attacker, target, damage).
        case Id::FmtAttackHit:               return "%s colpisce %s per %d danni.";
        case Id::FmtAttackMiss:              return "%s manca %s.";
        case Id::FmtAttackCrit:              return "Critico! %s colpisce %s per %d danni.";
        case Id::FmtAttackDeflected:         return "L'attacco di %s contro %s viene parato.";

        case Id::FmtSavingThrowSucceeded:    return "%s supera la salvezza: %s %d contro %d.";
        case Id::FmtSavingThrowFailed:       return "%s fallisce la salvezza: %s %d contro %d.";
        case Id::SaveTypeFort:               return "Tempra";
        case Id::SaveTypeReflex:             return "Riflessi";
        case Id::SaveTypeWill:               return "Volont\xE0";

        case Id::DialogReplyUnavailable:     return "non disponibile";
        case Id::FmtDialogReplyUnavailableRow: return "%s, %s, %d di %d";

        case Id::MessagesTitleCombatLog:     return "Registro di combattimento.";
        case Id::MessagesTitleDialogLog:     return "Registro dei dialoghi.";

        case Id::MapPrevNote:                return "Nota precedente";
        case Id::MapNextNote:                return "Nota successiva";

        case Id::MapCursorUnexplored:        return "Inesplorato";
        case Id::MapCursorWaypointPOI:       return "Punto d'interesse";
        case Id::MapCursorJunction:          return "Incrocio";
        case Id::MapCursorOffPath:           return "Muro";
        case Id::FmtMapCursorCorridor:       return "%s, %.0f metri";
        case Id::FmtMapCursorDeadEnd:        return "Vicolo cieco, %s";
        case Id::FmtMapCursorJunctionDirs:   return "Incrocio, %s";
        case Id::FmtMapCursorCorridorDir:    return "%s";
        case Id::MapCursorDoorNoun:          return "Porta";
        case Id::FmtMapCursorDoor:           return "%s %s";
        case Id::FmtMapCursorDoorTransition: return "%s %s verso %s";
        case Id::FmtMapCursorDoorLandmark:   return "%s %s, %s";
        case Id::MapCursorTransitionDoor:    return "Soglia";
        case Id::FmtMapCursorJunctionDeadEndExit: return "vicolo cieco %s";
        case Id::AxisNorthSouth:             return "nord-sud";
        case Id::AxisEastWest:               return "est-ovest";
        case Id::AreaNoun:                   return "Zona";
        case Id::AreaNounLarge:              return "Grande zona";
        case Id::FmtAreaAxisExits:           return "%s %s. Uscite: %s";
        case Id::FmtAreaExits:               return "%s. Uscite: %s";
        case Id::FmtAreaAxisOnly:            return "%s %s";

        case Id::FmtStorePriceBuyFinite:    return "Prezzo %d crediti, scorta %d";
        case Id::FmtStorePriceBuyUnlimited: return "Prezzo %d crediti, scorta illimitata";
        case Id::FmtStorePriceSell:         return "Prezzo %d crediti, ne possiedi %d";
        case Id::StoreModeBuy:              return "Modalit\xE0 acquisto";
        case Id::StoreModeSell:             return "Modalit\xE0 vendita";
        case Id::StoreSold:                 return "Venduto";
        case Id::StoreBought:               return "Acquistato";
        case Id::StoreCannotSell:           return "Non pu\xF2 essere venduto";
        case Id::StoreCannotBuy:            return "Non pu\xF2 essere acquistato";
        case Id::FmtStoreSoldFor:           return "Venduto per %d crediti";
        case Id::FmtStoreBoughtFor:         return "Acquistato per %d crediti";

        // ----- Pazaak -----
        case Id::PazaakStart:            return "Pazaak. Premi C per la mano, T per il tavolo.";
        case Id::PazaakEmpty:            return "vuoto";
        case Id::PazaakFaceDown:         return "coperta";
        case Id::PazaakBoardEmpty:       return "vuoto";
        case Id::PazaakFmtPlus:          return "pi\xF9 %d";
        case Id::PazaakFmtMinus:         return "meno %d";
        case Id::PazaakFmtPlain:         return "%d";
        case Id::PazaakFmtFlipBoth:      return "pi\xF9 o meno %d";
        case Id::PazaakFmtFlipCurrently: return "%s, attualmente %s";
        case Id::PazaakFmtYouDrew:       return "Peschi %s. Il tuo totale %d.";
        case Id::PazaakOverTwenty:       return "Oltre venti.";
        case Id::PazaakFmtYouPlayed:     return "%s giocata. Il tuo totale %d.";
        case Id::PazaakYourTurn:         return "Tocca a te.";
        case Id::PazaakTurnEnded:        return "Turno terminato.";
        case Id::PazaakFmtOppDrew:       return "L'avversario pesca %s. Totale %d.";
        case Id::PazaakFmtOppPlayed:     return "L'avversario gioca %s. Totale %d.";
        case Id::PazaakFmtOppStands:     return "L'avversario sta a %d.";
        case Id::PazaakFmtYouStand:      return "Stai a %d.";
        case Id::PazaakFmtWinSet:        return "Vinci il set. %d a %d.";
        case Id::PazaakFmtLoseSet:       return "Perdi il set. %d a %d.";
        case Id::PazaakWinMatch:         return "Vinci la partita!";
        case Id::PazaakLoseMatch:        return "Perdi la partita.";
        case Id::PazaakTieReplay:        return "Pareggio. Set ripetuto.";
        case Id::PazaakFmtHand:          return "Mano: %s";
        case Id::PazaakHandEmpty:        return "Mano vuota.";
        case Id::PazaakFmtYourBoard:     return "Il tuo tavolo: %s, totale %d.";
        case Id::PazaakFmtOppBoard:      return "Tavolo avversario: %s, totale %d.";
        case Id::PazaakNoPlayable:       return "Nessuna carta da giocare.";
        case Id::PazaakNotYourTurn:      return "Non \xE8 il tuo turno.";
        case Id::PazaakSelectCardFirst:  return "Scegli prima una carta.";
        case Id::PazaakChooseSign:       return "Scegli il segno. Sinistra o destra per cambiare, Invio per giocare.";
        case Id::PazaakCancelled:        return "Annullato.";
        case Id::PazaakDeckAvailable:    return "%s, %d disponibili";
        case Id::PazaakDeckNoneLeft:     return "%s, nessuna rimasta";
        case Id::PazaakDeckSlotFilled:   return "Posto %d: %s";
        case Id::PazaakDeckSlotEmpty:    return "Posto %d: vuoto";
        case Id::PazaakDeckPlay:         return "Gioca, %d su 10 nel mazzo";
        case Id::PazaakDeckAdded:        return "Aggiunto %s. %d su 10.";
        case Id::PazaakDeckRemoved:      return "Rimosso %s.";
        case Id::PazaakDeckFull:         return "Mazzo pieno.";
        case Id::PazaakFmtOppHand:       return "L'avversario ha %d carte in mano.";
        case Id::PazaakStandLabel:       return "Stare";
        case Id::PazaakEndTurnLabel:     return "Fine turno";
        case Id::PazaakWagerLess:        return "Riduci puntata";
        case Id::PazaakWagerMore:        return "Aumenta puntata";
        case Id::PazaakFmtWager:         return "Puntata %d su %d massimo.";
        case Id::PazaakFmtWagerRow:      return "Puntata %d. %s";
        case Id::FmtStoreNotEnoughCredits:  return "Crediti insufficienti, ne servono %d, ne hai %d";
        case Id::JournalQuestItemsButton:   return "Oggetti delle missioni";

        case Id::FmtCredits:                return "Crediti: %s";

        case Id::WorkbenchSlotWeapon1:       return "Slot di miglioramento 1";
        case Id::WorkbenchSlotWeapon2:       return "Slot di miglioramento 2";
        case Id::WorkbenchSlotWeapon3:       return "Slot di miglioramento 3";
        case Id::WorkbenchSlotSaberCrystal1: return "Slot cristallo 1";
        case Id::WorkbenchSlotSaberCrystal2: return "Slot cristallo 2";
        case Id::WorkbenchSlotSaberCrystal3: return "Slot cristallo 3";
        case Id::WorkbenchSlotSaberCrystal4: return "Slot cristallo 4";
        case Id::WorkbenchItemsEmpty:        return "Nessun oggetto migliorabile in questa categoria";
        case Id::WorkbenchUpgradesEmpty:     return "Nessun miglioramento compatibile nell'inventario";
        case Id::WorkbenchSlotInstalled:     return "Miglioramento installato";
        case Id::WorkbenchSlotRemoved:       return "Miglioramento rimosso";
        case Id::WorkbenchSlotNoMatch:       return "Nessun miglioramento corrispondente nell'inventario";
        case Id::WorkbenchSlotFilled:        return "occupato";
        case Id::WorkbenchSlotPeekEmpty:     return "Alloggiamento vuoto, nessun miglioramento installato";
        case Id::WorkbenchFmtSlotItem:       return "%s, con %s";
        case Id::WorkbenchPickerInstalled:   return "installato";

        case Id::SoundOptionsMovieVolume:    return "Volume video";

        case Id::SwoopRaceStarted:
            // Terse opener ? concatenated with SwoopRaceControls below
            // into one utterance by swoop_race.cpp::AnnounceEntry.
            return "Corsa Swoop.";
        case Id::SwoopRaceControls:
            // Short cheat sheet. Full keymap (Enter/Mouse 1 also work,
            // Pause pauses the race, etc.) is in the manual ? verbose
            // spoken intro got in the way of the start countdown.
            return "Spazio per salire di marcia al segnale audio. Sterza con A e D. Evita gli ostacoli e colpisci le piastre acceleratrici per battere il tempo.";
        case Id::SwoopRaceEnded:
            return "Corsa Swoop terminata.";
        case Id::SwoopRaceObstacleNear:
            return "Ostacolo a %d metri";
        case Id::FmtSwoopRaceGear:
            return "Marcia %d";
        case Id::FmtSwoopRaceTime:
            return "Tempo: %d,%02d secondi. Corsa Swoop terminata.";

        case Id::TurretGameStarted:
            // Terse opener ? concatenated with TurretGameControls below
            // into one utterance by turret_game.cpp::AnnounceEntry.
            return "Torretta.";
        case Id::TurretGameControls:
            return "Mira con WASD. Spazio per sparare. Q ed E scelgono i bersagli.";
        case Id::TurretGameEnded:
            return "Torretta terminata.";
        case Id::FmtTurretTarget:
            return "Caccia %d, %d metri";
        case Id::FmtTurretDestroyed:
            return "Caccia %d distrutto.";
        case Id::TurretNoTargets:
            return "Nessun bersaglio.";
        case Id::TurretTargetLost:
            return "Bersaglio perso.";

        case Id::ModSettingsRootLabel:        return "Impostazioni mod";
        case Id::ModSettingsOpened:           return "Impostazioni mod aperte";
        case Id::ModSettingsClosed:           return "Impostazioni mod chiuse";
        case Id::ModSettingExtendedCycling:   return "Selezione oggetti su tutta la mappa";
        case Id::ModSettingRoomShapes:        return "Descrizioni della forma delle stanze";
        case Id::ModSettingWallSounds:        return "Suoni dei muri";
        case Id::ModSettingHumanSubtitles:    return "Leggi i sottotitoli dei parlanti doppiati";
        case Id::ModSettingTurretAutoAim:     return "Mira automatica";
        case Id::ModSettingSkipIntros:        return "Salta i video introduttivi";
        case Id::ModSettingSkipIntrosOnNextLaunch: return "I video introduttivi verranno saltati al prossimo avvio.";
        case Id::ModSettingPlayIntrosOnNextLaunch: return "I video introduttivi verranno riprodotti al prossimo avvio.";
        case Id::ModSettingSkipIntrosToggleFailed: return "Impossibile alternare i video introduttivi. I file potrebbero essere stati rimossi.";
        case Id::ModSettingStateOn:           return "attivo";
        case Id::ModSettingStateOff:          return "disattivo";
        case Id::FmtModSettingOption:         return "%s: %s";
        case Id::ModSettingCueVolume:         return "Volume dei segnali sonori";
        case Id::FmtModSettingSlider:         return "%s: %d percento";
        case Id::ModSettingUrgentVolume:      return "Volume degli annunci vocali";
        case Id::ModSettingUrgentVolumePreview: return "Annuncio di esempio";

        case Id::ModSettingAudioGlossary:           return "Glossario audio";
        case Id::ModSettingsAudioGlossaryOpened:    return "Glossario audio aperto";
        case Id::GlossaryEntryDoorOpen:             return "Porta aperta";
        case Id::GlossaryEntryDoorClosedMetal:      return "Porta di metallo chiusa";
        case Id::GlossaryEntryDoorClosedWood:       return "Porta di legno chiusa";
        case Id::GlossaryEntryDoorClosedStone:      return "Porta di pietra chiusa";
        case Id::GlossaryEntryWall:                 return "Muro";
        case Id::GlossaryEntryHazard:               return "Pericolo";
        case Id::GlossaryEntryCollision:            return "Collisione";
        case Id::GlossaryEntryBeaconActive:         return "Segnale attivo";
        case Id::GlossaryEntryBeaconWaypoint:       return "Punto intermedio raggiunto";
        case Id::GlossaryEntryBeaconDestination:    return "Destinazione raggiunta";
        case Id::GlossaryEntrySwoopAccelpadBoost:   return "Piastra acceleratrice Swoop";
        case Id::GlossaryEntrySwoopObstacleWarn:    return "Allerta ostacolo Swoop";
        case Id::GlossaryEntrySwoopWallImpact:      return "Impatto contro muro Swoop";
        case Id::GlossaryEntrySwoopAligned:         return "Swoop in traiettoria";
        case Id::GlossaryEntrySwoopShiftReady:      return "Swoop cambio marcia pronto";

        case Id::FmtUpdateAvailable:    return "Aggiornamento disponibile, versione %s. Premi F5 dal menu principale per installare.";
        case Id::UpdateDownloadStarting: return "Avvio del download.";
        case Id::UpdateDownloading:     return "Download dell'aggiornamento in corso.";
        case Id::UpdateDownloaded:      return "Aggiornamento scaricato. Chiusura del gioco per installare.";
        case Id::UpdateFailed:          return "Download dell'aggiornamento fallito. Premi F5 per riprovare.";
        case Id::FmtUpdateNotAvailable: return "Nessun aggiornamento disponibile. Sei alla versione %s.";
        case Id::UpdateNotInMenu:       return "Gli aggiornamenti possono essere installati solo dal menu principale.";

        case Id::PanelTitleMainMenu:    return "Menu principale";
        case Id::LoadingPleaseWait:     return "Il gioco sta ancora caricando, attendere prego.";
        case Id::LoadingStuckWorkaround: return "Il menu non risponde ancora. Premi Alt F4 e annulla la finestra di uscita per risvegliarlo.";

        case Id::GamePaused:            return "In pausa.";
        case Id::GameResumed:           return "Pausa rimossa.";

        case Id::GalaxyMapTitle:        return "Mappa galattica";

        // ---- Help system ----
        case Id::HelpGroupGeneral:      return "Navigazione";
        case Id::HelpGroupMovement:     return "Movimento e telecamera";
        case Id::HelpGroupInteraction:  return "Bersagli e interazione";
        case Id::HelpGroupCombat:       return "Combattimento e azioni";
        case Id::HelpGroupExploration:  return "Esplorazione e orientamento";
        case Id::HelpGroupScreens:      return "Schermate";
        case Id::HelpGroupMap:          return "Mappa";
        case Id::HelpGroupMod:          return "Funzioni della mod";

        case Id::HelpKeyUpDown:          return "Freccia su e gi\xF9: scorrere elenchi e voci di menu";
        case Id::HelpKeyLeftRight:       return "Freccia sinistra e destra: cambiare categoria o modificare un valore";
        case Id::HelpKeyHomeEnd:         return "Inizio e Fine: andare alla prima o all'ultima voce";
        case Id::HelpKeyEnter:           return "Invio: attivare la voce selezionata";
        case Id::HelpKeyEsc:             return "Esc: chiudere la schermata o tornare indietro";
        case Id::HelpKeyReadDescription: return "Maiusc pi\xF9 una freccia: leggere la descrizione completa senza spostarsi";
        case Id::HelpKeySwitchWindows:   return "Q ed E: cambiare finestra o scheda, schermata di menu, e modalit\xE0 in negozi e contenitori";
        case Id::HelpKeyF1:              return "F1: aprire o chiudere questo elenco di tasti";
        case Id::HelpKeyCtrlF1:          return "Ctrl pi\xF9 F1: leggere i tasti della schermata attuale";

        case Id::HelpKeyWalk:           return "W e S: avanzare e indietreggiare";
        case Id::HelpKeyCameraRotate:   return "A e D: ruotare la telecamera a sinistra e a destra";
        case Id::HelpKeyStrafe:         return "Z e C: spostarsi di lato a sinistra e a destra";
        case Id::HelpKeyPause:          return "Spazio: mettere in pausa e riprendere";
        case Id::HelpKeyViewMode:       return "B: modalit\xE0 osservazione, restare fermi mentre si gira la telecamera";
        case Id::HelpKeySwitchLeader:   return "Tab: cambiare il membro del gruppo controllato";

        case Id::HelpKeyCycleTargets:   return "Q ed E: scorrere i bersagli vicini";
        case Id::HelpKeyInteract:       return "Invio: interagire con il bersaglio selezionato o attaccarlo";
        case Id::HelpKeyOpenActionMenu: return "Maiusc pi\xF9 Invio: aprire il menu azioni del bersaglio selezionato";
        case Id::HelpKeySelfStatus:     return "H: annunciare la propria salute, gli effetti e l'arma";
        case Id::HelpKeyAnnounceFocus:  return "Meno: annunciare l'oggetto evidenziato";
        case Id::HelpKeyWalkToFocus:    return "Maiusc pi\xF9 meno: camminare verso l'oggetto evidenziato";
        case Id::HelpKeyBeacon:         return "Ctrl pi\xF9 meno: avviare una guida verso l'oggetto evidenziato";
        case Id::HelpKeyDialogRepeat:   return "R: ripetere la battuta attuale";

        case Id::HelpKeyCycleObjects:   return "Virgola e punto: scorrere gli oggetti della categoria attuale";
        case Id::HelpKeyCycleCategory:  return "Maiusc pi\xF9 virgola o punto: categoria precedente o successiva";
        case Id::HelpKeyCycleEnds:      return "Ctrl pi\xF9 virgola o punto: andare all'oggetto pi\xF9 vicino o pi\xF9 lontano";
        case Id::HelpKeyHeading:        return "Alt destro: annunciare la direzione esatta in gradi";
        case Id::HelpKeyCameraOrient:   return "N: girare la telecamera verso la direzione successiva, o orientarsi al prossimo punto di passaggio";
        case Id::HelpKeyDropMarker:     return "Maiusc pi\xF9 N: posizionare un indicatore sulla mappa nella tua posizione";

        case Id::FmtHelpNumberActions:   return "Da 1 a 7: usare l'ultima azione di una categoria. 1 %s, 2 %s, 3 %s, 4 %s, 5 %s, 6 %s, 7 %s";
        case Id::HelpKeyOpenCategory:    return "Maiusc pi\xF9 1 a 7: aprire quella categoria per scegliere un'azione";
        case Id::HelpKeyActionQueue:     return "Maiusc pi\xF9 H: aprire la coda delle azioni";
        case Id::HelpKeyLevelUp:         return "Maiusc pi\xF9 L: aprire la schermata di avanzamento di livello";
        case Id::HelpKeyCancelCombat:    return "F: annullare il combattimento";

        case Id::HelpKeyScreenMap:       return "M: aprire la mappa";
        case Id::HelpKeyScreenMessages:  return "J: messaggi e feedback";
        case Id::HelpKeyScreenQuests:    return "L: missioni";
        case Id::HelpKeyScreenAbilities: return "K: abilit\xE0, talenti e poteri della Forza";
        case Id::HelpKeyScreenCharacter: return "P: scheda del personaggio";
        case Id::HelpKeyScreenInventory: return "I: inventario del gruppo";
        case Id::HelpKeyScreenEquip:     return "U: equipaggiare il personaggio";
        case Id::HelpKeyScreenOptions:   return "O: opzioni";

        case Id::HelpKeyMapCursor:       return "W, A, S e D: spostare il cursore della mappa per leggere terreno e indicatori";
        case Id::HelpKeyMapPosition:     return "Alt destro: annunciare la tua posizione e orientamento sulla mappa";


        case Id::HelpKeyModSettings:     return "Le impostazioni della mod sono nelle Opzioni, in fondo all'elenco";

        case Id::HelpMenuOpened:    return "Guida ai tasti. Su e gi\xF9 per leggere, Esc per chiudere.";
        case Id::HelpMenuClosed:    return "Guida ai tasti chiusa.";
        case Id::FmtHelpRowOf:      return "%s. %d di %d";
        case Id::FmtHelpGroupHeader: return "Sezione: %s";

        case Id::HelpContextNothing: return "Nessun tasto speciale per questa schermata.";
        case Id::FmtHelpContextLine: return "%s %s.";
        case Id::HelpContextWorld:       return "Nel mondo.";
        case Id::HelpContextMenu:        return "Menu.";
        case Id::HelpContextMap:         return "Mappa.";
        case Id::HelpContextActionMenu:  return "Menu azioni.";
        case Id::HelpContextDialog:      return "Conversazione.";
        case Id::HelpContextContainer:   return "Contenitore.";
        case Id::HelpContextStore:       return "Negozio.";

        case Id::InputBlockedBigPicture:
            return "Il gioco non pu\xF2 ricevere i tuoi tasti perch\xE9 la "
                   "modalit\xE0 Steam Big Picture \xE8 in primo piano.";

        // ---- Tastenbelegung (mod keybind configurator) ----
        case Id::KeybindsRootLabel:       return "Combinazioni di tasti";
        case Id::KeybindsOpened:          return "Combinazioni di tasti aperte";
        case Id::KeybindCatWorld:         return "Mondo e azioni";
        case Id::KeybindCatExploration:   return "Esplorazione e telecamera";
        case Id::KeybindCatMenus:         return "Menu e immissione";
        case Id::KeybindCatMinigames:     return "Minigiochi";
        case Id::KeybindCatGeneral:       return "Generale";
        case Id::KeybindResetAll:         return "Ripristina predefiniti";
        case Id::KeybindResetDone:        return "Combinazioni di tasti ripristinate";
        case Id::FmtKeybindCapturePrompt: return "Premi il nuovo tasto per %s. Esc annulla.";
        case Id::FmtKeybindRebound:       return "%s riassegnato a %s";
        case Id::FmtKeybindConflictMod:   return "Gi\xE0 assegnato a %s. Premi un altro tasto.";
        case Id::KeybindConflictEngine:   return "Usato dal gioco. Premi un altro tasto.";
        case Id::KeybindCaptureCancelled: return "Annullato";
        case Id::FmtKeymapModConflict:    return "Attenzione: il mod usa questo tasto per %s";
        // World & actions
        case Id::KbNameInteractTarget:      return "Interagisci";
        case Id::KbNameInteractForceRadial: return "Forza menu radiale";
        case Id::KbNameTargetKey1:          return "Tasto bersaglio 1";
        case Id::KbNameTargetKey2:          return "Tasto bersaglio 2";
        case Id::KbNameTargetKey3:          return "Tasto bersaglio 3";
        case Id::KbNamePersonalKey1:        return "Azione personale 1";
        case Id::KbNamePersonalKey2:        return "Azione personale 2";
        case Id::KbNamePersonalKey3:        return "Azione personale 3";
        case Id::KbNamePersonalKey4:        return "Azione personale 4";
        case Id::KbNameActionBarOpen1:      return "Apri barra azioni 1";
        case Id::KbNameActionBarOpen2:      return "Apri barra azioni 2";
        case Id::KbNameActionBarOpen3:      return "Apri barra azioni 3";
        case Id::KbNameActionBarOpen4:      return "Apri barra azioni 4";
        case Id::KbNameTargetActionOpen1:   return "Apri azione bersaglio 1";
        case Id::KbNameTargetActionOpen2:   return "Apri azione bersaglio 2";
        case Id::KbNameTargetActionOpen3:   return "Apri azione bersaglio 3";
        case Id::KbNameLevelUpOpen:         return "Avanzamento di livello";
        case Id::KbNameExamineOpen:         return "Esamina";
        case Id::KbNameCombatQueueOpen:     return "Coda azioni";
        case Id::KbNameSelfStatusAnnounce:  return "Stato personale";
        // Exploration & camera
        case Id::KbNameCycleItemPrev:       return "Oggetto precedente";
        case Id::KbNameCycleCategoryPrev:   return "Categoria precedente";
        case Id::KbNameCycleItemNext:       return "Oggetto successivo";
        case Id::KbNameCycleCategoryNext:   return "Categoria successiva";
        case Id::KbNameCycleItemFirst:      return "Primo oggetto";
        case Id::KbNameCycleItemLast:       return "Ultimo oggetto";
        case Id::KbNameAnnounceFocus:       return "Annuncia bersaglio";
        case Id::KbNamePathfindFocus:       return "Vai al bersaglio";
        case Id::KbNamePathfindFocusForce:  return "Vai al bersaglio, forza";
        case Id::KbNameBeaconFocus:         return "Segnale al bersaglio";
        case Id::KbNameAnnounceDegrees:     return "Direzione in gradi";
        case Id::KbNamePartyLeaderAnnounce: return "Annuncia capogruppo";
        case Id::KbNameCameraOrient:        return "Orienta telecamera";
        case Id::KbNameSaveMarkerAtCursor:  return "Posiziona segnaposto";
        case Id::KbNameViewModeToggle:      return "Modalit\xE0 osservazione";
        // Menus & input
        case Id::KbNameNavUp:               return "Menu su";
        case Id::KbNameNavDown:             return "Menu gi\xF9";
        case Id::KbNameNavLeft:             return "Menu sinistra";
        case Id::KbNameNavRight:            return "Menu destra";
        case Id::KbNameNavHome:             return "All'inizio";
        case Id::KbNameNavEnd:              return "Alla fine";
        case Id::KbNameSubmenuEsc:          return "Chiudi menu";
        case Id::KbNameQueueClearAll:       return "Svuota coda";
        case Id::KbNameContainerGiveMode:   return "Contenitore modalit\xE0 dare";
        case Id::KbNameStoreModeToggle:     return "Negozio compra o vendi";
        case Id::KbNameEditboxReReadUp:     return "Rileggi campo verso l'alto";
        case Id::KbNameEditboxReReadDown:   return "Rileggi campo verso il basso";
        case Id::KbNameEditboxSubmit:       return "Conferma immissione";
        case Id::KbNameEditboxCancel:       return "Annulla immissione";
        // Minigames
        case Id::KbNamePazaakStand:         return "Pazaak: Stai";
        case Id::KbNamePazaakEndTurn:       return "Pazaak: Fine turno";
        case Id::KbNamePazaakReviewHand:    return "Pazaak: Mano";
        case Id::KbNamePazaakReviewTable:   return "Pazaak: Tavolo";
        case Id::KbNamePazaakNextCard:      return "Pazaak: Carta successiva";
        case Id::KbNamePazaakPrevCard:      return "Pazaak: Carta precedente";
        case Id::KbNamePazaakPlay:          return "Pazaak: Gioca carta";
        case Id::KbNamePazaakOptLeft:       return "Pazaak: Opzione sinistra";
        case Id::KbNamePazaakOptRight:      return "Pazaak: Opzione destra";
        case Id::KbNamePazaakCancel:        return "Pazaak: Annulla";
        case Id::KbNamePazaakOppHand:       return "Pazaak: Mano avversario";
        case Id::KbNameTurretCyclePrev:     return "Torretta: Bersaglio precedente";
        case Id::KbNameTurretCycleNext:     return "Torretta: Bersaglio successivo";
        // General
        case Id::KbNameHelpMenuOpen:        return "Aiuto tasti";
        case Id::KbNameHelpContext:         return "Aiuto contestuale";
        case Id::KbNameCheckForUpdate:      return "Cerca aggiornamenti";
        case Id::KbNameDialogRepeatLine:    return "Ripeti battuta di dialogo";

        case Id::FloorPuzzleIntro:
            return "Enigma delle piastre: nove piastre in una griglia tre per tre, "
                   "piastra di ripristino sul lato sud.";
        case Id::FmtFloorSoloHint:
            return "Consiglio: attiva la modalit\xE0 solo, tasto %s, altrimenti anche i compagni attivano le piastre.";
        case Id::FloorPartyToggled:  return "Un compagno ha cambiato delle piastre";
        case Id::FmtPlateName:       return "Piastra %s";
        case Id::PlateCenterWord:    return "Centro";
        case Id::PlateResetName:     return "Piastra di ripristino";
        case Id::FmtPlateEntered:    return "Su %s";
        case Id::FmtPlateLit:        return "%s accesa";
        case Id::FmtPlateDark:       return "%s spenta";
        case Id::FmtPlateLitCount:   return "%d su 9 accese";
        case Id::PlatesAllDark:      return "Tutte le piastre spente";
        case Id::FloorPuzzleSolved:
            return "Enigma risolto. La porta massiccia si apre.";

        case Id::Count_:               return "";
    }
    return "";
}

}  // namespace acc::strings::lang_it
